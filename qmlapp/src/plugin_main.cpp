// The WinShun plugin entry of the QML edition.
//
// WinShun runs the plugin as a process named in plugin.json; diskview-qml.exe
// is a starter without Qt that loads DiskViewQmlApp.dll (built on the host's
// own Qt) and calls winshunDiskViewQmlMain. That function owns the GUI thread
// and runs the protocol on a worker, the same split WinShun's own
// winshun.capture and ra9fael.binarylab-qml use.
//
// The window itself is the existing QmlHost: this file only maps the protocol
// onto it.

#include "QmlHost.h"
#include "ScanService.h"
#include "ScanSettings.h"
#include "scan.h"

#include "winshun.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>

#include <windows.h>

#include <cstdio>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace {

std::string tr(bool zh, const char* chinese, const char* english)
{
    return zh ? chinese : english;
}

// plugin.json's defaultView: anything but "sunburst" opens on the treemap.
int defaultViewMode(const winshun::Json& settings)
{
    const winshun::Json& v = settings["defaultView"];
    return v.isString() && v.toString() == "sunburst" ? 1 : 0;
}

// The scan settings the host owns (its own settings page shows and persists
// them), as the raw values the window applies.
QVariantMap hostScanSettings(const winshun::Json& settings)
{
    QVariantMap map;
    const auto put = [&map, &settings](const char* key) {
        const winshun::Json& value = settings[key];
        if (value.isString() && !value.toString().empty()) {
            map.insert(QString::fromLatin1(key), QString::fromStdString(value.toString()));
        }
    };
    put("scanMode");
    put("workers");
    put("media");
    put("preview");
    return map;
}

// One row per fixed drive, then the row that opens the window; the same shape
// the native plugin answers with, so the launcher looks unchanged.
winshun::Items emptyItems(bool zh, const std::wstring& lastPath)
{
    winshun::Items items;
    int score = 100;
    for (const diskview::Drive& d : diskview::listFixedDrives()) {
        const std::string used = winshun::utf8(winshun::wide(diskview::formatBytes(d.used(), zh)));
        const std::string total = winshun::utf8(winshun::wide(diskview::formatBytes(d.total, zh)));
        const unsigned percent = d.total > 0 ? static_cast<unsigned>(d.used() * 100 / d.total) : 0;
        const std::string letter = std::string(1, static_cast<char>(d.letter)) + ":";
        winshun::Item item("disk.drive." + std::string(1, static_cast<char>(d.letter)),
                           tr(zh, "扫描 ", "Scan ") + letter,
                           used + tr(zh, " 已用 · ", " used · ") + total
                               + tr(zh, " 总容量 · ", " total · ") + std::to_string(percent) + tr(zh, "%", "%"));
        item.withIcon("color:#0F766E")
            .withScore(score--)
            .withData(winshun::Json(winshun::Json::Object{{"kind", "open"}, {"path", letter + "\\"}}));
        items.push_back(std::move(item));
    }

    std::string title = tr(zh, "打开磁盘透视", "Open Disk View");
    std::string subtitle = tr(zh, "Enter 打开窗口 · 输入 disk <路径> 可直接开始扫描，如 disk D:\\工作",
                              "Enter opens the window · type disk <path> to scan right away, e.g. disk D:\\work");
    if (!lastPath.empty()) {
        subtitle += tr(zh, " · 上次：", " · last: ") + winshun::utf8(lastPath);
        title += tr(zh, " · 上次扫描 ", " · last scan ") + winshun::utf8(lastPath);
    }
    winshun::Item open("disk.open", title, subtitle);
    open.withIcon("color:#0F766E")
        .withScore(50)
        .withData(winshun::Json(winshun::Json::Object{{"kind", "open"}, {"path", ""}}));
    items.push_back(std::move(open));

    const std::string usage = "disk C:\\";
    winshun::Item help("disk.help", usage,
                       tr(zh, "回车把示例填入输入框 · 窗口里改路径、按 F5 随时重扫",
                          "Enter fills the example · change the path in the window and press F5 to rescan"));
    help.invalid()
        .withIcon("color:#185FA5")
        .withScore(90)
        .withCopy(usage)
        .withAutocomplete(usage)
        .withPreview(usage + "\n\n"
                     + tr(zh, "方块图与圆盘共用同一份扫描数据，单击选中；双击方块在左侧文件树里展开该文件夹，"
                              "双击列表里的文件夹才进入；顶部面包屑和退格键返回上级。",
                              "The treemap and the sunburst share one scan; click to select, "
                              "double-click a tile to expand that folder in the list, and double-click a row to enter; "
                              "the breadcrumb and Backspace go up."));
    items.push_back(std::move(help));
    return items;
}

winshun::Items pathItems(const std::string& text, bool zh)
{
    winshun::Items items;
    winshun::Item item("disk.open", tr(zh, "打开磁盘透视 · ", "Open Disk View · ") + text,
                       tr(zh, "Enter 打开窗口并扫描此路径", "Enter opens the window and scans this path"));
    item.withIcon("color:#0F766E")
        .withScore(100)
        .withData(winshun::Json(winshun::Json::Object{{"kind", "open"}, {"path", text}}));
    items.push_back(std::move(item));
    return items;
}

QString fromUtf8(const std::string& text)
{
    return QString::fromStdString(text);
}

// The window, owned by the GUI thread. Opening and closing is marshalled onto
// that thread; the protocol thread never touches a widget.
QmlHost* g_host = nullptr;
QWindow* g_window = nullptr;
// Where the host's QtQuick modules live: WinShun's qml folder.
QStringList g_importPaths;
// The host's settings, kept for windows that are opened later.
QVariantMap g_hostSettings;
std::atomic<bool> g_closing{false};

struct WindowRequest {
    QString path;
    int viewMode = 0;
    QString settingsFile;
};

// Runs on the GUI thread: creates the window, and starts the scan when a path
// came with the request.
bool openWindowOnGui(const WindowRequest& request, QString* error)
{
    if (g_host && g_window) {
        // Already open: retarget the existing window instead of stacking
        // another one, which is what the host expects from "open" again.
        if (!request.path.isEmpty()) {
            if (auto* service = g_host->scanner()) {
                service->start(request.path);
            }
            if (auto* settings = g_host->settings()) {
                settings->setLastPath(request.path);
                settings->save();
            }
        }
        if (auto* window = qobject_cast<QQuickWindow*>(g_window)) {
            window->show();
            window->raise();
            window->requestActivate();
        }
        return true;
    }

    QmlHost::Options options;
    options.viewMode = request.viewMode;
    options.scanPath = request.path;
    options.importPaths = g_importPaths;
    options.hostSettings = g_hostSettings;
    auto* host = new QmlHost(options);
    if (!host->load(error)) {
        delete host;
        return false;
    }
    auto* rootWindow = qobject_cast<QQuickWindow*>(host->window());
    if (!rootWindow) {
        *error = QStringLiteral("the window did not come up");
        delete host;
        return false;
    }
    // The host owns its own lifetime: the window closes when the user closes
    // it, and the next "open" makes a new one.
    QObject::connect(rootWindow, &QWindow::visibleChanged, host, [rootWindow, host] {
        if (!rootWindow->isVisible()) {
            rootWindow->deleteLater();
            host->deleteLater();
            g_host = nullptr;
            g_window = nullptr;
        }
    });
    g_host = host;
    g_window = rootWindow;
    // Do not raise a new window when the action came from the WinShun
    // launcher: the foreground hand-off has to be granted while we still own
    // it, before the worker thread asks ShellExecuteEx to show the prompt.
    rootWindow->show();
    rootWindow->raise();
    rootWindow->requestActivate();
    // The host asked for this path: start reading it now, the way the
    // standalone launcher does with --scan.
    if (!request.path.isEmpty()) {
        QString scanError;
        if (!host->startScan(request.path, &scanError)) {
            std::fprintf(stderr, "diskview-qml: scan did not start: %s\n", qUtf8Printable(scanError));
        }
    }
    return true;
}

class DiskViewQml : public winshun::Plugin {
public:
    std::string name() const override { return "ra9fael.diskview.qml"; }

    // The host's data directory is where the settings file belongs, so the
    // model finds the last path and the settings dialog saves beside it.
    void initialized() override
    {
        if (!dataDir().empty()) {
            qputenv("WS_DATA_DIR", QByteArray::fromStdString(dataDir()));
        }
        g_hostSettings = hostScanSettings(settings());
    }

    void settingsChanged(const winshun::Json& s) override
    {
        // The host edited its settings page: apply them to the open window at
        // once, and remember them for the next one.
        g_hostSettings = hostScanSettings(s);
        if (g_host) {
            const QVariantMap map = g_hostSettings;
            QMetaObject::invokeMethod(qGuiApp, [map] {
                if (g_host) g_host->applyHostSettings(map);
            }, Qt::BlockingQueuedConnection);
        }
    }

    ~DiskViewQml() override
    {
        // The window lives on the GUI thread; close it there and wait, so the
        // process does not exit with a UI thread still running.
        if (g_host) {
            QMetaObject::invokeMethod(qGuiApp, [] {
                if (auto* window = qobject_cast<QQuickWindow*>(g_window)) window->close();
                g_host = nullptr;
                g_window = nullptr;
            }, Qt::BlockingQueuedConnection);
        }
    }

    winshun::Items query(const winshun::Query& q) override
    {
        const bool zh = chinese();
        const std::string text = winshun::trim(q.text);
        if (text == "help" || text == "?" || text == "帮助") return emptyItems(zh, {});
        if (text.empty()) {
            // The settings file lives in the host's data directory; reading it
            // needs the directory the host named at initialize.
            std::wstring last;
            if (!dataDir().empty()) {
                qputenv("WS_DATA_DIR", QByteArray::fromStdString(dataDir()));
                ScanSettings settings;
                settings.load();
                last = settings.lastPath().toStdWString();
            }
            return emptyItems(zh, last);
        }
        return pathItems(text, zh);
    }

    winshun::Effects action(const winshun::ActionRequest& a) override
    {
        if (a.action != "open" && a.action != "default") return {winshun::effect::keep()};
        const winshun::Json data = a.item["data"];
        const std::string path = data["path"].isString() ? data["path"].toString() : winshun::trim(a.text);
        const bool zh = chinese();

        WindowRequest request;
        request.path = fromUtf8(path);
        request.viewMode = defaultViewMode(settings());

        QString error;
        bool opened = false;
        if (QThread::currentThread() == qGuiApp->thread()) {
            opened = openWindowOnGui(request, &error);
        } else {
            // The protocol runs on its own thread: the window has to be made
            // where the event loop that drives it lives.
            QMetaObject::invokeMethod(qGuiApp, [&] { opened = openWindowOnGui(request, &error); },
                                      Qt::BlockingQueuedConnection);
        }
        if (!opened) {
            winshun::debug("ra9fael.diskview.qml: window failed: " + error.toStdString());
            return {winshun::effect::notify(tr(zh, "无法打开磁盘透视窗口：", "Could not open the disk view window: ")
                                            + error.toStdString()),
                    winshun::effect::hide()};
        }
        return {winshun::effect::notify(tr(zh, "磁盘透视已打开", "Disk view opened")),
                winshun::effect::hide()};
    }

    void appearanceChanged() override
    {
        // The QML window has its own palette; nothing to push into it yet.
        winshun::debug("ra9fael.diskview.qml: appearanceChanged");
    }

    void shuttingDown() override
    {
        if (g_closing.exchange(true)) return;
        QMetaObject::invokeMethod(qGuiApp, [] {
            if (auto* window = qobject_cast<QQuickWindow*>(g_window)) window->close();
        }, Qt::BlockingQueuedConnection);
    }
};

} // namespace

// The entry the stub calls. The GUI thread is this thread; the protocol runs
// on a worker, and serve() returning ends the process.
extern "C" __declspec(dllexport) int winshunDiskViewQmlMain(const wchar_t* appDirName)
{
    Q_UNUSED(appDirName);
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    // Two deployments are possible: the plugin ships its own Qt beside the
    // DLL, or it runs on WinShun's. The shipped one wins when it is there,
    // because that is the Qt the DLL was linked against.
    const QString hostDir = QString::fromWCharArray(appDirName);
    wchar_t self[32768]{};
    const DWORD selfLength = ::GetModuleFileNameW(nullptr, self, DWORD(std::size(self)));
    const QString selfDir = selfLength
        ? QFileInfo(QString::fromWCharArray(self, int(selfLength))).absolutePath()
        : QString();
    // The test is whether a Qt runtime sits beside us, not whether some QML
    // file does: a package may ship only the modules the host lacks.
    const bool selfContained = !selfDir.isEmpty()
        && QFileInfo::exists(selfDir + QStringLiteral("/Qt6Core.dll"));
    const QString qtDir = selfContained ? selfDir : hostDir;
    QCoreApplication::setLibraryPaths({qtDir});
    // Imports come from whatever this package carries first, then the host's
    // qml tree: a package may ship only the modules the host lacks.
    if (!selfDir.isEmpty() && QFileInfo::exists(selfDir + QStringLiteral("/qml")))
        g_importPaths.append(selfDir + QStringLiteral("/qml"));
    if (!hostDir.isEmpty() && QFileInfo::exists(hostDir + QStringLiteral("/qml")))
        g_importPaths.append(hostDir + QStringLiteral("/qml"));
    qputenv("QML_IMPORT_PATH", g_importPaths.join(QLatin1Char(';')).toUtf8());

    int argc = 1;
    char name[] = "diskview-qml";
    char* argv[] = {name, nullptr};
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Disk View QML"));
    // The host may open the window again after the user closed it.
    QGuiApplication::setQuitOnLastWindowClosed(false);

    int rc = 0;
    std::thread serve([&] {
        DiskViewQml plugin;
        rc = plugin.serve();
        QMetaObject::invokeMethod(&app, &QCoreApplication::quit, Qt::QueuedConnection);
    });
    app.exec();
    serve.join();
    return rc;
}
