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
    model.setFilter(QStringLiteral("ZULU"));
    check(model.rowCount() == 1, "the filter ignores the case");
    model.setFilter(QStringLiteral("nothing"));
    check(model.rowCount() == 0, "a filter that matches nothing empties the list");
    model.setFilter(QString());
    check(model.rowCount() == 3, "clearing the filter brings the rows back");
    // The filter looks at the whole path, so a folder can be found by a part
    // of the path above it.
    model.setFilter(QStringLiteral("D:\\"));
    check(model.rowCount() == 3, "the filter matches the whole path");
}

void testLargest()
{
    DiskViewModel model;
    model.loadTree(tree());
    model.setListMode(DiskViewModel::ListLargest);
    check(model.rowCount() == 4, "the largest mode lists the files of the subtree");
    check(namesOf(model) == QStringList({"big.bin", "mid.dat", "mike.bin", "tiny.txt"}),
          "the largest mode starts with the biggest file");
    check(!model.data(model.index(0, 0), DiskViewModel::DirectoryRole).toBool(),
          "the largest mode lists files, not folders");
    // Inside a folder the ranking only covers that folder.
    model.navigate(QStringLiteral("2"));
    model.setListMode(DiskViewModel::ListLargest);
    check(namesOf(model) == QStringList({"big.bin", "tiny.txt"}),
          "the ranking follows the folder the list is in");
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
    testLargest();
    testHistory();
    testFormatting();
    if (g_failures == 0) {
        std::cout << "PASS\n";
        return 0;
    }
    std::cout << "FAIL " << g_failures << "\n";
    return 1;
}
