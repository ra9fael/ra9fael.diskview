#pragma once

#include <QObject>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QString>
#include <QStringList>

class ScanService;
class ScanSettings;

class QmlHost : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString qmlDir;
        QString fixture;
        QString scanPath;
        int viewMode = 0;
        QString screenshotPath;
        int screenshotDelayMs = 1200;
        bool selfTest = false;
        bool help = false;
        // Opens the scan settings dialog right away, so it can be looked at
        // without a pointer.
        bool openSettings = false;
        // Starts the scan and leaves it paused, so the held state can be
        // looked at.
        bool pauseOnStart = false;
        // How many nodes the chart benchmark builds; 0 means no benchmark.
        int benchNodes = 0;
        // Where the QtQuick modules live when the app runs on a host's Qt
        // instead of its own: the plugin build sets this to WinShun's qml
        // folder. Empty leaves the engine to its defaults.
        QString importPath;
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
    Q_INVOKABLE void copyToClipboard(const QString& text) const;
    Q_INVOKABLE void revealInExplorer(const QString& path) const;
    // Builds a tree of that many nodes and times the whole chart pipeline.
    bool runChartBenchmark(int nodes, QString* report);

    static bool parseArgs(const QStringList& arguments, Options* options, QString* error);
    static QString usage();

signals:
    void scanFinished();
    void scanFailed(const QString& message);

private:
    Options m_options;
    QQmlApplicationEngine m_engine;
    ScanService* m_scanService = nullptr;
    ScanSettings* m_scanSettings = nullptr;
    // The pause held while the scan was waiting: the check is only true when
    // the paused scan really produced nothing.
    bool m_pauseVerified = false;
    // What the QML engine complained about while loading; the reasons only
    // arrive through its warnings signal.
    QStringList m_loadWarnings;
};
