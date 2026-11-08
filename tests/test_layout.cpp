// test_layout.cpp - pure-logic tests for tree.h (layouts, formatting) and a
// real temp-directory round trip for scan.h. No window, no plugin process.
#include "scan.h"
#include "tree.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int g_failures = 0;

#define CHECK(cond)                                                                                                  \
    do {                                                                                                             \
        if (!(cond)) {                                                                                                \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                               \
            ++g_failures;                                                                                            \
        }                                                                                                            \
    } while (0)

#define CHECK_MSG(cond, msg)                                                                                          \
    do {                                                                                                             \
        if (!(cond)) {                                                                                                \
            std::printf("FAIL %s:%d  %s  (%s)\n", __FILE__, __LINE__, #cond, msg);                                   \
            ++g_failures;                                                                                            \
        }                                                                                                            \
    } while (0)

// ---- formatBytes / formatCount ----------------------------------------------------

static void testFormat()
{
    using diskview::formatBytes;
    CHECK(formatBytes(0, true) == "0 B");
    CHECK(formatBytes(512, true) == "512 B");
    CHECK(formatBytes(1024, true) == "1.0 KB");
    CHECK(formatBytes(1536, true) == "1.5 KB");
    CHECK(formatBytes(100ull * 1024, true) == "100 KB"); // >=100: no decimals
    CHECK(formatBytes(5ull * 1024 * 1024 + 512ull * 1024, true) == "5.5 MB");
    CHECK(formatBytes(1024ull * 1024 * 1024, false) == "1.0 GiB");
    CHECK(formatBytes(3ull * 1024 * 1024 * 1024 * 1024, true) == "3.0 TB");
    CHECK(diskview::formatCount(0) == "0");
    CHECK(diskview::formatCount(999) == "999");
    CHECK(diskview::formatCount(1000) == "1,000");
    CHECK(diskview::formatCount(1234567) == "1,234,567");
}

// ---- squarified treemap -------------------------------------------------------------

static diskview::Node makeDir(const std::wstring& name,
                              std::initializer_list<std::pair<std::wstring, std::uint64_t>> files)
{
    diskview::Node d;
    d.name = name;
    d.dir = true;
    for (const auto& [fname, size] : files) {
        diskview::Node f;
        f.name = fname;
        f.logical = size;
        f.allocApprox = size;
        d.children.push_back(std::move(f));
        d.logical += size;
        d.allocApprox += size;
        ++d.files;
    }
    return d;
}

static void testTreemapCoversArea()
{
    diskview::Node root = makeDir(L"root", {{L"a", 600}, {L"b", 300}, {L"c", 100}});
    const diskview::Rect area {0, 0, 100, 100};
    std::vector<diskview::Tile> tiles;
    diskview::treemapTiles(root, area, 0, 0.0, tiles);
    CHECK(tiles.size() == 3);
    double sum = 0;
    for (const diskview::Tile& t : tiles) {
        sum += t.rect.area();
        CHECK(t.rect.x >= area.x - 1e-9 && t.rect.y >= area.y - 1e-9);
        CHECK(t.rect.x + t.rect.w <= area.x + area.w + 1e-9);
        CHECK(t.rect.y + t.rect.h <= area.y + area.h + 1e-9);
        CHECK(t.depth == 0);
        CHECK(t.path == t.node->name);
    }
    CHECK_MSG(std::abs(sum - area.area()) < 1e-6, "tiles must cover the parent exactly when nothing is culled");

    // Proportionality: each tile's area matches its logical share (±0.5%).
    for (const diskview::Tile& t : tiles) {
        const double expected = area.area() * static_cast<double>(t.node->logical)
                                / static_cast<double>(root.logical);
        CHECK(std::abs(t.rect.area() - expected) / expected < 0.005);
    }

    // No pairwise overlaps.
    for (std::size_t i = 0; i < tiles.size(); ++i)
        for (std::size_t j = i + 1; j < tiles.size(); ++j) {
            const diskview::Rect& a = tiles[i].rect;
            const diskview::Rect& b = tiles[j].rect;
            const bool overlap = a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
            CHECK_MSG(!overlap, "tiles must not overlap");
        }
}

static void testTreemapAspectRatio()
{
    // Four equal blocks in a 2:1 area: the squarified pass should keep every
    // tile within a modest aspect ratio (a naive column split would give 8:1).
    diskview::Node root = makeDir(L"root", {{L"a", 25}, {L"b", 25}, {L"c", 25}, {L"d", 25}});
    const diskview::Rect area {0, 0, 200, 100};
    std::vector<diskview::Tile> tiles;
    diskview::treemapTiles(root, area, 0, 0.0, tiles);
    CHECK(tiles.size() == 4);
    for (const diskview::Tile& t : tiles) {
        const double ratio = std::max(t.rect.w / t.rect.h, t.rect.h / t.rect.w);
        CHECK_MSG(ratio <= 4.0 + 1e-9, "squarified rows keep tiles near square");
    }
}

static void testTreemapCullingKeepsParent()
{
    // One huge child, one below the pixel budget: the small one is culled, the
    // big one keeps its exact share, and the leftover stays as background
    // (the parent's total is never redistributed).
    diskview::Node root = makeDir(L"root", {{L"big", 9900}, {L"tiny", 10}});
    const diskview::Rect area {0, 0, 50, 50};
    std::vector<diskview::Tile> tiles;
    diskview::treemapTiles(root, area, 0, 100.0, tiles);
    CHECK(tiles.size() == 1);
    CHECK(tiles[0].node->name == L"big");
    const double expected = area.area() * 9900.0 / 9910.0;
    CHECK(std::abs(tiles[0].rect.area() - expected) / expected < 0.005);
    CHECK(tiles[0].rect.area() < area.area()); // the culled child's space is left empty
    CHECK(root.logical == 9910);               // totals are untouched by culling
}

static void testTreemapNestsPaths()
{
    diskview::Node sub = makeDir(L"sub", {{L"x", 50}, {L"y", 50}});
    diskview::Node root = makeDir(L"root", {{L"sub", 100}, {L"z", 100}});
    root.children.insert(root.children.begin(), sub);
    root.logical += sub.logical;
    root.files += sub.files;
    root.dirs = 1;
    const diskview::Rect area {0, 0, 100, 100};
    std::vector<diskview::Tile> tiles;
    diskview::treemapTiles(root, area, 0, 0.0, tiles);
    bool sawSubFile = false;
    for (const diskview::Tile& t : tiles) {
        if (t.node->name == L"x") {
            CHECK(t.depth == 1);
            CHECK(t.path == L"root\\sub\\x");
            sawSubFile = true;
        }
        if (t.node->name == L"z") {
            CHECK(t.depth == 0);
            CHECK(t.path == L"root\\z");
        }
    }
    CHECK(sawSubFile);
}

// ---- sunburst -----------------------------------------------------------------------

static void testSunburstSpans()
{
    diskview::Node root = makeDir(L"root", {{L"a", 50}, {L"b", 30}, {L"c", 20}});
    std::vector<diskview::Slice> slices;
    const double pi = 3.14159265358979323846;
    diskview::sunburstSlices(root, 0, 0, 40, 20, -pi / 2, -pi / 2 + 2 * pi, 0, 0.0, slices);
    CHECK(slices.size() == 3);
    const double twoPi = 2 * pi;
    double covered = 0;
    for (std::size_t i = 0; i < slices.size(); ++i) {
        CHECK(slices[i].depth == 0);
        CHECK(slices[i].r0 == 40 && slices[i].r1 == 60);
        CHECK(slices[i].a1 > slices[i].a0);
        if (i > 0)
            CHECK(std::abs(slices[i - 1].a1 - slices[i].a0) < 1e-9); // contiguous, no gaps
        covered += slices[i].a1 - slices[i].a0;
    }
    CHECK(std::abs(covered - twoPi) < 1e-6);
    // Shares follow logical size.
    CHECK(std::abs((slices[0].a1 - slices[0].a0) - 0.5 * twoPi) < 1e-6);
    CHECK(std::abs((slices[2].a1 - slices[2].a0) - 0.2 * twoPi) < 1e-6);
    // Angles start at the top (-π/2).
    CHECK(std::abs(slices[0].a0 - (-pi / 2)) < 1e-9);
}

static void testSunburstNestsRings()
{
    diskview::Node sub = makeDir(L"sub", {{L"x", 60}, {L"y", 40}});
    diskview::Node root = makeDir(L"root", {{L"sub", 100}, {L"z", 100}});
    root.children.insert(root.children.begin(), sub);
    root.logical += sub.logical;
    std::vector<diskview::Slice> slices;
    const double pi = 3.14159265358979323846;
    diskview::sunburstSlices(root, 0, 0, 10, 20, -pi / 2, -pi / 2 + 2 * pi, 0, 0.0, slices);
    const diskview::Slice* subSlice = nullptr;
    int depth1 = 0;
    for (const diskview::Slice& sl : slices) {
        if (sl.node->name == L"sub" && sl.depth == 0)
            subSlice = &sl;
        if (sl.depth == 1)
            ++depth1;
    }
    CHECK(subSlice != nullptr);
    CHECK(depth1 == 2);
    if (subSlice) {
        for (const diskview::Slice& sl : slices)
            if (sl.depth == 1) {
                CHECK(sl.r0 == subSlice->r1); // the child ring starts where the parent ends
                CHECK(sl.a0 >= subSlice->a0 - 1e-9 && sl.a1 <= subSlice->a1 + 1e-9);
                CHECK(sl.path.rfind(L"root\\sub\\", 0) == 0);
            }
    }
}

static void testSunburstCulling()
{
    diskview::Node root = makeDir(L"root", {{L"a", 990}, {L"tiny", 10}});
    std::vector<diskview::Slice> slices;
    const double pi = 3.14159265358979323846;
    const double twoPi = 2 * pi;
    // minAngle 0.1 rad > tiny's 1% share (0.0628 rad): culled.
    diskview::sunburstSlices(root, 0, 0, 40, 20, -pi / 2, -pi / 2 + twoPi, 0, 0.1, slices);
    CHECK(slices.size() == 1);
    CHECK(slices[0].node->name == L"a");
    CHECK(std::abs((slices[0].a1 - slices[0].a0) - 0.99 * twoPi) < 1e-6); // share unchanged
    CHECK(root.logical == 1000);
}

// ---- scan round trip (real temp directory) ------------------------------------------

class TempDir {
public:
    TempDir()
    {
        wchar_t base[MAX_PATH] = {};
        ::GetTempPathW(MAX_PATH, base);
        m_path = fs::path(base) / (L"diskview_test_" + std::to_wstring(::GetCurrentProcessId()) + L"_"
                                   + std::to_wstring(::GetTickCount64()));
        fs::create_directories(m_path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }
    const fs::path& path() const { return m_path; }

private:
    fs::path m_path;
};

static void writeFile(const fs::path& dir, const std::wstring& name, std::uint64_t bytes)
{
    std::ofstream f(dir / name, std::ios::binary | std::ios::trunc);
    std::vector<char> buf(4096, 'D');
    while (bytes > 0) {
        const std::uint64_t chunk = std::min<std::uint64_t>(bytes, buf.size());
        f.write(buf.data(), static_cast<std::streamsize>(chunk));
        bytes -= chunk;
    }
}

static void testScanSumsAndLargest()
{
    TempDir tmp;
    fs::create_directories(tmp.path() / L"sub");
    writeFile(tmp.path(), L"big.bin", 100000);
    writeFile(tmp.path(), L"small.txt", 7);
    writeFile(tmp.path() / L"sub", L"mid.dat", 5000);
    writeFile(tmp.path() / L"sub", L"deep.log", 300);

    diskview::ScanResult r;
    diskview::scanPath(tmp.path().wstring(), {}, std::atomic<bool> {false}, &r);
    CHECK(r.error.empty());
    CHECK(!r.cancelled);
    CHECK(r.root.logical == 105307);
    CHECK(r.root.files == 4);
    CHECK(r.root.dirs == 1);
    CHECK(r.files == 4);
    CHECK(r.dirs == 1);
    // largest: sorted descending, full paths
    CHECK(r.largest.size() == 4);
    if (r.largest.size() == 4) {
        CHECK(r.largest[0].logical == 100000);
        CHECK(r.largest[0].path.find(L"big.bin") != std::wstring::npos);
        CHECK(r.largest[1].logical == 5000);
        CHECK(std::is_sorted(r.largest.begin(), r.largest.end(), std::greater<>()));
    }
    // display root has no trailing backslash
    CHECK(!r.displayRoot.empty() && r.displayRoot.back() != L'\\');
    // children sorted by size, largest first
    CHECK(r.root.children.size() == 2);
    if (r.root.children.size() == 2) {
        CHECK(r.root.children[0].name == L"big.bin");
        CHECK(r.root.children[1].name == L"sub");
    }
}

static void testScanMissingAndEmpty()
{
    diskview::ScanResult missing;
    diskview::scanPath(L"Z:\\__diskview_definitely_missing__", {}, std::atomic<bool> {false}, &missing);
    CHECK(!missing.error.empty());

    TempDir tmp;
    fs::create_directories(tmp.path() / L"empty");
    diskview::ScanResult r;
    diskview::scanPath((tmp.path() / L"empty").wstring(), {}, std::atomic<bool> {false}, &r);
    CHECK(r.error.empty());
    CHECK(!r.cancelled);
    CHECK(r.files == 0 && r.dirs == 0);
    CHECK(r.root.logical == 0);
    CHECK(r.root.children.empty());
}

static void testScanCancel()
{
    TempDir tmp;
    for (int i = 0; i < 40; ++i) {
        const fs::path d = tmp.path() / (L"d" + std::to_wstring(i));
        fs::create_directories(d);
        for (int j = 0; j < 40; ++j)
            writeFile(d, L"f" + std::to_wstring(j) + L".bin", 100 + static_cast<std::uint64_t>(j));
    }
    diskview::ScanResult r;
    std::atomic<bool> cancel {true}; // cancelled before the first entry
    diskview::scanPath(tmp.path().wstring(), {}, cancel, &r);
    CHECK(r.cancelled);
    // The partial tree is discarded by callers; sums stay partial but bounded.
    CHECK(r.files <= 1600);
}

static void testNormalize()
{
    std::wstring prefixed;
    CHECK(diskview::normalizePath(L"c:", &prefixed) == L"c:\\");
    CHECK(prefixed == L"\\\\?\\c:\\");
    CHECK(diskview::normalizePath(L" D:\\工作 \\ ", &prefixed) == L"D:\\工作");
    CHECK(prefixed == L"\\\\?\\D:\\工作");
    CHECK(diskview::normalizePath(L"\"D:\\a\\b\\\\\"", &prefixed) == L"D:\\a\\b");
    CHECK(prefixed == L"\\\\?\\D:\\a\\b");
    CHECK(diskview::normalizePath(L"\\\\server\\share\\dir", &prefixed) == L"\\\\server\\share\\dir");
    CHECK(prefixed == L"\\\\?\\UNC\\server\\share\\dir");
    CHECK(diskview::normalizePath(L"  \"\"  ", nullptr).empty());
}

int main()
{
    testFormat();
    testTreemapCoversArea();
    testTreemapAspectRatio();
    testTreemapCullingKeepsParent();
    testTreemapNestsPaths();
    testSunburstSpans();
    testSunburstNestsRings();
    testSunburstCulling();
    testScanSumsAndLargest();
    testScanMissingAndEmpty();
    testScanCancel();
    testNormalize();

    if (g_failures == 0) {
        std::printf("diskview tests: all passed\n");
        return 0;
    }
    std::printf("diskview tests: %d failure(s)\n", g_failures);
    return 1;
}
