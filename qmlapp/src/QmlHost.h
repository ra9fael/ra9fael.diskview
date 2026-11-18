#pragma once

#include <QObject>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class ScanService;
class ScanSettings;
class ThemeBridge;
class QProcess;

class QmlHost : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString qmlDir;
        QString fixture;
        QString scanPath;
        int viewMode = 0;
        // Whether --view-mode named one: without it the window opens on the
        // default view the settings hold.
        bool viewModeSet = false;
        QString screenshotPath;
        int screenshotDelayMs = 1200;
        bool selfTest = false;
        // Uses only the built-in fixture and a synthetic C: row; no volume
        // enumeration is performed during visual validation.
        bool fixtureOnly = false;
        bool help = false;
        // Opens the scan settings dialog right away, so it can be looked at
        // without a pointer.
        bool openSettings = false;
        // Opens the settings window instead of the viewer: the same window the
        // host's card button opens, without a scanner or a disk tree.
        bool settingsWindow = false;
        // Starts the scan and leaves it paused, so the held state can be
        // looked at.
        bool pauseOnStart = false;
        // Keeps a standalone validation window open after a scan completes.
        bool holdWindow = false;
        // The pane to maximize on startup ("tables" or "chart"), so the
        // fullscreen layer can be looked at without a pointer. Empty leaves
        // the window as it opens.
        QString maximizeOnStart;
        // How many nodes the chart benchmark builds; 0 means no benchmark.
        int benchNodes = 0;
        // Where the QtQuick modules live when the app runs on a host's Qt
        // instead of its own: the plugin build sets this to WinShun's qml
        // folder, plus whatever the package added. Empty leaves the engine to
        // its defaults.
        QStringList importPaths;
        // The settings object the plugin shares between its windows. Empty
        // leaves this window to load its own from the local file, which is
        // what a standalone run does.
        ScanSettings* settings = nullptr;
        // Builds the settings window instead of the viewer: no scanner, no
        // disk model, no chart. The settings are the whole window.
        bool settingsOnly = false;
        // Where the settings the host owns go when the user changes them
        // here; the plugin bridges it to WinShun. Null in a standalone run,
        // where they are only written to the local file.
        QObject* hostBridge = nullptr;
        // A one-off file action, so the Windows side of "move to the recycle
        // bin" and "delete for good" can be checked without a pointer:
        // "recycle" or "delete", with the path in `fileActionPath`.
        QString fileAction;
        QString fileActionPath;
    };

    explicit QmlHost(Options options, QObject* parent = nullptr);

    bool load(QString* error);
    // The window the QML module created, for a host that has to manage it.
    QQuickWindow* window() const;
    ScanService* scanner() const { return m_scanService; }
    ScanSettings* settings() const { return m_scanSettings; }
    bool startScan(const QString& path, QString* error);
    bool runSelfTest(QString* report);
    bool saveScreenshot(const QString& path);
    // Opens the scan settings dialog of the window, if it has one.
    bool openSettingsDialog(QString* error);
    // One line about the scan: what it is doing and what it found.
    QString scanStatusLine() const;
    // The list panel has to sort, filter, rank and go back and forth again.
    bool runListChecks(class DiskViewModel* model, QString* detail);
    // The read-only actions of the context menu.
    // Puts the host's look onto the open window: the chart reads the same
    // theme the window does. Standalone runs keep the light look.
    Q_INVOKABLE void setTheme(const QString& theme) const;
    Q_INVOKABLE void setLocalAppearanceMode(int mode);
    // The host's appearance object (nativeLook, uiStyle, uiSize, corners):
    // the window draws its colours and radii from it.
    Q_INVOKABLE void setAppearance(const QVariantMap& appearance);
    Q_INVOKABLE void copyToClipboard(const QString& text) const;
    Q_INVOKABLE void revealInExplorer(const QString& path) const;
    Q_INVOKABLE bool openPath(const QString& path) const;
    Q_INVOKABLE bool showProperties(const QString& path) const;
    Q_INVOKABLE bool copyFilesToClipboard(const QString& path, bool cut) const;
    Q_INVOKABLE QVariantMap canModifyPath(const QString& path) const;
    Q_INVOKABLE void performFileActionAsync(const QString& path, bool permanent,
                                            const QString& nodeId, int requestId);
    // The same call from the command line ("recycle" or "delete"), so the
    // Windows side can be checked on a machine without a pointer.
    bool runFileActionTest(const QString& action, const QString& path, QString* error);
    // The fixed drives, for the address bar's location menu: path, label and
    // a short free/total line.
    Q_INVOKABLE QVariantList drives() const;
    // The system folder picker. Empty when the user cancels.
    Q_INVOKABLE QString chooseFolder(const QString& initial) const;
    // WinShun's own fuzzy search (the ws CLI over its index), as
    // [{path, name, dir, parent}]. Empty when ws or the index is unavailable.
    Q_INVOKABLE QVariantList searchPaths(const QString& text) const;
    Q_INVOKABLE void searchPathsAsync(const QString& text, int requestId);
    Q_INVOKABLE QVariantList defaultPaths(const QString& nearPath) const;
    Q_INVOKABLE QVariantMap diskForPath(const QString& path) const;
    // Builds a tree of that many nodes and times the whole chart pipeline.
    bool runChartBenchmark(int nodes, QString* report);

    static bool parseArgs(const QStringList& arguments, Options* options, QString* error);
    static QString usage();

signals:
    void scanFinished();
    void scanFailed(const QString& message);
    void pathSuggestionsReady(int requestId, const QVariantList& rows);
    void fileActionFinished(int requestId, const QString& nodeId, const QVariantMap& result);

private:
    bool runDockChecks(QString* report);
    QVariantMap validateFileActionPath(const QString& path) const;
    Options m_options;
    QQmlApplicationEngine m_engine;
    ScanService* m_scanService = nullptr;
    ScanSettings* m_scanSettings = nullptr;
    ThemeBridge* m_theme = nullptr;
    // The pause held while the scan was waiting: the check is only true when
    // the paused scan really produced nothing.
    bool m_pauseVerified = false;
    // What the QML engine complained about while loading; the reasons only
    // arrive through its warnings signal.
    QStringList m_loadWarnings;
    QProcess* m_searchProcess = nullptr;
    int m_searchRequest = 0;
};
