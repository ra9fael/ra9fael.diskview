#pragma once

#include <QQuickPaintedItem>
#include <QVariantList>
#include <vector>
#include "../../tree.h"

class ChartItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries WRITE setEntries NOTIFY entriesChanged)
    Q_PROPERTY(QVariantMap treeData READ treeData WRITE setTreeData NOTIFY treeDataChanged)
    Q_PROPERTY(int viewMode READ viewMode WRITE setViewMode NOTIFY viewModeChanged)
    Q_PROPERTY(QString selectedId READ selectedId WRITE setSelectedId NOTIFY selectedIdChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    Q_PROPERTY(QPointF pan READ pan WRITE setPan NOTIFY panChanged)
    Q_PROPERTY(QString centerLabel READ centerLabel WRITE setCenterLabel NOTIFY centerLabelChanged)

public:
    explicit ChartItem(QQuickItem* parent = nullptr);
    QVariantList entries() const { return m_entries; }
    void setEntries(const QVariantList& entries);
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
    Q_INVOKABLE QString hitTestAt(qreal x,qreal y) const { return hitTest(QPointF(x,y)); }
    Q_INVOKABLE QString firstRegionHit() const;
    Q_INVOKABLE bool validateHitTest() const;
    Q_INVOKABLE QString validateInteraction() const;
    Q_INVOKABLE QPointF firstRegionCenter() const;
    Q_INVOKABLE QString hitTestAfterTransform(const QPointF& localPoint, qreal zoom, const QPointF& pan) const;
    Q_INVOKABLE bool validateHitTransform() const;
    Q_INVOKABLE QString validateHitTestDetails() const;

signals:
    void entriesChanged();
    void treeDataChanged();
    void viewModeChanged();
    void selectedIdChanged();
    void zoomChanged();
    void panChanged();
    void activated(const QString& id, bool directory);
    void hovered(const QString& id);
    void centerActivated();
    void centerLabelChanged();

protected:
    void paint(QPainter* painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
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

    QVariantList m_entries;
    QVariantMap m_treeData;
    QVector<HitRegion> m_regions;
    diskview::Node m_chartRoot;
    QHash<std::wstring, QString> m_pathIds;
    std::vector<diskview::Tile> m_tiles;
    std::vector<diskview::Slice> m_slices;
    int m_viewMode = 0;
    QString m_selectedId;
    QString m_centerLabel;
    qreal m_zoom = 1.0;
    QPointF m_pan;
    bool m_dragging = false;
    QPointF m_dragStart;
    QPointF m_dragPan;
};
