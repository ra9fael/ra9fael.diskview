#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

// The tree behind the window: the folder table, the file list and the
// breadcrumb all come from here. The table is a flattened tree, so expanding
// a folder is a change of the visible rows, not of the model's contents.
class DiskViewModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentPathChanged)
    Q_PROPERTY(QString totalText READ totalText NOTIFY currentPathChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectedChanged)
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectedChanged)
    Q_PROPERTY(QVariantMap chartTree READ chartTree NOTIFY currentPathChanged)
    Q_PROPERTY(QString breadcrumbText READ breadcrumbText NOTIFY currentPathChanged)
    Q_PROPERTY(QVariantList breadcrumb READ breadcrumb NOTIFY currentPathChanged)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY listChanged)
    // The items the current folder holds, all levels down.
    Q_PROPERTY(int nodeItems READ nodeItems NOTIFY currentPathChanged)
    Q_PROPERTY(bool partial READ partial NOTIFY partialChanged)
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

    explicit DiskViewModel(QObject* parent = nullptr);
    Q_INVOKABLE bool loadFixture(const QString& fileName);
    Q_INVOKABLE bool loadTree(const QVariantMap& tree, bool partial = false);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void setPartial(bool partial);
    bool partial() const { return m_partial; }
    QString currentPath() const;
    QString currentName() const;
    QString totalText() const;
    QString selectedId() const { return m_selectedId; }
    int selectedRow() const;
    QVariantMap chartTree() const;
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
    bool canGoUp() const { return m_current > 0; }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void navigate(const QString& id);
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void select(const QString& id);
    Q_INVOKABLE void moveSelection(int delta);
    Q_INVOKABLE void openSelected();
    Q_INVOKABLE void toggleSort(int key);
    Q_INVOKABLE bool revealNode(const QString& id);
    Q_INVOKABLE void toggleExpanded(const QString& id);
    Q_INVOKABLE bool isExpanded(const QString& id) const;
    // What the current folder is made of, biggest extension first.
    Q_INVOKABLE QVariantList extensions() const;
    Q_INVOKABLE QString formatSize(qint64 bytes) const;
    Q_INVOKABLE QString formatCount(qint64 count) const;

signals:
    void currentPathChanged();
    void selectedChanged();
    void partialChanged();
    void listChanged();

private:
    struct Entry {
        QString id, name, path, ext;
        quint64 size = 0, alloc = 0;
        quint64 countFiles = 0, countDirs = 0;
        quint64 modified = 0;   // FILETIME ticks; 0 when the source has no stamp
        quint32 attributes = 0;
        bool directory = false;
        bool hasModified = false;
        int parent = -1;
        QVector<int> children;
    };
    int appendNode(const QVariantMap& node, int parent, const QString& path);
    void countItems();
    int indexForId(const QString& id) const;
    void resetAt(int index);
    void rebuildRows();
    void appendTreeRows(int parent, const QSet<int>& keep, bool filtering);
    void collectFiles(int node, QVector<int>& out) const;
    QSet<int> filterKeepSet() const;
    void sortEntries(QVector<int>& entries) const;
    void pushHistory(int index);
    int depthOf(int index) const;

    QVector<Entry> m_nodes;
    QVector<int> m_visible;
    QVector<int> m_history;
    QSet<int> m_expanded;
    int m_current = -1;
    int m_historyPos = -1;
    QString m_selectedId;
    QString m_filter;
    int m_sortKey = SortBySize;
    bool m_ascending = false;
    int m_viewMode = TreeView;
    bool m_partial = false;
    bool m_fuzzyMatch = false;
    bool m_wholeTree = false;
};
