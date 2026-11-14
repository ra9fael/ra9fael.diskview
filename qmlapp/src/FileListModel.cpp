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
    if (m_model) {
        // The panel follows the shared state: another folder, another filter,
        // another extension pick, a new scan — every one of them rebuilds the
        // rows. The model's own reset signals arrive as listChanged, which is
        // the one hook this needs.
        connect(m_model, &DiskViewModel::listChanged, this, &FileListModel::rebuild);
        connect(m_model, &DiskViewModel::currentPathChanged, this, &FileListModel::rebuild);
        connect(m_model, &DiskViewModel::selectedChanged, this, &FileListModel::selectionChanged);
        m_connected = true;
        rebuild();
    }
    endResetModel();
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
        ^ (quint64(qHash(m_model->m_filter)) << 1)
        ^ (m_model->m_fuzzyMatch ? 1u : 0u)
        ^ (m_model->m_extensionFilterOn ? (quint64(qHash(m_model->m_extensionFilter)) << 40 | (1ull << 32)) : 0ull)
        ^ (m_model->m_partial ? (1ull << 62) : 0ull);
    if (signature == m_signature && !m_rows.isEmpty()) return;
    m_signature = signature;

    beginResetModel();
    m_rows.clear();
    // The whole-tree search walks everything; a folder's list walks its own
    // subtree. Either way only files are rows.
    QVector<int> stack{root};
    QString path;
    while (!stack.isEmpty()) {
        const int node = stack.takeLast();
        const int count = m_model->childCountOf(node);
        for (int position = 0; position < count; ++position) {
            const int child = m_model->childAt(node, position);
            if (m_model->directoryOf(child)) stack.push_back(child);
            else m_rows.push_back(child);
        }
    }
    // The text filter: a substring of the path, or the fuzzy subsequence of
    // the name, the way the old file view matched.
    const QString& filter = m_model->m_filter;
    if (!filter.isEmpty()) {
        if (m_model->m_fuzzyMatch) {
            m_rows.erase(std::remove_if(m_rows.begin(), m_rows.end(), [this, &filter](int node) {
                return !fuzzyContains(m_model->nameOf(node), filter);
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
    if (!filter.isEmpty() && m_model->m_fuzzyMatch) {
        std::stable_sort(m_rows.begin(), m_rows.end(), [this, &filter](int a, int b) {
            return fuzzyRank(m_model->nameOf(a), filter) < fuzzyRank(m_model->nameOf(b), filter);
        });
    } else {
        const auto key = [this](int node) -> QString {
            switch (m_model->m_sortKey) {
            case DiskViewModel::SortByModified: return QString::number(m_model->modifiedOf(node), 36).rightJustified(12, '0');
            default: break;
            }
            // Names compare on their lowercase form; sizes compare by their
            // base-36 digits padded to a fixed width, which keeps the sort
            // stable and the code one lane.
            return m_model->m_sortKey == DiskViewModel::SortByName
                ? m_model->nameOf(node).toLower()
                : QString::number(m_model->sizeOf(node), 36).rightJustified(14, '0');
        };
        std::stable_sort(m_rows.begin(), m_rows.end(), [this, &key](int a, int b) {
            const QString ka = key(a), kb = key(b);
            if (ka != kb) return m_model->m_ascending ? ka < kb : kb < ka;
            return a < b;
        });
    }
    endResetModel();
    emit countChanged();
}

int FileListModel::fuzzyScore(int node) const
{
    return fuzzyRank(m_model->nameOf(node), m_model->m_filter);
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
