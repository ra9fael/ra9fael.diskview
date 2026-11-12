#include "DiskViewModel.h"

#include "ExtensionColors.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <QDateTime>
#include <algorithm>

namespace {
// A file system hands out space in clusters, and the fixture JSON does not
// carry the real figure, so it is rounded up here.
quint64 allocatedFor(quint64 size)
{
    return (size + 4095u) / 4096u * 4096u;
}

// WizTree shows the same date shape everywhere, so the column does not jump
// around: yyyy-MM-dd HH:mm.
QString modifiedText(quint64 fileTime)
{
    if (!fileTime) return QStringLiteral("—");
    // FILETIME counts 100 ns ticks from 1601-01-01 UTC.
    const qint64 msecsSinceEpoch = static_cast<qint64>(fileTime / 10000ull) - 11644473600000ll;
    if (msecsSinceEpoch <= 0) return QStringLiteral("—");
    return QDateTime::fromMSecsSinceEpoch(msecsSinceEpoch).toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

// The Windows attribute letters, the same set the Explorer shows.
QString attributesText(quint32 attributes)
{
    if (!attributes) return QStringLiteral("—");
    QString text;
    if (attributes & 0x1u) text += QLatin1Char('R');    // read only
    if (attributes & 0x2u) text += QLatin1Char('H');    // hidden
    if (attributes & 0x4u) text += QLatin1Char('S');    // system
    if (attributes & 0x10u) text += QLatin1Char('D');   // directory
    if (attributes & 0x20u) text += QLatin1Char('A');   // archive
    if (attributes & 0x400u) text += QLatin1Char('L');  // reparse point
    if (attributes & 0x800u) text += QLatin1Char('C');  // compressed
    return text.isEmpty() ? QStringLiteral("—") : text;
}

// A subsequence match: "scm" finds "scan-manager". Cheaper and more
// predictable than an edit distance, and it is what a fuzzy file finder
// means in practice.
bool fuzzyContains(const QString& haystack, const QString& needle)
{
    if (needle.isEmpty()) return true;
    int at = 0;
    for (const QChar ch : needle) {
        const int found = haystack.indexOf(ch, at, Qt::CaseInsensitive);
        if (found < 0) return false;
        at = found + 1;
    }
    return true;
}
}

DiskViewModel::DiskViewModel(QObject* parent) : QAbstractListModel(parent) {}

bool DiskViewModel::loadFixture(const QString& fileName)
{
    const QString localName = fileName.startsWith("file:") ? QUrl(fileName).toLocalFile() : fileName;
    QFile file(localName);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return false;
    return loadTree(doc.object().toVariantMap());
}

bool DiskViewModel::loadTree(const QVariantMap& tree, bool partial)
{
    if (tree.isEmpty()) return false;
    beginResetModel();
    m_nodes.clear(); m_visible.clear(); m_history.clear(); m_expanded.clear();
    m_selectedId.clear(); m_filter.clear();
    m_historyPos = -1;
    appendNode(tree, -1, {});
    countItems();
    m_current = 0;
    pushHistory(0);
    rebuildRows();
    endResetModel();
    setPartial(partial);
    emit currentPathChanged();
    emit selectedChanged();
    emit listChanged();
    return true;
}

void DiskViewModel::clear()
{
    beginResetModel();
    m_nodes.clear(); m_visible.clear(); m_history.clear(); m_expanded.clear();
    m_selectedId.clear(); m_filter.clear();
    m_current = -1;
    m_historyPos = -1;
    endResetModel();
    setPartial(false);
    emit currentPathChanged();
    emit selectedChanged();
    emit listChanged();
}

void DiskViewModel::setPartial(bool partial)
{
    if (m_partial == partial) return;
    m_partial = partial;
    emit partialChanged();
}

int DiskViewModel::appendNode(const QVariantMap& node, int parent, const QString& path)
{
    Entry e;
    e.name = node.value("name").toString();
    e.size = static_cast<quint64>(node.value("size").toDouble());
    // The scan reports the space actually taken; the fixture does not.
    if (node.contains("alloc")) e.alloc = static_cast<quint64>(node.value("alloc").toDouble());
    if (e.alloc == 0) e.alloc = allocatedFor(e.size);
    // The scan knows the entry counts; the fixture does not, and they are
    // summed up afterwards.
    e.countFiles = static_cast<quint64>(node.value("files").toDouble());
    e.countDirs = static_cast<quint64>(node.value("dirs").toDouble());
    const QString stamp = node.value("modified").toString();
    if (!stamp.isEmpty()) {
        e.modified = stamp.toULongLong(&e.hasModified);
    }
    e.attributes = node.value("attrs").toUInt();
    e.directory = node.value("dir").toBool();
    e.parent = parent;
    e.ext = e.directory ? QString() : diskviewqml::extensionOf(e.name);
    // A root that already ends with a separator keeps it, so the paths read
    // the way they do in the file system.
    e.path = path.isEmpty() ? e.name : (path.endsWith('\\') ? path + e.name : path + "\\" + e.name);
    e.id = QString::number(m_nodes.size() + 1);
    const int index = m_nodes.size();
    m_nodes.push_back(e);
    if (parent >= 0) m_nodes[parent].children.push_back(index);
    for (const auto& child : node.value("children").toList()) {
        if (child.canConvert<QVariantMap>()) appendNode(child.toMap(), index, e.path);
    }
    return index;
}

void DiskViewModel::countItems()
{
    // Children always sit after their parent, so a backward walk has every
    // child's totals ready when the parent asks for them.
    for (int i = m_nodes.size() - 1; i >= 0; --i) {
        if (!m_nodes[i].directory) continue;
        quint64 files = 0, dirs = 0;
        for (int child : m_nodes[i].children) {
            if (m_nodes[child].directory) {
                dirs += 1 + m_nodes[child].countDirs;
                files += m_nodes[child].countFiles;
            } else {
                ++files;
            }
        }
        m_nodes[i].countFiles = files;
        m_nodes[i].countDirs = dirs;
    }
}

int DiskViewModel::nodeItems() const
{
    if (m_current < 0) return 0;
    const Entry& e = m_nodes[m_current];
    return static_cast<int>(e.countFiles + e.countDirs);
}

QString DiskViewModel::formatSize(qint64 bytes) const
{
    // The native plugin reads sizes in 1024 steps and drops the decimal once
    // the number is large enough not to need it.
    static const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double n = std::max<qint64>(0, bytes);
    int u = 0;
    while (n >= 1024.0 && u < 5) { n /= 1024.0; ++u; }
    if (u == 0) return QString::number(qint64(n)) + " " + units[u];
    return QString::number(n, 'f', n >= 100.0 ? 0 : 1) + " " + units[u];
}

QString DiskViewModel::formatCount(qint64 count) const
{
    QString digits = QString::number(std::max<qint64>(0, count));
    for (int i = digits.size() - 3; i > 0; i -= 3) digits.insert(i, QChar(','));
    return digits;
}

QString DiskViewModel::currentName() const { return m_current >= 0 ? m_nodes[m_current].name : QString(); }
QString DiskViewModel::currentPath() const { return m_current >= 0 ? m_nodes[m_current].path : QString(); }
QString DiskViewModel::totalText() const { return m_current >= 0 ? formatSize(m_nodes[m_current].size) : QString(); }
QString DiskViewModel::breadcrumbText() const { return currentPath().replace('\\', "  /  "); }

QVariantList DiskViewModel::breadcrumb() const
{
    QVariantList crumbs;
    if (m_current < 0) return crumbs;
    QVector<int> chain;
    for (int index = m_current; index >= 0; index = m_nodes[index].parent) chain.push_back(index);
    for (int i = chain.size() - 1; i >= 0; --i) {
        const Entry& e = m_nodes[chain[i]];
        crumbs.push_back(QVariantMap{{"name", e.name}, {"id", e.id}, {"current", chain[i] == m_current}});
    }
    return crumbs;
}

int DiskViewModel::selectedRow() const
{
    for (int row = 0; row < m_visible.size(); ++row) {
        if (m_nodes[m_visible[row]].id == m_selectedId) return row;
    }
    return -1;
}

int DiskViewModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : m_visible.size(); }

QVariant DiskViewModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size()) return {};
    const int nodeIndex = m_visible[index.row()];
    const Entry& e = m_nodes[nodeIndex];
    const double parentTotal = e.parent >= 0 ? std::max<quint64>(1, m_nodes[e.parent].size) : 1;
    switch (role) {
    case IdRole: return e.id;
    case NameRole: return e.name;
    case SizeRole: return QVariant::fromValue<qulonglong>(e.size);
    case SizeTextRole: return formatSize(e.size);
    case PercentRole: return 100.0 * e.size / parentTotal;
    case DirectoryRole: return e.directory;
    case PathRole: return e.path;
    case DepthRole: return depthOf(nodeIndex);
    case HasChildrenRole: return e.directory && !e.children.isEmpty();
    case ExpandedRole: return m_expanded.contains(nodeIndex);
    case AllocTextRole: return formatSize(e.alloc);
    case ExtRole: return e.directory ? QString() : e.ext;
    case ExtColorRole: return e.directory ? QStringLiteral("#8f9aa8") : diskviewqml::extensionColor(e.ext).name();
    case ItemsRole: return static_cast<int>(e.countFiles + e.countDirs);
    case FilesRole: return static_cast<int>(e.countFiles);
    case DirsRole: return static_cast<int>(e.countDirs);
    case ModifiedTextRole: return e.hasModified ? modifiedText(e.modified) : QStringLiteral("—");
    case ModifiedValueRole: return QVariant::fromValue<qulonglong>(e.hasModified ? e.modified : 0);
    case AttributesRole: return attributesText(e.attributes);
    default: return {};
    }
}

QHash<int, QByteArray> DiskViewModel::roleNames() const
{
    return {{IdRole, "nodeId"}, {NameRole, "name"}, {SizeRole, "size"}, {SizeTextRole, "sizeText"},
            {PercentRole, "percent"}, {DirectoryRole, "directory"}, {PathRole, "path"},
            {DepthRole, "depth"}, {HasChildrenRole, "hasChildren"}, {ExpandedRole, "expanded"},
            {AllocTextRole, "allocText"}, {ExtRole, "ext"}, {ExtColorRole, "extColor"},
            {ItemsRole, "items"}, {FilesRole, "files"}, {DirsRole, "dirs"},
            {ModifiedTextRole, "modifiedText"}, {ModifiedValueRole, "modifiedValue"},
            {AttributesRole, "attributes"}};
}

int DiskViewModel::indexForId(const QString& id) const
{
    for (int i = 0; i < m_nodes.size(); ++i) if (m_nodes[i].id == id) return i;
    return -1;
}

int DiskViewModel::depthOf(int index) const
{
    int depth = 0;
    for (int p = m_nodes[index].parent; p >= 0 && p != m_current; p = m_nodes[p].parent) ++depth;
    return depth;
}

void DiskViewModel::sortEntries(QVector<int>& entries) const
{
    std::stable_sort(entries.begin(), entries.end(), [&](int a, int b) {
        const Entry& x = m_nodes[a];
        const Entry& y = m_nodes[b];
        int order = 0;
        switch (m_sortKey) {
        case SortByName:
            order = QString::compare(x.name, y.name, Qt::CaseInsensitive);
            break;
        case SortByModified:
            if (x.modified != y.modified) order = x.modified < y.modified ? -1 : 1;
            break;
        default:
            if (x.size != y.size) order = x.size < y.size ? -1 : 1;
            break;
        }
        return m_ascending ? order < 0 : order > 0;
    });
}

void DiskViewModel::collectFiles(int node, QVector<int>& out) const
{
    QVector<int> stack{node};
    while (!stack.isEmpty()) {
        const int index = stack.takeLast();
        for (int child : m_nodes[index].children) {
            if (m_nodes[child].directory) stack.push_back(child);
            else out.push_back(child);
        }
    }
}

QSet<int> DiskViewModel::filterKeepSet() const
{
    QSet<int> keep;
    if (m_filter.isEmpty()) return keep;
    // A match keeps its whole path visible, otherwise the row would float
    // without the folder it lives in.
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (!m_nodes[i].path.contains(m_filter, Qt::CaseInsensitive)) continue;
        for (int a = i; a >= 0; a = m_nodes[a].parent) keep.insert(a);
    }
    return keep;
}

void DiskViewModel::appendTreeRows(int parent, const QSet<int>& keep, bool filtering)
{
    QVector<int> kids = m_nodes[parent].children;
    sortEntries(kids);
    for (int child : kids) {
        if (filtering && !keep.contains(child)) continue;
        m_visible.push_back(child);
        // While a filter is on, the path to every match is shown open; the
        // folders the user opened by hand are left exactly as they were.
        const bool open = m_expanded.contains(child) || filtering;
        if (m_nodes[child].directory && open) {
            appendTreeRows(child, keep, filtering);
        }
    }
}

void DiskViewModel::rebuildRows()
{
    m_visible.clear();
    if (m_current < 0) return;
    if (m_viewMode == FileView) {
        QVector<int> files;
        collectFiles(m_wholeTree ? 0 : m_current, files);
        if (!m_filter.isEmpty()) {
            files.erase(std::remove_if(files.begin(), files.end(), [this](int i) {
                const QString& name = m_nodes[i].name;
                // The folder scope is narrow enough to keep matching the
                // whole path, the whole-tree one only makes sense on the name.
                return m_fuzzyMatch ? !fuzzyContains(name, m_filter)
                                    : !m_nodes[i].path.contains(m_filter, Qt::CaseInsensitive);
            }), files.end());
        }
        sortEntries(files);
        m_visible = files;
        return;
    }
    const QSet<int> keep = filterKeepSet();
    appendTreeRows(m_current, keep, !m_filter.isEmpty());
}

void DiskViewModel::pushHistory(int index)
{
    if (m_historyPos >= 0 && m_historyPos < m_history.size() && m_history[m_historyPos] == index) return;
    m_history.resize(m_historyPos + 1);
    m_history.push_back(index);
    m_historyPos = m_history.size() - 1;
}

void DiskViewModel::resetAt(int index)
{
    if (index < 0 || index >= m_nodes.size()) return;
    beginResetModel();
    m_current = index;
    // Navigating clears the filter, the way the native panel does.
    m_filter.clear();
    m_selectedId.clear();
    rebuildRows();
    endResetModel();
    emit currentPathChanged();
    emit selectedChanged();
    emit listChanged();
}

void DiskViewModel::navigate(const QString& id)
{
    const int i = indexForId(id);
    if (i >= 0 && m_nodes[i].directory) { pushHistory(i); resetAt(i); }
}

void DiskViewModel::goUp()
{
    if (m_current <= 0) return;
    const int leaving = m_current;
    const int parent = m_nodes[leaving].parent;
    pushHistory(parent);
    resetAt(parent);
    m_selectedId = m_nodes[leaving].id;
    emit selectedChanged();
}

void DiskViewModel::goBack()
{
    if (!canGoBack()) return;
    --m_historyPos;
    resetAt(m_history[m_historyPos]);
}

void DiskViewModel::goForward()
{
    if (!canGoForward()) return;
    ++m_historyPos;
    resetAt(m_history[m_historyPos]);
}

void DiskViewModel::select(const QString& id)
{
    if (m_selectedId == id) return;
    m_selectedId = id;
    emit selectedChanged();
}

void DiskViewModel::moveSelection(int delta)
{
    if (m_visible.isEmpty()) return;
    int row = selectedRow();
    if (row < 0) row = delta > 0 ? -1 : m_visible.size();
    row += delta;
    row = std::clamp<int>(row, 0, m_visible.size() - 1);
    m_selectedId = m_nodes[m_visible[row]].id;
    emit selectedChanged();
}

void DiskViewModel::openSelected()
{
    const int i = indexForId(m_selectedId);
    if (i >= 0 && m_nodes[i].directory) { pushHistory(i); resetAt(i); }
}

void DiskViewModel::toggleSort(int key)
{
    const int wanted = (key == SortByName || key == SortByModified) ? key : SortBySize;
    // The same head flips the direction, another head takes over and starts
    // in its own natural direction.
    m_ascending = (m_sortKey == wanted) ? !m_ascending : (wanted == SortByName);
    m_sortKey = wanted;
    rebuildRows();
    emit listChanged();
}

void DiskViewModel::setSortKey(int key)
{
    const int wanted = (key == SortByName || key == SortByModified) ? key : SortBySize;
    if (m_sortKey == wanted) return;
    m_sortKey = wanted;
    rebuildRows();
    emit listChanged();
}

void DiskViewModel::setAscending(bool ascending)
{
    if (m_ascending == ascending) return;
    m_ascending = ascending;
    rebuildRows();
    emit listChanged();
}

void DiskViewModel::setFilter(const QString& filter)
{
    if (m_filter == filter) return;
    m_filter = filter;
    rebuildRows();
    emit listChanged();
}

void DiskViewModel::setViewMode(int mode)
{
    const int wanted = (mode == FileView) ? FileView : TreeView;
    if (m_viewMode == wanted) return;
    m_viewMode = wanted;
    rebuildRows();
    emit listChanged();
}

void DiskViewModel::setFuzzyMatch(bool enabled)
{
    if (m_fuzzyMatch == enabled) return;
    m_fuzzyMatch = enabled;
    rebuildRows();
    emit listChanged();
}

void DiskViewModel::setWholeTree(bool enabled)
{
    if (m_wholeTree == enabled) return;
    m_wholeTree = enabled;
    rebuildRows();
    emit listChanged();
}

void DiskViewModel::toggleExpanded(const QString& id)
{
    const int i = indexForId(id);
    if (i < 0 || !m_nodes[i].directory) return;
    if (m_expanded.contains(i)) m_expanded.remove(i);
    else m_expanded.insert(i);
    rebuildRows();
    emit listChanged();
}

bool DiskViewModel::isExpanded(const QString& id) const
{
    return m_expanded.contains(indexForId(id));
}

bool DiskViewModel::revealNode(const QString& id)
{
    const int index = indexForId(id);
    if (index < 0) return false;
    const int parent = m_nodes[index].parent;
    if (parent < 0) return false;
    // Every folder between the view root and the row has to be open for the
    // row to be visible at all.
    for (int a = parent; a >= 0; a = m_nodes[a].parent) {
        if (m_nodes[a].directory) m_expanded.insert(a);
    }
    if (m_current != parent) { pushHistory(parent); resetAt(parent); }
    if (m_viewMode != TreeView) { m_viewMode = TreeView; rebuildRows(); }
    m_selectedId = m_nodes[index].id;
    emit listChanged();
    emit selectedChanged();
    return true;
}

QVariantList DiskViewModel::extensions() const
{
    struct Agg { quint64 size = 0, alloc = 0; int files = 0; };
    QHash<QString, Agg> totals;
    if (m_current >= 0) {
        QVector<int> files;
        collectFiles(m_current, files);
        for (int index : files) {
            const Entry& e = m_nodes[index];
            Agg& agg = totals[e.ext];
            agg.size += e.size;
            agg.alloc += e.alloc;
            agg.files += 1;
        }
    }
    QVector<QPair<QString, Agg>> sorted;
    sorted.reserve(totals.size());
    for (auto it = totals.cbegin(); it != totals.cend(); ++it) sorted.push_back({it.key(), it.value()});
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.second.size > b.second.size;
    });
    const double total = m_current >= 0 ? std::max<quint64>(1, m_nodes[m_current].size) : 1;
    QVariantList rows;
    for (const auto& row : sorted) {
        rows.push_back(QVariantMap{
            {"ext", row.first},
            {"label", diskviewqml::extensionLabel(row.first)},
            {"size", double(row.second.size)},
            {"sizeText", formatSize(static_cast<qint64>(row.second.size))},
            {"allocText", formatSize(static_cast<qint64>(row.second.alloc))},
            {"files", row.second.files},
            {"percent", 100.0 * static_cast<double>(row.second.size) / total},
            {"color", diskviewqml::extensionColor(row.first).name()},
        });
    }
    return rows;
}

QVariantMap DiskViewModel::chartTree() const
{
    if (m_current < 0) return {};
    // The chart draws at most a few levels, and only the largest children of a
    // folder get a visible tile anyway; handing it the whole subtree copied
    // hundreds of thousands of nodes on every navigation.
    constexpr int kChartLevels = 3;
    constexpr int kChartChildren = 256;
    const auto exportNode = [&](auto&& self, int index, int level) -> QVariantMap {
        const Entry& e = m_nodes[index];
        QVariantList children;
        if (level < kChartLevels) {
            const int count = std::min<int>(e.children.size(), kChartChildren);
            for (int i = 0; i < count; ++i) children.push_back(self(self, e.children[i], level + 1));
        }
        return {{"id", e.id}, {"name", e.name}, {"size", double(e.size)}, {"directory", e.directory}, {"children", children}};
    };
    return exportNode(exportNode, m_current, 0);
}
