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

void ScanSettings::setChartDepth(int depth)
{
    // One level means the folders stay whole tiles, five is as deep as the
    // chart reads well; anything else is snapped into that range.
    const int clamped = std::clamp(depth, 1, 5);
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
    static const QStringList known = {QStringLiteral("tablesTop"), QStringLiteral("tablesBottom"),
                                      QStringLiteral("tablesLeft"), QStringLiteral("tablesRight"), QStringLiteral("dock")};
    // Anything unknown falls back to the default instead of leaving the window
    // in a layout it cannot draw.
    const QString kept = known.contains(layout) ? layout : QStringLiteral("tablesTop");
    if (kept == m_chartLayout) {
        return;
    }
    m_chartLayout = kept;
    m_chartOnTop = (kept == QLatin1String("tablesBottom"));
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
        m_excludedPaths = defaultExcludedPaths();
        emit changed();
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return false;
    }
    const QJsonObject object = document.object();
    m_dockLayout = QString::fromUtf8(QJsonDocument(object.value(QStringLiteral("dockLayout")).toObject()).toJson(QJsonDocument::Compact));
    m_lastPath = object.value(QStringLiteral("path")).toString();
    // Out-of-range values from a hand-edited file are clamped, not trusted.
    m_scanMode = std::clamp(object.value(QStringLiteral("scanMode")).toInt(0), 0, 2);
    m_workers = static_cast<int>(snapWorkerLimit(object.value(QStringLiteral("workers")).toInt(0)));
    m_media = std::clamp(object.value(QStringLiteral("media")).toInt(0), 0, 3);
    const int interval = object.value(QStringLiteral("previewIntervalMs")).toInt(1500);
    m_previewIntervalMs = interval >= 5000 ? 5000 : 1500;
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
    m_chartDepth = std::clamp(object.value(QStringLiteral("chartDepth")).toInt(2), 1, 5);
    m_chartOnTop = object.value(QStringLiteral("chartOnTop")).toBool(false);
    const QString layout = object.value(QStringLiteral("chartLayout")).toString();
    m_chartLayout = layout.isEmpty() ? (m_chartOnTop ? QStringLiteral("tablesBottom") : QStringLiteral("tablesTop"))
                                 : (QStringList{QStringLiteral("tablesTop"), QStringLiteral("tablesBottom"), QStringLiteral("tablesLeft"), QStringLiteral("tablesRight"), QStringLiteral("dock")}.contains(layout) ? layout : QStringLiteral("tablesTop"));
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
    QJsonArray hidden;
    for (const QString& column : m_hiddenColumns) {
        hidden.append(column);
    }
    object.insert(QStringLiteral("hiddenColumns"), hidden);
    object.insert(QStringLiteral("fuzzyMatch"), m_fuzzyMatch);
    object.insert(QStringLiteral("wholeTree"), m_wholeTree);
    object.insert(QStringLiteral("chartDepth"), m_chartDepth);
    object.insert(QStringLiteral("chartOnTop"), m_chartOnTop);
    object.insert(QStringLiteral("chartLayout"), m_chartLayout);
    object.insert(QStringLiteral("dockLayout"), QJsonDocument::fromJson(m_dockLayout.toUtf8()).object());
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
