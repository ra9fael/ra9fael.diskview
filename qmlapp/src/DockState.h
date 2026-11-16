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
    // The preset the current tree still matches, or empty when the user has
    // dragged something: the menu ticks a preset only while it is untouched.
    Q_PROPERTY(QString preset READ preset NOTIFY changed)
public:
    explicit DockState(QObject* parent = nullptr);
    QVariantMap layout() const { return m_layout; }
    void setLayout(const QVariantMap& value);
    static QVariantMap defaultLayout();
    // A preset by id: lists-top, lists-bottom, lists-left, lists-right.
    Q_INVOKABLE static QVariantMap presetLayout(const QString& id);
    Q_INVOKABLE static QStringList presetIds();
    // Not static: a static Q_INVOKABLE is not reachable through the QML type
    // name, so the window asks the instance it already has.
    Q_INVOKABLE bool valid(const QVariantMap& value) const;
    Q_INVOKABLE QString preset() const;
    Q_INVOKABLE void reset();
    Q_INVOKABLE bool applyPreset(const QString& id);
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
