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

} // namespace

int main()
{
    std::cout << "scan settings\n";
    testDefaults();
    testClamping();
    testExcludedPaths();
    testRoundTrip();
    if (g_failures == 0) {
        std::cout << "PASS\n";
        return 0;
    }
    std::cout << "FAIL " << g_failures << "\n";
    return 1;
}
