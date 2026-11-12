#include "DiskViewModel.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <algorithm>
#include <queue>

namespace {
// The native plugin keeps the thousand largest files of the current subtree.
constexpr int kMaxLargest = 1000;
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
    m_nodes.clear(); m_visible.clear(); m_largest.clear(); m_history.clear();
    m_selectedId.clear(); m_filter.clear();
    m_historyPos = -1;
    appendNode(tree, -1, {});
    m_current = 0;
    pushHistory(0);
    rebuildLargest();
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
    m_nodes.clear(); m_visible.clear(); m_largest.clear(); m_history.clear();
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
    e.name=node.value("name").toString(); e.size=static_cast<quint64>(node.value("size").toDouble());
    e.directory=node.value("dir").toBool(); e.parent=parent;
    e.path=path.isEmpty()?e.name:(path.endsWith('\\')?path+e.name:path+"\\"+e.name);
    e.id=QString::number(m_nodes.size()+1);
    const int index=m_nodes.size();m_nodes.push_back(e);
    if(parent>=0)m_nodes[parent].children.push_back(index);
    for(const auto& child:node.value("children").toList()) if(child.canConvert<QVariantMap>()) appendNode(child.toMap(),index,e.path);
    return index;
}

QString DiskViewModel::formatSize(qint64 bytes) const
{
    // The native plugin reads sizes in 1024 steps and drops the decimal once
    // the number is large enough not to need it.
    static const char* units[]={"B","KB","MB","GB","TB","PB"};
    double n=std::max<qint64>(0,bytes);int u=0;
    while(n>=1024.0&&u<5){n/=1024.0;++u;}
    if(u==0) return QString::number(qint64(n))+" "+units[u];
    return QString::number(n,'f',n>=100.0?0:1)+" "+units[u];
}

QString DiskViewModel::formatCount(qint64 count) const
{
    QString digits=QString::number(std::max<qint64>(0,count));
    for(int i=digits.size()-3;i>0;i-=3) digits.insert(i,QChar(','));
    return digits;
}
QString DiskViewModel::currentName() const { return m_current>=0?m_nodes[m_current].name:QString(); }
QString DiskViewModel::currentPath() const { return m_current>=0?m_nodes[m_current].path:QString(); }
QString DiskViewModel::totalText() const { return m_current>=0?formatSize(m_nodes[m_current].size):QString(); }
QString DiskViewModel::breadcrumbText() const { return currentPath().replace('\\',"  /  "); }

QVariantList DiskViewModel::breadcrumb() const
{
    QVariantList crumbs;
    if (m_current<0) return crumbs;
    QVector<int> chain;
    for (int index=m_current;index>=0;index=m_nodes[index].parent) chain.push_back(index);
    for (int i=chain.size()-1;i>=0;--i) {
        const Entry& e=m_nodes[chain[i]];
        crumbs.push_back(QVariantMap{{"name",e.name},{"id",e.id},{"current",chain[i]==m_current}});
    }
    return crumbs;
}

int DiskViewModel::selectedRow() const
{
    for (int row=0;row<m_visible.size();++row) {
        if (m_nodes[m_visible[row]].id==m_selectedId) return row;
    }
    return -1;
}

int DiskViewModel::rowCount(const QModelIndex& parent) const { return parent.isValid()?0:m_visible.size(); }
QVariant DiskViewModel::data(const QModelIndex& index,int role) const
{
    if(!index.isValid()||index.row()<0||index.row()>=m_visible.size())return {};
    const Entry& e=m_nodes[m_visible[index.row()]];
    const double total=m_current>=0?std::max<quint64>(1,m_nodes[m_current].size):1;
    switch(role){case IdRole:return e.id;case NameRole:return e.name;case SizeRole:return QVariant::fromValue<qulonglong>(e.size);case SizeTextRole:return formatSize(e.size);case PercentRole:return 100.0*e.size/total;case DirectoryRole:return e.directory;case PathRole:return e.path;default:return {};}
}
QHash<int,QByteArray> DiskViewModel::roleNames()const{return {{IdRole,"nodeId"},{NameRole,"name"},{SizeRole,"size"},{SizeTextRole,"sizeText"},{PercentRole,"percent"},{DirectoryRole,"directory"},{PathRole,"path"}};}
int DiskViewModel::indexForId(const QString& id)const{for(int i=0;i<m_nodes.size();++i)if(m_nodes[i].id==id)return i;return -1;}
void DiskViewModel::rebuildRows()
{
    m_visible.clear();
    if (m_current<0) return;
    const QVector<int>& base = (m_listMode==ListLargest) ? m_largest : m_nodes[m_current].children;
    for (int index : base) {
        if (m_filter.isEmpty() || m_nodes[index].path.contains(m_filter, Qt::CaseInsensitive)) {
            m_visible.push_back(index);
        }
    }
    const bool byName = m_sortKey==SortByName;
    std::stable_sort(m_visible.begin(),m_visible.end(),[&](int a,int b){
        const Entry& x=m_nodes[a];
        const Entry& y=m_nodes[b];
        if (byName) {
            const int order=QString::compare(x.name,y.name,Qt::CaseInsensitive);
            if (order!=0) return m_ascending?order<0:order>0;
            return false;
        }
        if (x.size!=y.size) return m_ascending?x.size<y.size:x.size>y.size;
        return false;
    });
}

void DiskViewModel::rebuildLargest()
{
    m_largest.clear();
    if (m_current<0) return;
    // A min-heap of the biggest files of the subtree: the smallest of them is
    // the one that gets dropped when a bigger one shows up.
    const auto smaller=[this](int a,int b){ return m_nodes[a].size>m_nodes[b].size; };
    std::priority_queue<int,QVector<int>,decltype(smaller)> heap(smaller);
    QVector<int> stack{m_current};
    while (!stack.isEmpty()) {
        const int index=stack.takeLast();
        for (int child : m_nodes[index].children) {
            const Entry& e=m_nodes[child];
            if (e.directory) { stack.push_back(child); continue; }
            if (static_cast<int>(heap.size())<kMaxLargest) { heap.push(child); continue; }
            if (e.size>m_nodes[heap.top()].size) { heap.pop(); heap.push(child); }
        }
    }
    m_largest.resize(static_cast<int>(heap.size()));
    for (int i=m_largest.size()-1;i>=0;--i) { m_largest[i]=heap.top(); heap.pop(); }
}

void DiskViewModel::pushHistory(int index)
{
    if (m_historyPos>=0 && m_historyPos<m_history.size() && m_history[m_historyPos]==index) return;
    m_history.resize(m_historyPos+1);
    m_history.push_back(index);
    m_historyPos=m_history.size()-1;
}

void DiskViewModel::resetAt(int index)
{
    if(index<0||index>=m_nodes.size())return;
    beginResetModel();
    m_current=index;
    // Navigating clears the filter, the way the native panel does.
    m_filter.clear();
    m_selectedId.clear();
    rebuildLargest();
    rebuildRows();
    endResetModel();
    emit currentPathChanged();
    emit selectedChanged();
    emit listChanged();
}
void DiskViewModel::navigate(const QString& id){const int i=indexForId(id);if(i>=0&&m_nodes[i].directory){pushHistory(i);resetAt(i);}}
void DiskViewModel::goUp(){if(m_current>0){const int leaving=m_current;const int parent=m_nodes[leaving].parent;pushHistory(parent);resetAt(parent);m_selectedId=m_nodes[leaving].id;emit selectedChanged();}}
void DiskViewModel::goBack(){if(!canGoBack())return;--m_historyPos;resetAt(m_history[m_historyPos]);}
void DiskViewModel::goForward(){if(!canGoForward())return;++m_historyPos;resetAt(m_history[m_historyPos]);}
void DiskViewModel::select(const QString& id){if(m_selectedId==id)return;m_selectedId=id;emit selectedChanged();}

void DiskViewModel::moveSelection(int delta)
{
    if (m_visible.isEmpty()) return;
    int row=selectedRow();
    if (row<0) row = delta>0 ? -1 : m_visible.size();
    row+=delta;
    row=std::clamp<int>(row,0,m_visible.size()-1);
    m_selectedId=m_nodes[m_visible[row]].id;
    emit selectedChanged();
}

void DiskViewModel::openSelected()
{
    const int i=indexForId(m_selectedId);
    if (i>=0&&m_nodes[i].directory) { pushHistory(i); resetAt(i); }
}

void DiskViewModel::toggleSort(int key)
{
    const int wanted=(key==SortByName)?SortByName:SortBySize;
    // The same head flips the direction, another head takes over and starts
    // in its own natural direction.
    m_ascending = (m_sortKey==wanted) ? !m_ascending : (wanted==SortByName);
    m_sortKey = wanted;
    rebuildRows();
    emit listChanged();
}

void DiskViewModel::setSortKey(int key)
{
    const int wanted=(key==SortByName)?SortByName:SortBySize;
    if (m_sortKey==wanted) return;
    m_sortKey=wanted;
    rebuildRows();
    emit listChanged();
}

void DiskViewModel::setAscending(bool ascending)
{
    if (m_ascending==ascending) return;
    m_ascending=ascending;
    rebuildRows();
    emit listChanged();
}

void DiskViewModel::setFilter(const QString& filter)
{
    if (m_filter==filter) return;
    m_filter=filter;
    rebuildRows();
    emit listChanged();
}

void DiskViewModel::setListMode(int mode)
{
    const int wanted=(mode==ListLargest)?ListLargest:ListCurrent;
    if (m_listMode==wanted) return;
    m_listMode=wanted;
    rebuildRows();
    emit listChanged();
}

bool DiskViewModel::revealNode(const QString& id)
{
    const int index=indexForId(id);
    if(index<0) return false;
    const int parent=m_nodes[index].parent;
    if(parent<0) return false;
    if(m_current!=parent){ pushHistory(parent); resetAt(parent); }
    // The row only has to be in sight, so the largest-files ranking gives way
    // to the folder that holds it.
    if(m_listMode!=ListCurrent){ m_listMode=ListCurrent; rebuildRows(); }
    m_selectedId=m_nodes[index].id;
    emit listChanged();
    emit selectedChanged();
    return true;
}
QVariantMap DiskViewModel::chartTree() const
{
    if(m_current<0)return {};
    const auto exportNode=[&](auto&& self,int index)->QVariantMap {
        const Entry& e=m_nodes[index]; QVariantList children; for(int child:e.children) children.push_back(self(self,child));
        return {{"id",e.id},{"name",e.name},{"size",double(e.size)},{"directory",e.directory},{"children",children}};
    };
    return exportNode(exportNode,m_current);
}
