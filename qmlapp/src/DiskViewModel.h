#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

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
    Q_PROPERTY(bool partial READ partial NOTIFY partialChanged)
    // The list panel behaves like the native one: two sort keys, a filter over
    // the full path and two modes (the current layer or the largest files).
    Q_PROPERTY(int sortKey READ sortKey WRITE setSortKey NOTIFY listChanged)
    Q_PROPERTY(bool ascending READ ascending WRITE setAscending NOTIFY listChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY listChanged)
    Q_PROPERTY(int listMode READ listMode WRITE setListMode NOTIFY listChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY currentPathChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY currentPathChanged)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY currentPathChanged)

public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, SizeRole, SizeTextRole, PercentRole, DirectoryRole, PathRole };
    enum SortKey { SortBySize = 0, SortByName = 1 };
    enum ListMode { ListCurrent = 0, ListLargest = 1 };

    explicit DiskViewModel(QObject* parent = nullptr);
    Q_INVOKABLE bool loadFixture(const QString& fileName);
    Q_INVOKABLE bool loadTree(const QVariantMap& tree, bool partial = false);
    // Drops the tree that is shown, so a new scan does not start with the
    // result of the previous one.
    Q_INVOKABLE void clear();
    // A cancelled or failed scan leaves an unfinished tree behind; the window
    // has to say so instead of pretending the data is complete.
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
    int sortKey() const { return m_sortKey; }
    void setSortKey(int key);
    bool ascending() const { return m_ascending; }
    void setAscending(bool ascending);
    QString filter() const { return m_filter; }
    void setFilter(const QString& filter);
    int listMode() const { return m_listMode; }
    void setListMode(int mode);
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
    // Shows the folder that holds the node and selects it.
    Q_INVOKABLE bool revealNode(const QString& id);
    Q_INVOKABLE QString formatSize(qint64 bytes) const;
    Q_INVOKABLE QString formatCount(qint64 count) const;

signals:
    void currentPathChanged();
    void selectedChanged();
    void partialChanged();
    void listChanged();

private:
    struct Entry { QString id,name,path; quint64 size=0; bool directory=false; int parent=-1; QVector<int> children; };
    int appendNode(const QVariantMap& node, int parent, const QString& path);
    int indexForId(const QString& id) const;
    int currentIndex() const { return m_current; }
    void resetAt(int index);
    void rebuildRows();
    void rebuildLargest();
    void pushHistory(int index);

    QVector<Entry> m_nodes;
    QVector<int> m_visible;
    QVector<int> m_largest;
    QVector<int> m_history;
    int m_current=-1;
    int m_historyPos=-1;
    QString m_selectedId;
    QString m_filter;
    int m_sortKey = SortBySize;
    bool m_ascending = false;
    int m_listMode = ListCurrent;
    bool m_partial=false;
};
