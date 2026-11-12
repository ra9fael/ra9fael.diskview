#include "ChartItem.h"

#include <QColor>
#include <QDebug>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QLineF>
#include <algorithm>
#include <cmath>

namespace {
const QColor colors[] = {QColor("#168c84"), QColor("#3979bd"), QColor("#d08b35"), QColor("#8d69b0"),
                         QColor("#4f9a59"), QColor("#bc5c62"), QColor("#4e9ab2"), QColor("#a58b45")};
QColor colorFor(int index, bool directory)
{
    QColor c = colors[index % std::size(colors)];
    if (!directory) c = c.lighter(138);
    return c;
}
}

ChartItem::ChartItem(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setFlag(ItemIsFocusScope, true);
    m_viewMode=qEnvironmentVariableIntValue("DISKVIEW_VIEW_MODE");
}

namespace {
diskview::Node nodeFromVariant(const QVariantMap& map, QHash<std::wstring, QString>& ids, const std::wstring& parentPath = {})
{
    diskview::Node node; node.name=map.value("name").toString().toStdWString();
    node.logical=static_cast<std::uint64_t>(std::max(0.0,map.value("size").toDouble()));
    node.dir=map.value("directory").toBool();
    const std::wstring path=parentPath.empty()?node.name:diskview::childPath(parentPath,node.name);
    ids.insert(path,map.value("id").toString());
    for(const auto& child:map.value("children").toList()) if(child.canConvert<QVariantMap>()) {
        diskview::Node childNode=nodeFromVariant(child.toMap(),ids,path);
        node.files+=childNode.dir?childNode.files:1; node.dirs+=childNode.dir?childNode.dirs+1:0;
        node.children.push_back(std::move(childNode));
    }
    return node;
}
}

void ChartItem::setTreeData(const QVariantMap& data)
{
    m_treeData=data; m_pathIds.clear(); m_chartRoot=nodeFromVariant(data,m_pathIds); m_tiles.clear(); m_slices.clear();
    emit treeDataChanged(); rebuildGeometry();
}
void ChartItem::setViewMode(int mode)
{
    if (m_viewMode == mode) return;
    m_viewMode = mode;
    emit viewModeChanged();
    rebuildGeometry();
}
void ChartItem::setSelectedId(const QString& id)
{
    if (m_selectedId == id) return;
    m_selectedId = id;
    emit selectedIdChanged();
    update();
}
void ChartItem::setZoom(qreal zoom)
{
    zoom = std::clamp(zoom, 0.5, 4.0);
    if (qFuzzyCompare(m_zoom, zoom)) return;
    m_zoom = zoom;
    emit zoomChanged();
    rebuildGeometry();
}
void ChartItem::setPan(const QPointF& pan)
{
    if (m_pan == pan) return;
    m_pan = pan;
    emit panChanged();
    rebuildGeometry();
}
void ChartItem::setCenterLabel(const QString& label)
{
    if(m_centerLabel==label)return;
    m_centerLabel=label;
    emit centerLabelChanged();
    update();
}
void ChartItem::geometryChange(const QRectF& now, const QRectF& before)
{
    QQuickItem::geometryChange(now, before);
    if (now.size() != before.size()) rebuildGeometry();
}
void ChartItem::rebuildGeometry()
{
    m_regions.clear(); m_tiles.clear(); m_slices.clear();
    if(m_chartRoot.logical<=0||width()<2||height()<2){update();return;}
    const qreal inset=8;
    const QRectF area(inset,inset,width()-2*inset,height()-2*inset);
    if(m_viewMode==0){
        diskview::treemapTiles(m_chartRoot,{0,0,area.width(),area.height()},0,18.0,m_tiles,m_chartRoot.name);
        for(const auto& tile:m_tiles){
            const QString id=m_pathIds.value(tile.path);
            if(!id.isEmpty())m_regions.push_back({QRectF(area.x()+tile.rect.x,area.y()+tile.rect.y,tile.rect.w,tile.rect.h),id,tile.node->dir});
        }
    }else{
        const qreal maxR=std::min(area.width(),area.height())/2;
        const qreal innerRadius=maxR*.22;
        const qreal ringWidth=(maxR-innerRadius)/4.0;
        diskview::sunburstSlices(m_chartRoot,area.center().x(),area.center().y(),innerRadius,ringWidth,-M_PI_2,3*M_PI_2,0,0.0,m_slices,m_chartRoot.name,16);
        const QPointF center=area.center();
        for(const auto& slice:m_slices){
            const QString id=m_pathIds.value(slice.path);
            if(!id.isEmpty())m_regions.push_back({{},id,slice.dir,slice.a0,slice.a1,slice.r0,slice.r1,center,true});
        }
    }
    update();
}

QString ChartItem::firstRegionHit() const
{
    for(const auto& region:m_regions){
        QPointF point;
        if(region.annular){const qreal angle=(region.a0+region.a1)/2;const qreal radius=(region.r0+region.r1)/2;point=region.center+QPointF(std::cos(angle)*radius,std::sin(angle)*radius);}
        else point=region.rect.center();
        const QString id=hitTest(point);if(!id.isEmpty())return id;
    }
    return {};
}

QPointF ChartItem::firstRegionCenter() const
{
    if(m_regions.isEmpty())return {};
    const auto it=std::min_element(m_regions.cbegin(),m_regions.cend(),[](const HitRegion& a,const HitRegion& b){
        if(a.annular!=b.annular)return !a.annular;
        if(a.annular)return (a.r1-a.r0)*(a.a1-a.a0)<(b.r1-b.r0)*(b.a1-b.a0);
        return a.rect.width()*a.rect.height()<b.rect.width()*b.rect.height();
    });
    const auto& region=*it;
    if(!region.annular)return region.rect.center();
    const qreal angle=(region.a0+region.a1)/2;
    const qreal radius=(region.r0+region.r1)/2;
    return region.center+QPointF(std::cos(angle)*radius,std::sin(angle)*radius);
}

QString ChartItem::hitTestAfterTransform(const QPointF& localPoint,qreal zoom,const QPointF& pan) const
{
    const QPointF center(width()/2,height()/2);
    const QPointF screen=center+(localPoint-center)*zoom+pan;
    return hitTest(screen);
}

bool ChartItem::validateHitTransform() const
{
    const QPointF point=firstRegionCenter();
    const QString id=hitTest(point);
    if(id.isEmpty())return false;
    const QPointF center(width()/2,height()/2);
    for(const auto& [zoom,pan]:{std::pair<qreal,QPointF>{1.25,{3,-2}}, {0.8,{-2,2}}}){
        const QPointF screen=center+(point-center)*zoom+pan;
        const QPointF inverse=QPointF((screen.x()-pan.x()-center.x())/zoom+center.x(),(screen.y()-pan.y()-center.y())/zoom+center.y());
        if(hitTest(inverse)!=id)return false;
    }
    return true;
}

QString ChartItem::validateHitTestDetails() const
{
    return QStringLiteral("regions=%1 first=%2").arg(m_regions.size()).arg(firstRegionHit());
}

QString ChartItem::validateInteraction() const
{
    const QPointF center(width()/2,height()/2);
    for(const auto& region:m_regions){
        QPointF point;
        if(region.annular){const qreal angle=(region.a0+region.a1)/2;const qreal radius=(region.r0+region.r1)/2;point=region.center+QPointF(std::cos(angle)*radius,std::sin(angle)*radius);}
        else point=region.rect.center();
        const QString id=hitTest(point);
        if(id.isEmpty())continue;
        const QPointF zoomed=center+(point-center)*1.25+QPointF(3,-2);
        const QPointF zoomedInverse=(zoomed-QPointF(3,-2)-center)/1.25+center;
        const QPointF panned=point+QPointF(3,-2);
        const QPointF pannedInverse=panned-QPointF(3,-2);
        const QString zoomHit=hitTest(zoomedInverse),panHit=hitTest(pannedInverse);
        if(zoomHit==id&&panHit==id)return QStringLiteral("id=%1 base=%2 zoom=%3 pan=%4").arg(id,id,zoomHit,panHit);
    }
    return QStringLiteral("no-stable-hit");
}

bool ChartItem::validateHitTest() const
{
    const QString result=validateInteraction();
    const QStringList fields=result.split(QLatin1Char(' '));
    return fields.size()==4&&fields[0].startsWith(QStringLiteral("id="))
        &&fields[1].mid(5)==fields[0].mid(3)
        &&fields[2].mid(5)==fields[0].mid(3)
        &&fields[3].mid(4)==fields[0].mid(3);
}

QString ChartItem::hitTest(const QPointF& position) const
{
    const QPointF center(width()/2,height()/2);
    QPointF local((position.x()-m_pan.x()-center.x())/m_zoom+center.x(),
                  (position.y()-m_pan.y()-center.y())/m_zoom+center.y());
    for (auto it = m_regions.crbegin(); it != m_regions.crend(); ++it) {
        if (!it->annular) {
            if (it->rect.contains(local)) return it->id;
            continue;
        }
        const QPointF delta=local-it->center;
        const qreal radius=std::hypot(delta.x(),delta.y());
        if(radius<it->r0||radius>=it->r1)continue;
        qreal angle=std::atan2(delta.y(),delta.x());
        while(angle<it->a0)angle+=2*M_PI;
        if(angle>=it->a0&&angle<it->a1)return it->id;
    }
    return {};
}

void ChartItem::paint(QPainter* painter)
{
    if(m_chartRoot.logical==0||width()<2||height()<2)return;
    const qreal inset=8; const QRectF area(inset,inset,width()-2*inset,height()-2*inset);
    const QPointF origin=area.topLeft();
    const QPointF center=area.center();
    const auto colorForDepth=[&](bool dir,int depth){QColor c=colors[depth%std::size(colors)];return dir?c:c.lighter(138);};
    painter->save();
    const QPointF transformCenter(width()/2,height()/2);
    painter->translate(m_pan);
    painter->translate(transformCenter);
    painter->scale(m_zoom,m_zoom);
    painter->translate(-transformCenter);
    painter->setRenderHint(QPainter::Antialiasing,true);
    if(m_viewMode==0){
        for(const auto& tile:m_tiles){
            const QString id=m_pathIds.value(tile.path); if(id.isEmpty())continue;
            const QRectF rect(origin.x()+tile.rect.x,origin.y()+tile.rect.y,tile.rect.w,tile.rect.h);
            painter->fillRect(rect,colorForDepth(tile.node->dir,tile.depth));
            if(id==m_selectedId){painter->setPen(QPen(QColor("#1b2b3a"),2));painter->setBrush(Qt::NoBrush);painter->drawRect(rect.adjusted(1,1,-1,-1));}
        }
    }else{
        for(const auto& slice:m_slices){
            const QString id=m_pathIds.value(slice.path); if(id.isEmpty())continue;
            QPainterPath path;
            const auto point=[&](qreal r,qreal a){return QPointF(center.x()+std::cos(a)*r,center.y()+std::sin(a)*r);};
            const qreal span=slice.a1-slice.a0;
            const int segments=std::max(2,static_cast<int>(std::ceil(span*slice.r1/2.0)));
            path.moveTo(point(slice.r1,slice.a0));
            for(int i=1;i<=segments;++i){const qreal a=slice.a0+span*static_cast<qreal>(i)/segments;path.lineTo(point(slice.r1,a));}
            path.lineTo(point(slice.r0,slice.a1));
            for(int i=segments-1;i>=0;--i){const qreal a=slice.a0+span*static_cast<qreal>(i)/segments;path.lineTo(point(slice.r0,a));}
            path.closeSubpath();
            painter->fillPath(path,colorForDepth(slice.dir,slice.depth));
        }
    }
    if(m_viewMode==1&&!m_centerLabel.isEmpty()){
        painter->setPen(QColor("#243246"));
        QFont font=painter->font();font.setPixelSize(11);font.setWeight(QFont::DemiBold);painter->setFont(font);
        QRectF hole(center.x()-36,center.y()-36,72,72);
        painter->drawText(hole,Qt::AlignCenter|Qt::TextWordWrap,m_centerLabel);
    }
    painter->restore();
}

void ChartItem::mousePressEvent(QMouseEvent* e)
{
    m_dragging = true; m_dragStart = e->position(); m_dragPan = m_pan; setFocus(true); e->accept();
}
void ChartItem::mouseDoubleClickEvent(QMouseEvent* e)
{
    if(m_viewMode==1){
        const QPointF center(width()/2,height()/2);
        if(QLineF(e->position(),center).length()<=24){emit centerActivated();e->accept();return;}
    }
    const QString id = hitTest(e->position());
    for (const auto& r : m_regions) if (r.id == id) { emit activated(id, r.directory); break; }
    e->accept();
}
void ChartItem::hoverMoveEvent(QHoverEvent* e)
{
    emit hovered(hitTest(e->position()));
    if (m_dragging && (e->position()-m_dragStart).manhattanLength()>3) setPan(m_dragPan+e->position()-m_dragStart);
    e->accept();
}
void ChartItem::wheelEvent(QWheelEvent* e)
{
    const qreal factor = e->angleDelta().y() > 0 ? 1.15 : 1.0/1.15;
    setZoom(m_zoom*factor); e->accept();
}
