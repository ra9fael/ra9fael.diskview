#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

// The scan settings, the ones the native plugin keeps in its data directory.
// The file name is deliberately different (diskview-qml.json): the QML edition
// and the native plugin live side by side, and they must not overwrite each
// other's settings. The keys are the same, so both understand the same file
// shape.
class ScanSettings : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int scanMode READ scanMode WRITE setScanMode NOTIFY changed)
    Q_PROPERTY(int workers READ workers WRITE setWorkers NOTIFY changed)
    Q_PROPERTY(int media READ media WRITE setMedia NOTIFY changed)
    Q_PROPERTY(int previewIntervalMs READ previewIntervalMs WRITE setPreviewIntervalMs NOTIFY changed)
    Q_PROPERTY(QStringList excludedPaths READ excludedPaths WRITE setExcludedPaths NOTIFY changed)
    Q_PROPERTY(QString lastPath READ lastPath WRITE setLastPath NOTIFY changed)
    // The table columns the user hid. New columns default to visible, so a
    // setting file from an older build still shows everything it knows.
    Q_PROPERTY(QStringList hiddenColumns READ hiddenColumns WRITE setHiddenColumns NOTIFY changed)
    // The file view: match loosely (a subsequence of the name) and look
    // through the whole scan instead of the folder that is open.
    Q_PROPERTY(bool fuzzyMatch READ fuzzyMatch WRITE setFuzzyMatch NOTIFY changed)
    Q_PROPERTY(bool wholeTree READ wholeTree WRITE setWholeTree NOTIFY changed)
    Q_PROPERTY(QString filePath READ filePath CONSTANT)

public:
    explicit ScanSettings(QObject* parent = nullptr);

    // 0 automatic, 1 fast (the NTFS MFT through the elevated helper), 2 ordinary
    // enumeration. The same three values the native plugin offers.
    int scanMode() const { return m_scanMode; }
    void setScanMode(int mode);
    int workers() const { return m_workers; }
    void setWorkers(int workers);
    int media() const { return m_media; }
    void setMedia(int media);
    int previewIntervalMs() const { return m_previewIntervalMs; }
    void setPreviewIntervalMs(int intervalMs);
    QStringList excludedPaths() const { return m_excludedPaths; }
    void setExcludedPaths(const QStringList& paths);
    QString lastPath() const { return m_lastPath; }
    void setLastPath(const QString& path);
    QStringList hiddenColumns() const { return m_hiddenColumns; }
    void setHiddenColumns(const QStringList& columns);
    bool fuzzyMatch() const { return m_fuzzyMatch; }
    void setFuzzyMatch(bool fuzzy);
    bool wholeTree() const { return m_wholeTree; }
    void setWholeTree(bool whole);
    QString filePath() const;

    Q_INVOKABLE bool load();
    Q_INVOKABLE bool save();
    Q_INVOKABLE void addExcludedPath(const QString& path);
    Q_INVOKABLE void removeExcludedPath(int index);

signals:
    void changed();

private:
    int m_scanMode = 0;    // 0 automatic, 1 fast (MFT), 2 ordinary
    int m_workers = 0;      // 0 lets the device decide
    int m_media = 0;        // ScanOptions::Media
    int m_previewIntervalMs = 1500;
    QStringList m_excludedPaths;
    QString m_lastPath;
    QStringList m_hiddenColumns;
    bool m_fuzzyMatch = false;
    bool m_wholeTree = false;
};
