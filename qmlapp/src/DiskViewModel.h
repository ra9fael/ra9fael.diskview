#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QObject>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

class DiskViewModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentPathChanged)
    Q_PROPERTY(QString totalText READ totalText NOTIFY currentPathChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectedChanged)
    Q_PROPERTY(QVariantMap chartTree READ chartTree NOTIFY currentPathChanged)
    Q_PROPERTY(QString breadcrumbText READ breadcrumbText NOTIFY currentPathChanged)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY currentPathChanged)

public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, SizeRole, SizeTextRole, PercentRole, DirectoryRole, PathRole };
    explicit DiskViewModel(QObject* parent = nullptr);
    Q_INVOKABLE bool loadFixture(const QString& fileName);
    Q_INVOKABLE bool loadTree(const QVariantMap& tree);
    QString currentPath() const;
    QString currentName() const;
    QString totalText() const;
    QString selectedId() const { return m_selectedId; }
    QVariantMap chartTree() const;
    QString breadcrumbText() const;
    int itemCount() const { return m_visible.size(); }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void navigate(const QString& id);
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void select(const QString& id);
    Q_INVOKABLE QString formatSize(qint64 bytes) const;

signals:
    void currentPathChanged();
    void selectedChanged();

private:
    struct Entry { QString id,name,path; quint64 size=0; bool directory=false; int parent=-1; QVector<int> children; };
    int appendNode(const QVariantMap& node, int parent, const QString& path);
    int indexForId(const QString& id) const;
    int currentIndex() const { return m_current; }
    void resetAt(int index);

    QVector<Entry> m_nodes;
    QVector<int> m_visible;
    QVector<int> m_history;
    int m_current=-1;
    QString m_selectedId;
};
