// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#pragma once

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

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
    Q_INVOKABLE QVariantList listFor(const QString &ip) const;

Q_SIGNALS:
    void groupsChanged();

private:
    QString filePath() const;
    void load();
    void save();

    QHash<QString, QVariantMap> m_groups; // gid -> group data
    // set when groups.json exists but will not parse, which blocks saving
    bool m_loadFailed = false;
};
