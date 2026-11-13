#pragma once

#include <QQuickPaintedItem>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <vector>
#include "../../tree.h"

class ChartItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap treeData READ treeData WRITE setTreeData NOTIFY treeDataChanged)
    Q_PROPERTY(int viewMode READ viewMode WRITE setViewMode NOTIFY viewModeChanged)
    Q_PROPERTY(QString selectedId READ selectedId WRITE setSelectedId NOTIFY selectedIdChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    Q_PROPERTY(QPointF pan READ pan WRITE setPan NOTIFY panChanged)
    Q_PROPERTY(QString centerLabel READ centerLabel WRITE setCenterLabel NOTIFY centerLabelChanged)
    Q_PROPERTY(int depth READ depth WRITE setDepth NOTIFY depthChanged)
    Q_PROPERTY(QString highlightExtension READ highlightExtension WRITE setHighlightExtension NOTIFY highlightChanged)
    Q_PROPERTY(QString hoverId READ hoverId NOTIFY hoverChanged)
    Q_PROPERTY(QPointF hoverPosition READ hoverPosition NOTIFY hoverChanged)

public:
    // What the window needs to describe a node without touching the tree.
    struct NodeInfo { QString name; QString path; quint64 size = 0; bool dir = false; };
    explicit ChartItem(QQuickItem* parent = nullptr);
    QVariantMap treeData() const { return m_treeData; }
    void setTreeData(const QVariantMap& data);
    int viewMode() const { return m_viewMode; }
    void setViewMode(int mode);
    QString selectedId() const { return m_selectedId; }
    void setSelectedId(const QString& id);
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    QPointF pan() const { return m_pan; }
    void setPan(const QPointF& pan);
    QString centerLabel() const { return m_centerLabel; }
    void setCenterLabel(const QString& label);
    // How many levels the chart draws: 1 leaves the folders as whole tiles,
    // which is what makes them clickable at all.
    int depth() const { return m_depth; }
    void setDepth(int depth);    // The extension the legend picked. Every other file is dimmed, so one
    // colour can be followed across the whole chart.
    QString highlightExtension() const { return m_highlightExtension; }
    void setHighlightExtension(const QString& extension);
    QString hoverId() const { return m_hoverId; }
    QPointF hoverPosition() const { return m_hoverPosition; }
    // The name, path, size and kind of the node a region belongs to.
    Q_INVOKABLE QVariantMap hoveredInfo(const QString& id) const;
    // Where to aim a synthetic double click to enter a folder.
    Q_INVOKABLE QPointF firstDirectoryCenter() const;
    Q_INVOKABLE QString hitTestAt(qreal x,qreal y) const { return hitTest(QPointF(x,y)); }
    Q_INVOKABLE QString firstRegionHit() const;
    Q_INVOKABLE bool validateHitTest() const;
    Q_INVOKABLE QString validateInteraction() const;
    Q_INVOKABLE QPointF firstRegionCenter() const;
    Q_INVOKABLE QString hitTestAfterTransform(const QPointF& localPoint, qreal zoom, const QPointF& pan) const;
    Q_INVOKABLE bool validateHitTransform() const;
    Q_INVOKABLE QString validateHitTestDetails() const;
    // Timing for the B3 work: parse, lay out and paint the current tree, so
    // the limits of the painted path can be written down with numbers.
    Q_INVOKABLE QString benchmark(int iterations);

signals:
    void treeDataChanged();
    void viewModeChanged();
    void selectedIdChanged();
    void zoomChanged();
    void panChanged();
    void activated(const QString& id, bool directory);
    void clicked(const QString& id, qreal x, qreal y);
    void hovered(const QString& id);
    void hoverChanged();
    void depthChanged();
    void highlightChanged();
    // A right click on a region, in the item's own coordinates.
    void contextRequested(const QString& id, qreal x, qreal y);
    void centerActivated();
    void centerLabelChanged();

protected:
    void paint(QPainter* painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    struct HitRegion {
        QRectF rect;
        QString id;
        bool directory = false;
        qreal a0 = 0, a1 = 0, r0 = 0, r1 = 0;
        QPointF center;
        bool annular = false;
    };
    void rebuildGeometry();
    QString hitTest(const QPointF& position) const;
    // The fill of a tile or slice: folders are neutral, files take the colour
    // of their extension.
    QColor fillFor(const diskview::Node& node, bool* dimmed = nullptr) const;

    QVariantMap m_treeData;
    QVector<HitRegion> m_regions;
    diskview::Node m_chartRoot;
    QHash<std::wstring, QString> m_pathIds;
    QHash<QString, NodeInfo> m_nodeInfo;
    std::vector<diskview::Tile> m_tiles;
    std::vector<diskview::Slice> m_slices;
    int m_viewMode = 0;
    int m_depth = 1;
    QString m_highlightExtension;
    QString m_selectedId;
    QString m_centerLabel;
    qreal m_zoom = 1.0;
    QPointF m_pan;
    qreal m_innerRadius = 0;
    QString m_hoverId;
    QPointF m_hoverPosition;
    bool m_dragging = false;
    QPointF m_dragStart;
    QPointF m_dragPan;
};
