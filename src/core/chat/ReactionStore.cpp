// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "ReactionStore.h"

#include "koutnet_chat_debug.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

ReactionStore::ReactionStore(QObject *parent)
    : QObject(parent)
{
    m_saveTimer.setSingleShot(true);
    connect(&m_saveTimer, &QTimer::timeout, this, &ReactionStore::save);
    load();
}

QString ReactionStore::makeKey(const QString &chatId, double ts)
{
    return chatId + QStringLiteral("|||") + QString::number(ts, 'f', 3);
}

void ReactionStore::add(const QString &chatId, double ts, const QString &emoji, const QString &username)
{
    const QString key = makeKey(chatId, ts);
    QStringList &users = m_data[key][emoji];
    if (!users.contains(username))
        users.append(username);
    Q_EMIT reactionsChanged(chatId, ts);
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
    Q_EMIT reactionsChanged(chatId, ts);
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
        m[QStringLiteral("emoji")] = p.first;
        m[QStringLiteral("count")] = p.second;
        out.append(m);
    }
    return out;
}

void ReactionStore::save()
{
    if (m_loadFailed) {
        qCWarning(KOUTNET_LOG_CHAT) << "not saving, the file on disk failed to parse";
        return;
    }

    QJsonObject root;
    for (auto it = m_data.constBegin(); it != m_data.constEnd(); ++it) {
        QJsonObject emojiObj;
        for (auto eit = it.value().constBegin(); eit != it.value().constEnd(); ++eit)
            emojiObj[eit.key()] = QJsonArray::fromStringList(eit.value());
        root[it.key()] = emojiObj;
    }

    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    // debounced full rewrite, so it has to be atomic: the old file stays put
    // until the new one is complete
    QSaveFile f(dataDir + QStringLiteral("/reactions.json"));
    if (!f.open(QIODevice::WriteOnly)) {
        qCWarning(KOUTNET_LOG_CHAT) << "save failed:" << f.fileName() << f.errorString();
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!f.commit())
        qCWarning(KOUTNET_LOG_CHAT) << "commit failed:" << f.fileName() << f.errorString();
}

void ReactionStore::load()
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QFile f(dataDir + QStringLiteral("/reactions.json"));
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return;

    const QByteArray raw = f.readAll();
    f.close();
    if (raw.trimmed().isEmpty())
        return; // nothing written yet, saving over it is fine

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (!doc.isObject()) {
        // reactions the user actually made are in there somewhere, so keep the
        // file and refuse to save rather than silently starting from empty
        m_loadFailed = true;
        qCWarning(KOUTNET_LOG_CHAT) << "refusing to overwrite unreadable" << f.fileName() << err.errorString();
        return;
    }

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
