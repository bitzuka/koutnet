#include "ReactionStore.h"

#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <algorithm>

ReactionStore::ReactionStore(QObject *parent) : QObject(parent)
{
    m_saveTimer.setSingleShot(true);
    connect(&m_saveTimer, &QTimer::timeout, this, &ReactionStore::save);
    load();
}

QString ReactionStore::makeKey(const QString &chatId, double ts)
{
    return chatId + "|||" + QString::number(ts, 'f', 3);
}

void ReactionStore::add(const QString &chatId, double ts, const QString &emoji, const QString &username)
{
    const QString key = makeKey(chatId, ts);
    QStringList &users = m_data[key][emoji];
    if (!users.contains(username))
        users.append(username);
    emit reactionsChanged(chatId, ts);
}

void ReactionStore::remove(const QString &chatId, double ts, const QString &emoji, const QString &username)
{
    const QString key = makeKey(chatId, ts);
    auto chatIt = m_data.find(key);
    if (chatIt == m_data.end())
        return;
    auto emojiIt = chatIt->find(emoji);
    if (emojiIt == chatIt->end())
        return;
    emojiIt->removeAll(username);
    if (emojiIt->isEmpty())
        chatIt->remove(emoji);
    emit reactionsChanged(chatId, ts);
}

bool ReactionStore::toggle(const QString &chatId, double ts, const QString &emoji, const QString &username)
{
    const QString key = makeKey(chatId, ts);
    const bool present = m_data.value(key).value(emoji).contains(username);
    if (present) {
        remove(chatId, ts, emoji, username);
    } else {
        add(chatId, ts, emoji, username);
    }
    // debounce: restart the timer instead of stacking un-cancellable timers
    m_saveTimer.start(500);
    return !present;
}

QVariantMap ReactionStore::get(const QString &chatId, double ts) const
{
    QVariantMap out;
    const QString key = makeKey(chatId, ts);
    const auto &emojiMap = m_data.value(key);
    for (auto it = emojiMap.constBegin(); it != emojiMap.constEnd(); ++it) {
        QVariantList users;
        for (const QString &u : it.value())
            users.append(u);
        out.insert(it.key(), users);
    }
    return out;
}

QVariantList ReactionStore::summary(const QString &chatId, double ts) const
{
    const QString key = makeKey(chatId, ts);
    const auto &emojiMap = m_data.value(key);

    QVector<QPair<QString, int>> pairs;
    for (auto it = emojiMap.constBegin(); it != emojiMap.constEnd(); ++it) {
        if (!it.value().isEmpty())
            pairs.append({it.key(), it.value().size()});
    }
    std::sort(pairs.begin(), pairs.end(), [](const auto &a, const auto &b) {
        return a.second > b.second;
    });

    QVariantList out;
    for (const auto &p : pairs) {
        QVariantMap m;
        m["emoji"] = p.first;
        m["count"] = p.second;
        out.append(m);
    }
    return out;
}

void ReactionStore::save()
{
    QJsonObject root;
    for (auto it = m_data.constBegin(); it != m_data.constEnd(); ++it) {
        QJsonObject emojiObj;
        for (auto eit = it.value().constBegin(); eit != it.value().constEnd(); ++eit)
            emojiObj[eit.key()] = QJsonArray::fromStringList(eit.value());
        root[it.key()] = emojiObj;
    }

    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    QFile f(dataDir + "/reactions.json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        f.close();
    } else {
        qWarning() << "[ReactionStore] save failed:" << f.fileName();
    }
}

void ReactionStore::load()
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QFile f(dataDir + "/reactions.json");
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return;

    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        QHash<QString, QStringList> emojiMap;
        const QJsonObject emojiObj = it.value().toObject();
        for (auto eit = emojiObj.constBegin(); eit != emojiObj.constEnd(); ++eit) {
            QStringList users;
            for (const QJsonValue &v : eit.value().toArray())
                users.append(v.toString());
            emojiMap.insert(eit.key(), users);
        }
        m_data.insert(it.key(), emojiMap);
    }
}
