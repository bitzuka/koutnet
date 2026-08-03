// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "GroupManager.h"

#include <QStandardPaths>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QRandomGenerator>
#include <QDebug>
#include "koutnet_chat_debug.h"

GroupManager::GroupManager(QObject *parent) : QObject(parent)
{
    load();
}

QString GroupManager::filePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/groups.json");
}

QString GroupManager::createGroup(const QString &name, const QString &creatorIp)
{
    const QString gid = QStringLiteral("g_%1_%2")
        .arg(QDateTime::currentSecsSinceEpoch())
        // two groups created in the same second must not collide, so this is
        // wider than the old 4 digits and comes from the system generator
        .arg(QRandomGenerator::system()->generate(), 8, 16, QLatin1Char('0'));

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
    const QByteArray raw = f.readAll();
    f.close();
    if (raw.trimmed().isEmpty())
        return; // nothing written yet, saving over it is fine

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        // a truncated or hand-edited file is still the user's group list, so
        // block saving rather than replacing it with whatever we managed to read
        m_loadFailed = true;
        qCWarning(KOUTNET_LOG_CHAT) << "refusing to overwrite unreadable" << f.fileName()
                                    << err.errorString();
        return;
    }
    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it)
        m_groups.insert(it.key(), it.value().toObject().toVariantMap());
}

void GroupManager::save()
{
    if (m_loadFailed) {
        qCWarning(KOUTNET_LOG_CHAT) << "not saving, the file on disk failed to parse";
        return;
    }

    QJsonObject root;
    for (auto it = m_groups.constBegin(); it != m_groups.constEnd(); ++it)
        root[it.key()] = QJsonObject::fromVariantMap(it.value());

    // QSaveFile so a crash or a full disk mid-write leaves the old list intact
    QSaveFile f(filePath());
    if (!f.open(QIODevice::WriteOnly)) {
        qCWarning(KOUTNET_LOG_CHAT) << "save failed:" << f.fileName() << f.errorString();
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit())
        qCWarning(KOUTNET_LOG_CHAT) << "commit failed:" << f.fileName() << f.errorString();
}
