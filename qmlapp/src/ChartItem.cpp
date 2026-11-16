#include "ChartItem.h"

#include <iterator>

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
// True when `path` is `selected` itself or one of the folders above it. The
// path is the chart's own ("D:\dir\sub\dll"), so a prefix plus a separator is
// all it takes; the selected block and its whole branch stay lit while the
// rest of the picture steps back.
bool onSelectedBranch(const std::wstring& path, const std::wstring& selected)
{
    if (path == selected) return true;
    if (path.size() >= selected.size()) return false;
    return selected.compare(0, path.size(), path) == 0 && selected[path.size()] == L'\\';
}
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
                               const std::wstring& parentPath = {},
                               const std::wstring& absoluteRoot = {})
{
    diskview::Node node; node.name=map.value("name").toString().toStdWString();
    node.logical=static_cast<std::uint64_t>(std::max(0.0,map.value("size").toDouble()));
    node.dir=map.value("directory").toBool();
    node.group=map.value("group").toBool();
    node.rest=static_cast<std::uint64_t>(std::max(0.0,map.value("rest").toDouble()));
    node.restCount=static_cast<std::uint32_t>(std::max(0, map.value("restCount").toInt()));
    node.matchCount=static_cast<std::uint32_t>(std::max(0, map.value("matchCount").toInt()));
    node.restMatchCount=static_cast<std::uint32_t>(std::max(0, map.value("restMatchCount").toInt()));
    const std::wstring path=parentPath.empty()?absoluteRoot:diskview::childPath(parentPath,node.name);
    const QString id=map.value("id").toString();
    ids.insert(path,id);
    infos.insert(id,{QString::fromStdWString(node.name),QString::fromStdWString(path),
                     static_cast<quint64>(node.logical),node.dir});
    for(const auto& child:map.value("children").toList()) if(child.canConvert<QVariantMap>()) {
        diskview::Node childNode=nodeFromVariant(child.toMap(),ids,infos,path,absoluteRoot);
        node.files+=childNode.dir?childNode.files:1; node.dirs+=childNode.dir?childNode.dirs+1:0;
        node.children.push_back(std::move(childNode));
    }
    return node;
}
}

void ChartItem::setTreeData(const QVariantMap& data)
{
    // One integer, not a deep comparison of thousands of nested maps: the
    // comparison used to cost as much as parsing the payload.
    const int revision = data.value(QStringLiteral("revision")).toInt();
    if (revision == m_revision && !m_treeData.isEmpty()) return;
    m_revision = revision;
    m_treeData=data; m_pathIds.clear(); m_nodeInfo.clear();
    m_rootPath = data.value(QStringLiteral("rootPath")).toString().toStdWString();
    if (m_rootPath.empty()) m_rootPath = data.value(QStringLiteral("name")).toString().toStdWString();
    m_chartRoot=nodeFromVariant(data,m_pathIds,m_nodeInfo,{},m_rootPath); m_tiles.clear(); m_slices.clear();
    emit treeDataChanged(); rebuildGeometry();
}
bool ChartItem::hasDirectoryRegion() const
{
    for (const auto& region : m_regions) if (region.directory) return true;
    return false;
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
    // Never shrink the whole picture: 1:1 fills the item, and zoom is only for
    // inspecting a part of it.
    zoom = std::clamp(zoom, 1.0, 12.0);
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
void ChartItem::resetView()
{
    // The transform is all this resets: the hover is dropped (the pointer is
    // usually elsewhere by the time the button is pressed) and the selection
    // stays, since it belongs to the model. The folder is the model's
    // business, not the chart's.
    m_hoverId.clear();
    emit hoverChanged();
    setZoom(1.0);
    setPan(QPointF(0, 0));
    // The setters skip work when the value is already the default; a reset
    // must still re-lay out, so the picture fills the item after a resize.
    rebuildGeometry();
    update();
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
    // Zero is "auto": the area each folder gets decides how far the blocks
    // nest, the way WizTree does it. One to five keep a fixed limit, five
    // being as deep as the tiles stay readable.
    depth=std::clamp(depth,0,5);
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
void ChartItem::setTheme(const QString& theme)
{
    // The host sends "dark" or "dark|style"; only the light/dark part matters
    // here, the rest is the window's business.
    const QString wanted = theme.trimmed().toLower().startsWith(QStringLiteral("dark"))
                               ? QStringLiteral("dark")
                               : QStringLiteral("light");
    if(m_theme==wanted)return;
    m_theme=wanted;
    emit themeChanged();
    update();
}
void ChartItem::setAccent(const QColor& accent)
{
    if(!accent.isValid()||m_accent==accent)return;
    m_accent=accent;
    emit accentChanged();
    update();
}

void ChartItem::setSurfaceToken(const QColor& color) { if (color.isValid() && m_surfaceToken != color) { m_surfaceToken = color; emit paletteChanged(); update(); } }
void ChartItem::setBorderToken(const QColor& color) { if (color.isValid() && m_borderToken != color) { m_borderToken = color; emit paletteChanged(); update(); } }
void ChartItem::setNeutralToken(const QColor& color) { if (color.isValid() && m_neutralToken != color) { m_neutralToken = color; emit paletteChanged(); update(); } }
void ChartItem::setDimToken(const QColor& color) { if (color.isValid() && m_dimToken != color) { m_dimToken = color; emit paletteChanged(); update(); } }
void ChartItem::setFontFamily(const QString& family) { if (!family.isEmpty() && m_fontFamily != family) { m_fontFamily = family; emit paletteChanged(); update(); } }
void ChartItem::setFontPixels(int pixels) { pixels = std::clamp(pixels, 9, 24); if (m_fontPixels != pixels) { m_fontPixels = pixels; emit paletteChanged(); update(); } }

QColor ChartItem::segmentColor(double hue, int depth, bool isFile) const
{
    // A cyclic hue wheel: red → orange → amber → olive → green → teal → blue →
    // indigo → purple → magenta → pink → red. The wheel only supplies the hue;
    // saturation and lightness are fixed per theme, so neighbouring tiles stay
    // in one coherent family instead of random extension colours.
    static const double anchors[] = {0.00, 0.08, 0.15, 0.28, 0.40, 0.50,
                                     0.58, 0.66, 0.75, 0.85, 0.93};
    constexpr int count = int(std::size(anchors));
    // Start the first folder at blue rather than red: the first slice then
    // reads as the "main" one instead of a warning colour.
    double t = hue - std::floor(hue) + 0.58;
    t -= std::floor(t);
    if (!(t >= 0.0)) t = 0.0;
    const double pos = t * count;
    const int i = int(pos) % count;
    const int j = (i + 1) % count;
    const double f = pos - std::floor(pos);
    double h0 = anchors[i];
    double h1 = anchors[j];
    if (h1 <= h0) h1 += 1.0; // wrap pink -> red
    double h = std::fmod(h0 + (h1 - h0) * f, 1.0);
    const bool dark = m_theme == QStringLiteral("dark");
    const double d = std::max(0, depth - 1);
    // The wheel only supplies the hue. Saturation and lightness are held to a
    // narrow band, otherwise the vivid anchors turn into neon on a light
    // window. Deep rings step lighter (dark) or darker (light); files are a
    // little softer than their folders.
    const double delta = dark ? 0.03 : -0.015;
    double l = (dark ? 0.50 : 0.62) + d * delta;
    double s = (dark ? 0.62 : 0.52) * (1.0 - d * 0.03);
    if (isFile) {
        l += dark ? 0.04 : -0.02;
        s *= 0.92;
    }
    l = std::clamp(l, 0.24, 0.84);
    s = std::clamp(s, 0.10, 0.72);
    return QColor::fromHslF(float(h < 0 ? 0 : h), float(s), float(l), 1.0f);
}

QColor ChartItem::fillFor(const diskview::Node& node, double hue, int depth, bool* dimmed) const
{
    const QColor dimFill = m_dimToken.isValid() ? m_dimToken : (m_theme == QStringLiteral("dark")
        ? QColor(QStringLiteral("#323232")) : QColor(QStringLiteral("#e8e8e8")));
    // The small-files aggregate has no single extension: a neutral block, and
    // it steps back whenever an extension filter is on.
    if (node.group) {
        const bool hidden = !m_highlightExtension.isEmpty() && m_highlightExtension != QStringLiteral("@small");
        if (dimmed) *dimmed = hidden;
        if (hidden) return dimFill;
        return diskviewqml::extensionColor(QStringLiteral("@small"));
    }
    // "(none)" is the legend's "(无)" pick: files without an extension stay
    // lit, everything else dims.
    const bool nonePick = m_highlightExtension == QStringLiteral("(none)");
    const QString extension = diskviewqml::extensionOf(QString::fromStdWString(node.name));
    const bool matches = nonePick ? extension.isEmpty() : extension == m_highlightExtension;
    const bool hidden = !m_highlightExtension.isEmpty()
        && (node.dir ? node.restCount == 0 && node.children.empty() : !matches);
    if (dimmed)*dimmed=hidden;
    if(hidden)return dimFill;
    if (!node.dir && !m_highlightExtension.isEmpty())
        return diskviewqml::extensionColor(m_highlightExtension);
    // Layout normally supplies the hue; the extension colour is the fallback
    // for anything without a position (and matches the legend).
    if (hue < 0)
        return node.dir ? (m_theme=="dark"?QColor("#4a5568"):QColor("#8f9aa8"))
                        : diskviewqml::extensionColor(extension);
    return segmentColor(hue, depth, !node.dir);
}

// The quiet outline every tile and slice carries: a hairline in the window's
// border grey, so shapes separate from their neighbours and from the strips
// without any selection.
QColor ChartItem::strokeColor() const
{
    if (m_borderToken.isValid()) return m_borderToken;
    return m_theme=="dark"?QColor(255,255,255,46):QColor(24,38,56,56);
}

// The stronger stroke of a hover or a selection, in the accent the theme
// tokens carry (the system's): a step lighter while only hovered, full when
// selected.
QColor ChartItem::accentColor(bool selected) const
{
    return selected?m_accent:m_accent.lighter(118);
}

// A folder's frame and title strip. The fill is opaque on purpose: it hides
// the folder's own block where the content does not reach, so what shows
// through is structure rather than a colour that looks like a file.
QColor ChartItem::chromeFill() const
{
    if (m_surfaceToken.isValid()) return m_surfaceToken;
    return m_theme=="dark"?QColor(QStringLiteral("#20262E")):QColor(QStringLiteral("#E9EDF3"));
}

QColor ChartItem::chromeInk() const
{
    if (m_neutralToken.isValid()) return m_neutralToken;
    return m_theme=="dark"?QColor(QStringLiteral("#D6E0EB")):QColor(QStringLiteral("#31465E"));
}

QColor ChartItem::chromeBorder() const
{
    if (m_borderToken.isValid()) return m_borderToken;
    return m_theme=="dark"?QColor(255,255,255,86):QColor(QStringLiteral("#AEB9C6"));
}

QColor ChartItem::neutralFill() const
{
    if (m_neutralToken.isValid()) return m_neutralToken;
    return m_theme=="dark"?QColor(QStringLiteral("#3A4150")):QColor(QStringLiteral("#B9BFC8"));
}

// The tile's own gradient: a vertical fade from a lighter top to the base
// colour, plus a soft highlight band across the upper third. Flat fills read
// as one grey mass; the fade gives every block the depth the native chart's
// colour ramp has.
QBrush ChartItem::fillBrush(const diskview::Node& node, const QRectF& rect, double hue, int depth, bool* dimmed) const
{
    const QColor base = fillFor(node, hue, depth, dimmed);
    if (rect.width() < 4 || rect.height() < 4) return base;
    // A gentle fade: a strong ramp pushed the saturated palette into neon.
    QColor top = base.lighter(node.dir ? 106 : 112);
    QColor bottom = base.darker(node.dir ? 104 : 110);
    top.setAlphaF(base.alphaF());
    bottom.setAlphaF(base.alphaF());
    QLinearGradient g(rect.topLeft(), rect.bottomLeft());
    g.setColorAt(0.0, top);
    g.setColorAt(0.55, base);
    g.setColorAt(1.0, bottom);
    return QBrush(g);
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
    // The picture fills the item; only the sunburst keeps a hair of room for
    // its outermost stroke.
    const qreal inset = m_viewMode == 0 ? 0.0 : 2.0;
    const QRectF area(inset,inset,width()-2*inset,height()-2*inset);
    m_innerRadius=0;
    // The top level is depth 0, so one level means no recursion at all; auto
    // lets the per-folder area budget stop it instead.
    const int maxDepth=m_depth<=0?24:m_depth-1;
    if(m_viewMode==0){
        diskview::treemapTiles(m_chartRoot,{0,0,area.width(),area.height()},0,18.0,m_tiles,m_rootPath,maxDepth);
        for(const auto& tile:m_tiles){
            // The aggregate block has no node of its own; the folder's region
            // already covers that space, and a click on it selects the folder.
            if(tile.remainder||!tile.node)continue;
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
        diskview::sunburstSlices(m_chartRoot,area.center().x(),area.center().y(),innerRadius,ringWidth,-M_PI_2,3*M_PI_2,0,4.0/std::max<qreal>(1.0,maxR),m_slices,m_rootPath,maxDepth);
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
    // The revision guard skips identical payloads, which is what the live
    // window wants; the benchmark resets it so each run parses for real.
    const int revision = m_revision;
    for (int i = 0; i < iterations; ++i) { m_revision = -1; setTreeData(m_treeData); }
    m_revision = revision;
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
        // A folder that draws its children covers all of its inside with
        // them, so the only spot that still belongs to the folder itself is
        // its title strip along the top. A folded folder has no strip and is
        // its own block throughout.
        if(r.width()>12&&r.height()>12){
            const QPointF title(r.left()+6,r.top()+6);
            if(hitTest(title)==region.id)return title;
        }
        // The children of a folder cover it, so only the strip and a leaf
        // folder are clickable. A coarse grid finds one without guessing
        // where.
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
    const qreal inset = m_viewMode == 0 ? 0.0 : 2.0;
    const QRectF area(inset,inset,width()-2*inset,height()-2*inset);
    const QPointF origin=area.topLeft();
    const QPointF center=area.center();
    painter->save();
    const QPointF transformCenter(width()/2,height()/2);
    painter->translate(m_pan);
    painter->translate(transformCenter);
    painter->scale(m_zoom,m_zoom);
    painter->translate(-transformCenter);
    painter->setRenderHint(QPainter::Antialiasing,true);
    // The selection the chart can actually show: the node whose tile is
    // drawn. A row for a file inside a folded folder has no block of its own,
    // and dimming the whole picture for it would be worse than doing nothing.
    std::wstring selectedPath;
    if (!m_selectedId.isEmpty()) {
        const std::wstring wanted = m_nodeInfo.value(m_selectedId).path.toStdWString();
        for (const auto& tile : m_tiles)
            if (tile.path == wanted) { selectedPath = wanted; break; }
    }
    const std::wstring hoverPath = !m_hoverId.isEmpty()
        ? m_nodeInfo.value(m_hoverId).path.toStdWString() : std::wstring();
    if(m_viewMode==0){
        // While something is selected the rest of the picture steps back, so
        // the eye lands on the selection instead of hunting for a ring.
        const QColor scrim=m_theme==QStringLiteral("dark")?QColor(0,0,0,46):QColor(0,0,0,26);
        const bool dimOthers=!selectedPath.empty();
        // Pass 1: the content. Every tile is filled with its branch colour; an
        // aggregate is neutral, because it is not one thing.
        for(const auto& tile:m_tiles){
            const QRectF rect(origin.x()+tile.rect.x,origin.y()+tile.rect.y,tile.rect.w,tile.rect.h);
            bool dimmed=false;
            const bool remainderDimmed = tile.remainder && !m_highlightExtension.isEmpty()
                                         && tile.remainderMatchCount == 0;
            const QColor remainderColor = remainderDimmed
                ? (m_dimToken.isValid() ? m_dimToken : neutralFill())
                : (!m_highlightExtension.isEmpty()
                   ? diskviewqml::extensionColor(m_highlightExtension == QStringLiteral("(none)")
                                                ? QString() : m_highlightExtension)
                   : neutralFill());
            const QBrush fill=tile.remainder?QBrush(remainderColor)
                                            :fillBrush(*tile.node,rect,tile.hue,tile.depth,&dimmed);
            painter->fillRect(rect,fill);
            painter->setPen(QPen(strokeColor(),1));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(rect.adjusted(0.5,0.5,-0.5,-0.5));
            if(dimOthers&&!onSelectedBranch(tile.path,selectedPath))painter->fillRect(rect,scrim);
            // A selected file has no strip to turn: it takes a wash of the
            // accent over its own block instead.
            if(dimOthers&&tile.path==selectedPath&&tile.node&&!tile.node->dir){
                QColor wash=m_accent;
                wash.setAlphaF(0.24);
                painter->fillRect(rect,wash);
            }
        }
        // Pass 2: structure. A folder that keeps a title strip gets a neutral
        // frame around itself and an opaque strip across its top: the frame
        // means "folder", the colour means "content", and the two never mix.
        // This is what makes the nesting readable instead of a flat pile of
        // colours, and it also hides the parent's own block where its children
        // do not reach.
        for(const auto& tile:m_tiles){
            if(tile.remainder || !tile.node || !tile.node->dir)continue;
            if(tile.rect.w < 36 || tile.rect.h < 14)continue;
            const QRectF rect(origin.x()+tile.rect.x,origin.y()+tile.rect.y,tile.rect.w,tile.rect.h);
            painter->setPen(QPen(chromeBorder(),1.5));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(rect.adjusted(1,1,-1,-1));
            if(tile.header<=0)continue;
            const QRectF strip(rect.left(),rect.top(),rect.width(),tile.header);
            painter->fillRect(strip,chromeFill());
            painter->setPen(QPen(chromeBorder(),1));
            painter->drawLine(strip.bottomLeft(),strip.bottomRight());
            painter->setPen(QPen(strokeColor(),1));
            painter->drawRect(rect.adjusted(0.5,0.5,-0.5,-0.5));
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
            painter->fillPath(path,fillFor(*slice.node,slice.hue,slice.depth));
            if(id==m_selectedId||id==m_hoverId){
                painter->setPen(QPen(accentColor(id==m_selectedId),id==m_selectedId?2:1));
                painter->setBrush(Qt::NoBrush);
                painter->drawPath(path);
            }else{
                painter->setPen(QPen(strokeColor(),1));
                painter->setBrush(Qt::NoBrush);
                painter->drawPath(path);
            }
        }
    }
    if(m_viewMode==1&&!m_centerLabel.isEmpty()){
        painter->setPen(m_theme=="dark"?QColor("#e6edf5"):QColor("#243246"));
        QFont font=painter->font();font.setFamily(m_fontFamily);font.setPixelSize(m_fontPixels);font.setWeight(QFont::DemiBold);painter->setFont(font);
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
    QFont micro=painter->font();micro.setFamily(m_fontFamily);micro.setPixelSize(std::max(9, m_fontPixels - 2));
    const QFontMetricsF metrics(micro);
    if(m_viewMode==0){
        painter->setFont(micro);
        for(const auto& tile:m_tiles){
            const QString id=m_pathIds.value(tile.path); if(id.isEmpty())continue;
            const QRectF rect(origin.x()+tile.rect.x,origin.y()+tile.rect.y,tile.rect.w,tile.rect.h);
            const QRectF screen(toScreen(rect.topLeft()),toScreen(rect.bottomRight()));
            if(tile.remainder){
                // One neutral block standing for many small things: name it,
                // and keep the count when the folder knows it.
                if(screen.width()<36||screen.height()<12)continue;
                const QString text=tile.remainderCount>0
                    ?QStringLiteral("其余 %1 项").arg(tile.remainderCount)
                    :QStringLiteral("其他");
                painter->setPen(chromeInk());
                painter->drawText(screen.adjusted(4,1,-4,-1),Qt::AlignCenter,
                                  metrics.elidedText(text,Qt::ElideRight,screen.width()-8));
                continue;
            }
            bool dimmed=false;
            const QColor fill=fillFor(*tile.node,tile.hue,tile.depth,&dimmed);
            // A folder with a strip writes its name into the strip; the block
            // underneath belongs to its children, so nothing else goes there.
            if(tile.header>0){
                const qreal band=tile.header*m_zoom;
                if(screen.width()<36||band<8)continue;
                const QRectF strip(screen.left()+5,screen.top(),screen.width()-10,band);
                QColor ink=chromeInk();
                if(dimmed)ink.setAlphaF(0.55);
                painter->setPen(ink);
                const QString label=QString::fromStdWString(tile.node->name)+"  "
                                    +QString::fromStdString(diskview::formatBytes(tile.node->logical,true));
                painter->drawText(strip,Qt::AlignLeft|Qt::AlignVCenter,
                                  metrics.elidedText(label,Qt::ElideRight,strip.width()));
                continue;
            }
            // A collapsed folder and a file are both one block: the folder
            // keeps its name and size on one line, the file gets two.
            if(screen.width()<40||screen.height()<12)continue;
            QColor ink=diskviewqml::textOnColor(fill);
            if(dimmed)ink.setAlphaF(0.45);
            painter->setPen(ink);
            const QRectF inner=screen.adjusted(5,2,-4,-2);
            const QString name=QString::fromStdWString(tile.node->name);
            if(tile.node->dir){
                const QString label=name+"  "+QString::fromStdString(diskview::formatBytes(tile.node->logical,true));
                painter->drawText(inner,Qt::AlignLeft|Qt::AlignVCenter,
                                  metrics.elidedText(label,Qt::ElideRight,inner.width()));
                continue;
            }
            painter->drawText(inner,Qt::AlignLeft|Qt::AlignTop,
                              metrics.elidedText(name,Qt::ElideRight,inner.width()));
            if(screen.height()>=32&&!dimmed){
                QRectF sub=inner;sub.setTop(inner.top()+13);
                QColor subColor=ink;subColor.setAlphaF(0.75);
                painter->setPen(subColor);
                painter->drawText(sub,Qt::AlignLeft|Qt::AlignTop,
                                  QString::fromStdString(diskview::formatBytes(tile.node->logical,true)));
            }
        }
        // Pass 3: the selection and the hover come last, over the strips and
        // the labels, so neither can hide them. The ring follows the tile's
        // whole boundary, which for a folder is the frame of all it holds.
        for(const auto& tile:m_tiles){
            if(tile.remainder)continue;
            const bool selected=!selectedPath.empty()&&tile.path==selectedPath;
            const bool hovered=!hoverPath.empty()&&tile.path==hoverPath;
            if(!selected&&!hovered)continue;
            const QRectF rect(origin.x()+tile.rect.x,origin.y()+tile.rect.y,tile.rect.w,tile.rect.h);
            const QRectF screen(toScreen(rect.topLeft()),toScreen(rect.bottomRight()));
            const QRectF ring=screen.adjusted(1,1,-1,-1);
            // Keep the title strip neutral; confine the selected accent to a
            // compact label badge so both sides remain part of the chrome.
            if(selected&&tile.header>0){
                const qreal band=tile.header*m_zoom;
                const QRectF strip(screen.left(),screen.top(),screen.width(),band);
                const QString label=QString::fromStdWString(tile.node->name)+"  "
                                    +QString::fromStdString(diskview::formatBytes(tile.node->logical,true));
                 // Selection is already communicated by the halo and ring.
                 // Keep the title strip in the same neutral chrome as every
                 // other folder; selected text should not invert.
            }
            // A soft halo outside the block first: it reads on any background,
            // and the strong ring then sits on the block's own edge.
            QColor halo=m_accent;
            halo.setAlphaF(selected?0.45:0.35);
            painter->setPen(QPen(halo,4));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(ring.adjusted(-2,-2,2,2));
            painter->setPen(QPen(accentColor(selected),selected?2:1));
            painter->drawRect(ring);
            if(selected){
                // An inner hairline in the opposite direction keeps the ring
                // visible even on a block of the same colour as the accent.
                painter->setPen(QPen(m_theme==QStringLiteral("dark")?QColor(0,0,0,190):QColor(255,255,255,220),1));
                painter->drawRect(ring.adjusted(2,2,-2,-2));
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
            const QColor fill=fillFor(*slice.node,slice.hue,slice.depth,&dimmed);
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
    const qreal factor = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    const qreal wanted = std::clamp(m_zoom * factor, 1.0, 12.0);
    if (qFuzzyCompare(wanted, m_zoom)) { e->accept(); return; }
    // Keep the point under the pointer where it is, so a wheel turn inspects
    // that spot instead of drifting the whole picture.
    const QPointF position = e->position();
    const QPointF transformCenter(width() / 2, height() / 2);
    const QPointF local = (position - m_pan - transformCenter) / m_zoom + transformCenter;
    setZoom(wanted);
    QPointF pan = position - transformCenter - (local - transformCenter) * m_zoom;
    // Do not let the picture be dragged completely out of view.
    const qreal limitX = std::max<qreal>(0, (m_zoom - 1) * width() / 2);
    const qreal limitY = std::max<qreal>(0, (m_zoom - 1) * height() / 2);
    pan.setX(std::clamp(pan.x(), -limitX, limitX));
    pan.setY(std::clamp(pan.y(), -limitY, limitY));
    setPan(pan);
    e->accept();
}
