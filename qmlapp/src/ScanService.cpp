#include "ScanService.h"

#include "ScanSettings.h"

#include <QMetaObject>
#include <QVariantList>

// scan.h pulls in the WinShun SDK header and therefore windows.h, so it has to
// come after the Qt headers.
#include "scan.h"
#include "scan_helper.h"

#include <utility>

namespace {

// The preview of a scan in progress is a full copy of the tree read so far.
// WinShun caps this process's memory, so past a few hundred thousand nodes the
// copies stop: the finished tree arrives soon after anyway.
constexpr unsigned kPreviewNodeLimit = 500000;

QString systemErrorMessage(DWORD code)
{
    LPWSTR buffer = nullptr;
    const DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString message;
    if (length && buffer) {
        message = QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed();
    }
    if (buffer) {
        ::LocalFree(buffer);
    }
    return message;
}

// A restricted token — WinShun's sandbox builds one — can never be elevated:
// Windows does not even show the consent prompt, ShellExecuteW's "runas" just
// fails. Asking anyway only produces a silent fallback, so the caller is told
// up front and the reason can be shown.
bool elevationPossible()
{
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return true; // unknown: let the helper try
    }
    struct Guard {
        HANDLE token;
        ~Guard() { if (token) ::CloseHandle(token); }
    } guard{token};
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    if (::GetTokenInformation(token, TokenElevation, &elevation, sizeof elevation, &size)
        && elevation.TokenIsElevated) {
        return true; // already running as administrator
    }
    // A restricted token carries restricted SIDs, and those are exactly what
    // blocks an elevation request.
    DWORD bytes = 0;
    ::SetLastError(ERROR_SUCCESS);
    ::GetTokenInformation(token, TokenRestrictedSids, nullptr, 0, &bytes);
    return !(::GetLastError() == ERROR_INSUFFICIENT_BUFFER && bytes > 0);
}

// The tree without per-node maps: one pass, appending to parallel arrays. The
// node's children are appended right after it, so the parent index is all that
// is needed to rebuild the shape. The scanner's own nodes are emptied as they
// are read: under the host's memory cap the copy and the original must not
// both be resident for the whole walk.
void flattenInto(diskview::Node& node, int parent, FlatTree& out)
{
    const int index = out.names.size();
    out.names.append(QString::fromStdWString(node.name));
    out.parents.append(parent);
    out.sizes.append(static_cast<qint64>(node.logical));
    out.allocs.append(static_cast<qint64>(node.allocApprox));
    out.modified.append(static_cast<qint64>(node.modified));
    out.attributes.append(node.attributes);
    out.fileCounts.append(node.files);
    out.dirCounts.append(node.dirs);
    out.flags.append(static_cast<quint8>((node.dir ? 1 : 0) | (node.modified ? 2 : 0)));
    for (diskview::Node& child : node.children) {
        flattenInto(child, index, out);
    }
    node.name.clear();
    node.name.shrink_to_fit();
    node.children.clear();
    node.children.shrink_to_fit();
}

FlatTree flatten(diskview::Node& root)
{
    FlatTree out;
    out.names.reserve(4096);
    flattenInto(root, -1, out);
    return out;
}

} // namespace

ScanService::ScanService(QObject* parent) : QObject(parent) {}

ScanService::~ScanService()
{
    m_cancel.store(true);
    m_pause.store(false);
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool ScanService::start(const QString& path)
{
    return beginScan(path, false);
}

bool ScanService::startPaused(const QString& path)
{
    return beginScan(path, true);
}

bool ScanService::beginScan(const QString& path, bool startPaused)
{
    if (m_running.load()) {
        return false;
    }
    if (path.trimmed().isEmpty()) {
        setState(QStringLiteral("error"));
        m_errorText = QStringLiteral("路径为空");
        emit failed(m_errorText);
        return false;
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_cancel.store(false);
    m_pause.store(startPaused);
    m_running.store(true);
    m_progress = 0.0;
    m_fileCount = 0;
    m_dirCount = 0;
    m_byteCount = 0;
    m_elapsedMs = 0.0;
    m_partial = false;
    m_partialCount = 0;
    m_scanWorkers = 0;
    m_excludedItems = 0;
    m_fast = false;
    m_fallbackReason.clear();
    m_skippedDirs = 0;
    m_skippedLinks = 0;
    m_unknownSizes = 0;
    m_orphanRecords = 0;
    m_issues.clear();
    m_errorText.clear();
    m_rootPath = path;
    m_statusText = startPaused ? QStringLiteral("扫描已暂停") : QStringLiteral("正在扫描 %1").arg(path);
    setState(QStringLiteral("scanning"));
    // The path of the last scan is remembered, the way the native plugin
    // remembers it in its own settings file.
    if (m_settings) {
        m_settings->setLastPath(path);
        m_settings->save();
    }
    emit started(path);

    // The settings are read here, on the GUI thread, and handed to the worker
    // as a value: the dialog may change them while the scan runs.
    diskview::ScanOptions options;
    int scanMode = 2; // ordinary enumeration
    if (m_settings) {
        options.workers = static_cast<unsigned>(m_settings->workers());
        options.media = static_cast<diskview::ScanOptions::Media>(m_settings->media());
        options.previewIntervalMs = static_cast<unsigned>(m_settings->previewIntervalMs());
        for (const QString& excluded : m_settings->excludedPaths()) {
            options.excludedPaths.push_back(excluded.toStdWString());
        }
        scanMode = m_settings->scanMode();
    }
    options.paused = &m_pause;
    options.previewNodeLimit = kPreviewNodeLimit;

    // If this scan needs the elevated helper, Windows has to be able to hand
    // the foreground from this GUI process to its consent prompt. Ask before
    // starting the worker: ShellExecuteEx runs there, but this call is made on
    // the thread that owns the window (and may own the foreground).
    if (m_ownerWindow) ::AllowSetForegroundWindow(ASFW_ANY);
    m_thread = std::thread([this, path, options, scanMode] {
      // A scan can run out of memory (the host caps the process) or fail any
      // other way; an exception escaping a thread ends the whole process, so
      // it becomes an error the window can show.
      try {
          diskview::ScanResult result;
          const diskview::ProgressFn progress = [this](const diskview::ScanProgress& p) {
              const QString text = p.fast
                                       ? QStringLiteral("MFT 快扫 · 已读 %1 / %2 条记录")
                                             .arg(p.recordsDone)
                                             .arg(p.recordsTotal)
                                       : QStringLiteral("已扫描 %1 个文件 · %2 个目录")
                                             .arg(p.files)
                                             .arg(p.dirs);
              const quint64 bytes = p.bytes;
              QMetaObject::invokeMethod(
                  this,
                  [this, text, bytes] {
                      m_statusText = text;
                      m_byteCount = bytes;
                      emit stateChanged();
                  },
                  Qt::QueuedConnection);
              return !m_cancel.load();
          };
          // The scanner hands out immutable snapshots of the tree so far. The
          // window shows them while the scan runs, the way the native plugin
          // shows a partial result.
          const diskview::SnapshotFn snapshot = [this](std::shared_ptr<diskview::ScanResult> partial) {
              // One preview in flight is enough: the next one would only queue
              // another copy of the whole tree behind it.
              if (m_cancel.load() || m_snapshotPending.exchange(true)) {
                  return;
              }
              FlatTree tree = flatten(partial->root);
              QMetaObject::invokeMethod(
                  this,
                  [this, tree] {
                      m_snapshotPending.store(false);
                      m_partial = true;
                      ++m_partialCount;
                      emit stateChanged();
                      emit partialTree(tree);
                  },
                  Qt::QueuedConnection);
          };
          const std::wstring wide = path.toStdWString();
          // The fast scan reads the NTFS MFT through an elevated helper. It only
          // works on a fixed NTFS drive root; anything else, and any failure of
          // the helper, falls back to the ordinary enumeration.
          bool ordinary = true;
          std::wstring fallback;
          if (scanMode != 2 && diskview::mft::eligible(diskview::normalizePath(wide, nullptr))) {
              if (!elevationPossible()) {
                  fallback = L"当前由 WinShun 的受限进程运行，无法申请管理员权限：已改用普通遍历";
              } else {
                  std::wstring reason;
                  const auto outcome = diskview::helper::scan(
                      reinterpret_cast<HWND>(m_ownerWindow), wide, progress, snapshot, m_cancel, result, reason, options);
                  ordinary = outcome == diskview::helper::Outcome::Fallback;
                  if (outcome == diskview::helper::Outcome::Cancelled) {
                      result.cancelled = true;
                  }
                  if (ordinary) {
                      fallback = reason;
                  }
              }
          }
          if (ordinary && !m_cancel.load()) {
              diskview::scanPath(wide, progress, m_cancel, &result, snapshot, options);
          } else if (m_cancel.load()) {
              result.cancelled = true;
          }
          result.fallbackReason = fallback;

          const bool scanFailed = !result.error.empty();
          const QString error = QString::fromStdString(result.error);
          const QString root = QString::fromStdWString(result.displayRoot);
          const quint64 files = result.files;
          const quint64 dirs = result.dirs;
          const double elapsed = static_cast<double>(result.elapsedMs);
          const bool wasCancelled = result.cancelled;
          const int workers = static_cast<int>(result.workers);
          const quint32 excluded = result.excludedItems;
          const bool fast = result.fast;
          const QString fallbackText = QString::fromStdWString(result.fallbackReason);
          const int skippedDirs = static_cast<int>(result.skippedDirs);
          const int skippedLinks = static_cast<int>(result.skippedLinks);
          const int unknownSizes = static_cast<int>(result.unknownSizes);
          const int orphanRecords = static_cast<int>(result.orphanRecords);
          QVariantList issues;
          for (const diskview::SkippedDir& skipped : result.skippedList) {
              issues.append(QVariantMap {
                  {"path", QString::fromStdWString(skipped.path)},
                  {"code", static_cast<uint>(skipped.error)},
                  {"message", systemErrorMessage(skipped.error)},
              });
          }
          FlatTree tree;
          if (!scanFailed) {
              tree = flatten(result.root);
          }
          // The scanner's own tree is no longer needed: dropping it here halves
          // the peak, which matters under the host's memory cap.
          result = diskview::ScanResult {};
          QMetaObject::invokeMethod(
              this,
              [this, tree, scanFailed, error, root, files, dirs, elapsed, wasCancelled, workers, excluded, fast,
               fallbackText, skippedDirs, skippedLinks, unknownSizes, orphanRecords, issues] {
                  m_running.store(false);
                  m_scanWorkers = workers;
                  m_excludedItems = excluded;
                  m_fast = fast;
                  m_fallbackReason = fallbackText;
                  m_skippedDirs = skippedDirs;
                  m_skippedLinks = skippedLinks;
                  m_unknownSizes = unknownSizes;
                  m_orphanRecords = orphanRecords;
                  m_issues = issues;
                  if (scanFailed) {
                      m_errorText = error;
                      m_statusText = error;
                      setState(QStringLiteral("error"));
                      emit failed(error);
                      return;
                  }
                  m_rootPath = root;
                  m_fileCount = files;
                  m_dirCount = dirs;
                  m_elapsedMs = elapsed;
                  m_partial = false;
                  if (wasCancelled) {
                      m_statusText = QStringLiteral("扫描已取消");
                      setState(QStringLiteral("cancelled"));
                      // A cancelled scan still has the part it managed to read:
                      // it is shown, marked as unfinished.
                      if (!tree.isEmpty()) {
                          emit partialTree(tree);
                      }
                      emit cancelled();
                      return;
                  }
                  m_progress = 1.0;
                  m_statusText = QStringLiteral("扫描完成 · %1 个文件 · %2 个目录 · %3 ms")
                                     .arg(files)
                                     .arg(dirs)
                                     .arg(qint64(elapsed));
                  setState(QStringLiteral("done"));
                  emit finished(tree);
              },
              Qt::QueuedConnection);
      } catch (const std::exception& e) {
        reportScanFailure(QStringLiteral("扫描中断：%1").arg(QString::fromUtf8(e.what())));
      } catch (...) {
        reportScanFailure(QStringLiteral("扫描中断：未知错误"));
      }
    });
    return true;
}

// The scanning thread's failure path. Everything that is not a scan error is
// turned into the same "failed" state the window already knows how to show.
void ScanService::reportScanFailure(const QString& message)
{
    QMetaObject::invokeMethod(
        this,
        [this, message] {
            m_running.store(false);
            m_partial = false;
            m_errorText = message;
            m_statusText = message;
            setState(QStringLiteral("error"));
            emit failed(message);
        },
        Qt::QueuedConnection);
}

void ScanService::cancel()
{
    m_cancel.store(true);
    // A paused scan waits for the flag, so it has to be let go before it can
    // notice the cancellation.
    m_pause.store(false);
}

void ScanService::pause()
{
    if (!m_running.load()) {
        return;
    }
    m_pause.store(true);
    emit stateChanged();
}

void ScanService::resume()
{
    m_pause.store(false);
    emit stateChanged();
}

bool ScanService::rescan()
{
    if (m_running.load()) {
        return false;
    }
    const QString path = m_settings ? m_settings->lastPath() : m_rootPath;
    if (path.trimmed().isEmpty()) {
        return false;
    }
    return start(path);
}

void ScanService::setState(const QString& state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged();
}
