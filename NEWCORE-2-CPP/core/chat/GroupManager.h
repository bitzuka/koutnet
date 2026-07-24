#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QHash>
#include <QVariantMap>
#include <QVariantList>

// Manages group chat metadata (name, creator, members, created date),
// persisted as JSON.
class GroupManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit GroupManager(QObject *parent = nullptr);

    static GroupManager *create(QQmlEngine *, QJSEngine *)
    {
        return new GroupManager;
    }

    Q_INVOKABLE QString createGroup(const QString &name, const QString &creatorIp);
    Q_INVOKABLE void addMember(const QString &gid, const QString &ip);
    Q_INVOKABLE void removeMember(const QString &gid, const QString &ip);
    Q_INVOKABLE void deleteGroup(const QString &gid);
    Q_INVOKABLE void rename(const QString &gid, const QString &newName);
    Q_INVOKABLE QVariantMap get(const QString &gid) const;
    // Returns list of { gid, name, creator, members, created } maps
    // for every group the given ip is a member of.
    Q_INVOKABLE QVariantList listFor(const QString &ip) const;

signals:
    void groupsChanged();

private:
    QString filePath() const;
    void load();
    void save();

    QHash<QString, QVariantMap> m_groups; // gid -> group data
};
