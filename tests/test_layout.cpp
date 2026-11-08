// test_layout.cpp - pure-logic tests for tree.h (layouts, formatting) and a
// real temp-directory round trip for scan.h. No window, no plugin process.
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
        d.allocApprox += c.logical;
        d.files += c.files;
        d.dirs += c.dir ? 1 : 0;
        d.children.push_back(std::move(c));
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
    diskview::Node root = makeDirOf(L"root", {sub, makeFile(L"z", 100)});
    const diskview::Rect area {0, 0, 100, 100};
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
    TempDir tmp;
    const std::string dir = winshun::utf8(tmp.path().wstring());
    {
        std::ofstream f(tmp.path() / L"diskview.json", std::ios::binary | std::ios::trunc);
        f << "{\"path\":\"D:\\\\work\",\"sunburst\":true}";
    }
    const auto c = diskview::loadViewConfig(dir);
    CHECK(c.hasFile);
    CHECK(c.lastPath == L"D:\\work");

    // What is written now holds the path alone.
    diskview::ViewConfig out;
    out.lastPath = L"D:\\other";
    out.hasFile = true;
    diskview::saveViewConfig(dir, out);
    const auto c2 = diskview::loadViewConfig(dir);
    CHECK(c2.lastPath == L"D:\\other");
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

int main()
{
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
    RUN(testTreemapNestsPaths);
    RUN(testSunburstSpans);
    RUN(testSunburstNestsRings);
    RUN(testSunburstCulling);
    RUN(testScanSumsAndLargest);
    RUN(testScanMissingAndEmpty);
    RUN(testScanCancel);
    RUN(testNormalize);
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
