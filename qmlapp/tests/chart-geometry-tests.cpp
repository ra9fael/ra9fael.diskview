#include "../../tree.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
constexpr double pi = 3.14159265358979323846;

void expectNear(double actual, double expected, double epsilon = 1e-8)
{
    assert(std::abs(actual - expected) <= epsilon);
}

void testSunburstCoverage()
{
    diskview::Node root;
    root.name = L"root";
    root.logical = 100;
    root.dir = true;

    diskview::Node first;
    first.name = L"first";
    first.logical = 75;
    first.dir = true;
    diskview::Node firstChild;
    firstChild.name = L"child";
    firstChild.logical = 75;
    first.children.push_back(firstChild);

    diskview::Node second;
    second.name = L"second";
    second.logical = 25;

    root.children = {first, second};
    std::vector<diskview::Slice> slices;
    diskview::sunburstSlices(root, 0, 0, 20, 10, -pi / 2, 3 * pi / 2, 0, 0, slices, root.name);
    assert(slices.size() == 3);
    double rootCoverage = 0;
    bool foundFirstSector = false;
    for (const auto& slice : slices) {
        if (slice.depth == 0) rootCoverage += slice.a1 - slice.a0;
        if (slice.depth == 1) {
            foundFirstSector = true;
            expectNear(slice.a0, -pi / 2);
            expectNear(slice.a1, pi);
        }
    }
    assert(foundFirstSector);
    expectNear(rootCoverage, 2 * pi);
}

void testSunburstPreservesActualGaps()
{
    diskview::Node root;
    root.name = L"root";
    root.logical = 100;
    root.dir = true;
    diskview::Node only;
    only.name = L"only";
    only.logical = 80;
    root.children.push_back(only);

    std::vector<diskview::Slice> slices;
    diskview::sunburstSlices(root, 0, 0, 10, 10, 0, 2 * pi, 0, 0, slices, root.name);
    assert(slices.size() == 1);
    expectNear(slices.front().a1 - slices.front().a0, 1.6 * pi);
    assert(slices.front().a1 < 2 * pi);
}

void testTreemapAreaAndCoverage()
{
    diskview::Node root;
    root.name = L"root";
    root.logical = 100;
    diskview::Node a;
    a.name = L"a";
    a.logical = 60;
    diskview::Node b;
    b.name = L"b";
    b.logical = 40;
    root.children = {a, b};

    std::vector<diskview::Tile> tiles;
    diskview::treemapTiles(root, {0, 0, 100, 80}, 0, 0, tiles, root.name);
    assert(tiles.size() == 2);
    double area = 0;
    for (const auto& tile : tiles) area += tile.rect.area();
    expectNear(area, 8000.0);
}
}

int main()
{
    testSunburstCoverage();
    testSunburstPreservesActualGaps();
    testTreemapAreaAndCoverage();
    std::cout << "PASS chart geometry: sunburst parent coverage, fixture gaps, treemap area\n";
}
