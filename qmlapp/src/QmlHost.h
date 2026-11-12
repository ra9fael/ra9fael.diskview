#pragma once

#include <QObject>
#include <QQmlApplicationEngine>
#include <QString>
#include <QStringList>

class ScanService;

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
    };

    explicit QmlHost(Options options, QObject* parent = nullptr);

    bool load(QString* error);
    bool startScan(const QString& path, QString* error);
    bool runSelfTest(QString* report);
    bool saveScreenshot(const QString& path);

    static bool parseArgs(const QStringList& arguments, Options* options, QString* error);
    static QString usage();

signals:
    void scanFinished();
    void scanFailed(const QString& message);

private:
    Options m_options;
    QQmlApplicationEngine m_engine;
    ScanService* m_scanService = nullptr;
};
