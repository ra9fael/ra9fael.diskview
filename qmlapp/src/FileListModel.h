#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <limits>

#include "DiskViewModel.h"

// The matcher the two panels share: the subsequence test a fuzzy file finder
// means, and its rank (how far into the name the last character landed,
// lower is tighter).
bool fuzzyContains(QStringView haystack, QStringView needle);
int fuzzyRank(QStringView haystack, QStringView needle);

// The file view as its own panel: the files of the whole scan or of the
// folder the chart is on, sorted and filtered the way the file view always
// was, while the outline panel keeps its own rows. Both read the one
// DiskViewModel — the arrays, the selection, the extension filter and the
// history stay shared, so picking a file here highlights it in the chart and
// the outline exactly as picking it there would.
class FileListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(DiskViewModel* model READ model WRITE setModel NOTIFY modelChanged)
    // Where the files come from: the folder the chart is on, or the whole
    // scan (the old file view's "整个扫描").
    Q_PROPERTY(bool wholeTree READ wholeTree WRITE setWholeTree NOTIFY wholeTreeChanged)
    // Walk the folder's whole subtree. Off by default: listing a folder with
    // hundreds of thousands of descendants is what made a double click stall.
    Q_PROPERTY(bool recursive READ recursive WRITE setRecursive NOTIFY recursiveChanged)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY countChanged)
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectionChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(bool fuzzyMatch READ fuzzyMatch WRITE setFuzzyMatch NOTIFY filterChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1, NameRole, SizeRole, SizeTextRole, PercentRole,
        DirectoryRole, PathRole, AllocTextRole, ExtRole, ExtColorRole,
        FilesRole, ModifiedTextRole, ModifiedValueRole, AttributesRole,
        FuzzyRankRole, SelectedRole
    };

    explicit FileListModel(QObject* parent = nullptr);

    DiskViewModel* model() const { return m_model; }
    void setModel(DiskViewModel* model);
    bool wholeTree() const { return m_wholeTree; }
    void setWholeTree(bool whole);
    bool recursive() const { return m_recursive; }
    void setRecursive(bool recursive);
    int itemCount() const { return m_rows.size(); }
    int selectedRow() const;
    QString filter() const { return m_filter; }
    void setFilter(const QString& filter);
    bool fuzzyMatch() const { return m_fuzzyMatch; }
    void setFuzzyMatch(bool fuzzy);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void select(const QString& id);
    // The keyboard: up and down move within this panel's rows, Return opens
    // the folder of the row (the tree navigates along), FuzzyRank drives the
    // ordering of a whole-tree search.
    Q_INVOKABLE void moveSelection(int delta);
    Q_INVOKABLE void openSelected();
    Q_INVOKABLE QString idAt(int row) const;

signals:
    void modelChanged();
    void wholeTreeChanged();
    void recursiveChanged();
    void countChanged();
    void selectionChanged();
    void filterChanged();

private:
    void rebuild();
    int fuzzyScore(int node) const;

    DiskViewModel* m_model = nullptr;
    QVector<int> m_rows;
    bool m_wholeTree = false;
    bool m_recursive = false;
    QString m_filter;
    bool m_fuzzyMatch = false;
    bool m_connected = false;
    quint64 m_signature = 0;
    bool m_cacheValid = false;
};
