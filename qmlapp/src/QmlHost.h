#pragma once

#include <QObject>
#include <QQmlApplicationEngine>
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
    };

    explicit QmlHost(Options options, QObject* parent = nullptr);

    bool load(QString* error);
    bool startScan(const QString& path, QString* error);
    bool runSelfTest(QString* report);
    bool saveScreenshot(const QString& path);
    // Opens the scan settings dialog of the window, if it has one.
    bool openSettingsDialog(QString* error);

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
};
