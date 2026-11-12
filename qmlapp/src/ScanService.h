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
class ScanSettings;

class ScanService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(quint64 fileCount READ fileCount NOTIFY stateChanged)
    Q_PROPERTY(quint64 dirCount READ dirCount NOTIFY stateChanged)
    Q_PROPERTY(quint64 byteCount READ byteCount NOTIFY stateChanged)
    Q_PROPERTY(double elapsedMs READ elapsedMs NOTIFY stateChanged)
    Q_PROPERTY(bool partial READ partial NOTIFY stateChanged)
    // How many partial snapshots the scan has published. Zero means the scan
    // was over before the first one was due.
    Q_PROPERTY(int partialCount READ partialCount NOTIFY stateChanged)
    // What the scan actually did with the settings: how many workers it used,
    // how many items the exclusion list kept out.
    Q_PROPERTY(int scanWorkers READ scanWorkers NOTIFY stateChanged)
    Q_PROPERTY(quint32 excludedItems READ excludedItems NOTIFY stateChanged)

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
    bool partial() const { return m_partial; }
    int partialCount() const { return m_partialCount; }
    int scanWorkers() const { return m_scanWorkers; }
    quint32 excludedItems() const { return m_excludedItems; }

    // The settings decide the worker count, the media, the preview interval
    // and the excluded paths of the next scan.
    void setSettings(ScanSettings* settings) { m_settings = settings; }
    ScanSettings* settings() const { return m_settings; }

    // Starts a scan of the given path. False when a scan is already running.
    Q_INVOKABLE bool start(const QString& path);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool isRunning() const { return m_running; }

signals:
    void stateChanged();
    // Emitted right before the scan starts, so the window can drop the tree
    // it is showing: it belongs to another scan.
    void started(const QString& path);
    // The complete result.
    void finished(const QVariantMap& tree);
    // An immutable snapshot of the scan so far, while the scan runs.
    void partialTree(const QVariantMap& tree);
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
    bool m_partial = false;
    int m_partialCount = 0;
    int m_scanWorkers = 0;
    quint32 m_excludedItems = 0;
    ScanSettings* m_settings = nullptr;

    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};
