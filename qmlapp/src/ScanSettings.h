#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
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
    // The layout a new window opens with: 0 treemap, 1 sunburst. Switching the
    // layout while browsing does not touch it.
    Q_PROPERTY(int defaultView READ defaultView WRITE setDefaultView NOTIFY changed)
    // Whether a folder's result menu offers "analyze this folder". The host
    // owns it in plugin mode; a standalone run keeps the local value.
    Q_PROPERTY(bool folderMenuEnabled READ folderMenuEnabled WRITE setFolderMenuEnabled NOTIFY changed)
    // Files below this many bytes are folded into one row per directory.
    Q_PROPERTY(int smallFileThreshold READ smallFileThreshold WRITE setSmallFileThreshold NOTIFY changed)
    Q_PROPERTY(QStringList excludedPaths READ excludedPaths WRITE setExcludedPaths NOTIFY changed)
    Q_PROPERTY(QString lastPath READ lastPath WRITE setLastPath NOTIFY changed)
    // The table columns the user hid. New columns default to visible, so a
    // setting file from an older build still shows everything it knows.
    Q_PROPERTY(QStringList hiddenColumns READ hiddenColumns WRITE setHiddenColumns NOTIFY changed)
    // The file view: match loosely (a subsequence of the name) and look
    // through the whole scan instead of the folder that is open.
    Q_PROPERTY(bool fuzzyMatch READ fuzzyMatch WRITE setFuzzyMatch NOTIFY changed)
    Q_PROPERTY(bool wholeTree READ wholeTree WRITE setWholeTree NOTIFY changed)
    // Whether the file panel lists the folder's whole subtree. Off by default;
    // the recursive walk is what made a big folder's first paint slow.
    Q_PROPERTY(bool fileRecursive READ fileRecursive WRITE setFileRecursive NOTIFY changed)
    // How many levels the chart draws, 1 (folders as whole tiles) to 5. The
    // native plugin offers the same switch; a deeper chart is what makes a
    // drive readable at a glance.
    Q_PROPERTY(int chartDepth READ chartDepth WRITE setChartDepth NOTIFY changed)
    // Legacy setting retained so older settings files can still be read.
    Q_PROPERTY(bool chartOnTop READ chartOnTop WRITE setChartOnTop NOTIFY changed)
    // Legacy preset selector retained for settings compatibility. The QML
    // workspace now defaults to the persisted Dock layout.
    Q_PROPERTY(QString chartLayout READ chartLayout WRITE setChartLayout NOTIFY changed)
    Q_PROPERTY(QString dockLayout READ dockLayout WRITE setDockLayout NOTIFY changed)
    // The table column widths, one JSON object per table ("tree", "file",
    // "legend"). Only the columns the user dragged are in it; an empty or
    // broken value falls back to the spec defaults in Main.qml.
    Q_PROPERTY(QString columnWidths READ columnWidths WRITE setColumnWidths NOTIFY changed)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY changed)
    Q_PROPERTY(int treeRowHeight READ treeRowHeight WRITE setTreeRowHeight NOTIFY changed)
    Q_PROPERTY(int fileRowHeight READ fileRowHeight WRITE setFileRowHeight NOTIFY changed)
    Q_PROPERTY(int headerRowHeight READ headerRowHeight WRITE setHeaderRowHeight NOTIFY changed)
    Q_PROPERTY(int appearanceMode READ appearanceMode WRITE setAppearanceMode NOTIFY changed)
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
    int defaultView() const { return m_defaultView; }
    void setDefaultView(int view);
    bool folderMenuEnabled() const { return m_folderMenuEnabled; }
    void setFolderMenuEnabled(bool enabled);
    int smallFileThreshold() const { return m_smallFileThreshold; }
    void setSmallFileThreshold(int bytes);
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
    bool fileRecursive() const { return m_fileRecursive; }
    void setFileRecursive(bool recursive);
    int chartDepth() const { return m_chartDepth; }
    void setChartDepth(int depth);
    bool chartOnTop() const { return m_chartOnTop; }
    void setChartOnTop(bool onTop);
    QString chartLayout() const { return m_chartLayout; }
    QString dockLayout() const { return m_dockLayout; }
    void setDockLayout(const QString& value) { if (m_dockLayout != value) { m_dockLayout = value; emit changed(); } }
    QString columnWidths() const { return m_columnWidths; }
    void setColumnWidths(const QString& value) { if (m_columnWidths != value) { m_columnWidths = value; emit changed(); } }
    int fontSize() const { return m_fontSize; }
    void setFontSize(int value);
    int treeRowHeight() const { return m_treeRowHeight; }
    void setTreeRowHeight(int value);
    int fileRowHeight() const { return m_fileRowHeight; }
    void setFileRowHeight(int value);
    int headerRowHeight() const { return m_headerRowHeight; }
    void setHeaderRowHeight(int value);
    int appearanceMode() const { return m_appearanceMode; }
    void setAppearanceMode(int value);
    void setChartLayout(const QString& layout);
    QString filePath() const;

    // Visual fixture runs do not inspect mounted volumes to seed exclusions.
    void setDefaultVolumeEnumerationEnabled(bool enabled) { m_defaultVolumeEnumerationEnabled = enabled; }

    Q_INVOKABLE bool load();
    Q_INVOKABLE bool save();
    Q_INVOKABLE void addExcludedPath(const QString& path);
    Q_INVOKABLE void removeExcludedPath(int index);

    // Applies the settings the host sent (its manifest defaults merged with the
    // user's values): every key in `settings` wins over the local file, and a
    // key the host does not send leaves the local value alone. Reading the
    // host's card and writing it back use the same table as this.
    void applyHostSettings(const QVariantMap& settings);

    // The host stores choices as strings; the scanner and the window use the
    // enumerations below. One function per key, in both directions, so a value
    // cannot mean one thing on the card and another in the scan.
    static int scanModeFromHost(const QString& value);
    static QString scanModeToHost(int mode);
    static int workersFromHost(const QString& value);
    static QString workersToHost(int workers);
    static int mediaFromHost(const QString& value);
    static QString mediaToHost(int media);
    static int previewIntervalFromHost(const QString& value);
    static QString previewIntervalToHost(int intervalMs);
    static int defaultViewFromHost(const QString& value);
    static QString defaultViewToHost(int view);

signals:
    void changed();

private:
    int m_scanMode = 0;    // 0 automatic, 1 fast (MFT), 2 ordinary
    int m_workers = 0;      // 0 lets the device decide
    int m_media = 0;        // ScanOptions::Media
    int m_previewIntervalMs = 1500;
    int m_smallFileThreshold = 0;
    int m_defaultView = 0;
    bool m_folderMenuEnabled = true;
    QStringList m_excludedPaths;
    QString m_lastPath;
    QStringList m_hiddenColumns;
    bool m_fuzzyMatch = false;
    bool m_wholeTree = false;
    bool m_fileRecursive = false;
    bool m_defaultVolumeEnumerationEnabled = true;
    bool m_excludedPathsInitialized = false;
    int m_chartDepth = 0; // 0 = auto: the area budget picks the depth
    bool m_chartOnTop = false;
    QString m_chartLayout = QStringLiteral("dock");
    QString m_dockLayout;
    QString m_columnWidths;
    int m_fontSize = 13;
    int m_treeRowHeight = 26;
    int m_fileRowHeight = 28;
    int m_headerRowHeight = 26;
    int m_appearanceMode = 0;
};
