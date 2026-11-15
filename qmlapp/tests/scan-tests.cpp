// The scanner's contract with the caller: a held scan reads nothing and goes
// on when it is let go, and a directory that cannot be read is reported
// instead of being passed over in silence.

#include "ScanService.h"
#include "ScanSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTimer>

#include "scan.h"
// The in-process fast scan: declared after scan.h, which pulls windows.h.
#include "fast_scan.h"

#include <atomic>
#include <cstdio>
#include <thread>

#include <windows.h>

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

void writeFile(const QString& path, int bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::fprintf(stderr, "FAIL cannot write %s\n", qPrintable(path));
        ++failures;
        return;
    }
    file.write(QByteArray(bytes, 'x'));
    file.close();
}

// alpha/beta/one.txt, alpha/gamma/two.bin and delta.txt: three files in two
// directories below the root.
QString makeTree(const QString& root)
{
    QDir().mkpath(root + "/alpha/beta");
    QDir().mkpath(root + "/alpha/gamma");
    writeFile(root + "/alpha/beta/one.txt", 4096);
    writeFile(root + "/alpha/gamma/two.bin", 16384);
    writeFile(root + "/delta.txt", 2048);
    return root;
}

void waitMs(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

// The engine on its own: a scan that is held must not read anything, and it
// has to finish once the hold is released.
void testHeldScanReadsNothing()
{
    const QString root = makeTree(QDir::current().filePath(QStringLiteral("dv-scan-pause")));
    std::atomic<bool> paused {true};
    std::atomic<bool> cancel {false};
    std::atomic<bool> done {false};
    std::atomic<int> seen {0};
    diskview::ScanOptions options;
    options.paused = &paused;
    diskview::ScanResult result;
    const diskview::ProgressFn progress = [&seen](const diskview::ScanProgress& p) {
        seen.store(static_cast<int>(p.files));
        return true;
    };
    std::thread worker([&] {
        diskview::scanPath(root.toStdWString(), progress, cancel, &result, {}, options);
        done.store(true);
    });

    waitMs(400);
    check(!done.load(), "a held scan does not finish");
    check(seen.load() == 0, "a held scan reads nothing");

    paused.store(false);
    worker.join();
    check(result.files == 3, "the scan finds the three files once it is let go");
    check(result.dirs == 3, "the scan finds the three directories once it is let go");
}

// The service on top of it: the pause reaches the scanner, and the result
// arrives when the scan is resumed.
void testServiceResumes()
{
    const QString root = makeTree(QDir::current().filePath(QStringLiteral("dv-scan-service")));
    ScanService service;
    QEventLoop loop;
    bool finished = false;
    QObject::connect(&service, &ScanService::finished, &loop, [&] {
        finished = true;
        loop.quit();
    });
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);

    check(service.startPaused(root), "the scan starts");
    waitMs(400);
    check(service.paused(), "the service reports the pause");
    check(service.isRunning(), "the service is still scanning");
    check(service.fileCount() == 0, "a held scan has no result yet");

    service.resume();
    loop.exec();
    check(finished, "the scan finishes after the resume");
    check(service.state() == QStringLiteral("done"), "the service reports the finished scan");
    check(service.fileCount() == 3, "the result holds the three files");
    check(service.dirCount() == 3, "the result holds the three directories");
}

// A directory that cannot be opened is reported, not passed over quietly.
void testUnreadableDirectoryIsReported()
{
    const QString root = makeTree(QDir::current().filePath(QStringLiteral("dv-scan-issues")));
    const QString locked = root + "/locked";
    QDir().mkpath(locked);
    writeFile(locked + "/hidden.txt", 512);
    // The directory is held open without sharing: the scan cannot open it.
    HANDLE hold = ::CreateFileW(reinterpret_cast<const wchar_t*>(locked.utf16()), GENERIC_READ, 0, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    check(hold != INVALID_HANDLE_VALUE, "the directory can be held open");
    if (hold == INVALID_HANDLE_VALUE) {
        return;
    }

    ScanService service;
    QEventLoop loop;
    QObject::connect(&service, &ScanService::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    check(service.start(root), "the scan starts");
    loop.exec();

    check(service.skippedDirs() == 1, "the unreadable directory is counted");
    check(service.issues().size() == 1, "the unreadable directory is listed");
    if (service.issues().size() == 1) {
        const QVariantMap issue = service.issues().first().toMap();
        check(issue.value(QStringLiteral("path")).toString() == locked.toLower()
                  || issue.value(QStringLiteral("path")).toString().contains(QStringLiteral("locked")),
              "the issue names the directory that could not be read");
        check(!issue.value(QStringLiteral("message")).toString().isEmpty(),
              "the issue carries the system message");
    }
    ::CloseHandle(hold);
}

} // namespace

// The fast scan runs in this process now: no helper, no prompt of its own.
// What can be tested without an administrator token is the plumbing around
// it — the elevation probe, the fallback text and the reader refusing a
// path that is not a fixed NTFS drive root.
void testFastScanFallback()
{
    // The probe answers for this process, whatever it is; both answers are
    // valid, the point is that it does not crash and agrees with itself.
    const bool elevated = diskview::fastscan::processElevated();
    check(diskview::fastscan::processElevated() == elevated, "the elevation probe is stable");

    // Without the token the window is told how the fast scan becomes
    // available; with it, the reader's own error is passed through.
    const QString hint = ScanService::fastScanFallbackText(false, L"volume open failed");
    check(hint.contains(QStringLiteral("未以管理员")), "the fallback text tells a normal start why the fast scan is off");
    check(!ScanService::fastScanFallbackText(true, L"volume open failed").contains(QStringLiteral("未以管理员")),
          "an elevated process keeps the reader's own fallback reason");

    // A directory below the root is not a drive root, so the reader refuses
    // it and the outcome is a fallback with a reason, never a crash.
    const QString root = makeTree(QDir::current().filePath(QStringLiteral("dv-scan-fast")));
    std::atomic<bool> cancel {false};
    FlatTree rejected;
    std::wstring reason;
    std::uint64_t files = 0, dirs = 0, excluded = 0, orphans = 0, unknown = 0;
    const diskview::ProgressFn progress = [](const diskview::ScanProgress&) { return true; };
    const auto outcome = diskview::fastscan::scanToFlatSafe(
        root.toStdWString(), progress, cancel, rejected, reason, {},
        files, dirs, excluded, orphans, unknown);
    check(outcome == diskview::fastscan::Outcome::Fallback, "the in-process fast scan refuses a non-root path");
    check(!reason.empty(), "the refused fast scan says why");

    // A cancelled scan reports the cancellation, whatever the path.
    std::atomic<bool> cancelled {true};
    FlatTree cancelledTree;
    std::wstring cancelledReason;
    std::uint64_t cf = 0, cd = 0, ce = 0, co = 0, cu = 0;
    const auto cancelledOutcome = diskview::fastscan::scanToFlatSafe(
        root.toStdWString(), progress, cancelled, cancelledTree, cancelledReason, {},
        cf, cd, ce, co, cu);
    check(cancelledOutcome == diskview::fastscan::Outcome::Cancelled,
          "a cancelled fast scan ends without completing");
    (void)elevated;
}

// The flat builder orders parents before children, transfers names from the
// read batches, applies exclusions and totals each directory, without the
// intermediate Node tree or a per-record hash index.
void testFlatBuild()
{
    const auto entry = [](std::uint64_t id, std::uint64_t parent, const wchar_t* name,
                          bool dir, bool known, std::uint64_t bytes) {
        diskview::mft::Entry e;
        e.id = id; e.parent = parent; e.name = name; e.directory = dir;
        e.known = known; e.bytes = bytes;
        e.attributes = dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
        return e;
    };
    const std::uint64_t root = 5;
    auto nodes = diskview::fastscan::nodesFromEntries({
        entry(42, root, L"alpha", true, false, 0),
        entry(43, 42, L"a.txt", false, true, 100),
        entry(44, root, L"b.bin", false, true, 200),
    });

    FlatTree tree;
    std::uint64_t excluded = 0, orphans = 0, unknown = 0;
    const auto outcome = diskview::fastscan::buildFlat(nodes, 4096, L"C:\\", {}, tree,
                                                       excluded, orphans, unknown);
    check(outcome == diskview::fastscan::Outcome::Complete, "flat build completes");
    check(tree.count() == 4, "flat build writes root, folder and two files");
    check(tree.names.at(0) == QStringLiteral("C:\\") && (tree.flags.at(0) & 1),
          "flat build starts at the scanned root");
    check(tree.fileCounts.at(0) == 2 && tree.dirCounts.at(0) == 1,
          "flat build totals files and folders under the root");
    check(tree.sizes.at(0) == 300, "flat build sums logical bytes");
    for (int i = 1; i < tree.count(); ++i)
        check(tree.parents.at(i) >= 0 && tree.parents.at(i) < i, "flat build keeps parents before children");
    bool sawAlpha = false, sawA = false;
    for (int i = 0; i < tree.count(); ++i) {
        if (tree.names.at(i) == QStringLiteral("alpha")) sawAlpha = tree.sizes.at(i) == 100;
        if (tree.names.at(i) == QStringLiteral("a.txt")) sawA = true;
    }
    check(sawAlpha && sawA, "flat build keeps nested names and folder sizes");

    auto excludedInput = diskview::fastscan::nodesFromEntries({
        entry(42, root, L"alpha", true, false, 0),
        entry(43, 42, L"a.txt", false, true, 100),
        entry(44, root, L"b.bin", false, true, 200),
    });
    diskview::ScanOptions options;
    options.excludedPaths.push_back(L"C:\\alpha");
    FlatTree filtered;
    std::uint64_t filteredExcluded = 0, filteredOrphans = 0, filteredUnknown = 0;
    diskview::fastscan::buildFlat(excludedInput, 4096, L"C:\\", options, filtered,
                                  filteredExcluded, filteredOrphans, filteredUnknown);
    check(filtered.count() == 2, "flat build drops an excluded subtree");
    check(filteredExcluded == 1, "flat build counts the excluded subtree once");
    check(filtered.fileCounts.at(0) == 1 && filtered.sizes.at(0) == 200,
          "excluded bytes leave the totals");

    // A name that came from an extension record does not repeat the size; the
    // base-record table must fill it, not count the file as unknown.
    auto extensionNames = diskview::fastscan::nodesFromEntries({
        entry(50, root, L"", false, true, 4096),      // base carrier: size only
        entry(50, root, L"ext.bin", false, false, 0), // name from an extension record
    });
    FlatTree merged;
    std::uint64_t mergedExcluded = 0, mergedOrphans = 0, mergedUnknown = 0;
    diskview::fastscan::buildFlat(extensionNames, 4096, L"C:\\", {}, merged,
                                  mergedExcluded, mergedOrphans, mergedUnknown);
    check(merged.sizes.at(0) == 4096 && mergedUnknown == 0,
          "base-record size fills an extension-only name");

    // A hard link is one record with two names; the bytes must be counted once.
    auto hardLinks = diskview::fastscan::nodesFromEntries({
        entry(60, root, L"first.bin", false, true, 100),
        entry(60, root, L"second.bin", false, true, 100),
    });
    FlatTree linked;
    std::uint64_t linkedExcluded = 0, linkedOrphans = 0, linkedUnknown = 0;
    diskview::fastscan::buildFlat(hardLinks, 4096, L"C:\\", {}, linked,
                                  linkedExcluded, linkedOrphans, linkedUnknown);
    check(linked.count() == 2, "a hard-link alias is not emitted twice");
    check(linked.sizes.at(0) == 100, "hard-linked bytes are counted once");
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    testHeldScanReadsNothing();
    testServiceResumes();
    testUnreadableDirectoryIsReported();
    testFastScanFallback();
    testFlatBuild();
    {
        diskview::mft::Batch batch;
        diskview::mft::Entry entry;
        entry.name=std::wstring(256,L'x');entry.known=true;entry.bytes=42;
        batch.entries.push_back(std::move(entry));
        diskview::fastscan::Nodes accumulated;
        std::uint64_t files=0,bytes=0;
        diskview::fastscan::appendBatch(batch,accumulated,files,bytes);
        check(batch.entries.empty()&&accumulated.size()==1,"MFT batch is consumed");
        check(accumulated.nameAt(0)==QStringView(QString(256, QLatin1Char('x'))),
              "MFT name is copied into the packed character buffer");
        check(files==1&&bytes==42,"MFT batch counters are preserved");
    }
    {
        std::vector<diskview::mft::Entry> entries;
        diskview::mft::Entry folder;
        folder.id = 42; folder.parent = ws::ntfs::kRootRecord;
        folder.directory = true; folder.name = L"folder";
        entries.push_back(folder);
        diskview::mft::Entry file;
        file.id = 43; file.parent = 42; file.name = L"sample.txt";
        file.known = true; file.bytes = 1234;
        entries.push_back(file);
        diskview::ScanResult copied, consumed;
        diskview::mft::build(L"C:\\", entries, 4096, copied);
        diskview::mft::build(L"C:\\", entries, 4096, consumed, true, {}, nullptr, &entries);
        check(consumed.files == copied.files && consumed.dirs == copied.dirs,
              "consuming MFT names preserves counts");
        check(consumed.root.logical == 1234 && consumed.orphanRecords == 0,
              "compact visited flags preserve sizes and orphan accounting");
        check(consumed.root.children.at(0).name == L"folder"
              && consumed.root.children.at(0).children.at(0).name == L"sample.txt",
              "consuming build transfers directory and file names");
        check(copied.root.children.at(0).children.at(0).name == L"sample.txt",
              "non-consuming native build remains supported");
    }
    if (failures == 0) {
        std::printf("scan tests: PASS\n");
        return 0;
    }
    std::printf("scan tests: %d FAILURES\n", failures);
    return 1;
}
