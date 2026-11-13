#include "ChartItem.h"

#include <QColor>
#include <QDebug>
#include <QElapsedTimer>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QLineF>
#include <algorithm>
#include <cmath>

#include "ExtensionColors.h"

namespace {
// The colour a file gets when the legend has dimmed everything else.
const QColor kDimmedFill("#dfe4ea");
}

ChartItem::ChartItem(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    setAcceptedMouseButtons(Qt::LeftButton|Qt::RightButton);
    setAcceptHoverEvents(true);
    setFlag(ItemIsFocusScope, true);
    m_viewMode=qEnvironmentVariableIntValue("DISKVIEW_VIEW_MODE");
}

namespace {
diskview::Node nodeFromVariant(const QVariantMap& map, QHash<std::wstring, QString>& ids,
                               QHash<QString, ChartItem::NodeInfo>& infos,
                               const std::wstring& parentPath = {})
{
    diskview::Node node; node.name=map.value("name").toString().toStdWString();
    node.logical=static_cast<std::uint64_t>(std::max(0.0,map.value("size").toDouble()));
    node.dir=map.value("directory").toBool();
    const std::wstring path=parentPath.empty()?node.name:diskview::childPath(parentPath,node.name);
    const QString id=map.value("id").toString();
    ids.insert(path,id);
    infos.insert(id,{QString::fromStdWString(node.name),QString::fromStdWString(path),
                     static_cast<quint64>(node.logical),node.dir});
    for(const auto& child:map.value("children").toList()) if(child.canConvert<QVariantMap>()) {
        diskview::Node childNode=nodeFromVariant(child.toMap(),ids,infos,path);
        node.files+=childNode.dir?childNode.files:1; node.dirs+=childNode.dir?childNode.dirs+1:0;
        node.children.push_back(std::move(childNode));
    }
    return node;
}
}

void ChartItem::setTreeData(const QVariantMap& data)
{
    m_treeData=data; m_pathIds.clear(); m_nodeInfo.clear();
    m_chartRoot=nodeFromVariant(data,m_pathIds,m_nodeInfo); m_tiles.clear(); m_slices.clear();
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
void ChartItem::setDepth(int depth)
{
    // One level keeps the folders whole and clickable; five is as deep as the
    // tiles stay readable, the way WizTree stops.
    depth=std::clamp(depth,1,5);
    if(m_depth==depth)return;
    m_depth=depth;
    emit depthChanged();
    rebuildGeometry();
}
void ChartItem::setHighlightExtension(const QString& extension)
{
    if(m_highlightExtension==extension)return;
    m_highlightExtension=extension;
    emit highlightChanged();
    update();
}

QColor ChartItem::fillFor(const diskview::Node& node, bool* dimmed) const
{
    const bool hidden=!m_highlightExtension.isEmpty()&&!node.dir
        &&diskviewqml::extensionOf(QString::fromStdWString(node.name))!=m_highlightExtension;
    if(dimmed)*dimmed=hidden;
    if(hidden)return kDimmedFill;
    if(node.dir)return QColor("#8f9aa8");
    return diskviewqml::extensionColor(diskviewqml::extensionOf(QString::fromStdWString(node.name)));
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
    m_innerRadius=0;
    // The top level is depth 0, so one level means no recursion at all.
    const int maxDepth=m_depth-1;
    if(m_viewMode==0){
        diskview::treemapTiles(m_chartRoot,{0,0,area.width(),area.height()},0,18.0,m_tiles,m_chartRoot.name,maxDepth);
        for(const auto& tile:m_tiles){
            const QString id=m_pathIds.value(tile.path);
            if(!id.isEmpty())m_regions.push_back({QRectF(area.x()+tile.rect.x,area.y()+tile.rect.y,tile.rect.w,tile.rect.h),id,tile.node->dir});
        }
    }else{
        const qreal maxR=std::min(area.width(),area.height())/2;
        const qreal innerRadius=maxR*.22;
        // The hole is a target of its own: a double click in it goes up, so
        // its radius is kept for the hit test.
        m_innerRadius=innerRadius;
        const qreal ringWidth=(maxR-innerRadius)/4.0;
        diskview::sunburstSlices(m_chartRoot,area.center().x(),area.center().y(),innerRadius,ringWidth,-M_PI_2,3*M_PI_2,0,0.0,m_slices,m_chartRoot.name,maxDepth);
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

QString ChartItem::benchmark(int iterations)
{
    if (iterations < 1) iterations = 1;
    QElapsedTimer timer;
    // The payload the model hands over, turned into the chart's own tree.
    timer.start();
    for (int i = 0; i < iterations; ++i) setTreeData(m_treeData);
    const double prepare = double(timer.nsecsElapsed()) / 1e6 / iterations;

    timer.restart();
    for (int i = 0; i < iterations; ++i) rebuildGeometry();
    const double layout = double(timer.nsecsElapsed()) / 1e6 / iterations;

    // A frame, painted the way the painted item paints one.
    QImage frame(qMax(1, int(width())), qMax(1, int(height())), QImage::Format_ARGB32_Premultiplied);
    timer.restart();
    for (int i = 0; i < iterations; ++i) {
        frame.fill(Qt::white);
        QPainter painter(&frame);
        paint(&painter);
    }
    const double painted = double(timer.nsecsElapsed()) / 1e6 / iterations;

    return QStringLiteral("nodes=%1 tiles=%2 slices=%3 prepare=%4ms layout=%5ms paint=%6ms")
        .arg(m_nodeInfo.size())
        .arg(m_tiles.size())
        .arg(m_slices.size())
        .arg(prepare, 0, 'f', 1)
        .arg(layout, 0, 'f', 2)
        .arg(painted, 0, 'f', 2);
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

QPointF ChartItem::firstDirectoryCenter() const
{
    // A tile is drawn over by its own children, so the centre of a folder
    // tile usually belongs to a file inside it. The point has to be one that
    // really hit-tests back to the folder itself.
    for(const auto& region:m_regions){
        if(!region.directory)continue;
        if(region.annular){
            const qreal angle=(region.a0+region.a1)/2;const qreal radius=(region.r0+region.r1)/2;
            const QPointF point=region.center+QPointF(std::cos(angle)*radius,std::sin(angle)*radius);
            if(hitTest(point)==region.id)return point;
            continue;
        }
        const QRectF r=region.rect;
        // The children of a folder cover it, so only a leaf folder is
        // clickable. A coarse grid finds one without guessing where.
        for(int gy=1;gy<=4;++gy){
            for(int gx=1;gx<=4;++gx){
                const QPointF point(r.left()+r.width()*gx/5.0,r.top()+r.height()*gy/5.0);
                if(hitTest(point)==region.id)return point;
            }
        }
    }
    return {};
}

QVariantMap ChartItem::hoveredInfo(const QString& id) const
{
    const auto it=m_nodeInfo.constFind(id);
    if(it==m_nodeInfo.cend())return {};
    const double total=std::max<quint64>(1,m_chartRoot.logical);
    return {{"name",it->name},{"path",it->path},{"size",double(it->size)},
            {"percent",100.0*static_cast<double>(it->size)/total},{"directory",it->dir}};
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
            QColor fill=fillFor(*tile.node);
            const bool lit=id==m_selectedId||id==m_hoverId;
            if(lit)fill=fill.darker(112);
            painter->fillRect(rect,fill);
            if(id==m_selectedId||id==m_hoverId){painter->setPen(QPen(QColor("#1b2b3a"),id==m_selectedId?2:1));painter->setBrush(Qt::NoBrush);painter->drawRect(rect.adjusted(1,1,-1,-1));}
        }
    }else{
        for(const auto& slice:m_slices){
            const QString id=m_pathIds.value(slice.path); if(id.isEmpty())continue;
            QPainterPath path;
            const auto point=[&](qreal r,qreal a){return QPointF(center.x()+std::cos(a)*r,center.y()+std::sin(a)*r);};
            // Real arcs instead of a polyline: the edge stays smooth at any
            // zoom, which the ring outline shows badly otherwise.
            const qreal span=slice.a1-slice.a0;
            const qreal startDeg=-slice.a0*180.0/M_PI;
            const qreal sweepDeg=-span*180.0/M_PI;
            const QRectF outer(center.x()-slice.r1,center.y()-slice.r1,2*slice.r1,2*slice.r1);
            const QRectF inner(center.x()-slice.r0,center.y()-slice.r0,2*slice.r0,2*slice.r0);
            path.moveTo(point(slice.r1,slice.a0));
            path.arcTo(outer,startDeg,sweepDeg);
            path.arcTo(inner,startDeg+sweepDeg,-sweepDeg);
            path.closeSubpath();
            painter->fillPath(path,fillFor(*slice.node));
            if(id==m_selectedId||id==m_hoverId){
                painter->setPen(QPen(QColor("#1b2b3a"),id==m_selectedId?2:1));
                painter->setBrush(Qt::NoBrush);
                painter->drawPath(path);
            }
        }
    }
    if(m_viewMode==1&&!m_centerLabel.isEmpty()){
        painter->setPen(QColor("#243246"));
        QFont font=painter->font();font.setPixelSize(11);font.setWeight(QFont::DemiBold);painter->setFont(font);
        QRectF hole(center.x()-36,center.y()-36,72,72);
        // A long path would spill out of the hole and over the rings, so the
        // label is elided to what the hole can hold.
        const QFontMetricsF metrics(font);
        const QString shortLabel=metrics.elidedText(m_centerLabel,Qt::ElideMiddle,hole.width());
        painter->drawText(hole,Qt::AlignCenter|Qt::TextWordWrap,shortLabel);
    }
    painter->restore();

    // The labels are drawn after the transform, so their size does not follow
    // the zoom. A tile only gets one when it is big enough to hold it, which
    // is the rule the native chart uses.
    painter->save();
    painter->setClipRect(QRectF(0,0,width(),height()));
    const auto toScreen=[&](const QPointF& p){return transformCenter+(p-transformCenter)*m_zoom+m_pan;};
    QFont micro=painter->font();micro.setPixelSize(10);
    const QFontMetricsF metrics(micro);
    if(m_viewMode==0){
        painter->setFont(micro);
        for(const auto& tile:m_tiles){
            const QString id=m_pathIds.value(tile.path); if(id.isEmpty())continue;
            const QRectF rect(origin.x()+tile.rect.x,origin.y()+tile.rect.y,tile.rect.w,tile.rect.h);
            const QRectF screen(toScreen(rect.topLeft()),toScreen(rect.bottomRight()));
            if(screen.width()<48||screen.height()<14)continue;
            bool dimmed=false;
            const QColor fill=fillFor(*tile.node,&dimmed);
            QColor ink=diskviewqml::textOnColor(fill);
            if(dimmed)ink.setAlphaF(0.45);
            painter->setPen(ink);
            const QRectF inner=screen.adjusted(5,2,-4,-2);
            painter->drawText(inner,Qt::AlignLeft|Qt::AlignTop,
                              metrics.elidedText(QString::fromStdWString(tile.node->name),Qt::ElideRight,inner.width()));
            if(screen.height()>=32&&!dimmed){
                QRectF sub=inner;sub.setTop(inner.top()+13);
                QColor subColor=ink;subColor.setAlphaF(0.75);
                painter->setPen(subColor);
                painter->drawText(sub,Qt::AlignLeft|Qt::AlignTop,
                                  QString::fromStdString(diskview::formatBytes(tile.node->logical,true)));
            }
        }
    }else{
        painter->setFont(micro);
        for(const auto& slice:m_slices){
            const QString id=m_pathIds.value(slice.path); if(id.isEmpty())continue;
            const qreal span=slice.a1-slice.a0;
            const qreal thickness=(slice.r1-slice.r0)*m_zoom;
            if(span<=0.35||thickness<=26)continue;
            const qreal angle=(slice.a0+slice.a1)/2;
            const qreal radius=(slice.r0+slice.r1)/2;
            const QPointF point=toScreen(center+QPointF(std::cos(angle)*radius,std::sin(angle)*radius));
            const qreal half=std::min<qreal>(50,thickness*0.42);
            const QRectF box(point.x()-half,point.y()-10,half*2,20);
            bool dimmed=false;
            const QColor fill=fillFor(*slice.node,&dimmed);
            QColor ink=diskviewqml::textOnColor(fill);
            if(dimmed)ink.setAlphaF(0.45);
            painter->setPen(ink);
            const QString text=slice.dir?QString::fromStdWString(slice.node->name)
                                        :QString::fromStdString(diskview::formatBytes(slice.node->logical,true));
            painter->drawText(box,Qt::AlignCenter,metrics.elidedText(text,Qt::ElideRight,box.width()));
        }
    }
    painter->restore();
}

void ChartItem::mousePressEvent(QMouseEvent* e)
{
    if(e->button()==Qt::RightButton){
        emit contextRequested(hitTest(e->position()),e->position().x(),e->position().y());
        e->accept();
        return;
    }
    m_dragging = true; m_dragStart = e->position(); m_dragPan = m_pan; setFocus(true); e->accept();
}
void ChartItem::mouseMoveEvent(QMouseEvent* e)
{
    // The pan only follows a drag while the button is down; hovering alone
    // must not move the chart.
    if (m_dragging && (e->position()-m_dragStart).manhattanLength()>3) {
        setPan(m_dragPan+e->position()-m_dragStart);
        e->accept();
        return;
    }
    QQuickPaintedItem::mouseMoveEvent(e);
}
void ChartItem::mouseReleaseEvent(QMouseEvent* e)
{
    // Without this the drag never ends: every later hover would keep panning
    // the chart, and a double click could no longer land on the tile it
    // started on.
    const bool clicked = m_dragging && e->button()==Qt::LeftButton
                         && (e->position()-m_dragStart).manhattanLength()<=3;
    m_dragging = false;
    if(clicked) emit ChartItem::clicked(hitTest(e->position()),e->position().x(),e->position().y());
    e->accept();
}
void ChartItem::mouseDoubleClickEvent(QMouseEvent* e)
{
    m_dragging = false;
    if(m_viewMode==1){
        // The hole is much wider than a few pixels; a click anywhere in it is
        // a click on the parent.
        const QPointF center(width()/2,height()/2);
        if(QLineF(e->position(),center).length()<=std::max<qreal>(m_innerRadius,24)){emit centerActivated();e->accept();return;}
    }
    // Resolve the region first: the QML side may rebuild the tree as soon as
    // it hears about the activation, and the region list would be gone.
    const QString id = hitTest(e->position());
    QString target; bool directory=false;
    for (const auto& r : m_regions) if (r.id == id) { target=r.id; directory=r.directory; break; }
    if (!target.isEmpty()) emit activated(target, directory);
    e->accept();
}
void ChartItem::hoverMoveEvent(QHoverEvent* e)
{
    const QString id=hitTest(e->position());
    m_hoverPosition=e->position();
    if(m_hoverId!=id){m_hoverId=id;emit hovered(id);update();}
    emit hoverChanged();
    e->accept();
}
void ChartItem::hoverLeaveEvent(QHoverEvent* e)
{
    if(!m_hoverId.isEmpty()){m_hoverId.clear();emit hovered(QString());update();emit hoverChanged();}
    e->accept();
}
void ChartItem::wheelEvent(QWheelEvent* e)
{
    const qreal factor = e->angleDelta().y() > 0 ? 1.15 : 1.0/1.15;
    setZoom(m_zoom*factor); e->accept();
}
