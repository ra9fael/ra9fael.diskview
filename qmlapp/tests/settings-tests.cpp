// The scan settings: defaults, clamping of a hand-edited file and a round
// trip. The file name is the QML edition's own one, so the native plugin and
// this one can live in the same data directory.

#include "ScanSettings.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>
#include <iostream>

namespace {

int g_failures = 0;

void check(bool condition, const char* what)
{
    if (condition) {
        std::cout << "  ok   " << what << "\n";
        return;
    }
    std::cout << "  FAIL " << what << "\n";
    ++g_failures;
}

// The settings file is picked up from WS_DATA_DIR, so the tests point it at
// their own directory.
class ScopedDataDir {
public:
    ScopedDataDir(const QString& path)
    {
        m_previous = qgetenv("WS_DATA_DIR");
        qputenv("WS_DATA_DIR", path.toUtf8());
    }
    ~ScopedDataDir()
    {
        if (m_previous.isEmpty()) {
            qunsetenv("WS_DATA_DIR");
        } else {
            qputenv("WS_DATA_DIR", m_previous);
        }
    }

private:
    QByteArray m_previous;
};

void testDefaults()
{
    ScanSettings settings;
    check(settings.scanMode() == 0, "a fresh settings object keeps the automatic scan mode");
    check(settings.workers() == 0, "a fresh settings object keeps the automatic worker count");
    check(settings.media() == 0, "a fresh settings object keeps the automatic media");
    check(settings.previewIntervalMs() == 1500, "a fresh settings object keeps the 1.5 s preview interval");
    check(settings.chartDepth() == 0, "a fresh settings object lets the chart pick its own depth");
    check(settings.fontSize() == 13, "a fresh settings object uses the compact readable font size");
    check(settings.treeRowHeight() == 26, "a fresh settings object uses the compact tree row height");
    check(settings.fileRowHeight() == 28, "a fresh settings object uses the compact file row height");
    check(settings.headerRowHeight() == 26, "a fresh settings object uses the compact header row height");
    check(settings.appearanceMode() == 0, "a fresh settings object follows the host theme");
    check(settings.chartLayout() == QStringLiteral("dock"), "a fresh workspace uses the default Dock layout");
    check(settings.excludedPaths().isEmpty(), "a fresh settings object excludes nothing");
}

void testClamping()
{
    ScanSettings settings;
    settings.setScanMode(9);
    check(settings.scanMode() == 2, "the scan mode is clamped to the ordinary scan");
    settings.setScanMode(-1);
    check(settings.scanMode() == 0, "the scan mode is clamped to automatic");
    settings.setScanMode(1);
    check(settings.scanMode() == 1, "the fast scan mode is kept");
    settings.setWorkers(64);
    settings.setMedia(-3);
    settings.setPreviewIntervalMs(7);
    check(settings.workers() == 8, "the worker count is clamped to 8");
    check(settings.media() == 0, "the media is clamped to automatic");
    check(settings.previewIntervalMs() == 1500, "an out-of-range preview interval falls back to 1.5 s");
    settings.setPreviewIntervalMs(9000);
    check(settings.previewIntervalMs() == 5000, "the preview interval is capped at 5 s");

    // The worker counts the plugin offers are the only ones that mean
    // something to the scanner.
    settings.setWorkers(3);
    check(settings.workers() == 2 || settings.workers() == 4, "a worker count between the offered ones is snapped");
    settings.setWorkers(0);
    check(settings.workers() == 0, "zero keeps the automatic worker count");

    // The chart works its depth out from the room each folder gets (0), or
    // draws one to five fixed levels of blocks.
    settings.setChartDepth(-4);
    check(settings.chartDepth() == 0, "a negative depth falls back to the automatic one");
    settings.setChartDepth(9);
    check(settings.chartDepth() == 5, "the chart depth is clamped to five levels");
    settings.setChartDepth(3);
    check(settings.chartDepth() == 3, "a depth the chart draws is kept");
    settings.setChartDepth(0);
    check(settings.chartDepth() == 0, "zero keeps the automatic depth");
    settings.setFontSize(100);
    settings.setTreeRowHeight(1);
    settings.setFileRowHeight(100);
    settings.setHeaderRowHeight(1);
    check(settings.fontSize() == 16, "the font size is clamped to the readable upper bound");
    check(settings.treeRowHeight() == 22, "the tree row height is clamped to the compact lower bound");
    check(settings.fileRowHeight() == 44, "the file row height is clamped to the readable upper bound");
    check(settings.headerRowHeight() == 22, "the header row height is clamped to the compact lower bound");
    settings.setAppearanceMode(9);
    check(settings.appearanceMode() == 2, "the appearance mode is clamped to dark");
}

void testExcludedPaths()
{
    ScanSettings settings;
    settings.setExcludedPaths({QStringLiteral("D:\\a"), QStringLiteral("  "), QStringLiteral("D:\\a"),
                                QStringLiteral("D:\\b")});
    check(settings.excludedPaths().size() == 2, "the exclusion list drops the empty and the duplicate entries");
    check(settings.excludedPaths().first() == QStringLiteral("D:\\a"), "the exclusion list keeps its order");
}

void testRoundTrip()
{
    // The directory is created next to the executable instead of in the
    // system temp folder: a sandboxed run may not be allowed to write there.
    QTemporaryDir dir(QDir::current().filePath(QStringLiteral("dv-settings-test-XXXXXX")));
    if (!dir.isValid()) {
        check(false, "the temporary directory is usable");
        std::cout << "       temp root: " << QDir::currentPath().toStdString()
                  << " error: " << dir.errorString().toStdString() << "\n";
        return;
    }
    ScopedDataDir scoped(dir.path());

    ScanSettings saved;
    saved.setScanMode(1);
    saved.setWorkers(4);
    saved.setMedia(1);
    saved.setPreviewIntervalMs(5000);
    saved.setLastPath(QStringLiteral("D:\\工作"));
    saved.addExcludedPath(QStringLiteral("D:\\工作\\node_modules"));
    saved.addExcludedPath(QStringLiteral("D:\\工作\\build"));
    saved.addExcludedPath(QStringLiteral("D:\\工作\\build")); // a duplicate is ignored
    saved.setHiddenColumns({QStringLiteral("attributes"), QStringLiteral("modified"),
                            QStringLiteral("nonsense")});
    saved.setFuzzyMatch(true);
    saved.setWholeTree(true);
    saved.setChartDepth(4);
    saved.setChartLayout(QStringLiteral("tablesLeft"));
    saved.setDefaultView(1);
    saved.setFolderMenuEnabled(false);
    saved.setFontSize(14);
    saved.setTreeRowHeight(27);
    saved.setFileRowHeight(31);
    saved.setHeaderRowHeight(25);
    saved.setAppearanceMode(1);
    saved.setColumnWidths(QStringLiteral("{\"tree\":{\"size\":140},\"file\":{},\"legend\":{\"files\":60}}"));
    check(saved.save(), "the settings are written");

    ScanSettings back;
    check(back.load(), "the settings are read back");
    check(back.scanMode() == 1, "the scan mode survives the round trip");
    check(back.workers() == 4, "the worker count survives the round trip");
    check(back.media() == 1, "the media survives the round trip");
    check(back.previewIntervalMs() == 5000, "the preview interval survives the round trip");
    check(back.lastPath() == QStringLiteral("D:\\工作"), "the last path survives the round trip");
    check(back.excludedPaths().size() == 2, "the excluded paths survive the round trip without duplicates");
    check(back.hiddenColumns().size() == 2, "only the known columns are kept");
    check(back.fuzzyMatch(), "the fuzzy mode survives the round trip");
    check(back.wholeTree(), "the whole-tree scope survives the round trip");
    check(back.chartDepth() == 4, "the chart depth survives the round trip");
    check(back.chartLayout() == QStringLiteral("dock"), "legacy layout settings migrate to Dock");
    check(back.defaultView() == 1, "the default layout survives the round trip");
    check(!back.folderMenuEnabled(), "the folder menu switch survives the round trip");
    check(back.fontSize() == 14, "the font size survives the round trip");
    check(back.treeRowHeight() == 27, "the tree row height survives the round trip");
    check(back.fileRowHeight() == 31, "the file row height survives the round trip");
    check(back.headerRowHeight() == 25, "the header row height survives the round trip");
    check(back.appearanceMode() == 1, "the appearance mode survives the round trip");
    check(back.columnWidths().contains(QStringLiteral("\"size\":140"))
              && back.columnWidths().contains(QStringLiteral("\"files\":60")),
          "the column widths survive the round trip");

    // A hand-edited file with nonsense in it must not be trusted.
    const QString file = QDir(dir.path()).filePath(QStringLiteral("diskview-qml.json"));
    {
        QFile out(file);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            check(false, "the hand-edited file can be written");
        }
        out.write(R"({"path":"C:\\","scanMode":99,"workers":64,"media":-3,"previewIntervalMs":7,"excludedPaths":["C:\\x",42,""]})");
    }
    ScanSettings clamped;
    check(clamped.load(), "a hand-edited file is read");
    check(clamped.scanMode() == 2, "the hand-edited scan mode is clamped");
    check(clamped.workers() == 8, "the hand-edited worker count is clamped");
    check(clamped.media() == 0, "the hand-edited media is clamped");
    check(clamped.previewIntervalMs() == 1500, "the hand-edited preview interval falls back");
    check(clamped.excludedPaths().size() == 1, "only the usable excluded paths are kept");

    // A file that is not JSON at all leaves the defaults alone.
    {
        QFile out(file);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            check(false, "the broken file can be written");
        }
        out.write("not json");
    }
    ScanSettings broken;
    check(!broken.load(), "a broken file is not accepted");
    check(broken.workers() == 0, "a broken file leaves the defaults alone");
}

void testFixtureModeDoesNotEnumerateVolumes()
{
    QTemporaryDir dir(QDir::current().filePath(QStringLiteral("dv-settings-fixture-XXXXXX")));
    check(dir.isValid(), "the fixture settings directory is usable");
    if (!dir.isValid()) return;
    ScopedDataDir scoped(dir.path());
    ScanSettings settings;
    settings.setDefaultVolumeEnumerationEnabled(false);
    check(!settings.load(), "fixture settings start without a file");
    check(settings.excludedPaths().isEmpty(), "fixture defaults do not enumerate mounted volumes");
}

void testLegacyEmptyExclusionsAreSeededOnce()
{
    QTemporaryDir dir(QDir::current().filePath(QStringLiteral("dv-settings-exclusions-XXXXXX")));
    check(dir.isValid(), "the legacy exclusion settings directory is usable");
    if (!dir.isValid()) return;
    ScopedDataDir scoped(dir.path());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("diskview-qml.json"));
    {
        QFile file(path);
        check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "the legacy settings file is writable");
        file.write(R"({"excludedPaths":[]})");
    }

    ScanSettings migrated;
    check(migrated.load(), "the legacy settings file is read");
    check(!migrated.excludedPaths().isEmpty(), "an old empty exclusion list receives the default system paths");
    check(migrated.save(), "the seeded defaults and initialization marker are saved");

    migrated.setExcludedPaths({});
    check(migrated.save(), "an intentionally empty exclusion list is saved");
    ScanSettings restored;
    check(restored.load(), "the migrated settings are read back");
    check(restored.excludedPaths().isEmpty(), "user-cleared defaults stay cleared after restart");
}

// The host sends choices as strings; the scanner and the window use numbers.
// A value has to mean the same thing on the card and in the scan.
void testHostChoices()
{
    check(ScanSettings::scanModeFromHost(QStringLiteral("auto")) == 0
              && ScanSettings::scanModeFromHost(QStringLiteral("fast")) == 1
              && ScanSettings::scanModeFromHost(QStringLiteral("ordinary")) == 2,
          "the host's scan modes map to the scanner's three");
    check(ScanSettings::scanModeToHost(0) == QStringLiteral("auto")
              && ScanSettings::scanModeToHost(1) == QStringLiteral("fast")
              && ScanSettings::scanModeToHost(2) == QStringLiteral("ordinary"),
          "the scan modes map back to the host's names");
    check(ScanSettings::workersFromHost(QStringLiteral("auto")) == 0
              && ScanSettings::workersFromHost(QStringLiteral("4")) == 4,
          "the host's worker counts map to the scanner's");
    check(ScanSettings::workersToHost(0) == QStringLiteral("auto")
              && ScanSettings::workersToHost(8) == QStringLiteral("8"),
          "the worker counts map back to the host's names");
    check(ScanSettings::mediaFromHost(QStringLiteral("auto")) == 0
              && ScanSettings::mediaFromHost(QStringLiteral("ssd")) == 1
              && ScanSettings::mediaFromHost(QStringLiteral("hdd")) == 2
              && ScanSettings::mediaFromHost(QStringLiteral("network")) == 3,
          "the host's media map to the scanner's");
    check(ScanSettings::mediaToHost(3) == QStringLiteral("network")
              && ScanSettings::mediaToHost(0) == QStringLiteral("auto"),
          "the media map back to the host's names");
    check(ScanSettings::previewIntervalFromHost(QStringLiteral("balanced")) == 1500
              && ScanSettings::previewIntervalFromHost(QStringLiteral("less")) == 5000,
          "the host's preview rates map to the two intervals");
    check(ScanSettings::previewIntervalToHost(5000) == QStringLiteral("less")
              && ScanSettings::previewIntervalToHost(1500) == QStringLiteral("balanced"),
          "the preview intervals map back to the host's names");
    check(ScanSettings::defaultViewFromHost(QStringLiteral("sunburst")) == 1
              && ScanSettings::defaultViewFromHost(QStringLiteral("treemap")) == 0,
          "the host's default views map to the window's two layouts");
    check(ScanSettings::defaultViewToHost(1) == QStringLiteral("sunburst")
              && ScanSettings::defaultViewToHost(0) == QStringLiteral("treemap"),
          "the default views map back to the host's names");
}

// What the host sends wins over the file, key by key: a key it does not send
// leaves the file's value alone, which is what keeps the local-only scan
// parameters after they left the manifest.
void testApplyHostSettings()
{
    QTemporaryDir dir(QDir::current().filePath(QStringLiteral("dv-settings-host-XXXXXX")));
    check(dir.isValid(), "the host settings directory is usable");
    if (!dir.isValid()) return;
    ScopedDataDir scoped(dir.path());

    ScanSettings settings;
    settings.setScanMode(2);
    settings.setWorkers(8);
    settings.setMedia(2);
    settings.setPreviewIntervalMs(5000);
    settings.setDefaultView(0);
    settings.setFolderMenuEnabled(true);
    check(settings.save(), "the local settings of the host test are written");

    // The manifest's three public keys arrive on every start; the advanced
    // ones only when the user had set them on the card before the move.
    settings.applyHostSettings({
        {QStringLiteral("defaultView"), QStringLiteral("sunburst")},
        {QStringLiteral("scanMode"), QStringLiteral("fast")},
        {QStringLiteral("folderMenuEnabled"), false},
        {QStringLiteral("workers"), QStringLiteral("4")},
    });
    check(settings.defaultView() == 1, "the host's default view reaches the window setting");
    check(settings.scanMode() == 1, "the host's scan mode reaches the scanner");
    check(!settings.folderMenuEnabled(), "the host's folder menu switch reaches the action");
    check(settings.workers() == 4, "a worker count the host still sends wins over the file");
    check(settings.media() == 2, "a media value the host no longer sends keeps the file's");
    check(settings.previewIntervalMs() == 5000, "a preview rate the host no longer sends keeps the file's");

    // The defaults the manifest declares arrive too, so an untouched card
    // still means "automatic".
    settings.applyHostSettings({
        {QStringLiteral("defaultView"), QStringLiteral("treemap")},
        {QStringLiteral("scanMode"), QStringLiteral("auto")},
        {QStringLiteral("folderMenuEnabled"), true},
    });
    check(settings.defaultView() == 0, "the host's default layout is applied");
    check(settings.scanMode() == 0, "the host's automatic scan mode is applied");
    check(settings.folderMenuEnabled(), "the host's folder menu default is applied");
    check(settings.workers() == 4, "the advanced parameters survive a host update that omits them");

    // Applying the same values again changes nothing: the setters compare, so
    // a host echo cannot bounce back to it.
    int changes = 0;
    QObject::connect(&settings, &ScanSettings::changed, [&changes] { ++changes; });
    settings.applyHostSettings({
        {QStringLiteral("defaultView"), QStringLiteral("treemap")},
        {QStringLiteral("scanMode"), QStringLiteral("auto")},
        {QStringLiteral("folderMenuEnabled"), true},
    });
    check(changes == 0, "an unchanged host setting does not report a change");
}

} // namespace

int main()
{
    std::cout << "scan settings\n";
    testDefaults();
    testClamping();
    testExcludedPaths();
    testRoundTrip();
    testFixtureModeDoesNotEnumerateVolumes();
    testLegacyEmptyExclusionsAreSeededOnce();
    testHostChoices();
    testApplyHostSettings();
    if (g_failures == 0) {
        std::cout << "PASS\n";
        return 0;
    }
    std::cout << "FAIL " << g_failures << "\n";
    return 1;
}
