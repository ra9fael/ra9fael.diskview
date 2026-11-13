#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "ScanTree.h"

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
    // The scan is holding still: the workers wait until it is resumed.
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    // The fast scan (the NTFS MFT through the elevated helper) was the one that
    // produced the result.
    Q_PROPERTY(bool fast READ fast NOTIFY stateChanged)
    // Why the fast scan was not used, when it was asked for and could not run.
    Q_PROPERTY(QString fallbackReason READ fallbackReason NOTIFY stateChanged)
    // The directories the scan could not read, and the files whose size it
    // could not determine. A scan that reports them is still usable.
    Q_PROPERTY(int skippedDirs READ skippedDirs NOTIFY stateChanged)
    Q_PROPERTY(int skippedLinks READ skippedLinks NOTIFY stateChanged)
    Q_PROPERTY(int unknownSizes READ unknownSizes NOTIFY stateChanged)
    Q_PROPERTY(int orphanRecords READ orphanRecords NOTIFY stateChanged)
    // The same, as a list: {path, code, message} per entry.
    Q_PROPERTY(QVariantList issues READ issues NOTIFY stateChanged)

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
    bool paused() const { return m_pause.load(); }
    bool fast() const { return m_fast; }
    QString fallbackReason() const { return m_fallbackReason; }
    int skippedDirs() const { return m_skippedDirs; }
    int skippedLinks() const { return m_skippedLinks; }
    int unknownSizes() const { return m_unknownSizes; }
    int orphanRecords() const { return m_orphanRecords; }
    QVariantList issues() const { return m_issues; }

    // The settings decide the worker count, the media, the preview interval
    // and the excluded paths of the next scan.
    void setSettings(ScanSettings* settings) { m_settings = settings; }
    ScanSettings* settings() const { return m_settings; }

    // The window the elevated helper's authorization prompt belongs to.
    void setOwnerWindow(quintptr window) { m_ownerWindow = window; }

    // Starts a scan of the given path. False when a scan is already running.
    Q_INVOKABLE bool start(const QString& path);
    // The same, but the scan waits until it is resumed.
    Q_INVOKABLE bool startPaused(const QString& path);
    Q_INVOKABLE void cancel();
    // Holds the scan still, and lets it go again.
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    // Runs the last path again, with the settings as they are now.
    Q_INVOKABLE bool rescan();
    Q_INVOKABLE bool isRunning() const { return m_running.load(); }

signals:
    void stateChanged();
    // Emitted right before the scan starts, so the window can drop the tree
    // it is showing: it belongs to another scan.
    void started(const QString& path);
    // The complete result.
    void finished(const FlatTree& tree);
    // An immutable snapshot of the scan so far, while the scan runs.
    void partialTree(const FlatTree& tree);
    void failed(const QString& message);
    void cancelled();

private:
    void setState(const QString& state);
    bool beginScan(const QString& path, bool startPaused);

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
    bool m_fast = false;
    QString m_fallbackReason;
    int m_skippedDirs = 0;
    int m_skippedLinks = 0;
    int m_unknownSizes = 0;
    int m_orphanRecords = 0;
    QVariantList m_issues;
    ScanSettings* m_settings = nullptr;
    quintptr m_ownerWindow = 0;

    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_pause{false};
    std::thread m_thread;
};
