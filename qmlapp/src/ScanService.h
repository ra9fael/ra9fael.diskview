#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <thread>

// The real scanner, as a QML singleton: the QML edition keeps its own
// scanner instead of the WinShun host's, so the app can be exercised on its
// own. The scan runs on a worker thread and the result comes back as a plain
// tree, the same shape the fixture loader produces.
class ScanService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(quint64 fileCount READ fileCount NOTIFY stateChanged)
    Q_PROPERTY(quint64 dirCount READ dirCount NOTIFY stateChanged)
    Q_PROPERTY(quint64 byteCount READ byteCount NOTIFY stateChanged)
    Q_PROPERTY(double elapsedMs READ elapsedMs NOTIFY stateChanged)

public:
    explicit ScanService(QObject* parent = nullptr);
    ~ScanService() override;

    QString state() const { return m_state; }
    QString statusText() const { return m_statusText; }
    QString errorText() const { return m_errorText; }
    QString rootPath() const { return m_rootPath; }
    double progress() const { return m_progress; }
    quint64 fileCount() const { return m_fileCount; }
    quint64 dirCount() const { return m_dirCount; }
    quint64 byteCount() const { return m_byteCount; }
    double elapsedMs() const { return m_elapsedMs; }

    // Starts a scan of the given path. False when a scan is already running.
    Q_INVOKABLE bool start(const QString& path);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool isRunning() const { return m_running; }

signals:
    void stateChanged();
    void finished(const QVariantMap& tree);
    void failed(const QString& message);
    void cancelled();

private:
    void setState(const QString& state);

    QString m_state = QStringLiteral("idle");
    QString m_statusText;
    QString m_errorText;
    QString m_rootPath;
    double m_progress = 0.0;
    quint64 m_fileCount = 0;
    quint64 m_dirCount = 0;
    quint64 m_byteCount = 0;
    double m_elapsedMs = 0.0;

    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};
