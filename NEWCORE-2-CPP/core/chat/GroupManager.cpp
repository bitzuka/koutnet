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
        .arg(QRandomGenerator::global()->bounded(9999), 4, 10, QChar('0'));

    QVariantMap g;
    g["name"] = name;
    g["creator"] = creatorIp;
    g["members"] = QVariantList{creatorIp};
    g["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    m_groups.insert(gid, g);
    save();
    emit groupsChanged();
    return gid;
}

void GroupManager::addMember(const QString &gid, const QString &ip)
{
    auto it = m_groups.find(gid);
    if (it == m_groups.end())
        return;
    QVariantList members = it->value("members").toList();
    if (!members.contains(ip)) {
        members.append(ip);
        (*it)["members"] = members;
        save();
        emit groupsChanged();
    }
}

void GroupManager::removeMember(const QString &gid, const QString &ip)
{
    auto it = m_groups.find(gid);
    if (it == m_groups.end())
        return;
    QVariantList members = it->value("members").toList();
    members.removeAll(ip);
    (*it)["members"] = members;
    save();
    emit groupsChanged();
}

void GroupManager::deleteGroup(const QString &gid)
{
    if (m_groups.remove(gid) > 0) {
        save();
        emit groupsChanged();
    }
}

void GroupManager::rename(const QString &gid, const QString &newName)
{
    auto it = m_groups.find(gid);
    if (it == m_groups.end())
        return;
    (*it)["name"] = newName;
    save();
    emit groupsChanged();
}

QVariantMap GroupManager::get(const QString &gid) const
{
    return m_groups.value(gid);
}

QVariantList GroupManager::listFor(const QString &ip) const
{
    QVariantList out;
    for (auto it = m_groups.constBegin(); it != m_groups.constEnd(); ++it) {
        if (it.value().value("members").toList().contains(ip)) {
            QVariantMap g = it.value();
            g["gid"] = it.key();
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
