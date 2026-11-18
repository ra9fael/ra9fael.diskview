// The WinShun plugin entry of the QML edition.
//
// WinShun runs the plugin as a process named in plugin.json; diskview-qml.exe
// is a starter without Qt that loads DiskViewQmlApp.dll (built on the host's
// own Qt) and calls winshunDiskViewQmlMain. That function owns the GUI thread
// and runs the protocol on a worker, the same split WinShun's own
// winshun.capture and ra9fael.binarylab-qml use.
//
// Two windows share one settings object: the viewer and the settings window
// the host's card button opens. The protocol thread never touches either.

#include "QmlHost.h"
#include "ScanService.h"
#include "ScanSettings.h"
#include "scan.h"

#include "winshun.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#include <QVariantMap>

#include <windows.h>

#include <cstdio>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace {

std::string tr(bool zh, const char* chinese, const char* english)
{
    return zh ? chinese : english;
}

QString fromUtf8(const std::string& text)
{
    return QString::fromStdString(text);
}

// The host's settings object as the plain map ScanSettings reads. The plugin
// SDK speaks its own JSON; Qt's reader is right here and does the nesting.
QVariantMap fromJson(const winshun::Json& value)
{
    const QJsonDocument document = QJsonDocument::fromJson(QString::fromStdString(value.dump()).toUtf8());
    return document.toVariant().toMap();
}

winshun::Json fromVariant(const QVariant& value)
{
    switch (value.typeId()) {
    case QMetaType::Bool:
        return winshun::Json(value.toBool());
    case QMetaType::Int:
    case QMetaType::LongLong:
        return winshun::Json(value.toLongLong());
    case QMetaType::Double:
        return winshun::Json(value.toDouble());
    default:
        return winshun::Json(value.toString().toStdString());
    }
}

// The window, owned by the GUI thread. Opening and closing is marshalled onto
// that thread; the protocol thread never touches a widget.
QmlHost* g_host = nullptr;
QWindow* g_window = nullptr;
// The settings window: its own window, without the scanner or the disk tree.
QmlHost* g_settingsHost = nullptr;
QWindow* g_settingsWindow = nullptr;
// Where the host's QtQuick modules live: WinShun's qml folder.
QStringList g_importPaths;
// The last appearance object and theme string from the host, kept as JSON so a
// window opened later still matches WinShun before the next notification.
QString g_appearanceJson = QStringLiteral("{}");
QString g_theme = QStringLiteral("light");
std::atomic<bool> g_closing{false};

// The settings both windows read, and the bridge the settings window writes
// the host's keys back through. GUI thread only; they outlive every window.
ScanSettings* g_settings = nullptr;
class SettingsBridge* g_settingsBridge = nullptr;

// What the folder menu acts on, as the protocol thread sees it: the host's
// switch when it sent one, the local value otherwise.
std::atomic<bool> g_folderMenuOn{true};

// The host's public settings go back to WinShun, which validates them against
// the manifest, stores them and answers through settingsChanged. Only the keys
// the user changed are sent, and only from here: nothing else re-enters the
// host, so a host update cannot bounce back to it.
class SettingsBridge : public QObject {
    Q_OBJECT
public:
    explicit SettingsBridge(winshun::Plugin* plugin) : m_plugin(plugin) {}

    // The plugin outlives this object by a moment at shutdown; the pointer is
    // dropped there so a late call cannot reach a destroyed plugin.
    void forgetPlugin() { m_plugin = nullptr; }

    Q_INVOKABLE void applyPublic(const QVariantMap& values)
    {
        if (!m_plugin || values.isEmpty()) return;
        winshun::Json patch(winshun::Json::Object{});
        for (auto it = values.begin(); it != values.end(); ++it)
            patch.set(it.key().toStdString(), fromVariant(it.value()));
        m_plugin->updateSettings(patch);
    }

private:
    winshun::Plugin* m_plugin;
};

// One row per fixed drive, then the last target when there is one, the row
// that opens the window and the settings entry; the same shape the native
// plugin answers with, so the launcher looks unchanged.
winshun::Items emptyItems(bool zh, const std::wstring& lastPath, bool folderMenuOn)
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

    std::string subtitle = tr(zh, "Enter 打开窗口 · 输入 disk <路径> 可直接开始扫描，如 disk D:\\工作",
                              "Enter opens the window · type disk <path> to scan right away, e.g. disk D:\\work");
    if (folderMenuOn) {
        subtitle += tr(zh, " · 文件夹右键可直接分析", " · a folder's menu analyzes it");
    }
    winshun::Item open("disk.open", tr(zh, "打开磁盘透视", "Open Disk View"), subtitle);
    open.withIcon("color:#0F766E")
        .withScore(50)
        .withData(winshun::Json(winshun::Json::Object{{"kind", "open"}, {"path", ""}}));
    items.push_back(std::move(open));

    if (!lastPath.empty()) {
        const std::string last = winshun::utf8(lastPath);
        winshun::Item recent("disk.recent",
                             tr(zh, "上次扫描 · ", "Last scan · ") + last,
                             tr(zh, "Enter 打开窗口并重新扫描这个路径", "Enter opens the window and scans this path again"));
        recent.withIcon("color:#0F766E")
            .withScore(60)
            .withData(winshun::Json(winshun::Json::Object{{"kind", "open"}, {"path", last}}));
        items.push_back(std::move(recent));
    }

    winshun::Item settings("disk.settings", tr(zh, "磁盘透视设置", "Disk view settings"),
                           tr(zh, "扫描方式、默认视图、排除路径与高级参数",
                              "Scan mode, default view, exclusions and advanced parameters"));
    settings.withIcon("color:#185FA5")
        .withScore(40)
        .withData(winshun::Json(winshun::Json::Object{{"kind", "settings"}}));
    items.push_back(std::move(settings));

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
                     + tr(zh, "方块图与圆盘共用同一份扫描数据。图表：滚轮缩放、拖动平移，双击文件夹进入该目录，"
                              "双击圆心返回上级；文件树：单击选中，双击进入，小三角就地展开。"
                              "顶部面包屑和退格键返回上级。",
                              "The treemap and the sunburst share one scan. Chart: wheel zooms, drag pans, "
                              "double-click a folder to enter it, double-click the centre hole to go up. "
                              "Tree: click selects, double-click enters, the small triangle expands in place. "
                              "The breadcrumb and Backspace go up."));
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

struct WindowRequest {
    QString path;
};

// Runs on the GUI thread: creates the window, and starts the scan when a path
// came with the request. A scan already running refuses the retarget instead of
// reporting an open that did not happen.
bool openWindowOnGui(const WindowRequest& request, QString* error)
{
    if (g_host && g_window) {
        // Already open: retarget the existing window instead of stacking
        // another one, which is what the host expects from "open" again.
        if (!request.path.isEmpty()) {
            auto* service = g_host->scanner();
            if (!service || !service->start(request.path)) {
                *error = QStringLiteral("a scan is already running");
                return false;
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
    options.scanPath = request.path;
    options.importPaths = g_importPaths;
    // Both windows read the same settings object: what the host sends reaches
    // the next scan and the open window alike.
    options.settings = g_settings;
    options.hostBridge = g_settingsBridge;
    auto* host = new QmlHost(options);
    if (!host->load(error)) {
        delete host;
        return false;
    }
    // The host's look, as the appearance contract spells it out. Applying it
    // after load keeps the window from flashing the fallback palette first.
    host->setTheme(g_theme);
    {
        const QJsonDocument document = QJsonDocument::fromJson(g_appearanceJson.toUtf8());
        if (document.isObject()) host->setAppearance(document.object().toVariantMap());
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

// Opens the settings window, or brings the open one forward. Looking at the
// settings builds no scanner and reads no disk tree.
bool openSettingsWindowOnGui(QString* error)
{
    if (g_settingsHost && g_settingsWindow) {
        if (auto* window = qobject_cast<QQuickWindow*>(g_settingsWindow)) {
            window->show();
            window->raise();
            window->requestActivate();
        }
        return true;
    }

    QmlHost::Options options;
    options.settingsOnly = true;
    options.importPaths = g_importPaths;
    options.settings = g_settings;
    options.hostBridge = g_settingsBridge;
    auto* host = new QmlHost(options);
    if (!host->load(error)) {
        delete host;
        return false;
    }
    host->setTheme(g_theme);
    {
        const QJsonDocument document = QJsonDocument::fromJson(g_appearanceJson.toUtf8());
        if (document.isObject()) host->setAppearance(document.object().toVariantMap());
    }
    auto* window = qobject_cast<QQuickWindow*>(host->window());
    if (!window) {
        *error = QStringLiteral("the settings window did not come up");
        delete host;
        return false;
    }
    QObject::connect(window, &QWindow::visibleChanged, host, [window, host] {
        if (!window->isVisible()) {
            window->deleteLater();
            host->deleteLater();
            g_settingsHost = nullptr;
            g_settingsWindow = nullptr;
        }
    });
    g_settingsHost = host;
    g_settingsWindow = window;
    window->show();
    window->raise();
    window->requestActivate();
    return true;
}

// The two windows are created where the event loop that drives them lives.
bool onGui(const std::function<bool(QString*)>& work, QString* error)
{
    if (QThread::currentThread() == qGuiApp->thread()) {
        return work(error);
    }
    bool done = false;
    QMetaObject::invokeMethod(qGuiApp, [&] { done = work(error); }, Qt::BlockingQueuedConnection);
    return done;
}

class DiskViewQml : public winshun::Plugin {
public:
    std::string name() const override { return "ra9fael.diskview.qml"; }

    // The host's data directory is where the settings file belongs, so the
    // model finds the last path and the settings dialog saves beside it. The
    // settings are loaded once, with the host's values applied on top, and
    // every window from here on shares that object.
    void initialized() override
    {
        if (!dataDir().empty()) {
            qputenv("WS_DATA_DIR", QByteArray::fromStdString(dataDir()));
        }
        // Keep the host's look for the first window and every later one.
        g_theme = QString::fromStdString(theme().empty() ? "light" : theme());
        g_appearanceJson = QString::fromStdString(appearance().dump());
        const QVariantMap host = fromJson(settings());
        QMetaObject::invokeMethod(qGuiApp, [this, host] {
            g_settingsBridge = new SettingsBridge(this);
            g_settings = new ScanSettings(qGuiApp);
            g_settings->load();
            g_settings->applyHostSettings(host);
            // The protocol thread decides whether the folder menu acts; keep
            // its copy in step without reaching into this object.
            g_folderMenuOn.store(g_settings->folderMenuEnabled());
        }, Qt::BlockingQueuedConnection);
    }

    void settingsChanged(const winshun::Json& s) override
    {
        // A key the host sends wins over the file; the advanced scan
        // parameters it no longer declares keep their local value. The
        // window, the next scan and the settings window all read this object.
        const winshun::Json& folderMenu = s["folderMenuEnabled"];
        if (folderMenu.isBool()) g_folderMenuOn.store(folderMenu.toBool());
        const QVariantMap values = fromJson(s);
        QMetaObject::invokeMethod(qGuiApp, [values] {
            if (g_settings) g_settings->applyHostSettings(values);
        }, Qt::QueuedConnection);
    }

    // The host's card button: the plugin's own settings window.
    void showSettings() override
    {
        QString error;
        if (!onGui(openSettingsWindowOnGui, &error)) {
            winshun::debug("ra9fael.diskview.qml: settings window failed: " + error.toStdString());
        }
    }

    ~DiskViewQml() override
    {
        // The windows live on the GUI thread; close them there and wait, so
        // the process does not exit with a UI thread still running.
        if (g_host || g_settingsHost || g_settingsBridge) {
            QMetaObject::invokeMethod(qGuiApp, [] {
                if (g_settingsBridge) {
                    g_settingsBridge->forgetPlugin();
                    g_settingsBridge = nullptr;
                }
                if (auto* window = qobject_cast<QQuickWindow*>(g_settingsWindow)) window->close();
                if (auto* window = qobject_cast<QQuickWindow*>(g_window)) window->close();
                g_settingsHost = nullptr;
                g_settingsWindow = nullptr;
                g_host = nullptr;
                g_window = nullptr;
            }, Qt::BlockingQueuedConnection);
        }
    }

    winshun::Items query(const winshun::Query& q) override
    {
        const bool zh = chinese();
        const std::string text = winshun::trim(q.text);
        if (text == "help" || text == "?" || text == "帮助") return emptyItems(zh, {}, folderMenuEnabled());
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
            return emptyItems(zh, last, folderMenuEnabled());
        }
        return pathItems(text, zh);
    }

    winshun::Effects action(const winshun::ActionRequest& a) override
    {
        const bool zh = chinese();
        const winshun::Json data = a.item["data"];
        const std::string kind = data["kind"].isString() ? data["kind"].toString() : std::string();

        if (kind == "settings" || a.action == "settings") return openSettingsEffects(zh);

        // A folder chosen from the result menu: the host hands over the exact
        // path, so nothing is typed, split or re-quoted on the way in.
        if (a.trigger == "files" && a.action == "files") return analyzeFolder(a, zh);

        if (a.action != "open" && a.action != "default") return {winshun::effect::keep()};
        const std::string path = data["path"].isString() ? data["path"].toString() : winshun::trim(a.text);

        QString error;
        if (!openViewer(fromUtf8(path), &error)) {
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
        // The host changed style, light/dark, corners or UI size: keep the new
        // object for the next window and repaint the open ones.
        g_theme = QString::fromStdString(theme().empty() ? "light" : theme());
        g_appearanceJson = QString::fromStdString(appearance().dump());
        if (!g_host && !g_settingsHost) return;
        const QString json = g_appearanceJson;
        const QString look = g_theme;
        QMetaObject::invokeMethod(qGuiApp, [json, look] {
            const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
            for (QmlHost* host : {g_host, g_settingsHost}) {
                if (!host) continue;
                host->setTheme(look);
                if (document.isObject()) host->setAppearance(document.object().toVariantMap());
            }
        }, Qt::BlockingQueuedConnection);
    }

    void shuttingDown() override
    {
        if (g_closing.exchange(true)) return;
        QMetaObject::invokeMethod(qGuiApp, [] {
            if (auto* window = qobject_cast<QQuickWindow*>(g_settingsWindow)) window->close();
            if (auto* window = qobject_cast<QQuickWindow*>(g_window)) window->close();
        }, Qt::BlockingQueuedConnection);
    }

private:
    // Whether the folder entry is offered, from the host's own copy: the
    // protocol thread does not read the GUI thread's settings object.
    bool folderMenuEnabled() const
    {
        return g_folderMenuOn.load();
    }

    bool openViewer(const QString& path, QString* error)
    {
        WindowRequest request;
        request.path = path;
        return onGui([&request](QString* inner) { return openWindowOnGui(request, inner); }, error);
    }

    winshun::Effects openSettingsEffects(bool zh)
    {
        QString error;
        if (!onGui(openSettingsWindowOnGui, &error)) {
            winshun::debug("ra9fael.diskview.qml: settings window failed: " + error.toStdString());
            return {winshun::effect::notify(tr(zh, "无法打开设置窗口：", "Could not open the settings window: ")
                                            + error.toStdString()),
                    winshun::effect::hide()};
        }
        return {winshun::effect::hide()};
    }

    winshun::Effects analyzeFolder(const winshun::ActionRequest& a, bool zh)
    {
        // The menu entry is hidden while the switch is off; a host that sends
        // it anyway must not start a scan.
        if (!folderMenuEnabled()) return {winshun::effect::keep()};
        if (a.files.size() != 1) {
            return {winshun::effect::notify(tr(zh, "一次只能分析一个文件夹", "Only one folder can be analyzed at a time")),
                    winshun::effect::hide()};
        }
        const QString path = fromUtf8(a.files.front());
        const QFileInfo info(path);
        if (path.isEmpty() || !info.exists() || !info.isDir()) {
            return {winshun::effect::notify(tr(zh, "文件夹已不存在：", "The folder is gone: ") + path.toStdString()),
                    winshun::effect::hide()};
        }
        QString error;
        if (!openViewer(path, &error)) {
            winshun::debug("ra9fael.diskview.qml: window failed: " + error.toStdString());
            return {winshun::effect::notify(tr(zh, "无法打开磁盘透视窗口：", "Could not open the disk view window: ")
                                            + error.toStdString()),
                    winshun::effect::hide()};
        }
        return {winshun::effect::notify(tr(zh, "正在分析 ", "Analyzing ") + path.toStdString()),
                winshun::effect::hide()};
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
    // The host may open a window again after the user closed it.
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

#include "plugin_main.moc"
