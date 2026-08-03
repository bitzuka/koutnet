// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
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

Q_SIGNALS:
    void groupsChanged();

private:
    QString filePath() const;
    void load();
    void save();

    QHash<QString, QVariantMap> m_groups; // gid -> group data
    // set when groups.json exists but will not parse, which blocks saving so
    // a corrupt file is never traded for an empty one
    bool m_loadFailed = false;
};
