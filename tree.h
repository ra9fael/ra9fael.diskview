// tree.h - the scanned tree and its two layouts, pure C++ (no OS calls).
//
// The scanner (scan.h) fills a Node tree; this file turns it into drawable
// geometry: a squarified treemap (Bruls, Huizing & van Wijk) and a recursive
// sunburst. Both walk the same immutable tree and cull entries below a pixel
// budget; culled entries stay part of their parent's totals, so nothing is
// ever misreported - the parent simply shows the whole.
//
// Sizes: `logical` is the byte count from the file system. The scanner also
// keeps `allocApprox` (cluster-rounded); the layouts proportion by `logical`.

#ifndef DISKVIEW_TREE_H
#define DISKVIEW_TREE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace diskview {

struct Node {
    std::wstring name;                 // file or folder name; root: the scan path
    std::uint64_t logical = 0;         // bytes; a directory: the sum of its subtree
    std::uint64_t allocApprox = 0;     // cluster-rounded bytes; a directory: the sum
    // FILETIME of the last write. A directory keeps its own, not the newest
    // one below it. 0 means the scan could not tell.
    std::uint64_t modified = 0;
    std::uint32_t attributes = 0;      // FILE_ATTRIBUTE_* of the entry itself
    bool dir = false;
    // A display-only aggregate of files below the scan's small-file threshold.
    bool group = false;
    // Bytes and item count omitted by the bounded chart export. Treemap layout
    // renders these as a real aggregate tile rather than an unaccounted gap.
    std::uint64_t rest = 0;
    std::uint32_t restCount = 0;
    std::uint32_t matchCount = 0;
    std::uint32_t restMatchCount = 0;
    std::uint32_t files = 0;           // a directory: files in its subtree (self not counted)
    std::uint32_t dirs = 0;            // a directory: folders in its subtree (self not counted)
    int skippedChildren = 0;           // immediate entries this directory could not read
    std::vector<Node> children;        // sorted by logical, largest first (scan guarantees)

    std::uint64_t totalLogical() const noexcept { return logical; }
};

// ---- shared view helpers --------------------------------------------------------

inline std::wstring childPath(const std::wstring& base, const std::wstring& name)
{
    return base.empty() ? name : base + (base.back() == L'\\' ? L"" : L"\\") + name;
}

inline const Node* findChild(const Node& node, const std::wstring& name)
{
    for (const Node& c : node.children)
        if (c.name == name)
            return &c;
    return nullptr;
}

// Bytes as people read them: 1024-based, one decimal from MB up. The unit
// names are the Chinese convention when `zh`, otherwise English.
inline std::string formatBytes(std::uint64_t bytes, bool zh)
{
    const char* const zhUnits[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    const char* const enUnits[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    const char* const* units = zh ? zhUnits : enUnits;
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        ++unit;
    }
    char buf[64];
    if (unit == 0)
        std::snprintf(buf, sizeof buf, "%llu %s", static_cast<unsigned long long>(bytes), units[0]);
    else if (value >= 100.0)
        std::snprintf(buf, sizeof buf, "%.0f %s", value, units[unit]);
    else
        std::snprintf(buf, sizeof buf, "%.1f %s", value, units[unit]);
    return buf;
}

inline std::string formatCount(std::uint64_t n)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%llu", static_cast<unsigned long long>(n));
    std::string s = buf;
    // Group by three from the right. The loop stops at i > 3 so the index
    // never underflows (i -= 3 on a size_t would wrap past zero).
    for (std::size_t i = s.size(); i > 3; i -= 3)
        s.insert(i - 3, ",");
    return s;
}

// ---- squarified treemap ----------------------------------------------------------

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    double area() const noexcept { return w * h; }
    bool contains(double px, double py) const noexcept
    {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

struct Tile {
    Rect rect;
    const Node* node = nullptr;
    int depth = 0;
    std::wstring path; // display path; the recursion threads it through
    // Position on the colour wheel, inherited from the parent's branch so
    // neighbouring tiles stay in the same colour family.
    double hue = -1;
    // A folder that keeps a title strip along its top edge (and a thin frame
    // around itself) reports the strip's height here. Zero means the tile is
    // one filled block with a single label: too small to nest anything.
    double header = 0;
    // True when the folder's children were laid out inside it, so its own
    // fill only shows as the 2 px structure strips around them. Those strips
    // draw as neutral chrome, never as the folder's colour.
    bool expanded = false;
    // The children too small to draw on their own, merged into one aggregate
    // block so the folder's area stays completely covered. `path` is the
    // folder's own, so a click on the aggregate resolves to the folder.
    bool remainder = false;
    std::uint32_t remainderCount = 0;
    std::uint32_t remainderMatchCount = 0;
};

namespace detail {

// The squarified pass: fill the current row while the worst aspect ratio
// keeps improving, then lay the row out along the shorter side and continue
// with what is left of the area. For a row of total area s laid against a
// side of length `side`, the thickness is t = s/side and each child's extent
// is a/t; its aspect ratio is max(t/ext, ext/t) = max(t^2/a, a/t^2).
inline double rowWorst(const std::vector<double>& areas, double side)
{
    double sum = 0;
    for (const double a : areas)
        sum += a;
    if (side <= 0 || sum <= 0)
        return 1e300;
    const double t = sum / side;
    double worst = 0;
    for (const double a : areas) {
        if (a <= 0)
            return 1e300;
        worst = std::max(worst, std::max(t * t / a, a / (t * t)));
    }
    return worst;
}

// The same, as if `extra` joined the row (without copying the vector).
inline double rowWorstExtra(const std::vector<double>& areas, double side, double extra)
{
    double sum = extra;
    double worstExtra = 0;
    for (const double a : areas)
        sum += a;
    if (side <= 0 || sum <= 0)
        return 1e300;
    const double t = sum / side;
    for (const double a : areas) {
        if (a <= 0)
            return 1e300;
        worstExtra = std::max(worstExtra, std::max(t * t / a, a / (t * t)));
    }
    if (extra <= 0)
        return 1e300;
    return std::max(worstExtra, std::max(t * t / extra, extra / (t * t)));
}

} // namespace detail

// A folder's title strip grows with depth: the outer levels carry the eye, the
// inner ones need their room for content.
inline int treemapHeaderHeight(int depth)
{
    return depth <= 0 ? 18 : (depth == 1 ? 15 : 13);
}

// How many of a folder's children would still be readable blocks inside
// `area`. This is the rule that decides whether a folder splits or stays
// whole, so the depth comes from the room rather than from a fixed number.
inline int treemapBigChildren(const Node& node, double area, double minArea)
{
    if (node.logical == 0 || node.children.empty() || area <= 0)
        return 0;
    const double scale = area / static_cast<double>(node.logical);
    int count = 0;
    for (const Node& child : node.children)
        if (static_cast<double>(child.logical) * scale >= minArea)
            ++count;
    return count;
}

// Walks the tree, appending every visible tile. A folder takes one of three
// shapes, decided by the room it has rather than by a fixed depth:
//   collapsed - one filled block with a single label;
//   framed    - a chrome frame and title strip around its own block;
//   expanded  - the same frame and strip, with its children laid out inside.
// Children below `minArea` (device px^2), and any bytes the payload does not
// account for, are folded into one aggregate tile: a folder's inside is always
// covered by something, so its own flat colour never reads as a file.
// `basePath` is the display path of `node` itself; every tile carries its full
// path. `parentHue` is the position on the wheel the branch sits at.
inline void treemapTiles(const Node& node, const Rect& area, int depth, double minArea,
                         std::vector<Tile>& out, std::wstring basePath = {},
                         int maxDepth = 24, const std::vector<const Node*>* visible = nullptr,
                         double parentHue = -1.0)
{
    if (depth > maxDepth || area.w <= 0 || area.h <= 0
        || (node.children.empty() && node.rest == 0 && node.restCount == 0))
        return;
    std::vector<const Node*> kids;
    kids.reserve(node.children.size());
    if (visible) kids = *visible;
    else for (const Node& c : node.children) kids.push_back(&c);
    const auto larger = [](const Node* a, const Node* b) { return a->logical > b->logical; };
    if (!std::is_sorted(kids.begin(), kids.end(), larger))
        std::stable_sort(kids.begin(), kids.end(), larger);
    const double total = node.logical > 0 ? static_cast<double>(node.logical) : 0;
    if (total <= 0)
        return;
    const double scale = area.area() / total;

    struct Entry {
        const Node* node = nullptr;
        double area = 0;
        double hue = 0;
        bool remainder = false;
        std::uint32_t count = 0;
        std::uint32_t matchCount = 0;
    };
    std::vector<Entry> entries;
    entries.reserve(kids.size() + 1);
    std::uint32_t culledCount = 0;
    double shownArea = 0;
    for (const Node* kid : kids) {
        const double a = static_cast<double>(kid->logical) * scale;
        if (a < minArea) {
            ++culledCount; // folded into the aggregate below
            continue;
        }
        Entry entry;
        entry.node = kid;
        entry.area = a;
        shownArea += a;
        entries.push_back(entry);
    }
    // What is left of the folder: the culled children, plus any bytes no
    // exported child accounts for (the model keeps a bounded top-K per
    // folder). One aggregate keeps the folder's inside completely covered.
    const double restArea = std::max(0.0, area.area() - shownArea);
    const bool hasRest = restArea > 4.0 && (node.rest > 0 || node.restCount > 0 || culledCount > 0);
    if (hasRest) {
        Entry entry;
        entry.area = restArea;
        entry.remainder = true;
        entry.count = node.restCount + culledCount;
        entry.matchCount = node.restMatchCount;
        entries.push_back(entry);
    }
    if (entries.empty())
        return;

    // Colour follows the branch, not the area share: the top level spreads
    // folders around the wheel by the golden angle, so two large branches are
    // never the same colour; every level below steps inside its parent's
    // family, which keeps a subtree recognisable as one group.
    constexpr double kGoldenAngle = 0.6180339887498949;
    const std::size_t drawn = entries.size() - (hasRest ? 1 : 0);
    const double step = drawn > 1 ? 0.16 / static_cast<double>(drawn - 1) : 0.0;
    const double baseHue = parentHue < 0 ? 0.0 : parentHue;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        Entry& entry = entries[i];
        if (entry.remainder) {
            entry.hue = baseHue; // neutral in the fill; the position is unused
        } else if (parentHue < 0 && entry.node->dir) {
            entry.hue = static_cast<double>(i) * kGoldenAngle;
        } else {
            entry.hue = baseHue + (static_cast<double>(i) - (static_cast<double>(drawn) - 1) * 0.5) * step;
        }
    }

    double x = area.x, y = area.y, w = area.w, h = area.h;
    std::vector<const Entry*> row;
    std::vector<double> rowAreas;
    double rowSum = 0;
    const int stripHeight = treemapHeaderHeight(depth);
    const auto flushRow = [&] {
        if (row.empty())
            return;
        const double s = rowSum;
        if (s <= 0 || w <= 0 || h <= 0) {
            row.clear();
            rowAreas.clear();
            rowSum = 0;
            return;
        }
        const auto place = [&](const Entry& entry, const Rect& r) {
            Tile tile;
            tile.rect = r;
            tile.node = entry.node;
            tile.depth = depth;
            tile.path = entry.remainder ? basePath : childPath(basePath, entry.node->name);
            tile.hue = entry.hue;
            tile.remainder = entry.remainder;
            tile.remainderCount = entry.count;
            tile.remainderMatchCount = entry.matchCount;
            if (!entry.remainder && entry.node->dir && r.w >= 64.0 && r.h >= stripHeight + 10.0) {
                tile.header = stripHeight;
                const Rect inner {r.x + 2.0, r.y + stripHeight,
                                  std::max(0.0, r.w - 4.0),
                                  std::max(0.0, r.h - stripHeight - 2.0)};
                // Split only when the inside can hold two blocks worth
                // reading; otherwise the folder keeps its whole block under
                // the strip.
                if (inner.area() >= 900.0 && depth + 1 <= maxDepth
                    && treemapBigChildren(*entry.node, inner.area(), 48.0) >= 1) {
                    tile.expanded = true;
                    out.push_back(tile);
                    treemapTiles(*entry.node, inner, depth + 1, minArea, out, out.back().path,
                                 maxDepth, nullptr, entry.hue);
                    return;
                }
            }
            out.push_back(tile);
        };
        if (w >= h) {
            const double t = std::min(s / h, w);
            double cy = y;
            for (std::size_t i = 0; i < row.size(); ++i) {
                const double hh = rowAreas[i] / t;
                place(*row[i], Rect {x, cy, t, hh});
                cy += hh;
            }
            x += t;
            w -= t;
        } else {
            const double t = std::min(s / w, h);
            double cx = x;
            for (std::size_t i = 0; i < row.size(); ++i) {
                const double ww = rowAreas[i] / t;
                place(*row[i], Rect {cx, y, ww, t});
                cx += ww;
            }
            y += t;
            h -= t;
        }
        row.clear();
        rowAreas.clear();
        rowSum = 0;
    };

    for (const Entry& entry : entries) {
        if (entry.area <= 0)
            continue;
        if (w <= 0 || h <= 0)
            break;
        const double side = std::min(w, h);
        if (row.empty()) {
            row.push_back(&entry);
            rowAreas.push_back(entry.area);
            rowSum = entry.area;
            continue;
        }
        const double before = detail::rowWorst(rowAreas, side);
        const double after = detail::rowWorstExtra(rowAreas, side, entry.area);
        if (after <= before) {
            row.push_back(&entry);
            rowAreas.push_back(entry.area);
            rowSum += entry.area;
        } else {
            flushRow();
            row.push_back(&entry);
            rowAreas.push_back(entry.area);
            rowSum = entry.area;
        }
    }
    flushRow();
}

// ---- recursive sunburst -----------------------------------------------------------

struct Slice {
    const Node* node = nullptr;
    int depth = 0;
    double a0 = 0, a1 = 0; // radians; a1 - a0 = the angular share
    double r0 = 0, r1 = 0; // the ring's radii
    bool dir = false;
    std::wstring path; // display path; the recursion threads it through
    // Position on the colour wheel (0..1) from the slice's angle.
    double hue = -1;
};

inline void sunburstSlices(const Node& node, double cx, double cy, double r0, double ringWidth,
                           double a0, double a1, int depth, double minAngle,
                           std::vector<Slice>& out, std::wstring basePath = {},
                           int maxDepth = 16, const std::vector<const Node*>* visible = nullptr)
{
    if (depth > maxDepth || node.children.empty())
        return;
    const double total = node.logical > 0 ? static_cast<double>(node.logical) : 0;
    if (total <= 0)
        return;
    std::vector<const Node*> kids;
    kids.reserve(node.children.size());
    if (visible) kids = *visible;
    else for (const Node& c : node.children) kids.push_back(&c);
    const auto larger = [](const Node* a, const Node* b) { return a->logical > b->logical; };
    if (!std::is_sorted(kids.begin(), kids.end(), larger))
        std::stable_sort(kids.begin(), kids.end(), larger);
    double a = a0;
    const double r1 = r0 + ringWidth;
    for (const Node* kid : kids) {
        const double span = (a1 - a0) * (static_cast<double>(kid->logical) / total);
        if (span < minAngle || a + span > a1 + 1e-9) {
            a += span;
            continue; // culled: the parent's sector keeps covering this space
        }
        Slice s;
        s.node = kid;
        s.depth = depth;
        s.a0 = a;
        s.a1 = a + span;
        s.r0 = r0;
        s.r1 = r1;
        s.dir = kid->dir;
        s.path = childPath(basePath, kid->name);
        const double turn = (s.a0 + s.a1) / 2.0 / 6.283185307179586;
        s.hue = turn - std::floor(turn);
        out.push_back(std::move(s));
        if (kid->dir)
            sunburstSlices(*kid, cx, cy, r1, ringWidth, a, a + span, depth + 1, minAngle, out,
                           out.back().path, maxDepth);
        a += span;
    }
}

} // namespace diskview

#endif // DISKVIEW_TREE_H
