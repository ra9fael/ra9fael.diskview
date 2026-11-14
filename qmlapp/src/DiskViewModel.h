#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include "ScanTree.h"

// The tree behind the window: the folder table, the file list and the
// breadcrumb all come from here. The table is a flattened tree, so expanding
// a folder is a change of the visible rows, not of the model's contents.
//
// The scan's own flat arrays are the model's storage: there is no second copy
// of the tree in a per-node struct. A drive holds millions of nodes and the
// host caps this process's memory, so the difference between one copy and two
// is what decides whether a scan can be shown at all.
class DiskViewModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    // The file panel reads the arrays and the shared state through this
    // class; it builds its own rows from the same tree.
    friend class FileListModel;
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentPathChanged)
    Q_PROPERTY(QString totalText READ totalText NOTIFY currentPathChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectedChanged)
    // The extension of the selected file, so the legend can mark the row that
    // belongs to what the chart is showing.
    Q_PROPERTY(QString selectedExtension READ selectedExtension NOTIFY selectedChanged)
    // The extension the legend picked, or empty when every colour shows. One
    // state for three panels: the legend marks the row, the chart dims every
    // other extension and the file list keeps to the matching files. The flag
    // is what separates "off" from the "(无)" row, whose extension is empty.
    Q_PROPERTY(QString extensionFilter READ extensionFilter NOTIFY extensionFilterChanged)
    Q_PROPERTY(bool hasExtensionFilter READ hasExtensionFilter NOTIFY extensionFilterChanged)
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectedChanged)
    Q_PROPERTY(QVariantMap chartTree READ chartTree NOTIFY chartChanged)
    // How many levels below the current folder the chart is showing; it shares
    // the chart's own depth, so the export stops where the drawing stops.
    Q_PROPERTY(int chartDepth READ chartDepth WRITE setChartDepth NOTIFY chartChanged)
    Q_PROPERTY(QString breadcrumbText READ breadcrumbText NOTIFY currentPathChanged)
    Q_PROPERTY(QVariantList breadcrumb READ breadcrumb NOTIFY currentPathChanged)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY listChanged)
    // The items the current folder holds, all levels down.
    Q_PROPERTY(int nodeItems READ nodeItems NOTIFY currentPathChanged)
    Q_PROPERTY(bool partial READ partial NOTIFY partialChanged)
    // Why the last tree could not be shown; empty when it was.
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged)
    Q_PROPERTY(int sortKey READ sortKey WRITE setSortKey NOTIFY listChanged)
    Q_PROPERTY(bool ascending READ ascending WRITE setAscending NOTIFY listChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY listChanged)
    Q_PROPERTY(int viewMode READ viewMode WRITE setViewMode NOTIFY listChanged)
    // The file view: match a subsequence of the name, and look through the
    // whole scan instead of the folder that is open.
    Q_PROPERTY(bool fuzzyMatch READ fuzzyMatch WRITE setFuzzyMatch NOTIFY listChanged)
    Q_PROPERTY(bool wholeTree READ wholeTree WRITE setWholeTree NOTIFY listChanged)
    // The legend rows: what the current folder is made of.
    Q_PROPERTY(QVariantList extensionRows READ extensions NOTIFY listChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY currentPathChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY currentPathChanged)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY currentPathChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1, NameRole, SizeRole, SizeTextRole, PercentRole,
        DirectoryRole, PathRole, DepthRole, HasChildrenRole, ExpandedRole,
        AllocTextRole, ExtRole, ExtColorRole, ItemsRole, FilesRole, DirsRole,
        ModifiedTextRole, ModifiedValueRole, AttributesRole
    };
    enum SortKey { SortBySize = 0, SortByName = 1, SortByModified = 2 };
    enum ViewMode { TreeView = 0, FileView = 1 };

    // The shared helpers both panels use for their cells: the attribute
    // letters, the modified stamp and the colour of a node's extension.
    QString attributesText(quint32 attributes) const;
    QString modifiedText(quint64 modified) const;
    QColor extensionColorOf(int index) const;

    explicit DiskViewModel(QObject* parent = nullptr);
    Q_INVOKABLE bool loadFixture(const QString& fileName);
    Q_INVOKABLE bool loadTree(const QVariantMap& tree, bool partial = false);
    // The scan's own form: parallel arrays, taken over as they are.
    Q_INVOKABLE bool loadFlat(FlatTree tree, bool partial = false);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void setPartial(bool partial);
    bool partial() const { return m_partial; }
    QString loadError() const { return m_loadError; }
    QString currentPath() const;
    QString currentName() const;
    QString totalText() const;
    QString selectedId() const { return m_selectedId; }
    QString selectedExtension() const;
    QString extensionFilter() const { return m_extensionFilter; }
    bool hasExtensionFilter() const { return m_extensionFilterOn; }
    int selectedRow() const;
    QVariantMap chartTree() const;
    int chartDepth() const { return m_chartDepth; }
    void setChartDepth(int depth);
    QString breadcrumbText() const;
    QVariantList breadcrumb() const;
    int itemCount() const { return m_visible.size(); }
    int nodeItems() const;
    int sortKey() const { return m_sortKey; }
    void setSortKey(int key);
    bool ascending() const { return m_ascending; }
    void setAscending(bool ascending);
    QString filter() const { return m_filter; }
    void setFilter(const QString& filter);
    int viewMode() const { return m_viewMode; }
    void setViewMode(int mode);
    bool fuzzyMatch() const { return m_fuzzyMatch; }
    void setFuzzyMatch(bool enabled);
    bool wholeTree() const { return m_wholeTree; }
    void setWholeTree(bool enabled);
    bool canGoBack() const { return m_historyPos > 0; }
    bool canGoForward() const { return m_historyPos >= 0 && m_historyPos + 1 < m_history.size(); }
    bool canGoUp() const { return m_chartRoot > 0; }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void navigate(const QString& id);
    // One entry point for "go into this folder or open this file", shared by
    // the chart's double click and the tree's navigation, so the two cannot
    // drift apart: a folder becomes the chart's folder and the tree opens
    // down to it, a file is simply selected.
    Q_INVOKABLE void enterNode(const QString& id);
    // The chart returns to the folder the tree is rooted at and the view
    // resets to 1:1. The outline the user built is untouched.
    Q_INVOKABLE void resetChart();
    // The legend's pick: the same extension again clears it, "(无)" filters to
    // files without an extension.
    Q_INVOKABLE void selectExtension(const QString& extension);
    Q_INVOKABLE void clearExtensionFilter();
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void select(const QString& id);
    Q_INVOKABLE void moveSelection(int delta);
    Q_INVOKABLE void openSelected();
    Q_INVOKABLE void toggleSort(int key);
    Q_INVOKABLE bool revealNode(const QString& id);
    // Opens the folder in place: every folder on the way to it and the folder
    // itself are expanded in the list, and its row is selected. The window
    // stays on the folder it is showing, unlike navigate().
    Q_INVOKABLE bool expandNode(const QString& id);
    Q_INVOKABLE void toggleExpanded(const QString& id);
    Q_INVOKABLE bool isExpanded(const QString& id) const;
    // What the current folder is made of, biggest extension first.
    Q_INVOKABLE QVariantList extensions() const;
    Q_INVOKABLE QString formatSize(qint64 bytes) const;
    Q_INVOKABLE QString formatCount(qint64 count) const;

signals:
    void currentPathChanged();
    void selectedChanged();
    void extensionFilterChanged();
    void partialChanged();
    void listChanged();
    void loadErrorChanged();
    // The chart's payload: a new folder, or another depth.
    void chartChanged();

private:
    // ---- the tree, read straight out of the flat arrays ---------------------
    // The file panel (FileListModel) builds its rows from the same arrays, so
    // the accessors are shared through friendship.
    int nodeCount() const { return m_tree.names.size(); }
    const QString& nameOf(int index) const { return m_tree.names.at(index); }
    int parentOf(int index) const { return m_tree.parents.at(index); }
    bool directoryOf(int index) const { return (m_tree.flags.at(index) & 1) != 0; }
    quint64 sizeOf(int index) const { return static_cast<quint64>(std::max<qint64>(0, m_tree.sizes.at(index))); }
    // The space the file system really spends on it; the fixture carries no
    // figure of its own, so the cluster size is assumed there.
    quint64 allocOf(int index) const;
    quint64 modifiedOf(int index) const { return static_cast<quint64>(std::max<qint64>(0, m_tree.modified.at(index))); }
    bool hasModifiedOf(int index) const { return (m_tree.flags.at(index) & 2) != 0; }
    quint32 attributesOf(int index) const { return m_tree.attributes.at(index); }
    int childCountOf(int index) const { return m_childCount.at(index); }
    int childAt(int index, int position) const { return m_children.at(m_childStart.at(index) + position); }
    // A directory's children, in the order the scan wrote them.
    QVector<int> childrenOf(int index) const;

    // A directory's children are one contiguous range of m_children, so the
    // model needs two ints per node instead of a vector per node.
    void buildChildren();
    // The subtree counts of a tree that came without them (the fixture).
    void countItems();
    int appendNode(const QVariantMap& node, int parent);
    void resetStorage();
    void setLoadError(const QString& message);
    int indexForId(const QString& id) const;
    void resetAt(int index);
    void rebuildRows();
    // The same row set, written into a caller's buffer, so a navigation can
    // tell whether the visible outline actually changed before resetting the
    // model (a reset makes every delegate rebuild).
    void buildRows(QVector<int>& out) const;
    // The set of visible rows changed (a folder opened, the sort flipped, a
    // letter went into the filter). The view only learns about that from the
    // model's own reset, so this is the one way to rebuild the rows.
    void applyRowChange();
    void appendTreeRows(int parent, const QSet<int>& keep, bool filtering, QVector<int>& out) const;
    void collectFiles(int node, QVector<int>& out) const;
    // The path is derived from the parents instead of kept per node: a real
    // drive has millions of nodes, and one string each is a great deal of
    // memory for something the window reads one row at a time.
    QString pathOf(int index) const;
    QString extOf(int index) const;
    void filterWalk(int index, QString& path, QSet<int>& keep) const;
    // Row of a node in the outline, or -1. A folder's descendants are the
    // contiguous rows right after it, which is what lets a double click insert
    // or remove just that block instead of rebuilding every visible row.
    int rowOfNode(int index) const;
    void collectSubtreeRows(int index, QVector<int>& out) const;
    void collectFilesFiltered(int node, QString& path, QVector<int>& out) const;
    QSet<int> filterKeepSet() const;
    void sortEntries(QVector<int>& entries) const;
    void pushHistory(int index);
    int depthOf(int index) const;

    FlatTree m_tree;
    QVector<int> m_childStart; // -1 for a node without children
    QVector<int> m_childCount;
    QVector<int> m_children;   // node indices; a directory's range is contiguous

    QVector<int> m_visible;
    QVector<int> m_history;
    QSet<int> m_expanded;
    // The tree is an outline and the chart is the drill-down view, so they keep
    // separate roots: m_current is the folder the list is rooted at, m_chartRoot
    // is what the chart (and the breadcrumb) is showing. Drilling into the chart
    // therefore opens the tree down to that folder instead of moving it.
    int m_current = -1;
    int m_chartRoot = -1;
    int m_historyPos = -1;
    QString m_selectedId;
    // The extension the legend picked: the chart dims the rest and the file
    // list keeps to it. The flag separates "off" from the "(无)" pick, whose
    // extension is empty.
    QString m_extensionFilter;
    bool m_extensionFilterOn = false;
    QString m_filter;
    QString m_loadError;
    int m_sortKey = SortBySize;
    bool m_ascending = false;
    int m_viewMode = TreeView;
    int m_chartDepth = 2;
    // Bumped with every chartChanged, and written into the exported map, so
    // ChartItem can tell a new payload by one integer.
    int m_chartRevision = 0;
    // The legend aggregates the whole subtree. Its result only depends on the
    // chart root and the tree data, so it is cached instead of recomputed for
    // every read of the QML binding.
    mutable QVariantList m_extensionCache;
    mutable bool m_extensionCacheValid = false;
    mutable int m_extensionCacheRoot = -1;
    mutable int m_extensionCacheData = -1;
    // Bumped whenever the tree data itself changes (a load or a new scan).
    int m_dataRevision = 0;
    bool m_partial = false;
    bool m_fuzzyMatch = false;
    bool m_wholeTree = false;
};
