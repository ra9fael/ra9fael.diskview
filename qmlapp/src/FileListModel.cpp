#include "FileListModel.h"

#include "DiskViewModel.h"

#include <algorithm>
FileListModel::FileListModel(QObject* parent) : QAbstractListModel(parent) {}

void FileListModel::setModel(DiskViewModel* model)
{
    if (m_model == model) return;
    beginResetModel();
    if (m_model && m_connected) {
        QObject::disconnect(m_model, nullptr, this, nullptr);
        m_connected = false;
    }
    m_model = model;
    m_rows.clear();
    m_cacheValid = false;
    if (m_model) {
        // The panel follows the shared state: another folder, another filter,
        // another extension pick, a new scan — every one of them rebuilds the
        // rows. The model's own reset signals arrive as listChanged, which is
        // the one hook this needs.
        connect(m_model, &DiskViewModel::listChanged, this, &FileListModel::rebuild);
        connect(m_model, &DiskViewModel::currentPathChanged, this, &FileListModel::rebuild);
        connect(m_model, &QAbstractItemModel::modelReset, this, [this] { m_cacheValid = false; });
        connect(m_model, &DiskViewModel::selectedChanged, this, [this] {
            if (!m_rows.isEmpty()) emit dataChanged(index(0), index(m_rows.size() - 1), {SelectedRole});
            emit selectionChanged();
        });
        m_connected = true;
    }
    endResetModel();
    rebuild();
    emit modelChanged();
    emit countChanged();
}

void FileListModel::setWholeTree(bool whole)
{
    if (m_wholeTree == whole) return;
    m_wholeTree = whole;
    emit wholeTreeChanged();
    rebuild();
}

void FileListModel::setRecursive(bool recursive)
{
    if (m_recursive == recursive) return;
    m_recursive = recursive;
    emit recursiveChanged();
    rebuild();
}

void FileListModel::setFilter(const QString& filter)
{
    if (m_filter == filter) return;
    m_filter = filter;
    emit filterChanged();
    rebuild();
}

void FileListModel::setFuzzyMatch(bool fuzzy)
{
    if (m_fuzzyMatch == fuzzy) return;
    m_fuzzyMatch = fuzzy;
    emit filterChanged();
    rebuild();
}

void FileListModel::rebuild()
{
    if (!m_model || m_model->m_current < 0) {
        if (m_rows.isEmpty()) return;
        beginResetModel();
        m_rows.clear();
        endResetModel();
        emit countChanged();
        return;
    }
    // The file list changes only when the folder, the filter or the scan
    // does; an expand or a collapse in the outline must not reorder it. The
    // signature says what the rows depend on.
    const int root = m_wholeTree ? 0 : m_model->m_chartRoot;
    const quint64 signature = (quint64(root) << 8)
        ^ (quint64(qHash(m_filter)) << 1)
        ^ (m_fuzzyMatch ? 1u : 0u)
        ^ (quint64(m_model->m_sortKey + 1) << 55)
        ^ (m_model->m_ascending ? (1ull << 59) : 0ull)
        ^ (m_model->m_extensionFilterOn ? (quint64(qHash(m_model->m_extensionFilter)) << 40 | (1ull << 32)) : 0ull)
        ^ (m_model->m_partial ? (1ull << 62) : 0ull)
        ^ (m_recursive ? (1ull << 61) : 0ull);
    if (m_cacheValid && signature == m_signature && !m_rows.isEmpty()) return;
    m_signature = signature;
    m_cacheValid = true;

    beginResetModel();
    m_rows.clear();
    // The whole-tree search walks everything; a folder's list walks its own
    // subtree. Either way only files are rows.
    if (m_recursive || m_wholeTree) {
        QVector<int> stack{root};
        while (!stack.isEmpty()) {
            const int node = stack.takeLast();
            const int count = m_model->childCountOf(node);
            for (int position = 0; position < count; ++position) {
                const int child = m_model->childAt(node, position);
                if (m_model->m_tree.flags.at(child) & 32) continue; // small-files group
                if (m_model->directoryOf(child)) stack.push_back(child);
                else m_rows.push_back(child);
            }
        }
    } else {
        // The default: the folder's own files. Walking a large subtree on every
        // navigation was the remaining second of a double click.
        const int count = m_model->childCountOf(root);
        for (int position = 0; position < count; ++position) {
            const int child = m_model->childAt(root, position);
            if (m_model->m_tree.flags.at(child) & 32) continue;
            if (!m_model->directoryOf(child)) m_rows.push_back(child);
        }
    }
    // The text filter: a substring of the path, or the fuzzy subsequence of
    // the name, the way the old file view matched.
    const QString& filter = m_filter;
    if (!filter.isEmpty()) {
        if (m_fuzzyMatch) {
            m_rows.erase(std::remove_if(m_rows.begin(), m_rows.end(), [this, &filter](int node) {
                return !fuzzyContains(m_model->nameViewOf(node), filter);
            }), m_rows.end());
        } else {
            m_rows.erase(std::remove_if(m_rows.begin(), m_rows.end(), [this, &filter](int node) {
                return !m_model->pathOf(node).contains(filter, Qt::CaseInsensitive);
            }), m_rows.end());
        }
    }
    // The extension pick keeps its own files: "(无)" keeps the ones with no
    // extension at all.
    if (m_model->m_extensionFilterOn) {
        m_rows.erase(std::remove_if(m_rows.begin(), m_rows.end(), [this](int node) {
            return m_model->extOf(node) != m_model->m_extensionFilter;
        }), m_rows.end());
    }
    // Order: a fuzzy search ranks its best matches first, everything else is
    // the panel's own sort (size downwards, the model's key applies here
    // too).
    if (!filter.isEmpty() && m_fuzzyMatch) {
        std::stable_sort(m_rows.begin(), m_rows.end(), [this, &filter](int a, int b) {
            return fuzzyRank(m_model->nameViewOf(a), filter) < fuzzyRank(m_model->nameViewOf(b), filter);
        });
    } else {
        // Compare the actual values. The old key built two base-36 QStrings
        // per comparison; at 200k rows that is millions of allocations and a
        // whole second of the file panel rebuilding.
        std::stable_sort(m_rows.begin(), m_rows.end(), [this](int a, int b) {
            int order = 0;
            switch (m_model->m_sortKey) {
            case DiskViewModel::SortByName:
                order = m_model->nameViewOf(a).compare(m_model->nameViewOf(b), Qt::CaseInsensitive);
                break;
            case DiskViewModel::SortByModified: {
                const quint64 x = m_model->hasModifiedOf(a) ? m_model->modifiedOf(a) : 0;
                const quint64 y = m_model->hasModifiedOf(b) ? m_model->modifiedOf(b) : 0;
                if (x != y) order = x < y ? -1 : 1;
                break;
            }
            default: {
                const quint64 x = m_model->sizeOf(a);
                const quint64 y = m_model->sizeOf(b);
                if (x != y) order = x < y ? -1 : 1;
                break;
            }
            }
            if (order == 0) return a < b;
            return m_model->m_ascending ? order < 0 : order > 0;
        });
    }
    endResetModel();
    emit countChanged();
}

int FileListModel::fuzzyScore(int node) const
{
    return fuzzyRank(m_model->nameViewOf(node), m_filter);
}

int FileListModel::selectedRow() const
{
    for (int row = 0; row < m_rows.size(); ++row) {
        if (QString::number(m_rows[row] + 1) == m_model->m_selectedId) return row;
    }
    return -1;
}

int FileListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant FileListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size() || !m_model) return {};
    const int node = m_rows.at(index.row());
    const quint64 size = m_model->sizeOf(node);
    switch (role) {
    case IdRole: return QString::number(node + 1);
    case NameRole: return m_model->nameOf(node);
    case SizeRole: return QVariant::fromValue<qulonglong>(size);
    case SizeTextRole: return m_model->formatSize(static_cast<qint64>(size));
    case PercentRole: {
        const int parent = m_model->parentOf(node);
        const quint64 total = parent >= 0 ? std::max<quint64>(1, m_model->sizeOf(parent)) : 1;
        return 100.0 * static_cast<double>(size) / static_cast<double>(total);
    }
    case DirectoryRole: return false;
    case PathRole: return m_model->pathOf(node);
    case AllocTextRole: return m_model->formatSize(static_cast<qint64>(m_model->allocOf(node)));
    case ExtRole: return m_model->extOf(node);
    case ExtColorRole: return m_model->extensionColorOf(node).name();
    case FilesRole: return 0;
    case ModifiedTextRole:
        return m_model->hasModifiedOf(node) ? m_model->modifiedText(m_model->modifiedOf(node)) : QStringLiteral("—");
    case ModifiedValueRole:
        return QVariant::fromValue<qulonglong>(m_model->hasModifiedOf(node) ? m_model->modifiedOf(node) : 0);
    case AttributesRole: return m_model->attributesText(m_model->attributesOf(node));
    case FuzzyRankRole: return fuzzyScore(node);
    case SelectedRole: return QString::number(node + 1) == m_model->m_selectedId;
    default: return {};
    }
}

QHash<int, QByteArray> FileListModel::roleNames() const
{
    return {{IdRole, "nodeId"}, {NameRole, "name"}, {SizeRole, "size"}, {SizeTextRole, "sizeText"},
            {PercentRole, "percent"}, {DirectoryRole, "directory"}, {PathRole, "path"},
            {AllocTextRole, "allocText"}, {ExtRole, "ext"}, {ExtColorRole, "extColor"},
            {FilesRole, "files"}, {ModifiedTextRole, "modifiedText"}, {ModifiedValueRole, "modifiedValue"},
            {AttributesRole, "attributes"}, {FuzzyRankRole, "fuzzyRank"}, {SelectedRole, "selected"}};
}

void FileListModel::select(const QString& id)
{
    if (m_model) m_model->select(id);
}

void FileListModel::moveSelection(int delta)
{
    if (m_rows.isEmpty()) return;
    int row = selectedRow();
    if (row < 0) row = delta > 0 ? -1 : m_rows.size();
    row = std::clamp(row + delta, 0, static_cast<int>(m_rows.size()) - 1);
    if (m_model) m_model->select(QString::number(m_rows[row] + 1));
}

void FileListModel::openSelected()
{
    if (!m_model) return;
    // A file's row opens the folder it sits in: the tree navigates there, the
    // chart follows, and the extension filter keeps its pick.
    const int row = selectedRow();
    if (row < 0) return;
    const int node = m_rows[row];
    const int parent = m_model->parentOf(node);
    if (parent >= 0) m_model->navigate(QString::number(parent + 1));
}

QString FileListModel::idAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? QString::number(m_rows[row] + 1) : QString();
}
