#include "ScanService.h"

#include "ScanSettings.h"

#include <QMetaObject>
#include <QVariantList>

// scan.h pulls in the WinShun SDK header and therefore windows.h, so it has to
// come after the Qt headers.
#include "scan.h"

#include <utility>

namespace {

QVariantMap toVariantTree(const diskview::Node& node)
{
    QVariantList children;
    children.reserve(static_cast<int>(node.children.size()));
    for (const diskview::Node& child : node.children) {
        children.push_back(toVariantTree(child));
    }
    return QVariantMap {
        {"name", QString::fromStdWString(node.name)},
        {"size", static_cast<double>(node.logical)},
        {"dir", node.dir},
        {"children", children},
    };
}

} // namespace

ScanService::ScanService(QObject* parent) : QObject(parent) {}

ScanService::~ScanService()
{
    m_cancel.store(true);
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool ScanService::start(const QString& path)
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
    m_errorText.clear();
    m_rootPath = path;
    m_statusText = QStringLiteral("正在扫描 %1").arg(path);
    setState(QStringLiteral("scanning"));
    // The path of the last scan is remembered, the way the native plugin
    // remembers it in its own settings file.
    if (m_settings) {
        m_settings->setLastPath(path);
        m_settings->save();
    }
    emit started(path);

    m_thread = std::thread([this, path] {
        diskview::ScanResult result;
        diskview::ScanOptions options;
        if (m_settings) {
            options.workers = static_cast<unsigned>(m_settings->workers());
            options.media = static_cast<diskview::ScanOptions::Media>(m_settings->media());
            options.previewIntervalMs = static_cast<unsigned>(m_settings->previewIntervalMs());
            for (const QString& excluded : m_settings->excludedPaths()) {
                options.excludedPaths.push_back(excluded.toStdWString());
            }
        }
        const diskview::ProgressFn progress = [this](const diskview::ScanProgress& p) {
            const QString text = QStringLiteral("已扫描 %1 个文件 · %2 个目录")
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
            if (m_cancel.load()) {
                return;
            }
            QVariantMap tree = toVariantTree(partial->root);
            QMetaObject::invokeMethod(
                this,
                [this, tree] {
                    m_partial = true;
                    ++m_partialCount;
                    emit stateChanged();
                    emit partialTree(tree);
                },
                Qt::QueuedConnection);
        };
        diskview::scanPath(path.toStdWString(), progress, m_cancel, &result, snapshot, options);

        const bool scanFailed = !result.error.empty();
        const QString error = QString::fromStdString(result.error);
        const QString root = QString::fromStdWString(result.displayRoot);
        const quint64 files = result.files;
        const quint64 dirs = result.dirs;
        const double elapsed = static_cast<double>(result.elapsedMs);
        const bool wasCancelled = result.cancelled;
        const int workers = static_cast<int>(result.workers);
        const quint32 excluded = result.excludedItems;
        QVariantMap tree;
        if (!scanFailed) {
            tree = toVariantTree(result.root);
        }
        QMetaObject::invokeMethod(
            this,
            [this, tree, scanFailed, error, root, files, dirs, elapsed, wasCancelled, workers, excluded] {
                m_running.store(false);
                m_scanWorkers = workers;
                m_excludedItems = excluded;
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
    });
    return true;
}

void ScanService::cancel()
{
    m_cancel.store(true);
}

void ScanService::setState(const QString& state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged();
}
