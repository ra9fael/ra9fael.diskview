#include "DiskViewModel.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <algorithm>

DiskViewModel::DiskViewModel(QObject* parent) : QAbstractListModel(parent) {}

bool DiskViewModel::loadFixture(const QString& fileName)
{
    const QString localName = fileName.startsWith("file:") ? QUrl(fileName).toLocalFile() : fileName;
    QFile file(localName);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return false;
    beginResetModel();
    m_nodes.clear(); m_visible.clear(); m_history.clear(); m_selectedId.clear();
    appendNode(doc.object(), -1, {});
    m_current = 0;
    m_history.push_back(m_current);
    const auto root = m_nodes[0];
    for (int c : root.children) m_visible.push_back(c);
    std::stable_sort(m_visible.begin(),m_visible.end(),[&](int a,int b){return m_nodes[a].size>m_nodes[b].size;});
    endResetModel();
    emit currentPathChanged();
    return true;
}

int DiskViewModel::appendNode(const QJsonObject& json, int parent, const QString& path)
{
    Entry e;
    e.name=json.value("name").toString(); e.size=static_cast<quint64>(json.value("size").toDouble());
    e.directory=json.value("dir").toBool(); e.parent=parent;
    e.path=path.isEmpty()?e.name:path+"\\"+e.name;
    e.id=QString::number(m_nodes.size()+1);
    const int index=m_nodes.size();m_nodes.push_back(e);
    if(parent>=0)m_nodes[parent].children.push_back(index);
    for(const auto& child:json.value("children").toArray()) if(child.isObject()) appendNode(child.toObject(),index,e.path);
    return index;
}

QString DiskViewModel::formatSize(qint64 bytes) const
{
    static const char* units[]={"B","KB","MB","GB","TB","PB"};
    double n=std::max<qint64>(0,bytes);int u=0;
    while(n>=1024.0&&u<5){n/=1024.0;++u;}
    return u?QString::number(n,'f',1)+" "+units[u]:QString::number(qint64(n))+" B";
}
QString DiskViewModel::currentName() const { return m_current>=0?m_nodes[m_current].name:QString(); }
QString DiskViewModel::currentPath() const { return m_current>=0?m_nodes[m_current].path:QString(); }
QString DiskViewModel::totalText() const { return m_current>=0?formatSize(m_nodes[m_current].size):QString(); }
QString DiskViewModel::breadcrumbText() const { return currentPath().replace('\\',"  /  "); }

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
void DiskViewModel::resetAt(int index)
{
    if(index<0||index>=m_nodes.size())return;
    beginResetModel();m_current=index;m_visible=m_nodes[index].children;
    std::stable_sort(m_visible.begin(),m_visible.end(),[&](int a,int b){return m_nodes[a].size>m_nodes[b].size;});
    m_selectedId.clear();endResetModel();emit currentPathChanged();emit selectedChanged();
}
void DiskViewModel::navigate(const QString& id){const int i=indexForId(id);if(i>=0&&m_nodes[i].directory){m_history.push_back(i);resetAt(i);}}
void DiskViewModel::goUp(){if(m_current>0){const int leaving=m_current;const int parent=m_nodes[leaving].parent;resetAt(parent);m_selectedId=m_nodes[leaving].id;emit selectedChanged();}}
void DiskViewModel::select(const QString& id){if(m_selectedId==id)return;m_selectedId=id;emit selectedChanged();}
QVariantList DiskViewModel::chartEntries()const
{
    QVariantList out;if(m_current<0)return out;const double total=std::max<quint64>(1,m_nodes[m_current].size);
    for(int i:m_visible){const Entry&e=m_nodes[i];out.push_back(QVariantMap{{"id",e.id},{"name",e.name},{"size",double(e.size)},{"directory",e.directory},{"share",double(e.size)/total}});}
    return out;
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
