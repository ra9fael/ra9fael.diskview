#include "ScanSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>
#include <cstdlib>

// The reserved folders of every fixed volume, for the first run's exclusions.
#include <windows.h>

namespace {

QString settingsFilePath()
{
    // The host passes its data directory; the standalone launcher has to find
    // a place of its own.
    const QByteArray dataDir = qgetenv("WS_DATA_DIR");
    QDir dir;
    if (!dataDir.isEmpty()) {
        dir = QDir(QString::fromUtf8(dataDir));
    } else {
        dir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    }
    return dir.filePath(QStringLiteral("diskview-qml.json"));
}

// The folders every Windows volume keeps for itself and that nobody wants in a
// space report. Seeded on the first run, and only those: they hold no user data,
// so leaving them out does not make the totals wrong.
QStringList defaultExcludedPaths()
{
    QStringList paths;
    const DWORD mask = ::GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if ((mask & (1u << i)) == 0) continue;
        const wchar_t root[4] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', L'\0'};
        if (::GetDriveTypeW(root) != DRIVE_FIXED) continue;
        const QString drive = QString::fromWCharArray(root, 3);
        paths.append(drive + QStringLiteral("System Volume Information"));
        paths.append(drive + QStringLiteral("$Recycle.Bin"));
        paths.append(drive + QStringLiteral("Recovery"));
    }
    return paths;
}

// The worker counts the plugin offers. A value outside them is snapped to the
// nearest one, so the setting always means something the scanner can use.
unsigned snapWorkerLimit(int workers)
{
    const unsigned limits[] = {0, 1, 2, 4, 8};
    if (workers <= 0) {
        return 0;
    }
    if (workers >= 8) {
        return 8;
    }
    unsigned best = 1;
    for (unsigned limit : limits) {
        if (limit == 0) continue;
        if (std::abs(static_cast<int>(limit) - workers) < std::abs(static_cast<int>(best) - workers)) {
            best = limit;
        }
    }
    return best;
}

} // namespace

ScanSettings::ScanSettings(QObject* parent) : QObject(parent) {}

QString ScanSettings::filePath() const
{
    return settingsFilePath();
}

void ScanSettings::setScanMode(int mode)
{
    const int clamped = std::clamp(mode, 0, 2);
    if (clamped == m_scanMode) {
        return;
    }
    m_scanMode = clamped;
    emit changed();
}

void ScanSettings::setWorkers(int workers)
{
    const int clamped = static_cast<int>(snapWorkerLimit(workers));
    if (clamped == m_workers) {
        return;
    }
    m_workers = clamped;
    emit changed();
}

void ScanSettings::setMedia(int media)
{
    const int clamped = std::clamp(media, 0, 3);
    if (clamped == m_media) {
        return;
    }
    m_media = clamped;
    emit changed();
}

void ScanSettings::setPreviewIntervalMs(int intervalMs)
{
    // Two intervals only, the same ones the native plugin offers: a fast
    // preview and a cheap one.
    const int clamped = intervalMs >= 5000 ? 5000 : 1500;
    if (clamped == m_previewIntervalMs) {
        return;
    }
    m_previewIntervalMs = clamped;
    emit changed();
}

void ScanSettings::setSmallFileThreshold(int bytes)
{
    // 0 means every file; the upper bound keeps the aggregate meaningful.
    const int clamped = std::clamp(bytes, 0, 1024 * 1024);
    if (clamped == m_smallFileThreshold) return;
    m_smallFileThreshold = clamped;
    emit changed();
}

void ScanSettings::setLastPath(const QString& path)
{
    if (path == m_lastPath) {
        return;
    }
    m_lastPath = path;
    emit changed();
}

void ScanSettings::setHiddenColumns(const QStringList& columns)
{
    // Only the names the table knows; a stale entry from an older build is
    // dropped rather than kept forever.
    static const QStringList known{QStringLiteral("pct"), QStringLiteral("alloc"),
                                   QStringLiteral("items"), QStringLiteral("files"),
                                   QStringLiteral("dirs"), QStringLiteral("modified"),
                                   QStringLiteral("attributes")};
    QStringList kept;
    for (const QString& column : columns) {
        if (known.contains(column) && !kept.contains(column)) {
            kept.append(column);
        }
    }
    if (kept == m_hiddenColumns) {
        return;
    }
    m_hiddenColumns = kept;
    emit changed();
}

void ScanSettings::setFuzzyMatch(bool fuzzy)
{
    if (fuzzy == m_fuzzyMatch) {
        return;
    }
    m_fuzzyMatch = fuzzy;
    emit changed();
}

void ScanSettings::setWholeTree(bool whole)
{
    if (whole == m_wholeTree) {
        return;
    }
    m_wholeTree = whole;
    emit changed();
}

void ScanSettings::setFileRecursive(bool recursive)
{
    if (recursive == m_fileRecursive) return;
    m_fileRecursive = recursive;
    emit changed();
}

void ScanSettings::setChartDepth(int depth)
{
    // Zero is "auto": each folder nests as deep as its room allows. One to
    // five are fixed limits; anything else snaps into that range.
    const int clamped = std::clamp(depth, 0, 5);
    if (clamped == m_chartDepth) {
        return;
    }
    m_chartDepth = clamped;
    emit changed();
}

void ScanSettings::setChartOnTop(bool onTop)
{
    if (onTop == m_chartOnTop) {
        return;
    }
    m_chartOnTop = onTop;
    emit changed();
}

void ScanSettings::setChartLayout(const QString& layout)
{
    Q_UNUSED(layout);
    const QString kept = QStringLiteral("dock");
    if (kept == m_chartLayout) {
        return;
    }
    m_chartLayout = kept;
    m_chartOnTop = (kept == QLatin1String("tablesBottom"));
    emit changed();
}

void ScanSettings::setFontSize(int value)
{
    const int clamped = std::clamp(value, 11, 16);
    if (m_fontSize == clamped) return;
    m_fontSize = clamped;
    emit changed();
}

void ScanSettings::setTreeRowHeight(int value)
{
    const int clamped = std::clamp(value, 22, 40);
    if (m_treeRowHeight == clamped) return;
    m_treeRowHeight = clamped;
    emit changed();
}

void ScanSettings::setFileRowHeight(int value)
{
    const int clamped = std::clamp(value, 22, 44);
    if (m_fileRowHeight == clamped) return;
    m_fileRowHeight = clamped;
    emit changed();
}

void ScanSettings::setHeaderRowHeight(int value)
{
    const int clamped = std::clamp(value, 22, 36);
    if (m_headerRowHeight == clamped) return;
    m_headerRowHeight = clamped;
    emit changed();
}

void ScanSettings::setAppearanceMode(int value)
{
    const int clamped = std::clamp(value, 0, 2);
    if (m_appearanceMode == clamped) return;
    m_appearanceMode = clamped;
    emit changed();
}

void ScanSettings::addExcludedPath(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty() || m_excludedPaths.contains(trimmed)) {
        return;
    }
    m_excludedPaths.append(trimmed);
    emit changed();
}

void ScanSettings::setExcludedPaths(const QStringList& paths)
{
    QStringList kept;
    for (const QString& path : paths) {
        const QString trimmed = path.trimmed();
        if (!trimmed.isEmpty() && !kept.contains(trimmed)) {
            kept.append(trimmed);
        }
    }
    if (kept == m_excludedPaths) {
        return;
    }
    m_excludedPaths = kept;
    emit changed();
}

void ScanSettings::removeExcludedPath(int index)
{
    if (index < 0 || index >= m_excludedPaths.size()) {
        return;
    }
    m_excludedPaths.removeAt(index);
    emit changed();
}

bool ScanSettings::load()
{
    QFile file(settingsFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        // First run: the volume's own reserved folders are excluded from the
        // start, the way the report is meant to be read.
        m_excludedPaths = m_defaultVolumeEnumerationEnabled ? defaultExcludedPaths() : QStringList();
        emit changed();
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return false;
    }
    const QJsonObject object = document.object();
    m_dockLayout = QString::fromUtf8(QJsonDocument(object.value(QStringLiteral("dockLayout")).toObject()).toJson(QJsonDocument::Compact));
    m_columnWidths = QString::fromUtf8(QJsonDocument(object.value(QStringLiteral("columnWidths")).toObject()).toJson(QJsonDocument::Compact));
    m_lastPath = object.value(QStringLiteral("path")).toString();
    // Out-of-range values from a hand-edited file are clamped, not trusted.
    m_scanMode = std::clamp(object.value(QStringLiteral("scanMode")).toInt(0), 0, 2);
    m_workers = static_cast<int>(snapWorkerLimit(object.value(QStringLiteral("workers")).toInt(0)));
    m_media = std::clamp(object.value(QStringLiteral("media")).toInt(0), 0, 3);
    const int interval = object.value(QStringLiteral("previewIntervalMs")).toInt(1500);
    m_previewIntervalMs = interval >= 5000 ? 5000 : 1500;
    m_smallFileThreshold = std::clamp(object.value(QStringLiteral("smallFileThreshold")).toInt(0), 0, 1024 * 1024);
    m_excludedPaths.clear();
    const QJsonArray excluded = object.value(QStringLiteral("excludedPaths")).toArray();
    for (const QJsonValue& item : excluded) {
        if (!item.isString()) {
            continue;
        }
        const QString path = item.toString();
        if (!path.isEmpty() && !m_excludedPaths.contains(path)) {
            m_excludedPaths.append(path);
        }
    }
    QStringList hidden;
    const QJsonArray hiddenArray = object.value(QStringLiteral("hiddenColumns")).toArray();
    for (const QJsonValue& item : hiddenArray) {
        if (item.isString()) hidden.append(item.toString());
    }
    setHiddenColumns(hidden);
    m_fuzzyMatch = object.value(QStringLiteral("fuzzyMatch")).toBool(false);
    m_wholeTree = object.value(QStringLiteral("wholeTree")).toBool(false);
    m_fileRecursive = object.value(QStringLiteral("fileRecursive")).toBool(false);
    m_chartDepth = std::clamp(object.value(QStringLiteral("chartDepth")).toInt(0), 0, 5);
    m_fontSize = std::clamp(object.value(QStringLiteral("fontSize")).toInt(13), 11, 16);
    m_treeRowHeight = std::clamp(object.value(QStringLiteral("treeRowHeight")).toInt(26), 22, 40);
    m_fileRowHeight = std::clamp(object.value(QStringLiteral("fileRowHeight")).toInt(28), 22, 44);
    m_headerRowHeight = std::clamp(object.value(QStringLiteral("headerRowHeight")).toInt(26), 22, 36);
    m_appearanceMode = std::clamp(object.value(QStringLiteral("appearanceMode")).toInt(0), 0, 2);
    m_chartOnTop = object.value(QStringLiteral("chartOnTop")).toBool(false);
    const QString layout = object.value(QStringLiteral("chartLayout")).toString();
    m_chartLayout = QStringLiteral("dock");
    emit changed();
    return true;
}

bool ScanSettings::save()
{
    QJsonObject object;
    object.insert(QStringLiteral("path"), m_lastPath);
    object.insert(QStringLiteral("scanMode"), m_scanMode);
    object.insert(QStringLiteral("workers"), m_workers);
    object.insert(QStringLiteral("media"), m_media);
    object.insert(QStringLiteral("previewIntervalMs"), m_previewIntervalMs);
    object.insert(QStringLiteral("smallFileThreshold"), m_smallFileThreshold);
    QJsonArray hidden;
    for (const QString& column : m_hiddenColumns) {
        hidden.append(column);
    }
    object.insert(QStringLiteral("hiddenColumns"), hidden);
    object.insert(QStringLiteral("fuzzyMatch"), m_fuzzyMatch);
    object.insert(QStringLiteral("wholeTree"), m_wholeTree);
    object.insert(QStringLiteral("fileRecursive"), m_fileRecursive);
    object.insert(QStringLiteral("chartDepth"), m_chartDepth);
    object.insert(QStringLiteral("fontSize"), m_fontSize);
    object.insert(QStringLiteral("treeRowHeight"), m_treeRowHeight);
    object.insert(QStringLiteral("fileRowHeight"), m_fileRowHeight);
    object.insert(QStringLiteral("headerRowHeight"), m_headerRowHeight);
    object.insert(QStringLiteral("appearanceMode"), m_appearanceMode);
    object.insert(QStringLiteral("chartOnTop"), m_chartOnTop);
    object.insert(QStringLiteral("chartLayout"), m_chartLayout);
    object.insert(QStringLiteral("dockLayout"), QJsonDocument::fromJson(m_dockLayout.toUtf8()).object());
    object.insert(QStringLiteral("columnWidths"), QJsonDocument::fromJson(m_columnWidths.toUtf8()).object());
    QJsonArray excluded;
    for (const QString& path : m_excludedPaths) {
        excluded.append(path);
    }
    object.insert(QStringLiteral("excludedPaths"), excluded);

    const QString path = settingsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    // A plain file, not QSaveFile: the write-to-a-sibling-temp-and-rename
    // gets "Access is denied" from the plugin process even in an ordinary
    // folder, so the atomic swap is given up for a small settings file.
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) >= 0;
}
