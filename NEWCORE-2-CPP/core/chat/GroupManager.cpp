// SPDX-FileCopyrightText: 2026 bitzuka <matveypotyzhno@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "GroupManager.h"

#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QRandomGenerator>
#include <QDebug>

GroupManager::GroupManager(QObject *parent) : QObject(parent)
{
    load();
}

QString GroupManager::filePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/groups.json";
}

QString GroupManager::createGroup(const QString &name, const QString &creatorIp)
{
    const QString gid = QStringLiteral("g_%1_%2")
        .arg(QDateTime::currentSecsSinceEpoch())
        .arg(QRandomGenerator::global()->bounded(9999), 4, 10, QLatin1Char('0'));

    QVariantMap g;
    g[QStringLiteral("name")] = name;
    g[QStringLiteral("creator")] = creatorIp;
    g[QStringLiteral("members")] = QVariantList{creatorIp};
    g[QStringLiteral("created")] = QDateTime::currentDateTime().toString(Qt::ISODate);

    m_groups.insert(gid, g);
    save();
    Q_EMIT groupsChanged();
    return gid;
}

void GroupManager::addMember(const QString &gid, const QString &ip)
{
    auto it = m_groups.find(gid);
    if (it == m_groups.end())
        return;
    QVariantList members = it->value(QStringLiteral("members")).toList();
    if (!members.contains(ip)) {
        members.append(ip);
        (*it)[QStringLiteral("members")] = members;
        save();
        Q_EMIT groupsChanged();
    }
}

void GroupManager::removeMember(const QString &gid, const QString &ip)
{
    auto it = m_groups.find(gid);
    if (it == m_groups.end())
        return;
    QVariantList members = it->value(QStringLiteral("members")).toList();
    members.removeAll(ip);
    (*it)[QStringLiteral("members")] = members;
    save();
    Q_EMIT groupsChanged();
}

void GroupManager::deleteGroup(const QString &gid)
{
    if (m_groups.remove(gid) > 0) {
        save();
        Q_EMIT groupsChanged();
    }
}

void GroupManager::rename(const QString &gid, const QString &newName)
{
    auto it = m_groups.find(gid);
    if (it == m_groups.end())
        return;
    (*it)[QStringLiteral("name")] = newName;
    save();
    Q_EMIT groupsChanged();
}

QVariantMap GroupManager::get(const QString &gid) const
{
    return m_groups.value(gid);
}

QVariantList GroupManager::listFor(const QString &ip) const
{
    QVariantList out;
    for (auto it = m_groups.constBegin(); it != m_groups.constEnd(); ++it) {
        if (it.value().value(QStringLiteral("members")).toList().contains(ip)) {
            QVariantMap g = it.value();
            g[QStringLiteral("gid")] = it.key();
            out.append(g);
        }
    }
    return out;
}

void GroupManager::load()
{
    QFile f(filePath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return;
    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it)
        m_groups.insert(it.key(), it.value().toObject().toVariantMap());
}

void GroupManager::save()
{
    QJsonObject root;
    for (auto it = m_groups.constBegin(); it != m_groups.constEnd(); ++it)
        root[it.key()] = QJsonObject::fromVariantMap(it.value());

    QFile f(filePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.close();
    } else {
        qWarning() << "[GroupManager] save failed:" << f.fileName();
    }
}
