#include "QmlHost.h"

#include "ChartItem.h"
#include "DiskViewModel.h"
#include "DockState.h"
#include "ScanService.h"
#include "ScanSettings.h"
#include "ThemeBridge.h"
#include "Phosphor.h"

#include <QClipboard>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStorageInfo>
#include <QRegularExpression>
#include <QDirIterator>
#include <QTimer>
#include <QThread>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>

#include <wrl/client.h>
#include <wrl/implements.h>
#include <shobjidl.h>

#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QUrl>

#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <shlwapi.h>

namespace {
const char* kBuiltInFixture = ":/fixtures/disk-tree.json";

QString cleanPath(QString path)
{
    path = QDir::fromNativeSeparators(path.trimmed());
    if (path.size() == 2 && path[1] == QLatin1Char(':')) path += QLatin1Char('/');
    return QDir::cleanPath(path);
}

bool sameOrBelowPath(const QString& path, const QString& root)
{
    const QString p = cleanPath(path).toCaseFolded();
    QString r = cleanPath(root).toCaseFolded();
    if (r.endsWith(QLatin1Char('/')) && r.size() > 3) r.chop(1);
    if (p == r) return true;
    // A drive root is already a complete path component: C:/foo is below
    // C:/, even though the character immediately after "c:/" is not '/'.
    if (r.size() == 3 && r[1] == QLatin1Char(':') && r[2] == QLatin1Char('/'))
        return p.startsWith(r);
    return p.startsWith(r) && p.size() > r.size() && p[r.size()] == QLatin1Char('/');
}

bool protectedPath(const QString& source)
{
    const QString path = cleanPath(QFileInfo(source).canonicalFilePath().isEmpty()
                                   ? source : QFileInfo(source).canonicalFilePath());
    if (path.isEmpty() || path.startsWith(QStringLiteral("//"))
        || (path.size() == 3 && path[1] == QLatin1Char(':'))) return true;
    const QString windows = QDir::cleanPath(QString::fromWCharArray([] { static wchar_t p[32768]{}; ::GetWindowsDirectoryW(p, 32768); return p; }()));
    if (sameOrBelowPath(path, windows)) return true;
    for (const wchar_t* variable : {L"ProgramFiles", L"ProgramFiles(x86)", L"ProgramData"}) {
        wchar_t value[32768]{};
        const DWORD size = ::GetEnvironmentVariableW(variable, value, 32768);
        if (size && size < 32768 && sameOrBelowPath(path, QString::fromWCharArray(value))) return true;
    }
    const QString name = QFileInfo(path).fileName().toCaseFolded();
    if (name == QStringLiteral("pagefile.sys") || name == QStringLiteral("hiberfil.sys")
        || name == QStringLiteral("swapfile.sys") || name == QStringLiteral("system volume information")) return true;
    const QString drive = QFileInfo(path).absolutePath().left(3);
    if (drive.size() < 3 || ::GetDriveTypeW(reinterpret_cast<LPCWSTR>(drive.utf16())) != DRIVE_FIXED) return true;
    const DWORD attrs = ::GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_REPARSE_POINT))) return true;
    QString ancestor = QFileInfo(path).absolutePath();
    while (!ancestor.isEmpty() && ancestor.size() >= 3) {
        const DWORD parentAttrs = ::GetFileAttributesW(reinterpret_cast<LPCWSTR>(ancestor.utf16()));
        if (parentAttrs == INVALID_FILE_ATTRIBUTES || (parentAttrs & FILE_ATTRIBUTE_REPARSE_POINT)) return true;
        const QString next = QFileInfo(ancestor).absolutePath();
        if (next == ancestor) break;
        ancestor = next;
    }
    return false;
}

class FileOperationSink final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IFileOperationProgressSink> {
public:
    bool requireRecycle = true;
    bool completed = false;
    bool recycled = false;
    HRESULT itemResult = E_PENDING;
    Microsoft::WRL::ComPtr<IShellItem> destination;
    IFACEMETHODIMP StartOperations() override { return S_OK; }
    IFACEMETHODIMP FinishOperations(HRESULT) override { return S_OK; }
    IFACEMETHODIMP PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return E_ABORT; }
    IFACEMETHODIMP PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return E_ABORT; }
    IFACEMETHODIMP PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return E_ABORT; }
    IFACEMETHODIMP PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP PreDeleteItem(DWORD flags, IShellItem*) override
    { return !requireRecycle || (flags & TSF_DELETE_RECYCLE_IF_POSSIBLE) ? S_OK : E_ABORT; }
    IFACEMETHODIMP PostDeleteItem(DWORD, IShellItem*, HRESULT hr, IShellItem* binItem) override
    { completed = true; itemResult = hr; recycled = SUCCEEDED(hr) && hr != S_FALSE && binItem; destination = binItem; return S_OK; }
    IFACEMETHODIMP PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return E_ABORT; }
    IFACEMETHODIMP PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP UpdateProgress(UINT, UINT) override { return S_OK; }
    IFACEMETHODIMP ResetTimer() override { return S_OK; }
    IFACEMETHODIMP PauseTimer() override { return S_OK; }
    IFACEMETHODIMP ResumeTimer() override { return S_OK; }
};
}

QmlHost::QmlHost(Options options, QObject* parent) : QObject(parent), m_options(std::move(options)) {}

bool QmlHost::load(QString* error)
{
    // The host's own QtQuick modules, when the app is a plugin on WinShun's Qt
    // rather than on its own deployment. Later paths win for a module that
    // exists in more than one.
    for (const QString& path : m_options.importPaths) {
        if (!path.isEmpty()) m_engine.addImportPath(path);
    }
    m_engine.rootContext()->setContextProperty("diskViewMode", m_options.viewMode);
    m_engine.rootContext()->setContextProperty("fixtureSource",
                                               m_options.fixture.isEmpty() ? QString::fromLatin1(kBuiltInFixture) : m_options.fixture);

    // The scanner is a plain object owned by the host and handed to the QML as
    // a context property: the same instance drives the window and the command
    // line. The settings decide how the next scan runs.
    m_scanService = new ScanService(this);
    m_scanSettings = new ScanSettings(this);
    m_scanSettings->setDefaultVolumeEnumerationEnabled(!m_options.fixtureOnly);
    m_scanSettings->load();
    // The host's own settings, when this window is a plugin: they win over the
    // local file for the keys the host shows.
    if (!m_options.hostSettings.isEmpty()) {
        applyHostSettings(m_options.hostSettings);
    }
    m_scanService->setSettings(m_scanSettings);
    m_engine.rootContext()->setContextProperty("scanService", m_scanService);
    m_engine.rootContext()->setContextProperty("scanSettings", m_scanSettings);
    // The settings page in WinShun owns these; the window's dialog must not
    // offer a second place to change them.
    m_engine.rootContext()->setContextProperty("hostManagedSettings", !m_options.hostSettings.isEmpty());
    // The window asks the host for the two things QML cannot do on its own.
    m_engine.rootContext()->setContextProperty("appHost", this);
    // The look: one token set the whole window and the chart read. The icon
    // fonts have to be in place before the engine draws its first glyph.
    ws::phosphor::loadFonts();
    m_theme = new ThemeBridge(this);
    m_engine.rootContext()->setContextProperty("theme", m_theme);
    // A standalone run can be given a real appearance object for screenshots
    // and checks, the way the host sends it.
    if (const QByteArray appearance = qgetenv("WS_APPEARANCE"); !appearance.isEmpty()) {
        const QJsonDocument document = QJsonDocument::fromJson(appearance);
        if (document.isObject()) m_theme->setAppearance(document.object().toVariantMap());
    }
    if (m_scanSettings->appearanceMode() != 0)
        m_theme->setLocalMode(m_scanSettings->appearanceMode());
    // A path given on the command line wins over the remembered one.
    m_engine.rootContext()->setContextProperty(
        "initialScanPath",
        m_options.scanPath.isEmpty() ? m_scanSettings->lastPath() : m_options.scanPath);

    // The engine only says why a load failed through this signal.
    connect(&m_engine, &QQmlApplicationEngine::warnings, this, [this](const QList<QQmlError>& warnings) {
        for (const QQmlError& warning : warnings) {
            m_loadWarnings << warning.toString();
        }
    });

    if (m_options.qmlDir.isEmpty()) {
        m_engine.loadFromModule("DiskViewPrototype", "Main");    } else {
        const QString mainQml = QDir(m_options.qmlDir).filePath("Main.qml");
        if (!QFileInfo::exists(mainQml)) {
            *error = QStringLiteral("no Main.qml under %1").arg(m_options.qmlDir);
            return false;
        }
        m_engine.load(QUrl::fromLocalFile(mainQml));
    }

    if (m_engine.rootObjects().isEmpty()) {
        // The engine reports the reasons through its warnings signal, which is
        // the only way to say why the module did not load.
        *error = m_options.qmlDir.isEmpty()
                     ? QStringLiteral("the built-in QML module failed to load")
                     : QStringLiteral("Main.qml failed to load from %1").arg(m_options.qmlDir);
        if (!m_loadWarnings.isEmpty()) {
            *error += QStringLiteral(": ") + m_loadWarnings.join(QStringLiteral("; "));
        }
        return false;
    }
    if (auto* window = qobject_cast<QQuickWindow*>(m_engine.rootObjects().value(0))) {
        // The same icon the native (GDI) edition shows: it embeds app.ico in
        // its rc and picks it up through WM_SETICON. The resource is compiled
        // into this binary, so both editions show one icon.
        window->setIcon(QIcon(QStringLiteral(":/app.ico")));
        // An alpha swap chain, so a see-through window background shows the
        // Mica behind it instead of black. Qt drops the alpha from the format
        // whenever the window colour turns opaque, so it is put back on every
        // colour change. The software renderer has no swap chain.
        if (QQuickWindow::graphicsApi() == QSGRendererInterface::Direct3D11) {
            const auto keepAlpha = [window] {
                QSurfaceFormat format = window->requestedFormat();
                if (format.alphaBufferSize() < 8) {
                    format.setAlphaBufferSize(8);
                    window->setFormat(format);
                }
            };
            keepAlpha();
            connect(window, &QQuickWindow::colorChanged, window, keepAlpha);
        }
        const auto applyTitleBarTheme = [this, window] {
            if (!m_theme) return;
            const BOOL darkAttribute = m_theme->dark() ? TRUE : FALSE;
            ::DwmSetWindowAttribute(reinterpret_cast<HWND>(window->winId()), 20,
                                    &darkAttribute, sizeof(darkAttribute));
            // The main window only. ComboBox popups are their own windows and
            // stay opaque.
            m_theme->applyWindowBackdrop(reinterpret_cast<HWND>(window->winId()));
        };
        applyTitleBarTheme();
        connect(m_theme, &ThemeBridge::changed, this, applyTitleBarTheme);
        // Hand the finished tree to the model by move. The QML signal handler
        // would copy the whole tree through a QVariant; on a full C: volume
        // that is a second multi-hundred-megabyte allocation.
        if (auto* model = window->findChild<DiskViewModel*>(QStringLiteral("diskModel"))) {
            connect(m_scanService, &ScanService::finished, model, [this, model] {
                if (auto tree = m_scanService->takeFinishedTree()) {
                    model->loadFlat(std::move(*tree), false);
                }
            });
        }
        // --maximize drives the window's own function, so the fullscreen layer
        // is exercised the way a press on the toolbar would.
        if (!m_options.maximizeOnStart.isEmpty()) {
            QQmlExpression expression(m_engine.rootContext(), window,
                                      QStringLiteral("maximizePane(\"%1\")").arg(m_options.maximizeOnStart));
            expression.evaluate();
        }
    }
    return true;
}

void QmlHost::applyHostSettings(const QVariantMap& settings)
{
    if (!m_scanSettings) return;
    const QString mode = settings.value(QStringLiteral("scanMode")).toString();
    m_scanSettings->setScanMode(mode == QLatin1String("fast") ? 1 : mode == QLatin1String("ordinary") ? 2 : 0);
    const QString workers = settings.value(QStringLiteral("workers")).toString();
    m_scanSettings->setWorkers(workers == QLatin1String("auto") ? 0 : workers.toInt());
    const QString media = settings.value(QStringLiteral("media")).toString();
    m_scanSettings->setMedia(media == QLatin1String("ssd") ? 1 : media == QLatin1String("hdd") ? 2
                             : media == QLatin1String("network") ? 3 : 0);
    const QString preview = settings.value(QStringLiteral("preview")).toString();
    m_scanSettings->setPreviewIntervalMs(preview == QLatin1String("less") ? 5000 : 1500);
}

QQuickWindow* QmlHost::window() const
{
    if (m_engine.rootObjects().isEmpty()) return nullptr;
    return qobject_cast<QQuickWindow*>(m_engine.rootObjects().value(0));
}

bool QmlHost::startScan(const QString& path, QString* error)
{
    if (!m_scanService) {
        *error = QStringLiteral("the scan service is not available");
        return false;
    }
    connect(m_scanService, &ScanService::finished, this, [this] { emit scanFinished(); });
    connect(m_scanService, &ScanService::failed, this, [this](const QString& message) { emit scanFailed(message); });

    if (m_options.pauseOnStart) {
        if (!m_scanService->startPaused(path)) {
            *error = QStringLiteral("a scan is already running");
            return false;
        }
        return true;
    }
    if (m_options.selfTest) {
        // The self-test starts the scan paused and lets it go again: a scan
        // that is held must produce nothing, and it has to go on when it is
        // resumed.
        if (!m_scanService->startPaused(path)) {
            *error = QStringLiteral("a scan is already running");
            return false;
        }
        QTimer::singleShot(400, this, [this] {
            auto* model = m_engine.rootObjects().isEmpty()
                              ? nullptr
                              : m_engine.rootObjects().value(0)->findChild<DiskViewModel*>(
                                    QStringLiteral("diskModel"));
            m_pauseVerified = m_scanService->paused() && (!model || model->rowCount() == 0);
            m_scanService->resume();
        });
        return true;
    }
    if (!m_scanService->start(path)) {
        *error = QStringLiteral("a scan is already running");
        return false;
    }
    return true;
}

QString QmlHost::scanStatusLine() const
{
    if (!m_scanService) {
        return {};
    }
    const bool fast = m_scanService->fast();
    QString line = QStringLiteral("scan\tstate=%1\tmode=%2\tworkers=%3\texcluded=%4\tpartials=%5\tfiles=%6\tdirs=%7\tskipped=%8")
                       .arg(m_scanService->paused() ? QStringLiteral("paused") : m_scanService->state())
                       .arg(fast ? QStringLiteral("fast") : QStringLiteral("ordinary"))
                       .arg(m_scanService->scanWorkers())
                       .arg(m_scanService->excludedItems())
                       .arg(m_scanService->partialCount())
                       .arg(m_scanService->fileCount())
                       .arg(m_scanService->dirCount())
                       .arg(m_scanService->skippedDirs());
    // Whether the process could read the MFT at all: the fast scan exists
    // only for the elevated Win顺 runner's plugins.
    line += QStringLiteral("\televated=%1").arg(m_scanService->elevated() ? QStringLiteral("true") : QStringLiteral("false"));
    if (!m_scanService->fallbackReason().isEmpty()) {
        line += QStringLiteral("\tfallback=%1").arg(m_scanService->fallbackReason());
    }
    return line + QStringLiteral("\n");
}

namespace {
// A tree shaped like a disk: a dozen top folders, each holding one flat level
// of files, so the benchmark's node count is exact and its layout is realistic.
QVariantMap syntheticTree(int nodes)
{
    const int topCount = 12;
    const int perTop = std::max(1, (nodes - 1) / topCount);
    std::uint64_t state = 88172645463325252ull;
    const auto next = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    QVariantList top;
    double total = 0;
    for (int t = 0; t < topCount; ++t) {
        QVariantList files;
        double folderTotal = 0;
        for (int i = 0; i < perTop; ++i) {
            const double size = double(1024 + (next() % (8u << 20)));
            folderTotal += size;
            files.push_back(QVariantMap{{"name", QStringLiteral("f_%1_%2.dat").arg(t).arg(i)},
                                        {"size", size}, {"dir", false}});
        }
        total += folderTotal;
        top.push_back(QVariantMap{{"name", QStringLiteral("dir_%1").arg(t)},
                                  {"size", folderTotal}, {"dir", true}, {"children", files}});
    }
    return QVariantMap{{"name", QStringLiteral("bench")}, {"size", total},
                       {"dir", true}, {"children", top}};
}

// The same shape as a flat tree, so the two loaders can be compared.
FlatTree flattenSynthetic(int nodes)
{
    const int topCount = 12;
    const int perTop = std::max(1, (nodes - 1) / topCount);
    FlatTree out;
    out.names.append(QStringLiteral("bench"));
    out.parents.append(-1);
    out.sizes.append(0);
    out.allocs.append(0);
    out.modified.append(0);
    out.attributes.append(0x10);
    out.fileCounts.append(perTop * topCount);
    out.dirCounts.append(topCount);
    out.flags.append(1);
    for (int t = 0; t < topCount; ++t) {
        const int topIndex = out.names.size();
        out.names.append(QStringLiteral("dir_%1").arg(t));
        out.parents.append(0);
        out.sizes.append(perTop * 4096);
        out.allocs.append(perTop * 4096);
        out.modified.append(0);
        out.attributes.append(0x10);
        out.fileCounts.append(perTop);
        out.dirCounts.append(0);
        out.flags.append(1);
        for (int i = 0; i < perTop; ++i) {
            out.names.append(QStringLiteral("f_%1_%2.dat").arg(t).arg(i));
            out.parents.append(topIndex);
            out.sizes.append(4096);
            out.allocs.append(4096);
            out.modified.append(0);
            out.attributes.append(0x20);
            out.fileCounts.append(0);
            out.dirCounts.append(0);
            out.flags.append(0);
        }
    }
    return out;
}
} // namespace

bool QmlHost::runDockChecks(QString* report)
{
    auto* window = qobject_cast<QQuickWindow*>(m_engine.rootObjects().value(0));
    auto* chart = window ? window->findChild<ChartItem*>(QStringLiteral("diskViewChart")) : nullptr;
    auto* model = window ? window->findChild<DiskViewModel*>(QStringLiteral("diskModel")) : nullptr;
    if (!chart || !model) {
        *report = QStringLiteral("bench: the chart or the model is missing\n");
        return false;
    }

    QQmlExpression dockCheck(m_engine.rootContext(), window, QStringLiteral("verifyDock()"));
    if (!dockCheck.evaluate().toBool() || dockCheck.hasError()) {
        *report = QStringLiteral("FAIL\tdock layout/fullscreen round trip: %1\n").arg(dockCheck.error().toString());
        return false;
    }
    QCoreApplication::processEvents();

    // Drive the actual title-bar MouseAreas through the window: drag the
    // files tab into the centre of the tree, then drag it out to the chart.
    if (auto* state = window->findChild<DockState*>(QStringLiteral("diskViewDockState"));
        state && window->property("chartLayout").toString() == QStringLiteral("dock")) {
        const auto original = state->layout();
        const std::function<QQuickItem*(QQuickItem*, const QString&)> findVisual =
            [&](QQuickItem* root, const QString& name) -> QQuickItem* {
                if (root->objectName() == name) return root;
                for (auto* child : root->childItems()) if (auto* found = findVisual(child, name)) return found;
                return nullptr;
            };
        const auto send = [&](QEvent::Type type, QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent event(type, position, window->mapToGlobal(position.toPoint()), button, buttons, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &event);
            QCoreApplication::processEvents();
        };
        const auto dragTab = [&](const QString& pane, const QString& target, bool center) {
            const auto before = state->layout();
            auto* source = findVisual(window->contentItem(), QStringLiteral("dock-tab-") + pane);
            auto* destination = findVisual(window->contentItem(), QStringLiteral("dock-group-") + target);
            if (!source || !destination) return false;
            const QPointF start = source->mapToScene(QPointF(source->width() / 2, source->height() / 2));
            auto* workspace = window->findChild<QQuickItem*>(QStringLiteral("diskViewDockWorkspace"));
            if (!workspace) return false;
            QPointF end;
            for (const auto& entry : state->placements(workspace->width(), workspace->height())) {
                const auto rect = entry.toMap();
                if (rect.value("type") == "tabs" && rect.value("active") == target) {
                    end = workspace->mapToScene(QPointF(rect.value("x").toDouble() + rect.value("width").toDouble() * .5,
                        rect.value("y").toDouble() + rect.value("height").toDouble() * (center ? .5 : .9)));
                    break;
                }
            }
            send(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
            send(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
            return state->layout() != before;
        };
        if (!dragTab(QStringLiteral("files"), QStringLiteral("tree"), true)
            || !dragTab(QStringLiteral("files"), QStringLiteral("chart"), false)) {
            *report = QStringLiteral("FAIL\tdock pointer drag\n");
            state->setLayout(original);
            return false;
        }
        state->setLayout(original);
        QCoreApplication::processEvents();
        auto* handle = findVisual(window->contentItem(), QStringLiteral("dock-handle-"));
        if (!handle) { *report = QStringLiteral("FAIL\tdock divider missing\n"); return false; }
        const QPointF start = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2));
        const QPointF end = start + QPointF(65, 0);
        send(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
        if (state->layout() == original) { *report = QStringLiteral("FAIL\tdock pointer divider resize\n"); return false; }
        state->setLayout(original);
        QCoreApplication::processEvents();
        QQmlExpression saveDock(m_engine.rootContext(), window, QStringLiteral("flushDockLayout()"));
        if (!saveDock.evaluate().toBool()) { *report = QStringLiteral("FAIL\tdock persistence\n"); return false; }
    }
    return true;
}

bool QmlHost::runChartBenchmark(int nodes, QString* report)
{
    auto* window = qobject_cast<QQuickWindow*>(m_engine.rootObjects().value(0));
    auto* chart = window ? window->findChild<ChartItem*>(QStringLiteral("diskViewChart")) : nullptr;
    auto* model = window ? window->findChild<DiskViewModel*>(QStringLiteral("diskModel")) : nullptr;
    if (!chart || !model) return false;
    const QVariantMap tree = syntheticTree(nodes);
    const FlatTree flat = flattenSynthetic(nodes);
    const int runs = 3;
    QElapsedTimer timer;

    timer.start();
    for (int i = 0; i < runs; ++i) model->loadFlat(flat, false);
    const double modelMs = double(timer.nsecsElapsed()) / 1e6 / runs;

    timer.restart();
    for (int i = 0; i < runs; ++i) model->loadTree(tree);
    const double nestedMs = double(timer.nsecsElapsed()) / 1e6 / runs;

    // Enter a folder and come back: the model reset and row rebuild that a
    // chart double click pays, measured apart from export and painting.
    qint64 enterNs = 0, resetNs = 0;
    for (int i = 0; i < runs; ++i) {
        timer.restart();
        model->enterNode(QStringLiteral("2"));
        enterNs += timer.nsecsElapsed();
        timer.restart();
        model->resetChart();
        resetNs += timer.nsecsElapsed();
    }
    const double navMs = double(enterNs) / 1e6 / runs;
    const double resetMs = double(resetNs) / 1e6 / runs;

    timer.restart();
    QVariantMap payload;
    for (int i = 0; i < runs; ++i) payload = model->chartTree();
    const double exportMs = double(timer.nsecsElapsed()) / 1e6 / runs;

    chart->setTreeData(payload);
    // Both drawing depths: one level is the default, two is what the 2 层
    // button shows, and the paint cost only shows up there.
    const QString shallow = chart->benchmark(runs);
    chart->setDepth(2);
    const QString deep = chart->benchmark(runs);
    chart->setDepth(1);
    const double nameMB = double(flat.nameBytes()) / (1024.0 * 1024.0);
    *report = QStringLiteral("bench model flat=%1ms nested=%2ms export=%3ms enter=%4ms reset=%5ms names=%6MB\nbench depth=1 %7\nbench depth=2 %8\n")
                  .arg(modelMs, 0, 'f', 1)
                  .arg(nestedMs, 0, 'f', 1)
                  .arg(exportMs, 0, 'f', 1)
                  .arg(navMs, 0, 'f', 1)
                  .arg(resetMs, 0, 'f', 1)
                  .arg(nameMB, 0, 'f', 1)
                  .arg(shallow, deep);
    return true;
}

bool QmlHost::runListChecks(DiskViewModel* model, QString* detail)
{
    if (!model) {
        *detail = QStringLiteral("no-model");
        return false;
    }
    const QString rootPath = model->currentPath();
    const auto value = [&](int row, int role) {
        return model->data(model->index(row, 0), role);
    };

    // The list starts with the biggest entries first.
    bool sizeSorted = true;
    for (int row = 1; row < model->rowCount(); ++row) {
        if (value(row - 1, DiskViewModel::SizeRole).toULongLong() < value(row, DiskViewModel::SizeRole).toULongLong()) {
            sizeSorted = false;
            break;
        }
    }
    // The filter looks at the whole path; a row survives when it contains the
    // text or when it is a folder on the way to one that does.
    const int allRows = model->rowCount();
    const QString sample = allRows > 0 ? value(0, DiskViewModel::NameRole).toString() : QString();
    model->setFilter(sample);
    const int filteredRows = model->rowCount();
    QSet<QString> onTheWay;
    for (int row = 0; row < filteredRows; ++row) {
        const QString path = value(row, DiskViewModel::PathRole).toString();
        if (!path.contains(sample, Qt::CaseInsensitive)) continue;
        for (int cut = path.indexOf(QLatin1Char('\\'), 3); cut > 0; cut = path.indexOf(QLatin1Char('\\'), cut + 1)) {
            onTheWay.insert(path.left(cut));
        }
    }
    bool filterWorks = !sample.isEmpty() && filteredRows >= 1;
    for (int row = 0; row < filteredRows && filterWorks; ++row) {
        const QString path = value(row, DiskViewModel::PathRole).toString();
        if (!path.contains(sample, Qt::CaseInsensitive) && !onTheWay.contains(path)) filterWorks = false;
    }
    model->setFilter(QString());

    // The file view lists the files of the subtree, nothing else.
    model->setViewMode(DiskViewModel::FileView);
    bool filesOnly = model->rowCount() > 0;
    for (int row = 0; row < model->rowCount() && filesOnly; ++row) {
        if (value(row, DiskViewModel::DirectoryRole).toBool()) filesOnly = false;
    }
    model->setViewMode(DiskViewModel::TreeView);

    // Expanding a folder adds its children, collapsing takes them away.
    bool expand = false;
    for (int row = 0; row < model->rowCount(); ++row) {
        if (!value(row, DiskViewModel::DirectoryRole).toBool()) continue;
        if (!value(row, DiskViewModel::HasChildrenRole).toBool()) continue;
        const QString id = value(row, DiskViewModel::IdRole).toString();
        const int before = model->rowCount();
        model->toggleExpanded(id);
        const int after = model->rowCount();
        model->toggleExpanded(id);
        expand = after > before && model->rowCount() == before;
        break;
    }

    // The history goes back and forward again.
    bool history = false;
    for (int row = 0; row < model->rowCount(); ++row) {
        if (!value(row, DiskViewModel::DirectoryRole).toBool()) continue;
        model->navigate(value(row, DiskViewModel::IdRole).toString());
        const QString inside = model->currentPath();
        model->goBack();
        const bool back = model->currentPath() == rootPath;
        model->goForward();
        history = back && inside != rootPath && model->currentPath() == inside;
        model->goBack();
        break;
    }

    QStringList passed;
    if (sizeSorted) passed << QStringLiteral("sort");
    if (filterWorks) passed << QStringLiteral("filter");
    if (filesOnly) passed << QStringLiteral("files");
    if (expand) passed << QStringLiteral("expand");
    if (history) passed << QStringLiteral("history");
    *detail = passed.join(QLatin1Char(','));
    return passed.size() == 5;
}

bool QmlHost::runSelfTest(QString* report)
{
    // The address bar's location menu needs at least one fixed drive.
    if (QObject* root = m_engine.rootObjects().value(0)) {
        QQmlExpression drives(m_engine.rootContext(), root,
                              QStringLiteral("appHost.drives().length"));
        if (drives.hasError() || drives.evaluate().toInt() <= 0) {
            *report = QStringLiteral("FAIL\tlocation menu has no drives\n");
            return false;
        }
    }
    // The column grips: dragging the tree's "size" header has to widen that
    // one column, and a double click has to put it back. The events go to the
    // window, so they pass the real hit test and the real mouse area.
    bool columnsOk = false;
    bool legendNameGripOk = false;
    QString columnDetail;
    if (QObject* root = m_engine.rootObjects().value(0)) {
        auto* columnWindow = qobject_cast<QQuickWindow*>(root);
        // A Repeater's delegates are visual children only, so they are not in
        // the window's QObject tree. The Repeater itself is, and itemAt(i)
        // gives each cell, which owns its grip.
        QQuickItem* grip = nullptr;
        if (auto* repeater = root->findChild<QObject*>(QStringLiteral("treeColumnRepeater"))) {
            const int count = repeater->property("count").toInt();
            for (int i = 0; i < count && !grip; ++i) {
                QQuickItem* cell = nullptr;
                if (!QMetaObject::invokeMethod(repeater, "itemAt", Q_RETURN_ARG(QQuickItem*, cell),
                                               Q_ARG(int, i)))
                    break;
                if (cell) grip = cell->findChild<QQuickItem*>(QStringLiteral("columnHandle:tree:size"));
            }
        }
        if (!grip || !grip->isVisible() || grip->width() <= 0) {
            // The dock layout may have the tree behind another tab; there is
            // nothing to drag, so this check is not a failure.
            columnsOk = true;
            columnDetail = QStringLiteral("not-shown");
        } else if (columnWindow) {
            const auto widthOf = [&](const QString& key) {
                QVariant result;
                if (auto* store = grip->property("store").value<QObject*>())
                    QMetaObject::invokeMethod(store, "width", Q_RETURN_ARG(QVariant, result),
                                              Q_ARG(QVariant, key));
                return result.toInt();
            };
            const auto send = [&](QEvent::Type type, const QPointF& local, Qt::MouseButton button,
                                  Qt::MouseButtons buttons) {
                // Sent straight to the mouse area: a synthetic double click
                // through the window does not reach its double-click handler,
                // while the drag itself goes through the same code either way.
                auto* area = grip->findChild<QQuickItem*>(QStringLiteral("columnHandleMouse"));
                if (!area) return;
                QMouseEvent event(type, local, area->mapToGlobal(local), button, buttons, Qt::NoModifier);
                QCoreApplication::sendEvent(area, &event);
            };
            const QPointF center(grip->width() / 2, grip->height() / 2);
            const int before = widthOf(QStringLiteral("size"));
            const int neighborBefore = widthOf(QStringLiteral("alloc"));
            const int totalBefore = columnWindow->findChild<QQuickItem*>(QStringLiteral("diskViewList"))
                                        ? int(columnWindow->findChild<QQuickItem*>(QStringLiteral("diskViewList"))->width()) : 0;
            send(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
            send(QEvent::MouseMove, center + QPointF(24, 0), Qt::NoButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, center + QPointF(24, 0), Qt::LeftButton, Qt::NoButton);
            QCoreApplication::processEvents();
            const int dragged = widthOf(QStringLiteral("size"));
            const int neighborDragged = widthOf(QStringLiteral("alloc"));
            const QPointF again(grip->width() / 2, grip->height() / 2);
            send(QEvent::MouseButtonPress, again, Qt::LeftButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, again, Qt::LeftButton, Qt::NoButton);
            send(QEvent::MouseButtonDblClick, again, Qt::LeftButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, again, Qt::LeftButton, Qt::NoButton);
            QCoreApplication::processEvents();
            const int reset = widthOf(QStringLiteral("size"));
            QQmlExpression expectedExpression(m_engine.rootContext(), root,
                                               QStringLiteral("autoFitWidth('tree', 'size')"));
            const int expected = expectedExpression.evaluate().toInt();
            const int totalAfter = columnWindow->findChild<QQuickItem*>(QStringLiteral("diskViewList"))
                                       ? int(columnWindow->findChild<QQuickItem*>(QStringLiteral("diskViewList"))->width()) : -1;
            columnsOk = dragged == before + 24 && neighborDragged == neighborBefore - 24
                        && totalBefore == totalAfter && reset == expected;
            columnDetail = QStringLiteral("%1->%2 neighbor=%3->%4 autofit=%5 total=%6/%7")
                               .arg(before).arg(dragged).arg(neighborBefore).arg(neighborDragged)
                               .arg(reset).arg(totalBefore).arg(totalAfter);
        }
        // This header cell is not a Repeater delegate, so it is part of the
        // regular QObject tree and can be checked by its stable object name.
        if (auto* legendGrip = root->findChild<QQuickItem*>(QStringLiteral("columnHandle:legend:name")))
            legendNameGripOk = legendGrip->isVisible() && legendGrip->width() > 0;
    }
    if (!columnsOk && columnDetail.isEmpty()) columnDetail = QStringLiteral("no-window");
    if (!runDockChecks(report)) return false;
    auto* window = qobject_cast<QQuickWindow*>(m_engine.rootObjects().value(0));
    auto* chart = window ? window->findChild<ChartItem*>(QStringLiteral("diskViewChart")) : nullptr;
    auto* model = window ? window->findChild<DiskViewModel*>(QStringLiteral("diskModel")) : nullptr;
    if (!chart || !model) {
        *report = QStringLiteral("FAIL\tmissing chart or model\n");
        return false;
    }

    // Zoom only magnifies: the whole picture fills the item at 1:1 and cannot
    // be shrunk away.
    chart->setZoom(0.25);
    const bool zoomFloor = qFuzzyCompare(chart->zoom(), 1.0);
    chart->setZoom(50.0);
    const bool zoomCeiling = chart->zoom() <= 12.0 && chart->zoom() > 1.0;
    chart->resetView();
    if (!zoomFloor || !zoomCeiling) {
        *report = QStringLiteral("FAIL\tchart zoom range\n");
        return false;
    }

    const QString hit = chart->firstRegionHit();
    const bool regions = chart->validateHitTest();
    const bool transform = !hit.isEmpty() && chart->validateHitTransform();
    // A click has to end: after the button is released, moving the mouse over
    // the chart must not keep panning it. The same events first prove that a
    // real drag still pans, so the check cannot pass by doing nothing.
    bool drag = false;
    {
        const QPointF before = chart->pan();
        const QPointF start(10, 10);
        const QPointF moved(60, 60);
        QMouseEvent press(QEvent::MouseButtonPress, start, chart->mapToGlobal(start), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(chart, &press);
        QMouseEvent move(QEvent::MouseMove, moved, chart->mapToGlobal(moved), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(chart, &move);
        const bool dragged = chart->pan() != before;
        QMouseEvent release(QEvent::MouseButtonRelease, moved, chart->mapToGlobal(moved), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(chart, &release);
        const QPointF afterRelease = chart->pan();
        const QPointF hover(200, 200);
        QHoverEvent hoverMove(QEvent::HoverMove, hover, chart->mapToGlobal(hover), hover);
        QCoreApplication::sendEvent(chart, &hoverMove);
        drag = dragged && chart->pan() == afterRelease;
        chart->setPan(before);
    }
    const QString rootPath = model->currentPath();
    bool navigate = false;
    bool back = false;
    if (m_options.scanPath.isEmpty()) {
        model->navigate(QStringLiteral("2"));
        navigate = model->currentPath().contains(QStringLiteral("WinShun"));
        model->goUp();
        back = model->currentPath() == rootPath;
    } else {
        // A scanned tree has no fixed layout: enter the first directory of the
        // list and come back.
        for (int row = 0; row < model->rowCount(); ++row) {
            const QModelIndex index = model->index(row, 0);
            if (!model->data(index, DiskViewModel::DirectoryRole).toBool()) {
                continue;
            }
            model->navigate(model->data(index, DiskViewModel::IdRole).toString());
            navigate = model->currentPath() != rootPath;
            model->goUp();
            back = model->currentPath() == rootPath;
            break;
        }
    }
    // The chart has to answer a real double click: once on a folder tile and,
    // in the sunburst, once in the hole that returns to the parent.
    QStringList chartDetail;
    {
        const auto doubleClick = [&](const QPointF& point) {
            const QPointF global = chart->mapToGlobal(point);
            QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(chart, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(chart, &release);
            QMouseEvent again(QEvent::MouseButtonDblClick, point, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(chart, &again);
            QMouseEvent done(QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(chart, &done);
        };
        // A double click on a folder tile enters the folder, the way the
        // native chart does; the tree's triangle is the in-place opener.
        const QPointF folder = chart->firstDirectoryCenter();
        const QString chartRoot = model->currentPath();
        if (!folder.isNull()) {
            doubleClick(folder);
            QCoreApplication::processEvents();
            if (model->currentPath() != chartRoot) chartDetail << QStringLiteral("double");
        }
        // The centre of the sunburst returns to the parent, so the check goes
        // into a folder and clicks the hole. It runs in the sunburst mode
        // only; the treemap has no hole.
        if (m_options.viewMode == 1) {
            // The folder the chart's double click just entered is the parent
            // the hole returns from.
            const QPointF center(chart->width() / 2, chart->height() / 2);
            QCoreApplication::processEvents();
            doubleClick(center);
            QCoreApplication::processEvents();
            if (model->currentPath() == rootPath) chartDetail << QStringLiteral("center");
        }
        while (model->canGoBack()) model->goBack();
    }
    // The list has to follow the model: opening a folder changes the rows the
    // model reports, and the view only learns about it through the model's own
    // reset. Reading the view's count back is the only way to see that.
    // The interaction checks above open folders; the checks below expect the
    // top level closed, so close whatever was opened.
    const auto collapseAll = [model] {
        QVector<QString> opened;
        for (int row = 0; row < model->rowCount(); ++row) {
            const QModelIndex index = model->index(row, 0);
            if (model->data(index, DiskViewModel::ExpandedRole).toBool()) {
                opened.push_back(model->data(index, DiskViewModel::IdRole).toString());
            }
        }
        for (const QString& id : opened) model->toggleExpanded(id);
    };
    collapseAll();
    // The list has to follow the model: opening a folder changes the rows the
    // model reports, and the view only learns about it through the model's own
    // reset. Reading the view's count back is the only way to see that.
    bool viewFollows = false;
    if (auto* view = window->findChild<QObject*>(QStringLiteral("diskViewList"))) {
        for (int row = 0; row < model->rowCount(); ++row) {
            const QModelIndex index = model->index(row, 0);
            if (!model->data(index, DiskViewModel::DirectoryRole).toBool()) continue;
            if (!model->data(index, DiskViewModel::HasChildrenRole).toBool()) continue;
            if (model->data(index, DiskViewModel::ExpandedRole).toBool()) continue;
            const QString id = model->data(index, DiskViewModel::IdRole).toString();
            const int before = model->rowCount();
            model->toggleExpanded(id);
            QCoreApplication::processEvents();
            viewFollows = model->rowCount() > before
                          && view->property("count").toInt() == model->rowCount();
            model->toggleExpanded(id);
            QCoreApplication::processEvents();
            break;
        }
    }
    const bool chartOk = chartDetail.contains(QStringLiteral("double"))
                         && (m_options.viewMode != 1 || chartDetail.contains(QStringLiteral("center")));
    // A scan that could be held and then let go is part of what the scan mode
    // has to show.
    const bool paused = m_options.scanPath.isEmpty() || m_pauseVerified;
    QString listDetail;
    const bool list = runListChecks(model, &listDetail);
    if (viewFollows) {
        if (!listDetail.isEmpty()) listDetail += QStringLiteral(",view");
    } else {
        listDetail = listDetail.isEmpty() ? QStringLiteral("bad-view") : listDetail + QStringLiteral(",bad-view");
    }
    const bool pass = !hit.isEmpty() && regions && transform && navigate && back && paused && list
                      && viewFollows && drag && chartOk && columnsOk && legendNameGripOk;


    *report = QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6\t%7\t%8\t%9\t%10\t%11\n")
                  .arg(m_options.viewMode)
                  .arg(hit)
                  .arg(regions ? QStringLiteral("regions") : QStringLiteral("no-regions"))
                  .arg(transform ? QStringLiteral("transform") : QStringLiteral("bad-transform"))
                  .arg(navigate ? QStringLiteral("navigate") : QStringLiteral("bad-navigate"))
                  .arg(back ? QStringLiteral("return") : QStringLiteral("bad-return"))
                  .arg(listDetail.isEmpty() ? QStringLiteral("bad-list")
                                             : QStringLiteral("list=%1").arg(listDetail))
                  .arg(drag ? QStringLiteral("drag") : QStringLiteral("bad-drag"))
                  .arg(chartOk ? QStringLiteral("chart=%1").arg(chartDetail.join(QLatin1Char(',')))
                               : QStringLiteral("bad-chart:%1").arg(chartDetail.join(QLatin1Char(','))))
                  .arg(columnsOk && legendNameGripOk ? QStringLiteral("columns=%1,legend-name-grip").arg(columnDetail)
                                 : !legendNameGripOk ? QStringLiteral("bad-legend-name-grip")
                                 : QStringLiteral("bad-columns:%1").arg(columnDetail))
                  .arg(pass ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
    if (!m_options.scanPath.isEmpty() && m_scanService) {
        // What the scan made of the settings, and how much of it was published
        // before it was over.
        *report += QStringLiteral("scan\tworkers=%1\texcluded=%2\tpartials=%3\tfiles=%4\tdirs=%5\tskipped=%6\tpause=%7\n")
                      .arg(m_scanService->scanWorkers())
                      .arg(m_scanService->excludedItems())
                      .arg(m_scanService->partialCount())
                      .arg(m_scanService->fileCount())
                      .arg(m_scanService->dirCount())
                      .arg(m_scanService->skippedDirs())
                      .arg(m_pauseVerified ? QStringLiteral("verified") : QStringLiteral("bad"));
    }
    return pass;
}

QVariantList QmlHost::drives() const
{
    if (m_options.fixtureOnly) {
        return {QVariantMap{
            {"path", QStringLiteral("C:\\")},
            {"label", QStringLiteral("C: (fixture)")},
            {"root", QStringLiteral("C:\\")},
            {"detail", QStringLiteral("内置视觉验收数据")},
            {"usedPercent", 0},
        }};
    }
    const auto gb = [](qint64 bytes) {
        return QStringLiteral("%1 GB").arg(static_cast<double>(bytes) / (1024.0 * 1024 * 1024), 0, 'f', 1);
    };
    QVariantList rows;
    for (const QFileInfo& entry : QDir::drives()) {
        QStorageInfo storage(entry.absoluteFilePath());
        if (!storage.isValid() || !storage.isReady() || storage.bytesTotal() <= 0) continue;
        QString root = entry.absoluteFilePath();
        while (root.endsWith(QLatin1Char('/')) || root.endsWith(QLatin1Char('\\'))) root.chop(1);
        root += QLatin1Char('\\');
        const QString volumeName = storage.displayName().trimmed();
        const QString label = volumeName.isEmpty() ? QStringLiteral("本地磁盘 (%1)").arg(root.left(2))
                                                   : QStringLiteral("%1 (%2)").arg(volumeName, root.left(2));
        const int usedPercent = storage.bytesTotal() > 0
            ? static_cast<int>(100.0 * (storage.bytesTotal() - storage.bytesAvailable()) / storage.bytesTotal()) : 0;
        rows.append(QVariantMap{
            {"path", root},
            {"label", label},
            {"root", root},
            {"detail", QStringLiteral("%1 可用 / %2").arg(gb(storage.bytesAvailable()), gb(storage.bytesTotal()))},
            {"usedPercent", usedPercent},
        });
    }
    return rows;
}

QVariantMap QmlHost::diskForPath(const QString& source) const
{
    const QString path = cleanPath(source);
    for (const QVariant& value : drives()) {
        const QVariantMap drive = value.toMap();
        if (sameOrBelowPath(path, drive.value(QStringLiteral("path")).toString())) return drive;
    }
    return drives().value(0).toMap();
}

QVariantList QmlHost::defaultPaths(const QString& nearPath) const
{
    QVariantList rows;
    QSet<QString> seen;
    const auto appendPath = [&](const QString& raw, bool isDir) {
        if (raw.isEmpty()) return;
        const QString path = cleanPath(raw);
        const QString key = path.toCaseFolded();
        if (seen.contains(key)) return;
        seen.insert(key);
        rows.append(QVariantMap{{"path", path}, {"name", QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName()},
                                {"dir", isDir}, {"parent", QFileInfo(path).absolutePath()}});
    };
    if (m_scanSettings) appendPath(m_scanSettings->lastPath(), true);
    const QFileInfo nearInfo(nearPath);
    const QString folder = nearInfo.isDir() ? nearInfo.absoluteFilePath() : nearInfo.absolutePath();
    if (!folder.isEmpty() && QDir(folder).exists()) {
        const QFileInfoList children = QDir(folder).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& child : children) { appendPath(child.absoluteFilePath(), true); if (rows.size() >= 10) break; }
    }
    for (const QVariant& drive : drives()) {
        const QVariantMap d = drive.toMap();
        if (!seen.contains(cleanPath(d.value(QStringLiteral("path")).toString()).toCaseFolded())) {
            rows.append(QVariantMap{{"path", d.value("path")}, {"name", d.value("label")}, {"dir", true},
                                    {"parent", d.value("path")}, {"detail", d.value("detail")}});
        }
    }
    return rows.mid(0, 12);
}

void QmlHost::searchPathsAsync(const QString& text, int requestId)
{
    ++m_searchRequest;
    const int generation = m_searchRequest;
    if (m_searchProcess) {
        m_searchProcess->disconnect(this);
        if (m_searchProcess->state() != QProcess::NotRunning) m_searchProcess->kill();
        m_searchProcess->deleteLater();
        m_searchProcess = nullptr;
    }
    const QString query = text.trimmed();
    if (m_options.fixtureOnly) {
        emit pathSuggestionsReady(requestId, defaultPaths(m_scanService ? m_scanService->rootPath() : QString()));
        return;
    }
    if (query.isEmpty()) {
        emit pathSuggestionsReady(requestId, defaultPaths(m_scanService ? m_scanService->rootPath() : QString()));
        return;
    }
    auto* process = new QProcess(this);
    m_searchProcess = process;
    process->setProgram(QStringLiteral("ws"));
    process->setArguments({QStringLiteral("find"), query, QStringLiteral("--json"), QStringLiteral("-n"), QStringLiteral("12")});
    connect(process, &QProcess::finished, this, [this, process, generation, requestId](int exitCode, QProcess::ExitStatus exitStatus) {
        QVariantList rows;
        if (generation == m_searchRequest && exitStatus == QProcess::NormalExit && exitCode == 0) {
            const QJsonDocument document = QJsonDocument::fromJson(process->readAllStandardOutput());
            if (document.isArray()) {
                for (const QJsonValue& value : document.array()) {
                    const QJsonObject object = value.toObject();
                    const QString path = object.value(QStringLiteral("path")).toString();
                    if (path.isEmpty()) continue;
                    const QString type = object.value(QStringLiteral("type")).toString();
                    const bool dir = type == QStringLiteral("folder") || type == QStringLiteral("dir") || type == QStringLiteral("directory");
                    rows.append(QVariantMap{{"path", path}, {"name", QFileInfo(path).fileName()}, {"dir", dir},
                                            {"parent", QFileInfo(path).absolutePath()}});
                }
            }
        }
        if (rows.isEmpty() && generation == m_searchRequest) rows = defaultPaths(m_scanService ? m_scanService->rootPath() : QString());
        if (generation == m_searchRequest) emit pathSuggestionsReady(requestId, rows);
        process->deleteLater();
        if (m_searchProcess == process) m_searchProcess = nullptr;
    });
    process->start();
}

QVariantList QmlHost::searchPaths(const QString& text) const
{
    QVariantList rows;
    const QString query = text.trimmed();
    if (query.isEmpty()) return rows;
    // `ws` indexes names (pinyin included) and is already the host's own
    // search. It is started on demand; a missing CLI or a cold index simply
    // leaves the suggestions empty and the field keeps working by hand.
    QProcess process;
    process.setProgram(QStringLiteral("ws"));
    process.setArguments({QStringLiteral("find"), query, QStringLiteral("--json"),
                          QStringLiteral("-n"), QStringLiteral("12")});
    process.start();
    if (!process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished(500);
        return rows;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return rows;
    const QJsonDocument document = QJsonDocument::fromJson(process.readAllStandardOutput());
    if (!document.isArray()) return rows;
    for (const QJsonValue& value : document.array()) {
        const QJsonObject object = value.toObject();
        const QString path = object.value(QStringLiteral("path")).toString();
        if (path.isEmpty()) continue;
        const QString type = object.value(QStringLiteral("type")).toString();
        const bool dir = type == QStringLiteral("folder") || type == QStringLiteral("dir")
                      || type == QStringLiteral("directory");
        rows.append(QVariantMap{
            {"path", path},
            {"name", QFileInfo(path).fileName()},
            {"dir", dir},
            {"parent", QFileInfo(path).absolutePath()},
        });
    }
    return rows;
}

QString QmlHost::chooseFolder(const QString& initial) const
{
    Q_UNUSED(initial);
    // New-style picker; the shell needs an apartment-threaded COM on this
    // thread, which the GUI thread may not have initialised yet.
    const HRESULT com = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    BROWSEINFOW info{};
    info.lpszTitle = const_cast<LPWSTR>(L"选择要扫描的位置");
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
    LPITEMIDLIST id = ::SHBrowseForFolderW(&info);
    QString result;
    if (id) {
        wchar_t path[32768]{};
        if (::SHGetPathFromIDListW(id, path)) result = QString::fromWCharArray(path);
        ::CoTaskMemFree(id);
    }
    if (SUCCEEDED(com)) ::CoUninitialize();
    return result;
}

void QmlHost::setAppearance(const QVariantMap& appearance)
{
    if (m_theme) m_theme->setAppearance(appearance);
    if (auto* chart = window() ? window()->findChild<ChartItem*>(QStringLiteral("diskViewChart")) : nullptr) {
        chart->setSurfaceToken(m_theme->backdrop() ? QColor() : m_theme->surfaceAlt());
        chart->setBorderToken(m_theme->border());
        chart->setNeutralToken(m_theme->textMuted());
        chart->setDimToken(m_theme->dark() ? QColor(QStringLiteral("#323232")) : QColor(QStringLiteral("#e8e8e8")));
        chart->setFontFamily(m_theme->fontFamily());
        chart->setFontPixels(m_theme->fontSmall());
    }
}

void QmlHost::setLocalAppearanceMode(int mode)
{
    if (m_scanSettings) m_scanSettings->setAppearanceMode(mode);
    if (m_theme) m_theme->setLocalMode(mode);
}

void QmlHost::performFileActionAsync(const QString& path, bool permanent,
                                     const QString& confirmation, const QString& nodeId,
                                     int requestId)
{
    const QVariantMap validation = validateFileActionPath(path);
    if (!validation.value(QStringLiteral("ok")).toBool()) {
        emit fileActionFinished(requestId, nodeId, {{"ok", false}, {"error", validation.value(QStringLiteral("reason"))}});
        return;
    }
    if (permanent && confirmation.compare(QFileInfo(path).fileName(), Qt::CaseInsensitive) != 0) {
        emit fileActionFinished(requestId, nodeId, {{"ok", false}, {"error", QStringLiteral("名称不匹配，未删除")}});
        return;
    }

    const QString checkedPath = validation.value(QStringLiteral("path")).toString();
    QPointer<QmlHost> guard(this);
    QThread* worker = QThread::create([guard, checkedPath, permanent, nodeId, requestId] {
        const HRESULT initialized = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        Microsoft::WRL::ComPtr<IFileOperation> operation;
        Microsoft::WRL::ComPtr<IShellItem> item;
        auto sink = Microsoft::WRL::Make<FileOperationSink>();
        sink->requireRecycle = !permanent;
        HRESULT hr = ::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(operation.GetAddressOf()));
        if (SUCCEEDED(hr)) hr = ::SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(checkedPath.utf16()), nullptr, IID_PPV_ARGS(item.GetAddressOf()));
        if (SUCCEEDED(hr)) hr = operation->SetOperationFlags(permanent
            ? (FOF_NOCONFIRMATION | FOF_NOERRORUI | FOFX_EARLYFAILURE)
            : (FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOFX_EARLYFAILURE));
        if (SUCCEEDED(hr)) hr = operation->DeleteItem(item.Get(), sink.Get());
        if (SUCCEEDED(hr)) hr = operation->PerformOperations();
        BOOL aborted = TRUE;
        if (SUCCEEDED(hr)) hr = operation->GetAnyOperationsAborted(&aborted);
        const bool ok = SUCCEEDED(hr) && !aborted && sink->completed
            && (permanent ? SUCCEEDED(sink->itemResult) : sink->recycled);
        const QString error = ok ? QString() : QStringLiteral("Windows 文件操作失败 (HRESULT 0x%1)")
            .arg(static_cast<quint32>(FAILED(hr) ? hr : sink->itemResult), 8, 16, QLatin1Char('0'));
        if (SUCCEEDED(initialized)) ::CoUninitialize();
        if (guard) QMetaObject::invokeMethod(guard, [guard, requestId, nodeId, ok, error] {
            if (guard) emit guard->fileActionFinished(requestId, nodeId, {{"ok", ok}, {"error", error}});
        }, Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void QmlHost::setTheme(const QString& theme) const
{
    // The tokens land in one object the whole window reads; the chart keeps
    // its plain light/dark string for its greys, and the accent reaches it
    // through the QML binding on the tokens.
    if (m_theme) m_theme->setTheme(theme);
    if (auto* chart = window()->findChild<ChartItem*>(QStringLiteral("diskViewChart"))) {
        chart->setTheme(theme);
        if (m_theme) {
            chart->setSurfaceToken(m_theme->backdrop() ? QColor() : m_theme->surfaceAlt());
            chart->setBorderToken(m_theme->border());
            chart->setNeutralToken(m_theme->textMuted());
            chart->setDimToken(m_theme->dark() ? QColor(QStringLiteral("#323232")) : QColor(QStringLiteral("#e8e8e8")));
            chart->setFontFamily(m_theme->fontFamily());
            chart->setFontPixels(m_theme->fontSmall());
        }
    }
}

void QmlHost::copyToClipboard(const QString& text) const
{
    if (auto* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

void QmlHost::revealInExplorer(const QString& path) const
{
    if (path.isEmpty()) return;
    const HRESULT initialized = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    PIDLIST_ABSOLUTE full = nullptr;
    HRESULT hr = ::SHParseDisplayName(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, &full, 0, nullptr);
    if (SUCCEEDED(hr) && full) {
        PIDLIST_ABSOLUTE parent = ::ILCloneFull(full);
        if (parent) {
            ::ILRemoveLastID(parent);
            PCUITEMID_CHILD child = ::ILFindLastID(full);
            hr = ::SHOpenFolderAndSelectItems(parent, 1, &child, 0);
            ::CoTaskMemFree(parent);
        } else hr = E_OUTOFMEMORY;
        ::CoTaskMemFree(full);
    }
    if (FAILED(hr)) {
        const QString parent = QFileInfo(path).absolutePath();
        if (!parent.isEmpty()) ::ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(parent.utf16()), nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (SUCCEEDED(initialized)) ::CoUninitialize();
}

bool QmlHost::openPath(const QString& path) const
{
    if (path.isEmpty()) return false;
    return reinterpret_cast<INT_PTR>(::ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

bool QmlHost::showProperties(const QString& path) const
{
    if (path.isEmpty()) return false;
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask = SEE_MASK_INVOKEIDLIST;
    info.hwnd = window() ? reinterpret_cast<HWND>(window()->winId()) : nullptr;
    info.lpVerb = L"properties";
    info.lpFile = reinterpret_cast<LPCWSTR>(path.utf16());
    info.nShow = SW_SHOWNORMAL;
    return ::ShellExecuteExW(&info) != FALSE;
}

bool QmlHost::copyFilesToClipboard(const QString& path, bool cut) const
{
    if (path.isEmpty()) return false;
    std::wstring list = QDir::toNativeSeparators(path).toStdWString();
    list.push_back(L'\0'); list.push_back(L'\0');
    const SIZE_T bytes = sizeof(DROPFILES) + list.size() * sizeof(wchar_t);
    HGLOBAL drop = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    HGLOBAL effect = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (!drop || !effect) { if (drop) ::GlobalFree(drop); if (effect) ::GlobalFree(effect); return false; }
    if (auto* header = static_cast<DROPFILES*>(::GlobalLock(drop))) {
        header->pFiles = sizeof(DROPFILES); header->fWide = TRUE;
        std::memcpy(reinterpret_cast<char*>(header) + sizeof(DROPFILES), list.c_str(), list.size() * sizeof(wchar_t));
        ::GlobalUnlock(drop);
    }
    if (auto* value = static_cast<DWORD*>(::GlobalLock(effect))) {
        *value = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY; ::GlobalUnlock(effect);
    }
    HWND owner = window() ? reinterpret_cast<HWND>(window()->winId()) : nullptr;
    if (!::OpenClipboard(owner)) { ::GlobalFree(drop); ::GlobalFree(effect); return false; }
    ::EmptyClipboard();
    const bool ok = ::SetClipboardData(CF_HDROP, drop) != nullptr;
    if (!ok) ::GlobalFree(drop);
    const UINT format = ::RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    if (ok && format && !::SetClipboardData(format, effect)) ::GlobalFree(effect);
    else if (!ok || !format) ::GlobalFree(effect);
    ::CloseClipboard();
    return ok;
}

QVariantMap QmlHost::validateFileActionPath(const QString& source) const
{
    const QString path = cleanPath(QFileInfo(source).canonicalFilePath().isEmpty() ? source : QFileInfo(source).canonicalFilePath());
    if (path.isEmpty() || !QDir::isAbsolutePath(path)) return {{"ok", false}, {"reason", QStringLiteral("路径不是绝对路径")}};
    if (!m_scanService) return {{"ok", false}, {"reason", QStringLiteral("扫描服务不可用")}};
    if (m_scanService->isRunning()) return {{"ok", false}, {"reason", QStringLiteral("扫描进行中，请完成扫描后再操作")}};
    if (!QFileInfo::exists(path)) return {{"ok", false}, {"reason", QStringLiteral("项目已不存在，请重新扫描")}};
    if (protectedPath(path)) return {{"ok", false}, {"reason", QStringLiteral("系统项目、重解析点或非本地固定磁盘受保护")}};
    return {{"ok", true}, {"reason", QString()}, {"path", path}};
}

QVariantMap QmlHost::canModifyPath(const QString& path) const { return validateFileActionPath(path); }

bool QmlHost::openSettingsDialog(QString* error){
    auto* window = qobject_cast<QQuickWindow*>(m_engine.rootObjects().value(0));
    if (!window) {
        *error = QStringLiteral("the window is not available");
        return false;
    }
    auto* dialog = window->findChild<QObject*>(QStringLiteral("scanSettingsDialog"));
    if (!dialog) {
        *error = QStringLiteral("the scan settings dialog is not part of the window");
        return false;
    }
    if (!QMetaObject::invokeMethod(dialog, "open")) {
        *error = QStringLiteral("the scan settings dialog did not open");
        return false;
    }
    return true;
}

bool QmlHost::saveScreenshot(const QString& path)
{
    auto* window = qobject_cast<QQuickWindow*>(m_engine.rootObjects().value(0));
    if (!window) return false;
    const QByteArray visualState = qgetenv("WS_VISUAL_STATE");
    if (m_options.fixtureOnly && !visualState.isEmpty()) {
        const QString state = QString::fromLatin1(visualState);
        if (state == QLatin1String("columns") || state == QLatin1String("suggestions")
            || state == QLatin1String("drives") || state == QLatin1String("resized-columns")
            || state == QLatin1String("address-focus") || state == QLatin1String("dock-tabs")
            || state == QLatin1String("dock-compass") || state == QLatin1String("columns-menu")
            || state == QLatin1String("file-filter") || state == QLatin1String("extension-highlight")
            || state == QLatin1String("context-menu") || state == QLatin1String("delete-confirmation")) {
            QQmlExpression prepare(m_engine.rootContext(), window,
                                  QStringLiteral("prepareFixtureVisualState('%1')").arg(state));
            prepare.evaluate();
            if (prepare.hasError()) {
                qWarning() << "fixture visual state failed:" << state << prepare.error().toString();
                return false;
            }
            QCoreApplication::processEvents();
            if (state == QLatin1String("context-menu") || state == QLatin1String("delete-confirmation")
                || state == QLatin1String("drives") || state == QLatin1String("dock-tabs")
                || state == QLatin1String("dock-compass") || state == QLatin1String("columns-menu")) {
                for (int frame = 0; frame < 4; ++frame)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            }
            if (state == QLatin1String("drives")) {
                // The ComboBox popup lives in its own transient window
                // (popupType Window); prefer that grab so the open picker is
                // visible in the shot, and fall back to the main window.
                const auto windows = QGuiApplication::allWindows();
                for (QWindow* candidate : windows) {
                    if (candidate == window || !candidate->isVisible()) continue;
                    auto* popupWindow = qobject_cast<QQuickWindow*>(candidate);
                    if (!popupWindow) continue;
                    const QImage popup = popupWindow->grabWindow();
                    if (!popup.isNull() && popup.width() > 60 && popup.height() > 20)
                        return popup.save(path);
                }
            }
        }
        if (state == QLatin1String("columns")) {
            QQmlExpression widen(m_engine.rootContext(), window,
                                 QStringLiteral("columnStore('tree').setWidth('name', 480)"));
            widen.evaluate();
            QCoreApplication::processEvents();
            if (auto* list = window->findChild<QQuickItem*>(QStringLiteral("diskViewList")))
                list->setProperty("contentX", 160);
            QCoreApplication::processEvents();
        }
    }
    // A screenshot aid, next to WS_APPEARANCE: select a node by its display
    // path, so the selected state can be looked at without a pointer.
    if (const QByteArray wanted = qgetenv("WS_SELECT_PATH"); !wanted.isEmpty()) {
        if (auto* model = window->findChild<DiskViewModel*>(QStringLiteral("diskModel"))) {
            const QString wantedPath = QString::fromUtf8(wanted);
            // Open the folders on the way down (the rows only list what is
            // open), then select the node. The root's own name may carry
            // characters a shell mangles, so a path tail is enough.
            QString prefix;
            QString selected;
            bool started = false;
            const QStringList parts = wantedPath.split(QLatin1Char('\\'), Qt::SkipEmptyParts);
            for (const QString& part : parts) {
                prefix = prefix.isEmpty() ? part : prefix + QLatin1Char('\\') + part;
                selected.clear();
                for (int row = 0; row < model->rowCount(); ++row) {
                    const QModelIndex index = model->index(row, 0);
                    if (model->data(index, DiskViewModel::NameRole).toString() != part) continue;
                    const QString rowPath = model->data(index, DiskViewModel::PathRole).toString();
                    if (rowPath != prefix && !rowPath.endsWith(QLatin1Char('\\') + prefix)) continue;
                    selected = model->data(index, DiskViewModel::IdRole).toString();
                    break;
                }
                if (selected.isEmpty()) {
                    // The leading components may be the root's own name, which
                    // is not a row; skip them, but stop once matching has begun.
                    if (started) break;
                    prefix.clear();
                    continue;
                }
                started = true;
                if (part != parts.last() && !model->isExpanded(selected)) model->toggleExpanded(selected);
            }
            if (!selected.isEmpty()) model->select(selected);
            QCoreApplication::processEvents();
        }
    }
    const QString state = QString::fromLatin1(visualState);
    if (m_options.fixtureOnly && (state == QLatin1String("drives")
                                  || state == QLatin1String("context-menu")
                                  || state == QLatin1String("delete-confirmation"))) {
        // Qt Quick Controls render Menu/Dialog as a separate popup window, so
        // the main window grab deliberately cannot contain it.
        for (QWindow* candidate : QGuiApplication::allWindows()) {
            if (candidate == window || !candidate->isVisible()) continue;
            auto* popupWindow = qobject_cast<QQuickWindow*>(candidate);
            if (!popupWindow) continue;
            const QImage popup = popupWindow->grabWindow();
            if (!popup.isNull() && popup.width() > 100 && popup.height() > 40) {
                return popup.save(path);
            }
            qWarning() << "fixture popup was not capturable:" << candidate->title()
                       << candidate->size() << popup.size();
        }
    }
    return window->grabWindow().save(path);
}

bool QmlHost::parseArgs(const QStringList& arguments, Options* options, QString* error)
{
    for (int i = 1; i < arguments.size(); ++i) {
        const QString argument = arguments.at(i);
        const auto value = [&](const QString& name) -> QString {
            if (i + 1 >= arguments.size()) {
                *error = QStringLiteral("%1 needs a value").arg(name);
                return {};
            }
            return arguments.at(++i);
        };

        if (argument == QStringLiteral("--help") || argument == QStringLiteral("-h")) {
            options->help = true;
        } else if (argument == QStringLiteral("--qml-dir")) {
            options->qmlDir = value(argument);
        } else if (argument == QStringLiteral("--fixture")) {
            options->fixture = value(argument);
        } else if (argument == QStringLiteral("--scan")) {
            options->scanPath = value(argument);
        } else if (argument == QStringLiteral("--view-mode")) {
            bool ok = false;
            const int mode = value(argument).toInt(&ok);
            if (!ok || (mode != 0 && mode != 1)) {
                *error = QStringLiteral("--view-mode takes 0 (treemap) or 1 (sunburst)");
                return false;
            }
            options->viewMode = mode;
        } else if (argument == QStringLiteral("--screenshot")) {
            options->screenshotPath = value(argument);
        } else if (argument == QStringLiteral("--delay")) {
            bool ok = false;
            const int delay = value(argument).toInt(&ok);
            if (!ok || delay < 0) {
                *error = QStringLiteral("--delay takes milliseconds");
                return false;
            }
            options->screenshotDelayMs = delay;
        } else if (argument == QStringLiteral("--self-test")) {
            options->selfTest = true;
        } else if (argument == QStringLiteral("--fixture-only")) {
            options->fixtureOnly = true;
        } else if (argument == QStringLiteral("--settings")) {
            options->openSettings = true;
        } else if (argument == QStringLiteral("--pause")) {
            options->pauseOnStart = true;
        } else if (argument == QStringLiteral("--hold-window")) {
            options->holdWindow = true;
        } else if (argument == QStringLiteral("--maximize")) {
            const QString group = value(argument);
            if (group != QStringLiteral("tables") && group != QStringLiteral("chart")) {
                *error = QStringLiteral("--maximize takes tables or chart");
                return false;
            }
            options->maximizeOnStart = group;
        } else if (argument == QStringLiteral("--bench-chart")) {
            bool ok = false;
            const int nodes = value(argument).toInt(&ok);
            if (!ok || nodes < 10) {
                *error = QStringLiteral("--bench-chart takes the number of nodes (at least 10)");
                return false;
            }
            options->benchNodes = nodes;
        } else {
            *error = QStringLiteral("unknown option %1").arg(argument);
            return false;
        }

        if (!error->isEmpty()) return false;
    }
    return true;
}

QString QmlHost::usage()
{
    return QStringLiteral(
        "Usage: diskview-qml [options]\n"
        "  --qml-dir <dir>    load Main.qml from <dir> instead of the built-in module\n"
        "  --fixture <file>   disk tree JSON; the built-in fixture is used by default\n"
        "  --scan <path>      scan <path> for real and show the result\n"
        "  --view-mode <0|1>  0 = treemap, 1 = sunburst\n"
        "  --screenshot <png> save a window grab and exit\n"
        "  --delay <ms>       wait before the grab (default 1200)\n"
        "  --settings         open the scan settings dialog on startup\n"
        "  --pause            start the scan and hold it there\n"
        "  --hold-window      keep a standalone validation window open after scan\n"
        "  --maximize <tables|chart>  open with that pane in the fullscreen layer\n"
        "  --bench-chart <n>  build a tree of n nodes and time the chart pipeline\n"
        "  --self-test        run the interaction checks and print the verdict\n"
        "  --fixture-only     use built-in data without enumerating mounted volumes\n"
        "  -h, --help         show this help\n");
}
