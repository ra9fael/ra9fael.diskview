// Layout/scanner regressions and real window messages on a hidden test window.
// The JSON-lines integration test separately starts the plugin process.
#include "scan.h"
#include "tree.h"
#include "viewer.h"

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

static diskview::Node makeFile(const std::wstring& name, std::uint64_t size)
{
    diskview::Node f;
    f.name = name;
    f.logical = size;
    f.allocApprox = size;
    return f;
}

// A directory node whose children are given as-is; the totals are summed.
static diskview::Node makeDirOf(std::wstring name, std::vector<diskview::Node> children)
{
    diskview::Node d;
    d.name = std::move(name);
    d.dir = true;
    for (diskview::Node& c : children) {
        d.logical += c.logical;
        d.allocApprox += c.allocApprox;
        d.files += c.dir ? c.files : 1;
        d.dirs += c.dir ? c.dirs + 1 : 0;
        d.children.push_back(std::move(c));
    }
    std::stable_sort(d.children.begin(), d.children.end(),
                     [](const auto& a, const auto& b) { return a.logical > b.logical; });
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
    // One huge child, one below the pixel budget: the small one is folded into
    // an aggregate block that keeps the folder's area completely covered, and
    // the big one keeps its exact share.
    diskview::Node root = makeDir(L"root", {{L"big", 9900}, {L"tiny", 10}});
    const diskview::Rect area {0, 0, 200, 200};
    std::vector<diskview::Tile> tiles;
    diskview::treemapTiles(root, area, 0, 100.0, tiles);
    CHECK(tiles.size() == 2);
    CHECK(tiles[0].node->name == L"big");
    CHECK(tiles[1].remainder);
    CHECK(tiles[1].remainderCount == 1);
    double sum = 0;
    for (const diskview::Tile& t : tiles) sum += t.rect.area();
    CHECK_MSG(std::abs(sum - area.area()) < 1e-6, "the aggregate leaves no hole in the folder");
    const double expected = area.area() * 9900.0 / 9910.0;
    CHECK(std::abs(tiles[0].rect.area() - expected) / expected < 0.005);
    CHECK(root.logical == 9910); // totals are untouched by culling
}

static void testTreemapAggregatesUnaccountedBytes()
{
    // A folder whose payload only carries part of its bytes (the model keeps a
    // bounded top-K): the rest still gets a block instead of a hole that looks
    // like a file of its own.
    diskview::Node root = makeDir(L"root", {{L"a", 400}});
    root.logical = 1000;
    root.rest = 600;
    const diskview::Rect area {0, 0, 100, 100};
    std::vector<diskview::Tile> tiles;
    diskview::treemapTiles(root, area, 0, 0.0, tiles);
    CHECK(tiles.size() == 2);
    CHECK(!tiles[0].remainder && tiles[0].node->name == L"a");
    CHECK(tiles[1].remainder && tiles[1].remainderCount == 0);
    double sum = 0;
    for (const diskview::Tile& t : tiles) sum += t.rect.area();
    CHECK_MSG(std::abs(sum - area.area()) < 1e-6, "unaccounted bytes stay inside the folder");
}

static void testTreemapFolderStates()
{
    // A folder nests when it has room and at least one child worth reading.
    // A narrow folder stays one block.
    diskview::Node sub;
    sub.name = L"sub";
    sub.dir = true;
    sub.logical = 100;
    sub.children = {makeFile(L"x", 60), makeFile(L"y", 40)};
    diskview::Node solo;
    solo.name = L"solo";
    solo.dir = true;
    solo.logical = 100;
    solo.children = {makeFile(L"only", 100)};
    diskview::Node root = makeDirOf(L"root", {std::move(sub), std::move(solo)});

    std::vector<diskview::Tile> tiles;
    diskview::treemapTiles(root, {0, 0, 400, 200}, 0, 18.0, tiles);
    const diskview::Tile* subTile = nullptr;
    const diskview::Tile* soloTile = nullptr;
    int depth1 = 0;
    for (const diskview::Tile& t : tiles) {
        if (t.node && t.node->name == L"sub" && t.depth == 0) subTile = &t;
        if (t.node && t.node->name == L"solo" && t.depth == 0) soloTile = &t;
        if (t.depth == 1) ++depth1;
    }
    CHECK(subTile && subTile->header > 0);
    CHECK(soloTile && soloTile->header > 0);   // room for a strip
    CHECK(subTile && subTile->expanded);
    CHECK(soloTile && soloTile->expanded);
    CHECK(depth1 == 3);
    CHECK(std::any_of(tiles.begin(), tiles.end(), [](const diskview::Tile& t) {
        return t.depth == 1 && t.node && t.node->name == L"only";
    }));

    // The same folders in a 60 px wide column: no strip, no nesting, and the
    // top level is still drawn whole.
    std::vector<diskview::Tile> narrow;
    diskview::treemapTiles(root, {0, 0, 60, 200}, 0, 18.0, narrow);
    CHECK(narrow.size() == 2);
    for (const diskview::Tile& t : narrow) CHECK(t.header == 0);
}

static void testTreemapBranchHues()
{
    // Top-level folders spread around the wheel; a child stays in its own
    // parent's family instead of taking a share of the wheel.
    diskview::Node a;
    a.name = L"a";
    a.dir = true;
    a.logical = 100;
    a.children = {makeFile(L"a1", 60), makeFile(L"a2", 40)};
    diskview::Node b;
    b.name = L"b";
    b.dir = true;
    b.logical = 100;
    b.children = {makeFile(L"b1", 50), makeFile(L"b2", 50)};
    diskview::Node root = makeDirOf(L"root", {std::move(a), std::move(b)});
    std::vector<diskview::Tile> tiles;
    diskview::treemapTiles(root, {0, 0, 400, 200}, 0, 18.0, tiles);

    const auto circular = [](double h0, double h1) {
        double d = std::fabs(h0 - h1);
        d -= std::floor(d);
        return std::min(d, 1.0 - d);
    };
    const diskview::Tile* aTile = nullptr;
    const diskview::Tile* bTile = nullptr;
    const diskview::Tile* a1 = nullptr;
    for (const diskview::Tile& t : tiles) {
        if (!t.node) continue;
        if (t.node->name == L"a" && t.depth == 0) aTile = &t;
        if (t.node->name == L"b" && t.depth == 0) bTile = &t;
        if (t.node->name == L"a1") a1 = &t;
    }
    CHECK(aTile && bTile);
    if (aTile && bTile) {
        CHECK_MSG(circular(aTile->hue, bTile->hue) > 0.3, "two top-level folders are far apart on the wheel");
        CHECK(std::fabs(aTile->hue) < 1e-9); // the largest branch starts at the base hue
    }
    CHECK(a1);
    if (aTile && a1) CHECK_MSG(circular(aTile->hue, a1->hue) < 0.1, "a child stays in its parent's colour family");
}

static void testTreemapNestsPaths()
{
    diskview::Node sub = makeDir(L"sub", {{L"x", 50}, {L"y", 50}});
    diskview::Node root = makeDirOf(L"root", {sub, makeFile(L"z", 100)});
    // 200 px wide: each top-level tile is 100 x 100, wide enough for a strip.
    const diskview::Rect area {0, 0, 200, 100};
    std::vector<diskview::Tile> tiles;
    // The base path is the display path of the node itself ("D:\dir" in the
    // window), so every tile carries it plus its own name.
    diskview::treemapTiles(root, area, 0, 0.0, tiles, L"root");
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
    diskview::Node root = makeDirOf(L"root", {sub, makeFile(L"z", 100)});
    std::vector<diskview::Slice> slices;
    const double pi = 3.14159265358979323846;
    diskview::sunburstSlices(root, 0, 0, 10, 20, -pi / 2, -pi / 2 + 2 * pi, 0, 0.0, slices, L"root");
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
        const std::wstring name = L"diskview_test_" + std::to_wstring(::GetCurrentProcessId()) + L"_"
                                  + std::to_wstring(::GetTickCount64());
        std::error_code ec;
        m_path = fs::path(tempRoot()) / name;
        fs::create_directories(m_path, ec);
        if (ec) {
            // A locked-down or heavily used machine can refuse a directory in
            // %TEMP%; next to the executable is the fallback the settings test
            // already relies on.
            const fs::path beside = fs::path(exeDirectory()) / name;
            ec.clear();
            fs::create_directories(beside, ec);
            if (ec) {
                std::printf("TempDir: cannot create %ls: %s\n", m_path.c_str(), ec.message().c_str());
            } else {
                m_path = beside;
            }
        }
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }
    const fs::path& path() const { return m_path; }

private:
    static std::wstring tempRoot()
    {
        wchar_t base[MAX_PATH] = {};
        const DWORD length = ::GetTempPathW(MAX_PATH, base);
        return length > 0 && length < MAX_PATH ? std::wstring(base, length) : std::wstring(L".\\");
    }
    static std::wstring exeDirectory()
    {
        wchar_t file[MAX_PATH] = {};
        const DWORD length = ::GetModuleFileNameW(nullptr, file, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) return L".";
        return fs::path(file).parent_path().wstring();
    }
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
    CHECK(r.root.children.size() == 3);
    if (r.root.children.size() == 3) {
        CHECK(r.root.children[0].name == L"big.bin");
        CHECK(r.root.children[1].name == L"sub");
        CHECK(r.root.children[2].name == L"small.txt");
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
    // Partial results stay bounded and must be labelled incomplete by callers.
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
    CHECK(diskview::normalizePath(L"\\\\?\\C:\\", &prefixed) == L"C:\\");
    CHECK(prefixed == L"\\\\?\\C:\\");
    CHECK(diskview::normalizePath(L"C:\\\\", &prefixed) == L"C:\\");
    CHECK(prefixed == L"\\\\?\\C:\\");
    CHECK(diskview::normalizePath(L"\\\\?\\UNC\\server\\share\\dir", &prefixed) == L"\\\\server\\share\\dir");
    CHECK(prefixed == L"\\\\?\\UNC\\server\\share\\dir");
    CHECK(diskview::normalizePath(L"C:/one/../two/", &prefixed) == L"C:\\two");
    CHECK(prefixed == L"\\\\?\\C:\\two");
    CHECK(diskview::normalizePath(L".", &prefixed) == fs::current_path().wstring());
    CHECK(diskview::normalizePath(L"", &prefixed).empty());
    CHECK(prefixed.empty());
}

static void testExtendedScanAndReuse()
{
    TempDir tmp;
    writeFile(tmp.path(), L"test.bin", 7);
    diskview::ScanResult r;
    diskview::scanPath(L"\\\\?\\" + tmp.path().wstring(), {}, std::atomic<bool> {false}, &r);
    CHECK(r.error.empty());
    CHECK(r.displayRoot == tmp.path().wstring());
    CHECK(r.root.name == r.displayRoot);
    CHECK(r.largest.size() == 1);
    if (!r.largest.empty())
        CHECK(r.largest.front().path == (tmp.path() / L"test.bin").wstring());
    diskview::scanPath((tmp.path() / L"test.bin").wstring(), {}, std::atomic<bool> {false}, &r);
    CHECK(!r.error.empty());
    CHECK(r.largest.empty());
    diskview::scanPath(tmp.path().wstring(), {}, std::atomic<bool> {false}, &r);
    CHECK(r.error.empty());
    CHECK(r.files == 1 && r.largest.size() == 1);
    int callbacks = 0;
    diskview::scanPath(tmp.path().wstring(), [&](const diskview::ScanProgress&) {
        ++callbacks;
        return false;
    }, std::atomic<bool> {false}, &r);
    CHECK(callbacks == 1 && r.cancelled);
    // Open the real volume root but cancel at the first progress callback.
    // This detects doubled separators without traversing the system drive.
    diskview::scanPath(tmp.path().root_path().wstring(), [](const diskview::ScanProgress&) {
        return false;
    }, std::atomic<bool> {false}, &r);
    CHECK(r.error.empty() && r.cancelled);
}

static void testViewerNavigation()
{
    using namespace diskview;
    using namespace diskview::detail;
    ViewerState s;
    Node root = makeDirOf(L"C:\\", {
        makeDirOf(L"parent", {makeDir(L"deep", {{L"small.txt", 7}}), makeDir(L"empty", {})}),
        makeFile(L"root.bin", 1000)});
    // All scan-wide slots belong to other files. The subtree must still
    // report its own file, which is absent from that ranking.
    for (int i = 0; i < 64; ++i)
        s.rootLargest.push_back({1000, L"C:\\root.bin"});
    s.tree = std::make_shared<Node>(std::move(root));
    s.viewRoot = s.tree.get();
    s.rcChart = {0, 0, 500, 400};
    rebuildLists(s);
    relayoutView(s);
    CHECK(s.folders.size() == 1);
    for (const Tile& tile : s.tiles)
        CHECK(tile.path.find(L"\\\\") == std::wstring::npos);
    const Node* parent = findChild(*s.tree, L"parent");
    const Node* deep = findChild(*parent, L"deep");
    CHECK(navigateTo(s, L"C:\\parent\\deep"));
    CHECK(s.viewRoot == deep);
    CHECK(s.drill.size() == 1 && s.drill.front() == parent);
    CHECK(nodePath(s, s.viewRoot) == L"C:\\parent\\deep");
    setListMode(s,false);
    CHECK(s.largest.size() == 1);
    if (!s.largest.empty())
        CHECK(s.largest.front().path == L"C:\\parent\\deep\\small.txt");
    goUp(s);
    CHECK(s.viewRoot == parent && s.drill.empty());
    CHECK(s.folders.size() == 2);
    setListMode(s, true);
    CHECK(openListRow(s, 1)); // an empty folder is still navigable
    CHECK(s.viewRoot->name == L"empty");
    CHECK(listCount(s) == 0);
    goUp(s);
    goUp(s);
    CHECK(s.viewRoot == s.tree.get());
    setListMode(s, false);
    CHECK(openListRow(s, 0)); // a file directly under the drive root
    CHECK(s.viewRoot == s.tree.get() && s.drill.empty());
    CHECK(navigateTo(s, L"C:\\parent\\deep"));
    s.crumbNodes = {s.tree.get(), parent, deep};
    drillTo(s, 1);
    CHECK(s.viewRoot == parent && s.drill.empty());
    CHECK(!navigateTo(s, L"C:\\parent-other"));
    CHECK(s.viewRoot == parent);
    s.sunburst = true;
    relayoutView(s);
    for (const Slice& slice : s.slices) {
        CHECK(slice.path.rfind(L"C:\\parent\\", 0) == 0);
        CHECK(slice.r1 <= 192.0); // chart radius minus its eight-pixel inset
    }
    CHECK(hitSlice(s, 501, 200) == -1);
    s.rcTopList = {500, 0, 800, 400};
    setListMode(s,true);
    CHECK(hitTop(s, 510, s.topHeaderH + 1) == 0);
    CHECK(hitTop(s, 510, 1) == -1);
    s.lastScanError = "failed";
    relayoutView(s);
    CHECK(!s.slices.empty()); // a failed refresh must not disable the old snapshot
    CHECK(hitTop(s, 510, s.topHeaderH + 1) == 0);
    CHECK(navigateTo(s, L"c:\\")); // Windows path matching is case-insensitive
    s.tree->name = L"C:\\scan";
    s.lastScanError.clear();
    s.rootLargest = {{1000, L"C:\\scan\\root.bin"}};
    CHECK(navigateTo(s, L"C:\\scan"));
    setListMode(s, false);
    CHECK(openListRow(s, 0)); // previously formed end() - 1 on an empty chain
    CHECK(s.drill.empty());
    // A second-level aggregate expands the actual parent's children, even
    // though navigation destroys the synthetic group that was selected.
    CHECK(navigateTo(s,L"C:\\scan\\parent"));
    const auto* groupParent=s.viewRoot;
    auto aggregate=visibleChildren(s,*groupParent,1);
    CHECK(aggregate.size()==1 && !s.groupMembers.empty());
    s.viewRoot=s.tree.get();s.drill.clear();
    selectNode(s,aggregate.front(),L"C:\\scan\\parent\\Other 1 items");
    CHECK(s.viewRoot==groupParent && s.groupFilter.size()==1);
    CHECK(s.rows.size()==1 && s.rows.front().node->name==L"deep");
    CHECK(!s.selected && !s.hovered);
    Node unknown=makeFile(L"unknown.bin",0);unknown.skippedChildren=1;
    CHECK(sizeUnknown(unknown));
    CHECK(sizeLabel(s,unknown)==L"大小未知");
}

// Exercise the real window procedure on this thread. The test window stays
// hidden; optional snapshots render exactly the same paint path to a DIB.
static void renderViewer(const char* name,HWND window=nullptr)
{
    using namespace diskview::detail;
    ViewerState& s = state();
    if(!window)window=s.hwnd;RECT client{};::GetClientRect(window,&client);
    const int w = client.right, h = client.bottom;
    HDC dc = ::CreateCompatibleDC(nullptr);
    BITMAPINFO info {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    CHECK(bitmap != nullptr);
    if (bitmap) {
        const auto old = ::SelectObject(dc, bitmap);
        ::SetBkMode(dc, TRANSPARENT);
        ::SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
        for (HWND child=::GetWindow(window,GW_CHILD);child;child=::GetWindow(child,GW_HWNDNEXT)) {
            if (!(::GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)) continue;
            RECT bounds{};
            ::GetWindowRect(child, &bounds);
            ::MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
            const int saved = ::SaveDC(dc);
            ::IntersectClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
            ::SetViewportOrgEx(dc, bounds.left, bounds.top, nullptr);
            ::SendMessageW(child, WM_PRINT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND);
            ::RestoreDC(dc, saved);
        }
        ::GdiFlush();
        const DWORD length = ::GetEnvironmentVariableW(L"DISKVIEW_SNAPSHOT_DIR", nullptr, 0);
        std::wstring dir(length, L'\0');
        if (length && ::GetEnvironmentVariableW(L"DISKVIEW_SNAPSHOT_DIR", dir.data(), length)) {
            dir.resize(length - 1);
            fs::create_directories(dir);
            BITMAPFILEHEADER header {};
            header.bfType = 0x4d42;
            header.bfOffBits = sizeof header + sizeof info.bmiHeader;
            header.bfSize = header.bfOffBits + static_cast<DWORD>(w * h * 4);
            std::ofstream out(fs::path(dir) / (std::string(name) + ".bmp"), std::ios::binary);
            out.write(reinterpret_cast<const char*>(&header), sizeof header);
            out.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof info.bmiHeader);
            out.write(static_cast<const char*>(pixels), static_cast<std::streamsize>(w) * h * 4);
            CHECK(out.good());
        }
        ::SelectObject(dc, old);
        ::DeleteObject(bitmap);
    }
    ::DeleteDC(dc);
}

#include "test_controls.h"

static void testViewerMessages()
{
    using namespace diskview;
    using namespace diskview::detail;
    ViewerState& s = state();
    ensureDpiAwareness();
    s.spec.zh = true;
    s.palette = lightPalette();
    s.dpi = 96;
    WNDCLASSW wc {};
    wc.lpfnWndProc = wndProc;
    wc.style = CS_DBLCLKS;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DiskViewTestWindow";
    CHECK(::RegisterClassW(&wc) != 0);
    s.hwnd = ::CreateWindowExW(0, wc.lpszClassName, L"Disk View test", WS_OVERLAPPEDWINDOW,
                              0, 0, 1200, 800, nullptr, nullptr, wc.hInstance, nullptr);
    CHECK(s.hwnd != nullptr);
    if (!s.hwnd)
        return;
    s.edit = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                             0, 0, 100, 20, s.hwnd, nullptr, wc.hInstance, nullptr);
    s.origEditProc=reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(s.edit,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(editProc)));
    s.filterEdit=::CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL,0,0,100,28,s.hwnd,reinterpret_cast<HMENU>(101),wc.hInstance,nullptr);
    s.origFilterProc=reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(s.filterEdit,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(filterProc)));
    makeFonts(s);
    computeLayout(s);
    TempDir tmp;
    writeFile(tmp.path(), L"card.bin", 10);
    s.driveCards = {{L'C', tmp.path().wstring(), 1024ull << 30, 250ull << 30},
                    {L'D', L"D:\\", 2048ull << 30, 1200ull << 30}};
    renderViewer("welcome-light");
    CHECK(s.driveRects.size() == 2);
    const auto click = [&](const RECT& r) {
        ::SendMessageW(s.hwnd, WM_LBUTTONUP, 0, MAKELPARAM(r.left + 10, r.top + 10));
    };
    click(s.driveRects.front());
    CHECK(s.scanning);
    CHECK(editText(s.edit) == tmp.path().wstring());
    CHECK(hitDrive(s, 40, 140) == -1);
    const auto deadline = ::GetTickCount64() + 10000;
    while (s.scanning && ::GetTickCount64() < deadline) {
        MSG msg;
        while (::PeekMessageW(&msg, s.hwnd, 0, 0, PM_REMOVE))
            ::DispatchMessageW(&msg);
        ::Sleep(1);
    }
    CHECK(!s.scanning && s.tree && s.tree->logical == 10);
    auto* result = new ScanResult();
    result->root = makeDirOf(L"C:\\", {
        makeDirOf(L"Projects", {makeDir(L"Disk View", {{L"archive.zip", 3ull << 30},
                                                       {L"source.cpp", 30ull << 20}}),
                                makeDir(L"Empty folder", {})}),
        makeDir(L"Videos", {{L"holiday.mp4", 5ull << 30}, {L"demo.mp4", 1ull << 30}}),
        makeFile(L"backup.img", 2ull << 30)});
    result->largest = {{5ull << 30, L"C:\\Videos\\holiday.mp4"},
                       {3ull << 30, L"C:\\Projects\\Disk View\\archive.zip"},
                       {2ull << 30, L"C:\\backup.img"},
                       {1ull << 30, L"C:\\Videos\\demo.mp4"},
                       {30ull << 20, L"C:\\Projects\\Disk View\\source.cpp"}};
    result->files = result->root.files;
    result->dirs = result->root.dirs;
    ::SetWindowTextW(s.edit, L"C:\\");
    ::SendMessageW(s.hwnd, WM_APP_DONE, 0, reinterpret_cast<LPARAM>(result));
    renderViewer("treemap-light");
    click(s.rcListFolders);
    CHECK(s.listFolders);
    renderViewer("folders-light");
    RECT projectsRow = s.rcTopList;
    projectsRow.top += s.topHeaderH + s.rowH;
    click(projectsRow);
    CHECK(s.viewRoot == s.tree.get());
    CHECK(s.selected && s.selected->name == L"Projects");
    ::SendMessageW(s.hwnd,WM_LBUTTONDBLCLK,0,MAKELPARAM(projectsRow.left+10,projectsRow.top+10));
    ::SendMessageW(s.hwnd,WM_LBUTTONUP,0,MAKELPARAM(projectsRow.left+10,projectsRow.top+10));
    CHECK(s.viewRoot && s.viewRoot->name == L"Projects");
    CHECK(s.drill.empty());
    CHECK(s.selected==nullptr); // trailing button-up must not select the new folder's row
    renderViewer("drill-light");
    ::SetWindowTextW(s.filterEdit,L"Empty");
    CHECK(listCount(s)==1 && s.rows[0].node->name==L"Empty folder");
    ::SetWindowTextW(s.filterEdit,L"");
    CHECK(listCount(s)==2);
    historyStep(s,-1);
    CHECK(s.viewRoot==s.tree.get());
    historyStep(s,1);
    CHECK(s.viewRoot->name==L"Projects");
    ::SendMessageW(s.hwnd,WM_KEYDOWN,VK_TAB,0);
    CHECK(s.keyboardFocus && s.focusIndex==0);
    ::SendMessageW(s.hwnd,WM_KEYDOWN,VK_RETURN,0); // focused history-back button
    CHECK(s.viewRoot==s.tree.get());
    s.keyboardFocus=false;
    historyStep(s,1);
    ::SendMessageW(s.hwnd, WM_KEYDOWN, VK_BACK, 0);
    CHECK(s.viewRoot == s.tree.get());
    CHECK(s.tiles.size()==3); // one level is the default
    const auto tile=s.tiles.front();
    updateHover(s,int(tile.rect.x+10),int(tile.rect.y+10));
    ::SendMessageW(s.hwnd,WM_TIMER,2,0);
    CHECK(s.tipVisible && s.hoveredPath==L"C:\\Videos");
    renderViewer("hover-light");
    clearHover(s);
    click(s.rcDepth2);CHECK(s.depth==2 && s.tiles.size()>3);
    renderViewer("nested-light");
    click(s.rcDepth1);
    click(s.rcListFiles);CHECK(!s.listFolders);
    selectNode(s,s.rows.front().node,s.rows.front().path);
    CHECK(s.viewRoot==s.tree.get());locateSelected(s);
    CHECK(s.viewRoot->name==L"Videos" && s.selected->name==L"holiday.mp4");
    goUp(s);setListMode(s,true);
    click(s.rcSunburstBtn);
    CHECK(s.sunburst);
    s.spec.zh = false;
    s.palette = darkPalette();
    renderViewer("sunburst-dark");
    // A narrow, high-DPI window must keep the chart, tabs and status separate.
    s.dpi = 144;
    makeFonts(s);
    ::SetWindowPos(s.hwnd, nullptr, 0, 0, 1050, 740, SWP_NOMOVE | SWP_NOZORDER);
    computeLayout(s);
    relayoutView(s);
    renderViewer("sunburst-dark-144dpi");
    const auto previous = s.tree;
    s.lastScanError = "";
    auto* failed = new ScanResult();
    failed->error = "Cannot read C:\\missing (Windows error 3)";
    ::SendMessageW(s.hwnd, WM_APP_DONE, 0, reinterpret_cast<LPARAM>(failed));
    CHECK(s.tree == previous);
    CHECK(!s.slices.empty());
    CHECK(s.notice.find(L"previous")!=std::wstring::npos);
    renderViewer("scan-error");
    CHECK(!s.crumbNodes.empty());
    auto* cancelled = new ScanResult();
    cancelled->cancelled = true;
    ::SendMessageW(s.hwnd, WM_APP_DONE, 0, reinterpret_cast<LPARAM>(cancelled));
    CHECK(s.tree == previous && s.cancelled);
    CHECK(s.lastScanError.empty() && !s.resultIncomplete);
    renderViewer("scan-cancelled");
    s.dpi=96;s.spec.zh=true;s.palette=lightPalette();s.notice.clear();makeFonts(s);
    // The compact layout's whole point: the chart gets most of the window.
    ::SetWindowPos(s.hwnd,nullptr,0,0,1184,761,SWP_NOMOVE|SWP_NOZORDER);
    s.mobileList=false;computeLayout(s);relayoutView(s);
    {
        const int chartH=s.rcChart.bottom-s.rcChart.top;
        const int share=chartH*100/std::max(1L,s.rcClient.bottom);
        CHECK(share>=72); // was ~60% with the old toolbar and detail panel
        CHECK(s.rcChartBar.bottom<=s.rcChart.top);
        CHECK(s.rcChart.bottom<=s.rcStatus.top);
        CHECK(s.rcStatus.bottom==s.rcClient.bottom);
    }
    ::SetWindowPos(s.hwnd,nullptr,0,0,410,900,SWP_NOMOVE|SWP_NOZORDER);
    s.mobileList=true;computeLayout(s);relayoutView(s);
    CHECK(s.narrow && s.rcTopList.right>s.rcTopList.left && maxRows(s)>0);
    CHECK(s.rcTopList.bottom<s.rcStatus.top);
    renderViewer("narrow-list");
    s.scanning=true;s.scanStarted=::GetTickCount64();s.progress.files=23456;s.progress.bytes=123456789;
    s.progress.current=L"C:\\Projects\\Disk View";computeLayout(s);relayoutView(s);
    CHECK(s.rcProgress.bottom<s.rcTopList.top && maxRows(s)>0);
    renderViewer("scan-progress-narrow");
    s.scanning=false;
    testNewWindowControls();
    ::SendMessageW(s.hwnd, WM_CLOSE, 0, 0);
    MSG quit;
    ::PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
    s.hwnd = s.edit = s.filterEdit = nullptr;
    if (s.editBrush) {
        ::DeleteObject(s.editBrush);
        s.editBrush = nullptr;
    }
    for (HFONT* font : {&s.font, &s.fontMedium, &s.fontSmall, &s.editFont, &s.fontMicro}) {
        ::DeleteObject(*font);
        *font = nullptr;
    }
    s.tree.reset();
    s.viewRoot = nullptr;
    s.drill.clear();
    s.folders.clear();
    s.largest.clear();
    s.rootLargest.clear();
    s.tiles.clear();
    s.slices.clear();
    s.crumbNodes.clear();
    s.crumbRects.clear();
    s.driveCards.clear();
    s.driveRects.clear();
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

// ---- the host's look (appearance().nativeLook) ----------------------------------------

static void testArgbParsing()
{
    // The host writes the alpha first: "#AARRGGBB" (docs/plugins.md, 外观).
    CHECK(diskview::detail::argbOf("#ffa8402c") == 0xffa8402cu);
    CHECK(diskview::detail::argbOf("#FFA8402C") == 0xffa8402cu);
    CHECK(diskview::detail::argbOf("#aabbcc") == 0xffaabbccu); // no alpha: opaque
    CHECK(diskview::detail::argbOf("") == 0u);
    CHECK(diskview::detail::argbOf("transparent") == 0u);
    CHECK(diskview::detail::argbOf("#gg1122") == 0u);
    CHECK(diskview::detail::argbOf("#ff11223344") == 0u);
}

static void testOverBase()
{
    // A half transparent colour over an opaque one.
    CHECK(diskview::detail::over(RGB(0, 0, 0), 0x80ffffffu) == RGB(128, 128, 128));
    CHECK(diskview::detail::over(RGB(255, 255, 255), 0x80ffffffu) == RGB(255, 255, 255));
    CHECK(diskview::detail::over(RGB(0, 0, 0), 0xffffffffu) == RGB(255, 255, 255));
    CHECK(diskview::detail::over(RGB(10, 20, 30), 0x00000000u) == RGB(10, 20, 30));
}

static void testHostPalette()
{
    using diskview::detail::hostPalette;
    using diskview::detail::lightPalette;
    using diskview::detail::darkPalette;

    // No look at all (an older host, or a test that runs the plugin alone):
    // the built-in palette stays.
    const winshun::Json empty;
    const auto kept = hostPalette(empty, darkPalette());
    CHECK(kept.bg == darkPalette().bg);
    CHECK(kept.accentFill == darkPalette().accentFill);
    CHECK(kept.accentText == darkPalette().accentText);

    // The host's own colours (Win顺's brand style, dark).
    const winshun::Json look(winshun::Json::Object {
        {"nativeLook", winshun::Json::Object {
                          {"dark", true},
                          {"background", "#ff1d1d20"},
                          {"layer", "#80202024"},
                          {"text", "#fff2f2f5"},
                          {"subtext", "#ffb6b6bf"},
                          {"accent", "#ffe06a5a"},
                          {"accentFill", "#ffa8402c"},
                          {"accentText", "#ffffffff"},
                          {"divider", "#ff3a3a40"},
                          {"danger", "#ffff7a80"},
                          {"control", "#ff2b2b31"},
                      }},
    });
    const auto p = hostPalette(look, darkPalette());
    CHECK(p.bg == RGB(0x1d, 0x1d, 0x20));
    CHECK(p.text == RGB(0xf2, 0xf2, 0xf5));
    CHECK(p.muted == RGB(0xb6, 0xb6, 0xbf));
    CHECK(p.accent == RGB(0xe0, 0x6a, 0x5a));
    CHECK(p.accentFill == RGB(0xa8, 0x40, 0x2c));
    CHECK(p.accentText == RGB(255, 255, 255));
    CHECK(p.border == RGB(0x3a, 0x3a, 0x40));
    CHECK(p.danger == RGB(0xff, 0x7a, 0x80));
    CHECK(p.input == RGB(0x2b, 0x2b, 0x31));
    // A half transparent layer lies over the background.
    CHECK(p.panel != RGB(0x20, 0x20, 0x24));
    CHECK(p.panel != p.bg);
    CHECK(p.hover != p.panel);

    // A look written for the other theme is not used.
    winshun::Json other = look;
    other.set("nativeLook", winshun::Json(winshun::Json::Object {
                                 {"dark", false},
                                 {"background", "#ffffffff"},
                             }));
    const auto p2 = hostPalette(other, darkPalette());
    CHECK(p2.bg == darkPalette().bg);

    // A missing colour keeps the built-in one.
    const winshun::Json partial(winshun::Json::Object {
        {"nativeLook", winshun::Json::Object {
                          {"dark", true},
                          {"background", "#ff101010"},
                      }},
    });
    const auto p3 = hostPalette(partial, darkPalette());
    CHECK(p3.bg == RGB(0x10, 0x10, 0x10));
    CHECK(p3.text == darkPalette().text);
    CHECK(p3.accentFill == darkPalette().accentFill);
}

// ---- the saved config (only the last path) ---------------------------------------------

static void testViewConfigKeepsOnlyThePath()
{
    // A file from before still carries "sunburst": it is read, and ignored.
    // Scan settings it does not carry fall back to the defaults.
    TempDir tmp;
    const std::string dir = winshun::utf8(tmp.path().wstring());
    {
        std::ofstream f(tmp.path() / L"diskview.json", std::ios::binary | std::ios::trunc);
        f << "{\"path\":\"D:\\\\work\",\"sunburst\":true}";
    }
    const auto c = diskview::loadViewConfig(dir);
    CHECK(c.hasFile);
    CHECK(c.lastPath == L"D:\\work");
    CHECK(c.scanMode == 0 && c.workers == 0 && c.media == 0 && c.excludedPaths.empty());

    // What is written now holds the path and the chosen scan settings.
    diskview::ViewConfig out;
    out.lastPath = L"D:\\other";
    out.scanMode = 1;
    out.workers = 2;
    out.media = 2;
    out.previewIntervalMs = 5000;
    out.excludedPaths = {L"D:\\other\\cache"};
    out.hasFile = true;
    diskview::saveViewConfig(dir, out);
    const auto c2 = diskview::loadViewConfig(dir);
    CHECK(c2.lastPath == L"D:\\other");
    CHECK(c2.scanMode == 1 && c2.workers == 2 && c2.media == 2 && c2.previewIntervalMs == 5000);
    CHECK(c2.excludedPaths.size() == 1 && c2.excludedPaths.front() == L"D:\\other\\cache");
}

// ---- the fixed-drive list ------------------------------------------------------------

static void testListFixedDrives()
{
    // Volume facts only (two system calls per drive); this machine has at
    // least one fixed NTFS drive, and every drive that comes back must be a
    // real lettered root with a nonzero total.
    const auto drives = diskview::listFixedDrives();
    CHECK(!drives.empty());
    for (const diskview::Drive& d : drives) {
        CHECK(d.letter >= L'A' && d.letter <= L'Z');
        CHECK(d.root.size() == 3 && d.root[1] == L':' && d.root[2] == L'\\');
        CHECK(d.total > 0);
        CHECK(d.used() <= d.total);
    }
    bool sawC = false;
    for (const diskview::Drive& d : drives)
        sawC = sawC || d.letter == L'C';
    CHECK(sawC); // the system drive is always fixed
}

template<class T> static void put(std::vector<std::byte>& data,std::size_t offset,T value) {
    std::memcpy(data.data()+offset,&value,sizeof value);
}
static std::vector<std::byte> recordImage(bool dir,const std::wstring& name,std::uint64_t parent,std::uint32_t bytes) {
    std::vector<std::byte> data(1024);put(data,0,std::uint32_t{0x454c4946});put(data,4,std::uint16_t{0x30});put(data,6,std::uint16_t{3});
    put(data,0x10,std::uint16_t{1});put(data,0x14,std::uint16_t{0x38});put(data,0x16,std::uint16_t(dir?3:1));
    std::size_t at=0x38;
    const auto resident=[&](std::uint32_t type,const std::vector<std::byte>& value) {
        const auto length=(24+value.size()+7)/8*8;put(data,at,type);put(data,at+4,std::uint32_t(length));
        put(data,at+16,std::uint32_t(value.size()));put(data,at+20,std::uint16_t{24});std::memcpy(data.data()+at+24,value.data(),value.size());at+=length;
    };
    std::vector<std::byte> filename(66+name.size()*2);put(filename,0,parent);filename[64]=std::byte(name.size());filename[65]=std::byte{1};std::memcpy(filename.data()+66,name.data(),name.size()*2);resident(0x30,filename);
    if(!dir)resident(0x80,std::vector<std::byte>(bytes));
    put(data,at,std::uint32_t{0xffffffff});put(data,0x18,std::uint32_t(at+8));put(data,0x30,std::uint16_t{0xabcd});
    for(int i=1;i<=2;++i){put(data,0x30+i*2,diskview::mft::load<std::uint16_t>(data.data()+i*512-2));put(data,i*512-2,std::uint16_t{0xabcd});}
    return data;
}
static void testMftMetadataAndTransport() {
    using namespace diskview;
    const std::uint64_t rootRef=(1ull<<48)|5,folderRef=(1ull<<48)|6;
    std::vector<std::byte> image(12*1024);
    for(auto [number,record]:std::vector<std::pair<int,std::vector<std::byte>>>{{5,recordImage(true,L".",rootRef,0)},{6,recordImage(true,L"资料",rootRef,0)},{7,recordImage(false,L"文档.txt",folderRef,37)}})
        std::memcpy(image.data()+number*1024,record.data(),record.size());
    ws::ntfs::MftReader reader(image,{512,4096,1024},{{0,image.size()}},0,image.size(),{2,false});
    CHECK(reader.valid());std::vector<mft::Entry> entries;std::wstring error;
    CHECK(mft::read(reader,4096,[&](mft::Batch& batch){
        const auto encoded=helper::encode(batch);mft::Batch decoded;CHECK(helper::decode(encoded,decoded));
        for(auto& e:decoded.entries)entries.push_back(std::move(e));return true;
    },error));
    CHECK(error.empty());ScanResult result;mft::build(L"D:\\",entries,4096,result);
    CHECK(result.fast && result.root.logical==37 && result.files==1 && result.dirs==1);
    CHECK(result.largest.front().path==L"D:\\资料\\文档.txt");
    // Hard links count once per path; extension records supply otherwise missing sizes.
    entries.push_back({(1ull<<48)|7,rootRef,37,0,false,true,L"hardlink.txt"});
    entries.push_back({8,rootRef,0,0,false,false,L"extended.bin"});entries.push_back({8,0,123,0,false,true,L""});
    entries.push_back({9,rootRef,0,FILE_ATTRIBUTE_REPARSE_POINT,true,false,L"junction"});
    entries.push_back({9,0,0,0,true,false,L""}); // extension must not erase reparse attributes
    entries.push_back({10,9,500,0,false,true,L"not-followed.txt"});
    entries.push_back({11,rootRef,0,0,false,false,L"unknown.bin"});
    entries.push_back({12,(2ull<<48)|6,800,0,false,true,L"stale-parent.txt"});
    mft::build(L"D:\\",entries,4096,result);
    CHECK(result.root.logical==197 && result.files==4 && result.skippedLinks==1);
    CHECK(result.unknownSizes==1 && result.orphanRecords==2);
    mft::Batch batch;batch.entries=entries;batch.cluster=4096;auto encoded=helper::encode(batch);mft::Batch decoded;
    encoded.pop_back();CHECK(!helper::decode(encoded,decoded));
    CHECK(!helper::pipeNameValid(L"\\\\remote\\pipe\\DiskView.Scan"));
    CHECK(helper::pipeNameValid(L"\\\\.\\pipe\\DiskView.Scan.{01234567-89ab-cdef-0123-456789abcdef}"));
    auto torn=recordImage(false,L"bad.txt",rootRef,1);torn[510]^=std::byte{1};ws::ntfs::FileRecord parsed;
    CHECK(!ws::ntfs::parseFileRecord(torn.data(),torn.size(),parsed));
}
static void testPartialScanConsistency() {
    TempDir tmp;fs::create_directories(tmp.path()/L"nested");writeFile(tmp.path()/L"nested",L"a",16);writeFile(tmp.path(),L"b",32);
    std::shared_ptr<diskview::ScanResult> part;diskview::ScanResult result;std::atomic<bool> cancel=false;int calls=0;
    diskview::ScanOptions options;options.previewIntervalMs=10;
    diskview::scanPath(tmp.path().wstring(),[&](const auto&){if(++calls==1)::Sleep(30);return true;},cancel,&result,[&](auto snapshot){if(!part)part=std::move(snapshot);},options);
    CHECK(part && part->root.logical<=result.root.logical);
    CHECK(result.root.logical==48 && result.files==result.root.files);
    if(part){const auto bytes=part->root.logical;result.root.logical=999;CHECK(part->root.logical==bytes);}
}
int main(int argc,char** argv)
{
    if(argc==2&&std::string(argv[1])=="--verify-recycle")return verifyRecycle();
    if(argc==3&&std::string(argv[1])=="--benchmark-ordinary")return benchmarkOrdinary(winshun::wide(argv[2]));
    if(argc==5&&std::string(argv[1])=="--scan-helper")return diskview::helper::run(diskview::normalizePath(winshun::wide(argv[2]),nullptr),winshun::wide(argv[3]),static_cast<DWORD>(std::stoul(argv[4])));
    if(argc==3&&std::string(argv[1])=="--benchmark-mft") {
        diskview::ScanResult result;std::wstring reason;std::atomic<bool> cancel=false;
        const auto outcome=diskview::helper::scan(nullptr,diskview::normalizePath(winshun::wide(argv[2]),nullptr),{}, {},cancel,result,reason);
        std::printf("outcome=%d files=%u bytes=%llu elapsed_ms=%llu unknown=%u orphan=%u reason=%s\n",int(outcome),result.files,static_cast<unsigned long long>(result.root.logical),static_cast<unsigned long long>(result.elapsedMs),result.unknownSizes,result.orphanRecords,winshun::utf8(reason).c_str());
        return outcome==diskview::helper::Outcome::Complete?0:2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#define RUN(fn)                                                                                                       \
    do {                                                                                                               \
        std::printf("-> %s\n", #fn);                                                                                    \
        fn();                                                                                                          \
    } while (0)
    RUN(testFormat);
    RUN(testTreemapCoversArea);
    RUN(testTreemapAspectRatio);
    RUN(testTreemapCullingKeepsParent);
    RUN(testTreemapAggregatesUnaccountedBytes);
    RUN(testTreemapFolderStates);
    RUN(testTreemapBranchHues);
    RUN(testTreemapNestsPaths);
    RUN(testSunburstSpans);
    RUN(testSunburstNestsRings);
    RUN(testSunburstCulling);
    RUN(testScanSumsAndLargest);
    RUN(testScanMissingAndEmpty);
    RUN(testScanCancel);
    RUN(testNormalize);
    RUN(testExtendedScanAndReuse);
    RUN(testPartialScanConsistency);
    RUN(testParallelScan);
    RUN(testPendingMftRecords);
    RUN(testCommandModel);
    RUN(testConfigRoundTrip);
    RUN(testMftMetadataAndTransport);
    RUN(testViewerNavigation);
    RUN(testViewerMessages);
    RUN(testListFixedDrives);
    RUN(testArgbParsing);
    RUN(testOverBase);
    RUN(testHostPalette);
    RUN(testViewConfigKeepsOnlyThePath);
#undef RUN

    if (g_failures == 0) {
        std::printf("diskview tests: all passed\n");
        return 0;
    }
    std::printf("diskview tests: %d failure(s)\n", g_failures);
    return 1;
}
