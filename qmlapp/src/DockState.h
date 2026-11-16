#pragma once

#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Only layout metadata lives here. The four existing pane items and the scan
// remain alive when the tree changes; docking never recreates either one.
class DockState : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap layout READ layout WRITE setLayout NOTIFY changed)
public:
    explicit DockState(QObject* parent = nullptr);
    QVariantMap layout() const { return m_layout; }
    void setLayout(const QVariantMap& value);
    static QVariantMap defaultLayout();
    Q_INVOKABLE static bool valid(const QVariantMap& value);
    Q_INVOKABLE void reset();
    Q_INVOKABLE bool movePane(const QString& pane, const QString& target, const QString& edge);
    Q_INVOKABLE bool setRatio(const QString& path, double ratio);
    Q_INVOKABLE bool activate(const QString& pane);
    // Pane placements, tab groups and split handles in workspace coordinates.
    Q_INVOKABLE QVariantList placements(double width, double height) const;
signals:
    void changed();
private:
    QVariantMap m_layout;
};
