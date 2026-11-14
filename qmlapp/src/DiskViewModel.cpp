#include "DiskViewModel.h"

#include "ExtensionColors.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <algorithm>
#include <new>

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

DiskViewModel::DiskViewModel(QObject* parent) : QAbstractListModel(parent)
{
    // The chart's payload follows the folder the window is in; one signal for
    // both keeps the QML binding simple.
    connect(this, &DiskViewModel::currentPathChanged, this, &DiskViewModel::chartChanged);
}

void DiskViewModel::setChartDepth(int depth)
{
    const int clamped = std::clamp(depth, 1, 5);
    if (clamped == m_chartDepth) return;
    m_chartDepth = clamped;
    emit chartChanged();
}

bool DiskViewModel::loadFixture(const QString& fileName)
{
    const QString localName = fileName.startsWith("file:") ? QUrl(fileName).toLocalFile() : fileName;
    QFile file(localName);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return false;
    return loadTree(doc.object().toVariantMap());
}

void DiskViewModel::resetStorage()
{
    m_tree = FlatTree {};
    m_childStart.clear();
    m_childCount.clear();
    m_children.clear();
    m_visible.clear();
    m_history.clear();
    m_expanded.clear();
    m_selectedId.clear();
    m_filter.clear();
    m_extensionFilter.clear();
    m_extensionFilterOn = false;
    m_current = -1;
    m_chartRoot = -1;
    m_historyPos = -1;
}

void DiskViewModel::buildChildren()
{
    const int count = nodeCount();
    m_childStart.fill(-1, count);
    m_childCount.fill(0, count);
    m_children.clear();
    if (count <= 1) return;
    // One entry per node that is not the root: the children always sit after
    // their parent, so a single forward pass is enough, and a parent pointer
    // that points forward is a broken tree rather than something to follow.
    for (int i = 1; i < count; ++i) {
        int& parent = m_tree.parents[i];
        if (parent < 0 || parent >= i) parent = -1;
        else ++m_childCount[parent];
    }
    m_children.resize(count - 1);
    int running = 0;
    for (int i = 0; i < count; ++i) {
        if (m_childCount.at(i) == 0) continue;
        m_childStart[i] = running;
        running += m_childCount.at(i);
    }
    QVector<int> cursor = m_childStart;
    for (int i = 1; i < count; ++i) {
        const int parent = m_tree.parents.at(i);
        if (parent < 0) continue;
        m_children[cursor[parent]++] = i;
    }
}

bool DiskViewModel::loadFlat(FlatTree tree, bool partial)
{
    const int count = tree.names.size();
    // The scan builds all of the arrays in one pass, so they agree with each
    // other; a tree that does not would be read out of bounds later.
    if (count == 0 || tree.parents.size() != count || tree.sizes.size() != count
        || tree.allocs.size() != count || tree.modified.size() != count
        || tree.attributes.size() != count || tree.fileCounts.size() != count
        || tree.dirCounts.size() != count || tree.flags.size() != count) {
        return false;
    }
    bool loaded = false;
    beginResetModel();
    try {
        // The arrays are taken over as they are: the model keeps the scan's
        // names, sizes and parents instead of copying them into per-node
        // structs. That is one tree in memory, not two.
        resetStorage();
        m_tree = std::move(tree);
        buildChildren();
        m_current = count > 0 ? 0 : -1;
        m_chartRoot = m_current;
        pushHistory(m_current);
        rebuildRows();
        loaded = true;
    } catch (const std::bad_alloc&) {
        // The host caps this process's memory. A tree that does not fit leaves
        // an empty window and a reason, not a dead process.
        resetStorage();
    }
    endResetModel();
    if (!loaded) {
        setLoadError(QStringLiteral("内存不足，无法显示这棵树（%1 个节点）").arg(count));
        emit currentPathChanged();
        emit selectedChanged();
        emit listChanged();
        return false;
    }
    setLoadError(QString());
    setPartial(partial);
    emit currentPathChanged();
    emit selectedChanged();
    emit listChanged();
    return true;
}

bool DiskViewModel::loadTree(const QVariantMap& tree, bool partial)
{
    if (tree.isEmpty()) return false;
    bool loaded = false;
    beginResetModel();
    try {
        resetStorage();
        appendNode(tree, -1);
        buildChildren();
        countItems();
        m_current = nodeCount() > 0 ? 0 : -1;
        m_chartRoot = m_current;
        pushHistory(m_current);
        rebuildRows();
        loaded = true;
    } catch (const std::bad_alloc&) {
        resetStorage();
    }
    endResetModel();
    if (!loaded) {
        setLoadError(QStringLiteral("内存不足，无法显示这棵树"));
        emit currentPathChanged();
        emit selectedChanged();
        emit listChanged();
        return false;
    }
    setLoadError(QString());
    setPartial(partial);
    emit currentPathChanged();
    emit selectedChanged();
    emit listChanged();
    return true;
}

void DiskViewModel::clear()
{
    beginResetModel();
    resetStorage();
    endResetModel();
    setLoadError(QString());
    setPartial(false);
    emit currentPathChanged();
    emit selectedChanged();
    emit listChanged();
}

void DiskViewModel::setLoadError(const QString& message)
{
    if (m_loadError == message) return;
    m_loadError = message;
    emit loadErrorChanged();
}

void DiskViewModel::setPartial(bool partial)
{
    if (m_partial == partial) return;
    m_partial = partial;
    emit partialChanged();
}

int DiskViewModel::appendNode(const QVariantMap& node, int parent)
{
    const QString name = node.value("name").toString();
    const QString stamp = node.value("modified").toString();
    bool hasModified = false;
    const quint64 modified = stamp.isEmpty() ? 0 : stamp.toULongLong(&hasModified);
    const int index = m_tree.names.size();
    m_tree.names.append(name);
    m_tree.parents.append(parent);
    m_tree.sizes.append(static_cast<qint64>(node.value("size").toDouble()));
    // The scan reports the space actually taken; the fixture does not, and
    // allocOf() falls back to the cluster size in that case.
    m_tree.allocs.append(node.contains("alloc") ? static_cast<qint64>(node.value("alloc").toDouble()) : 0);
    m_tree.modified.append(static_cast<qint64>(modified));
    m_tree.attributes.append(node.value("attrs").toUInt());
    // The scan knows the entry counts; the fixture does not, and countItems()
    // sums them up afterwards.
    m_tree.fileCounts.append(static_cast<quint32>(node.value("files").toDouble()));
    m_tree.dirCounts.append(static_cast<quint32>(node.value("dirs").toDouble()));
    const bool directory = node.value("dir").toBool();
    m_tree.flags.append(static_cast<quint8>((directory ? 1 : 0) | (hasModified ? 2 : 0)));
    for (const auto& child : node.value("children").toList()) {
        if (child.canConvert<QVariantMap>()) appendNode(child.toMap(), index);
    }
    return index;
}

void DiskViewModel::countItems()
{
    // Children always sit after their parent, so a backward walk has every
    // child's totals ready when the parent asks for them.
    for (int i = nodeCount() - 1; i >= 0; --i) {
        if (!directoryOf(i)) continue;
        quint64 files = 0, dirs = 0;
        for (int position = 0; position < childCountOf(i); ++position) {
            const int child = childAt(i, position);
            if (directoryOf(child)) {
                dirs += 1 + m_tree.dirCounts.at(child);
                files += m_tree.fileCounts.at(child);
            } else {
                ++files;
            }
        }
        m_tree.fileCounts[i] = static_cast<quint32>(files);
        m_tree.dirCounts[i] = static_cast<quint32>(dirs);
    }
}

int DiskViewModel::nodeItems() const
{
    if (m_chartRoot < 0) return 0;
    return static_cast<int>(m_tree.fileCounts.at(m_chartRoot) + m_tree.dirCounts.at(m_chartRoot));
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

QString DiskViewModel::currentName() const { return m_chartRoot >= 0 ? nameOf(m_chartRoot) : QString(); }
QString DiskViewModel::currentPath() const { return pathOf(m_chartRoot); }
QString DiskViewModel::totalText() const { return m_chartRoot >= 0 ? formatSize(static_cast<qint64>(sizeOf(m_chartRoot))) : QString(); }
QString DiskViewModel::selectedExtension() const
{
    const int index = indexForId(m_selectedId);
    return index >= 0 ? extOf(index) : QString();
}
QString DiskViewModel::breadcrumbText() const { return currentPath().replace('\\', "  /  "); }

QVariantList DiskViewModel::breadcrumb() const
{
    QVariantList crumbs;
    if (m_chartRoot < 0) return crumbs;
    QVector<int> chain;
    for (int index = m_chartRoot; index >= 0; index = parentOf(index)) chain.push_back(index);
    for (int i = chain.size() - 1; i >= 0; --i) {
        crumbs.push_back(QVariantMap{{"name", nameOf(chain[i])}, {"id", QString::number(chain[i] + 1)}, {"current", chain[i] == m_chartRoot}});
    }
    return crumbs;
}

int DiskViewModel::selectedRow() const
{
    for (int row = 0; row < m_visible.size(); ++row) {
        if (QString::number(m_visible[row] + 1) == m_selectedId) return row;
    }
    return -1;
}

int DiskViewModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : m_visible.size(); }

QVariant DiskViewModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size()) return {};
    const int nodeIndex = m_visible.at(index.row());
    const int parent = parentOf(nodeIndex);
    const quint64 size = sizeOf(nodeIndex);
    const quint64 parentTotal = parent >= 0 ? std::max<quint64>(1, sizeOf(parent)) : 1;
    const bool directory = directoryOf(nodeIndex);
    switch (role) {
    case IdRole: return QString::number(nodeIndex + 1);
    case NameRole: return nameOf(nodeIndex);
    case SizeRole: return QVariant::fromValue<qulonglong>(size);
    case SizeTextRole: return formatSize(static_cast<qint64>(size));
    case PercentRole: return 100.0 * static_cast<double>(size) / static_cast<double>(parentTotal);
    case DirectoryRole: return directory;
    case PathRole: return pathOf(nodeIndex);
    case DepthRole: return depthOf(nodeIndex);
    case HasChildrenRole: return directory && childCountOf(nodeIndex) > 0;
    case ExpandedRole: return m_expanded.contains(nodeIndex);
    case AllocTextRole: return formatSize(static_cast<qint64>(allocOf(nodeIndex)));
    case ExtRole: return extOf(nodeIndex);
    case ExtColorRole: return directory ? QStringLiteral("#8f9aa8") : diskviewqml::extensionColor(extOf(nodeIndex)).name();
    case ItemsRole: return static_cast<int>(m_tree.fileCounts.at(nodeIndex) + m_tree.dirCounts.at(nodeIndex));
    case FilesRole: return static_cast<int>(m_tree.fileCounts.at(nodeIndex));
    case DirsRole: return static_cast<int>(m_tree.dirCounts.at(nodeIndex));
    case ModifiedTextRole: return hasModifiedOf(nodeIndex) ? modifiedText(modifiedOf(nodeIndex)) : QStringLiteral("—");
    case ModifiedValueRole: return QVariant::fromValue<qulonglong>(hasModifiedOf(nodeIndex) ? modifiedOf(nodeIndex) : 0);
    case AttributesRole: return attributesText(attributesOf(nodeIndex));
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
    // The id is the position plus one, so it is read instead of matched
    // against a stored string: one less allocation per node.
    bool ok = false;
    const int value = id.toInt(&ok);
    if (!ok) return -1;
    const int index = value - 1;
    return index >= 0 && index < nodeCount() ? index : -1;
}

int DiskViewModel::depthOf(int index) const
{
    int depth = 0;
    for (int p = parentOf(index); p >= 0 && p != m_current; p = parentOf(p)) ++depth;
    return depth;
}

quint64 DiskViewModel::allocOf(int index) const
{
    const qint64 alloc = m_tree.allocs.at(index);
    return alloc > 0 ? static_cast<quint64>(alloc) : allocatedFor(sizeOf(index));
}

QVector<int> DiskViewModel::childrenOf(int index) const
{
    const int count = childCountOf(index);
    if (count <= 0) return {};
    const int start = m_childStart.at(index);
    return QVector<int>(m_children.constData() + start, m_children.constData() + start + count);
}

void DiskViewModel::sortEntries(QVector<int>& entries) const
{
    std::stable_sort(entries.begin(), entries.end(), [&](int a, int b) {
        int order = 0;
        switch (m_sortKey) {
        case SortByName:
            order = QString::compare(nameOf(a), nameOf(b), Qt::CaseInsensitive);
            break;
        case SortByModified: {
            const quint64 x = hasModifiedOf(a) ? modifiedOf(a) : 0;
            const quint64 y = hasModifiedOf(b) ? modifiedOf(b) : 0;
            if (x != y) order = x < y ? -1 : 1;
            break;
        }
        default: {
            const quint64 x = sizeOf(a);
            const quint64 y = sizeOf(b);
            if (x != y) order = x < y ? -1 : 1;
            break;
        }
        }
        return m_ascending ? order < 0 : order > 0;
    });
}

void DiskViewModel::collectFiles(int node, QVector<int>& out) const
{
    if (node < 0 || node >= nodeCount()) return;
    QVector<int> stack{node};
    while (!stack.isEmpty()) {
        const int index = stack.takeLast();
        const int count = childCountOf(index);
        for (int position = 0; position < count; ++position) {
            const int child = childAt(index, position);
            if (directoryOf(child)) stack.push_back(child);
            else out.push_back(child);
        }
    }
}

QString DiskViewModel::pathOf(int index) const
{
    if (index < 0 || index >= nodeCount()) return {};
    QVector<int> chain;
    for (int i = index; i >= 0; i = parentOf(i)) chain.push_back(i);
    QString path;
    for (int i = chain.size() - 1; i >= 0; --i) {
        const QString& name = nameOf(chain[i]);
        if (path.isEmpty()) path = name;
        else if (path.endsWith('\\')) path += name;
        else path += '\\' + name;
    }
    return path;
}

QString DiskViewModel::extOf(int index) const
{
    return directoryOf(index) ? QString() : diskviewqml::extensionOf(nameOf(index));
}

QSet<int> DiskViewModel::filterKeepSet() const
{
    QSet<int> keep;
    if (m_filter.isEmpty() || nodeCount() == 0) return keep;
    // A match keeps its whole path visible, otherwise the row would float
    // without the folder it lives in. The path lives in one string that grows
    // and shrinks with the walk, so the filter costs no memory of its own.
    QString path;
    filterWalk(0, path, keep);
    return keep;
}

void DiskViewModel::filterWalk(int index, QString& path, QSet<int>& keep) const
{
    const int mark = path.size();
    if (!path.isEmpty() && !path.endsWith('\\')) path += '\\';
    path += nameOf(index);
    if (path.contains(m_filter, Qt::CaseInsensitive)) {
        for (int a = index; a >= 0; a = parentOf(a)) keep.insert(a);
    }
    const int count = childCountOf(index);
    for (int position = 0; position < count; ++position) filterWalk(childAt(index, position), path, keep);
    path.truncate(mark);
}

void DiskViewModel::collectFilesFiltered(int node, QString& path, QVector<int>& out) const
{
    const int mark = path.size();
    if (!path.isEmpty() && !path.endsWith('\\')) path += '\\';
    path += nameOf(node);
    if (!directoryOf(node) && path.contains(m_filter, Qt::CaseInsensitive)) out.push_back(node);
    const int count = childCountOf(node);
    for (int position = 0; position < count; ++position) collectFilesFiltered(childAt(node, position), path, out);
    path.truncate(mark);
}

void DiskViewModel::appendTreeRows(int parent, const QSet<int>& keep, bool filtering)
{
    QVector<int> kids = childrenOf(parent);
    sortEntries(kids);
    for (int child : kids) {
        if (filtering && !keep.contains(child)) continue;
        m_visible.push_back(child);
        // While a filter is on, the path to every match is shown open; the
        // folders the user opened by hand are left exactly as they were.
        const bool open = m_expanded.contains(child) || filtering;
        if (directoryOf(child) && open) {
            appendTreeRows(child, keep, filtering);
        }
    }
}

void DiskViewModel::applyRowChange()
{
    // A reset, not a dataChanged: the rows themselves are different now, and
    // the view has to ask for the new count. The row set of one folder is
    // small, so the cost of a full reset is not worth finer signals here.
    beginResetModel();
    rebuildRows();
    endResetModel();
    emit listChanged();
}

void DiskViewModel::rebuildRows()
{    m_visible.clear();
    if (m_current < 0) return;
    if (m_viewMode == FileView) {
        QVector<int> files;
        if (!m_filter.isEmpty() && !m_fuzzyMatch) {
            // The walk carries the full path down instead of deriving it per
            // file; it starts from the row above the view root so the filter
            // still sees where the current folder sits.
            QString path = pathOf(parentOf(m_chartRoot));
            collectFilesFiltered(m_chartRoot, path, files);
        } else {
            collectFiles(m_wholeTree ? 0 : m_chartRoot, files);
        }
        if (!m_filter.isEmpty() && m_fuzzyMatch) {
            // The whole-tree search only makes sense on the name.
            files.erase(std::remove_if(files.begin(), files.end(), [this](int i) {
                return !fuzzyContains(nameOf(i), m_filter);
            }), files.end());
        }
        // The legend's pick narrows the rows to its own extension; "(无)"
        // keeps the files that have none.
        if (m_extensionFilterOn) {
            files.erase(std::remove_if(files.begin(), files.end(), [this](int i) {
                return extOf(i) != m_extensionFilter;
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
    if (index < 0) return;
    if (m_historyPos >= 0 && m_historyPos < m_history.size() && m_history[m_historyPos] == index) return;
    m_history.resize(m_historyPos + 1);
    m_history.push_back(index);
    m_historyPos = m_history.size() - 1;
}

void DiskViewModel::resetAt(int index)
{
    if (index < 0 || index >= nodeCount()) return;
    beginResetModel();
    // The chart is what moves; the list only opens down to it, so the outline
    // the user built stays where it is.
    m_chartRoot = index;
    for (int a = index; a >= 0 && a != m_current; a = parentOf(a)) {
        if (directoryOf(a)) m_expanded.insert(a);
    }
    // Navigating clears the filter, the way the native panel does — and the
    // extension pick with it, since the folder the chart was on is left
    // behind. The fields are cleared directly: the row rebuild happens in
    // this function's own reset, so the filter's helper must not nest one.
    const bool hadExtensionFilter = m_extensionFilterOn;
    m_filter.clear();
    m_extensionFilter.clear();
    m_extensionFilterOn = false;
    m_selectedId = QString::number(index + 1);
    rebuildRows();
    endResetModel();
    if (hadExtensionFilter) emit extensionFilterChanged();
    emit currentPathChanged();
    emit selectedChanged();
    emit listChanged();
}

void DiskViewModel::navigate(const QString& id)
{
    const int i = indexForId(id);
    if (i >= 0 && directoryOf(i)) { pushHistory(i); resetAt(i); }
}

void DiskViewModel::goUp()
{
    if (m_chartRoot <= 0) return;
    const int leaving = m_chartRoot;
    const int parent = parentOf(leaving);
    if (parent < 0) return;
    pushHistory(parent);
    resetAt(parent);
    m_selectedId = QString::number(leaving + 1);
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

void DiskViewModel::enterNode(const QString& id)
{
    const int index = indexForId(id);
    if (index < 0) return;
    if (directoryOf(index)) navigate(id);
    else select(id);
}

void DiskViewModel::resetChart()
{
    if (m_current < 0 || m_chartRoot == m_current) return;
    // The chart walks back to the folder the tree is rooted at; the outline
    // keeps its shape. The chart's own zoom and pan are the view's business
    // and the QML side resets them with resetView().
    m_chartRoot = m_current;
    emit currentPathChanged();
    emit chartChanged();
}

void DiskViewModel::selectExtension(const QString& extension)
{
    if (m_extensionFilterOn && m_extensionFilter == extension) {
        clearExtensionFilter();
        return;
    }
    m_extensionFilter = extension;
    m_extensionFilterOn = true;
    // The file view's rows are the ones the filter changes; the tree keeps
    // its outline. A reset is how this model reshapes its rows.
    applyRowChange();
    emit extensionFilterChanged();
}

void DiskViewModel::clearExtensionFilter()
{
    if (!m_extensionFilterOn) return;
    m_extensionFilter.clear();
    m_extensionFilterOn = false;
    applyRowChange();
    emit extensionFilterChanged();
}

void DiskViewModel::moveSelection(int delta)
{
    if (m_visible.isEmpty()) return;
    int row = selectedRow();
    if (row < 0) row = delta > 0 ? -1 : m_visible.size();
    row += delta;
    row = std::clamp<int>(row, 0, m_visible.size() - 1);
    m_selectedId = QString::number(m_visible[row] + 1);
    emit selectedChanged();
}

void DiskViewModel::openSelected()
{
    const int i = indexForId(m_selectedId);
    if (i >= 0 && directoryOf(i)) { pushHistory(i); resetAt(i); }
}

void DiskViewModel::toggleSort(int key)
{
    const int wanted = (key == SortByName || key == SortByModified) ? key : SortBySize;
    // The same head flips the direction, another head takes over and starts
    // in its own natural direction.
    m_ascending = (m_sortKey == wanted) ? !m_ascending : (wanted == SortByName);
    m_sortKey = wanted;
    applyRowChange();
}

void DiskViewModel::setSortKey(int key)
{
    const int wanted = (key == SortByName || key == SortByModified) ? key : SortBySize;
    if (m_sortKey == wanted) return;
    m_sortKey = wanted;
    applyRowChange();
}

void DiskViewModel::setAscending(bool ascending)
{
    if (m_ascending == ascending) return;
    m_ascending = ascending;
    applyRowChange();
}

void DiskViewModel::setFilter(const QString& filter)
{
    if (m_filter == filter) return;
    m_filter = filter;
    applyRowChange();
}

void DiskViewModel::setViewMode(int mode)
{
    const int wanted = (mode == FileView) ? FileView : TreeView;
    if (m_viewMode == wanted) return;
    m_viewMode = wanted;
    applyRowChange();
}

void DiskViewModel::setFuzzyMatch(bool enabled)
{
    if (m_fuzzyMatch == enabled) return;
    m_fuzzyMatch = enabled;
    applyRowChange();
}

void DiskViewModel::setWholeTree(bool enabled)
{
    if (m_wholeTree == enabled) return;
    m_wholeTree = enabled;
    applyRowChange();
}

void DiskViewModel::toggleExpanded(const QString& id)
{
    const int i = indexForId(id);
    if (i < 0 || !directoryOf(i)) return;
    if (m_expanded.contains(i)) m_expanded.remove(i);
    else m_expanded.insert(i);
    applyRowChange();
}

bool DiskViewModel::isExpanded(const QString& id) const
{
    return m_expanded.contains(indexForId(id));
}

bool DiskViewModel::expandNode(const QString& id)
{
    const int index = indexForId(id);
    if (index < 0 || !directoryOf(index)) return false;
    // The chart draws the subtree of the folder the window is in, so the node
    // is nearly always inside it. When it is not, there is no row to open and
    // entering the folder is the closest thing to what was asked for.
    bool inside = false;
    for (int a = index; a >= 0; a = parentOf(a)) {
        if (a == m_chartRoot) { inside = true; break; }
    }
    // The chart only exports the largest few children at each level. A
    // directory deeper in the tree may not be materialized, or may belong to
    // a branch the chart omitted; don't silently substitute a navigation.
    if (!inside) return false;
    QVector<int> route;
    for (int a = index; a != m_chartRoot; a = parentOf(a)) {
        if (a < 0) return false;
        route.push_back(a);
    }
    if (route.size() > m_chartDepth) return false;
    std::reverse(route.begin(), route.end());
    int parent = m_chartRoot;
    for (int child : route) {
        QVector<int> visible = childrenOf(parent);
        constexpr int kChartChildren = 256;
        if (visible.size() > kChartChildren) {
            std::nth_element(visible.begin(), visible.begin() + kChartChildren, visible.end(),
                             [this](int a, int b) { return sizeOf(a) > sizeOf(b); });
            visible.resize(kChartChildren);
        }
        if (!visible.contains(child)) return false;
        parent = child;
    }
    // Open every folder from the view root down to the node, the node itself
    // included, so its contents show up as rows right below it.
    for (int a = index; a >= 0; a = parentOf(a)) {
        if (directoryOf(a)) m_expanded.insert(a);
        if (a == m_chartRoot) break;
    }
    if (m_viewMode != TreeView) m_viewMode = TreeView;
    m_selectedId = QString::number(index + 1);
    applyRowChange();
    emit selectedChanged();
    return true;
}

bool DiskViewModel::revealNode(const QString& id)
{
    const int index = indexForId(id);
    if (index < 0) return false;
    const int parent = parentOf(index);
    if (parent < 0) return false;
    // Every folder between the view root and the row has to be open for the
    // row to be visible at all.
    for (int a = parent; a >= 0; a = parentOf(a)) {
        if (directoryOf(a)) m_expanded.insert(a);
    }
    if (m_current != parent) { pushHistory(parent); resetAt(parent); }
    if (m_viewMode != TreeView) { m_viewMode = TreeView; applyRowChange(); }
    m_selectedId = QString::number(index + 1);
    emit listChanged();
    emit selectedChanged();
    return true;
}

QVariantList DiskViewModel::extensions() const
{
    struct Agg { quint64 size = 0, alloc = 0; int files = 0; };
    QHash<QString, Agg> totals;
    if (m_chartRoot >= 0) {
        QVector<int> files;
        collectFiles(m_chartRoot, files);
        for (int index : files) {
            Agg& agg = totals[extOf(index)];
            agg.size += sizeOf(index);
            agg.alloc += allocOf(index);
            agg.files += 1;
        }
    }
    QVector<QPair<QString, Agg>> sorted;
    sorted.reserve(totals.size());
    for (auto it = totals.cbegin(); it != totals.cend(); ++it) sorted.push_back({it.key(), it.value()});
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.second.size > b.second.size;
    });
    const double total = m_chartRoot >= 0 ? static_cast<double>(std::max<quint64>(1, sizeOf(m_chartRoot))) : 1;
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
    if (m_chartRoot < 0) return {};
    // The chart draws as many levels below the current folder as the window
    // asked for. Only the largest children of a folder get a tile anyway, and
    // the export is capped in total, so a folder with hundreds of big
    // subfolders cannot turn a double click into a memory spike.
    const int levels = std::clamp(m_chartDepth, 1, 5) + 1;
    constexpr int kChartChildren = 256;
    const int kChartNodes = 2048 * std::clamp(m_chartDepth, 1, 5);
    int budget = kChartNodes;
    const auto exportNode = [&](auto&& self, int index, int level) -> QVariantMap {
        QVariantList children;
        --budget;
        if (level + 1 < levels && budget > 0) {
            QVector<int> kids = childrenOf(index);
            if (kids.size() > kChartChildren) {
                // The chart sorts by size itself, so only the largest children
                // are worth handing over.
                std::nth_element(kids.begin(), kids.begin() + kChartChildren, kids.end(),
                                 [this](int a, int b) { return sizeOf(a) > sizeOf(b); });
                kids.resize(kChartChildren);
            }
            for (int child : kids) {
                if (budget <= 0) break;
                children.push_back(self(self, child, level + 1));
            }
        }
        return {{"id", QString::number(index + 1)}, {"name", nameOf(index)}, {"size", double(sizeOf(index))},
                {"directory", directoryOf(index)}, {"children", children}};
    };
    QElapsedTimer timer;
    timer.start();
    QVariantMap map = exportNode(exportNode, m_chartRoot, 0);
    return map;
}
