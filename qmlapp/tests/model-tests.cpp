// The list panel: the order of the rows, the filter over the whole path, the
// ranking of the largest files and the history of the visited folders.

#include "DiskViewModel.h"

#include <QVariantMap>
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

QVariantMap file(const QString& name, double size)
{
    return {{"name", name}, {"size", size}, {"dir", false}};
}

QVariantMap folder(const QString& name, double size, const QVariantList& children)
{
    return {{"name", name}, {"size", size}, {"dir", true}, {"children", children}};
}

// D:\
//   zulu\      600000
//     big.bin  500000
//     tiny.txt   1000
//   alpha\     300000
//     mid.dat   300000
//   mike.bin   100000
QVariantMap tree()
{
    return folder(QStringLiteral("D:\\"), 1000000,
                  {folder(QStringLiteral("zulu"), 600000,
                          {file(QStringLiteral("big.bin"), 500000),
                           file(QStringLiteral("tiny.txt"), 1000)}),
                   folder(QStringLiteral("alpha"), 300000,
                          {file(QStringLiteral("mid.dat"), 300000)}),
                   file(QStringLiteral("mike.bin"), 100000)});
}

QStringList namesOf(const DiskViewModel& model)
{
    QStringList names;
    for (int row = 0; row < model.rowCount(); ++row) {
        names << model.data(model.index(row, 0), DiskViewModel::NameRole).toString();
    }
    return names;
}

void testDefaultOrder()
{
    DiskViewModel model;
    check(model.loadTree(tree()), "the tree is loaded");
    check(namesOf(model) == QStringList({"zulu", "alpha", "mike.bin"}),
          "the list starts with the biggest entries first");
}

void testSorting()
{
    DiskViewModel model;
    model.loadTree(tree());
    model.toggleSort(DiskViewModel::SortByName);
    check(model.ascending(), "the name head starts ascending");
    check(namesOf(model) == QStringList({"alpha", "mike.bin", "zulu"}), "the name head sorts by name");
    model.toggleSort(DiskViewModel::SortByName);
    check(!model.ascending(), "the same head flips the direction");
    check(namesOf(model) == QStringList({"zulu", "mike.bin", "alpha"}), "the name head sorts downwards");
    model.toggleSort(DiskViewModel::SortBySize);
    check(model.sortKey() == DiskViewModel::SortBySize, "the size head takes over");
    check(!model.ascending(), "the size head starts with the biggest first");
    check(namesOf(model) == QStringList({"zulu", "alpha", "mike.bin"}), "the size head sorts by size");
}

void testFilter()
{
    DiskViewModel model;
    model.loadTree(tree());
    model.setFilter(QStringLiteral("mike"));
    check(model.rowCount() == 1, "the filter narrows the list");
    // The filter looks at the whole path, so a folder that matches brings the
    // rows under it into sight as well.
    model.setFilter(QStringLiteral("BIG.BIN"));
    check(model.rowCount() == 2, "the filter ignores the case and shows the folder above the match");
    model.setFilter(QStringLiteral("nothing"));
    check(model.rowCount() == 0, "a filter that matches nothing empties the list");
    model.setFilter(QString());
    check(model.rowCount() == 3, "clearing the filter brings the rows back");
    // The filter does not leave folders open behind it.
    check(!model.isExpanded(QStringLiteral("2")), "the filter did not open the folder for good");
    model.setFilter(QStringLiteral("D:\\"));
    check(model.rowCount() == 6, "the filter matches the whole path");
}

void testFileView()
{
    DiskViewModel model;
    model.loadTree(tree());
    model.setViewMode(DiskViewModel::FileView);
    check(model.rowCount() == 4, "the file view lists the files of the subtree");
    check(namesOf(model) == QStringList({"big.bin", "mid.dat", "mike.bin", "tiny.txt"}),
          "the file view starts with the biggest file");
    check(!model.data(model.index(0, 0), DiskViewModel::DirectoryRole).toBool(),
          "the file view lists files, not folders");
    // Inside a folder the list only covers that folder.
    model.navigate(QStringLiteral("2"));
    model.setViewMode(DiskViewModel::FileView);
    check(namesOf(model) == QStringList({"big.bin", "tiny.txt"}),
          "the file view follows the folder it is in");
    // The whole-tree scope looks through everything again.
    model.setWholeTree(true);
    check(model.rowCount() == 4, "the whole-tree scope lists every file of the scan");
}

void testFileSearch()
{
    DiskViewModel model;
    model.loadTree(tree());
    model.setViewMode(DiskViewModel::FileView);
    model.setFilter(QStringLiteral("bgn"));
    check(model.rowCount() == 0, "a subsequence is not a match unless fuzzy is on");
    model.setFuzzyMatch(true);
    check(namesOf(model) == QStringList({"big.bin"}), "fuzzy matching finds a subsequence of the name");
    model.setFilter(QStringLiteral("mid"));
    check(namesOf(model) == QStringList({"mid.dat"}), "fuzzy matching still finds plain substrings");
}

void testExpanding()
{
    DiskViewModel model;
    model.loadTree(tree());
    // The tree starts closed: only the root's children are rows.
    check(model.rowCount() == 3, "the tree starts with the top level only");
    check(!model.data(model.index(0, 0), DiskViewModel::ExpandedRole).toBool(), "the first folder is closed");
    model.toggleExpanded(QStringLiteral("2"));
    check(model.isExpanded(QStringLiteral("2")), "the folder opens");
    check(model.rowCount() == 5, "opening a folder adds its two files");
    const int depth = model.data(model.index(1, 0), DiskViewModel::DepthRole).toInt();
    check(depth == 1, "the children of the open folder are one level deeper");
    model.toggleExpanded(QStringLiteral("2"));
    check(model.rowCount() == 3, "closing the folder takes them away again");
    // The filter opens the folders on the way to a match without touching the
    // folders the user opened.
    model.setFilter(QStringLiteral("tiny"));
    check(model.rowCount() == 2, "the filter shows the folder and the file it found");
    check(!model.isExpanded(QStringLiteral("2")), "the filter leaves the folder closed in the model");
}

void testExtensions()
{
    DiskViewModel model;
    model.loadTree(tree());
    const QVariantList exts = model.extensions();
    check(exts.size() == 3, "one row per extension");
    const QVariantMap first = exts.first().toMap();
    check(first.value("ext").toString() == QStringLiteral(".bin"), "the biggest extension comes first");
    check(first.value("files").toInt() == 2, "the extension counts its files");
    check(!first.value("color").toString().isEmpty(), "every extension has a colour");
    check(first.value("label").toString().length() > 0, "every extension has a name");
}

void testMetadataColumns()
{
    // 2026-11-13 01:51 UTC as a FILETIME; the column prints it in local time,
    // so the check only insists on a real stamp being shown.
    const quint64 stamp = 133'000'000'000'000'000ull;
    const QVariantMap fileWithMeta = {{"name", QStringLiteral("a.txt")}, {"size", 10.0},
                                      {"dir", false}, {"modified", QString::number(stamp)},
                                      {"attrs", 0x20u}};
    const QVariantMap treeWithMeta = {{"name", QStringLiteral("D:\\")}, {"size", 10.0}, {"dir", true},
                                      {"children", QVariantList{fileWithMeta}}};
    DiskViewModel model;
    model.loadTree(treeWithMeta);
    const QModelIndex row = model.index(0, 0);
    check(model.data(row, DiskViewModel::AttributesRole).toString() == QStringLiteral("A"),
          "the attributes column spells out the flags");
    check(model.data(row, DiskViewModel::ModifiedTextRole).toString().contains(QLatin1Char('-')),
          "the modified column formats the stamp as a date");
    check(model.data(row, DiskViewModel::ModifiedTextRole).toString() != QStringLiteral("—"),
          "a row without a stamp shows a dash instead");

    // A tree without the fields keeps the dash.
    DiskViewModel plain;
    plain.loadTree(tree());
    check(plain.data(plain.index(0, 0), DiskViewModel::ModifiedTextRole).toString() == QStringLiteral("—"),
          "a scan without timestamps shows a dash");
    check(plain.data(plain.index(0, 0), DiskViewModel::AttributesRole).toString() == QStringLiteral("—"),
          "a scan without attributes shows a dash");
}

void testFlatLoad()
{
    // The same shape as tree(), in the scan's own form. The counts come in
    // with the arrays instead of being summed up afterwards.
    FlatTree flat;
    const auto add = [&flat](const QString& name, int parent, qint64 size, bool dir,
                             quint32 files, quint32 dirs, quint8 flags) {
        flat.names.append(name);
        flat.parents.append(parent);
        flat.sizes.append(size);
        flat.allocs.append(size);
        flat.modified.append(0);
        flat.attributes.append(dir ? 0x10u : 0x20u);
        flat.fileCounts.append(files);
        flat.dirCounts.append(dirs);
        flat.flags.append(flags);
        return flat.names.size() - 1;
    };
    add(QStringLiteral("D:\\"), -1, 1000000, true, 4, 2, 1);
    const int zulu = add(QStringLiteral("zulu"), 0, 600000, true, 2, 0, 1);
    add(QStringLiteral("big.bin"), zulu, 500000, false, 0, 0, 0);
    add(QStringLiteral("tiny.txt"), zulu, 1000, false, 0, 0, 0);
    const int alpha = add(QStringLiteral("alpha"), 0, 300000, true, 1, 0, 1);
    add(QStringLiteral("mid.dat"), alpha, 300000, false, 0, 0, 0);
    add(QStringLiteral("mike.bin"), 0, 100000, false, 0, 0, 0);

    DiskViewModel model;
    check(model.loadFlat(flat, false), "the flat tree is loaded");
    check(model.rowCount() == 3, "the flat tree shows its top level");
    check(namesOf(model) == QStringList({"zulu", "alpha", "mike.bin"}),
          "the flat tree keeps the order of the payload");
    check(model.currentPath() == QStringLiteral("D:\\"), "the root path is built");
    check(model.nodeItems() == 6, "the item counts come from the payload");
    model.navigate(QStringLiteral("2"));
    check(model.currentPath().endsWith(QStringLiteral("zulu")), "navigation works on a flat tree");
    check(model.data(model.index(0, 0), DiskViewModel::PathRole).toString()
              == QStringLiteral("D:\\zulu\\big.bin"), "the child paths are built");
    model.setViewMode(DiskViewModel::FileView);
    check(model.rowCount() == 2, "the file view reads the flat tree too");
}

void testHistory()
{
    DiskViewModel model;
    model.loadTree(tree());
    const QString root = model.currentPath();
    check(!model.canGoBack(), "the history starts at the root");
    model.navigate(QStringLiteral("2"));
    check(model.currentPath() != root, "the folder is entered");
    check(model.canGoBack(), "there is something to go back to");
    model.goBack();
    check(model.currentPath() == root, "going back returns to the root");
    check(model.canGoForward(), "there is something to go forward to");
    model.goForward();
    check(model.currentPath().endsWith(QStringLiteral("zulu")), "going forward returns into the folder");
    model.goUp();
    check(model.currentPath() == root, "going up returns to the root");
    check(model.selectedId() == QStringLiteral("2"), "going up keeps the folder that was left selected");
}

void testFormatting()
{
    DiskViewModel model;
    check(model.formatSize(512) == QStringLiteral("512 B"), "a small size stays in bytes");
    check(model.formatSize(1536) == QStringLiteral("1.5 KB"), "a size below a hundred keeps its decimal");
    check(model.formatSize(200 * 1024 * 1024) == QStringLiteral("200 MB"), "a big size drops its decimal");
    check(model.formatCount(1234) == QStringLiteral("1,234"), "the counts are grouped by three");
    check(model.formatCount(12) == QStringLiteral("12"), "a short count stays as it is");
}

} // namespace

int main()
{
    std::cout << "list model\n";
    testDefaultOrder();
    testSorting();
    testFilter();
    testFileView();
    testFileSearch();
    testExpanding();
    testExtensions();
    testMetadataColumns();
    testFlatLoad();
    testHistory();
    testFormatting();
    if (g_failures == 0) {
        std::cout << "PASS\n";
        return 0;
    }
    std::cout << "FAIL " << g_failures << "\n";
    return 1;
}
