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
    bool dir = false;
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

// Walks the tree, appending every visible tile. Children smaller than
// `minArea` (device px^2) are skipped; their space stays inside the parent,
// which paints its own label, so the picture remains honest. `basePath` is
// the display path of `node` itself; every tile carries its full path.
inline void treemapTiles(const Node& node, const Rect& area, int depth, double minArea,
                         std::vector<Tile>& out, std::wstring basePath = {},
                         int maxDepth = 24, const std::vector<const Node*>* visible = nullptr)
{
    if (depth > maxDepth || node.children.empty() || area.w <= 0 || area.h <= 0)
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

    double x = area.x, y = area.y, w = area.w, h = area.h;
    std::vector<const Node*> row;
    std::vector<double> rowAreas;
    double rowSum = 0;
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
        if (w >= h) {
            const double t = std::min(s / h, w);
            double cy = y;
            for (std::size_t i = 0; i < row.size(); ++i) {
                const double hh = rowAreas[i] / t;
                Rect r {x, cy, t, hh};
                Tile tile {r, row[i], depth, childPath(basePath, row[i]->name)};
                out.push_back(std::move(tile));
                if (row[i]->dir && hh > 0 && t > 0)
                    treemapTiles(*row[i], Rect {x + 1, cy + 1, std::max(0.0, t - 2), std::max(0.0, hh - 2)},
                                 depth + 1, minArea, out, out.back().path, maxDepth);
                cy += hh;
            }
            x += t;
            w -= t;
        } else {
            const double t = std::min(s / w, h);
            double cx = x;
            for (std::size_t i = 0; i < row.size(); ++i) {
                const double ww = rowAreas[i] / t;
                Rect r {cx, y, ww, t};
                Tile tile {r, row[i], depth, childPath(basePath, row[i]->name)};
                out.push_back(std::move(tile));
                if (row[i]->dir && ww > 0 && t > 0)
                    treemapTiles(*row[i], Rect {cx + 1, y + 1, std::max(0.0, ww - 2), std::max(0.0, t - 2)},
                                 depth + 1, minArea, out, out.back().path, maxDepth);
                cx += ww;
            }
            y += t;
            h -= t;
        }
        row.clear();
        rowAreas.clear();
        rowSum = 0;
    };

    for (const Node* kid : kids) {
        const double a = static_cast<double>(kid->logical) * scale;
        if (a < minArea)
            continue; // culled: the parent keeps its whole area
        if (w <= 0 || h <= 0)
            break;
        const double side = std::min(w, h);
        if (row.empty()) {
            row.push_back(kid);
            rowAreas.push_back(a);
            rowSum = a;
            continue;
        }
        const double before = detail::rowWorst(rowAreas, side);
        const double after = detail::rowWorstExtra(rowAreas, side, a);
        if (after <= before) {
            row.push_back(kid);
            rowAreas.push_back(a);
            rowSum += a;
        } else {
            flushRow();
            row.push_back(kid);
            rowAreas.push_back(a);
            rowSum = a;
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
        out.push_back(std::move(s));
        if (kid->dir)
            sunburstSlices(*kid, cx, cy, r1, ringWidth, a, a + span, depth + 1, minAngle, out,
                           out.back().path, maxDepth);
        a += span;
    }
}

} // namespace diskview

#endif // DISKVIEW_TREE_H
