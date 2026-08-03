// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "HistoryManager.h"

#include <QStandardPaths>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QDebug>
#include "koutnet_chat_debug.h"

HistoryManager::HistoryManager(QObject *parent) : QObject(parent)
{
    historyDir().mkpath(QStringLiteral("."));
}

void HistoryManager::setHistorySavingEnabled(bool enabled)
{
    if (m_savingEnabled == enabled)
        return;
    m_savingEnabled = enabled;
    Q_EMIT historySavingEnabledChanged();
}

QDir HistoryManager::historyDir() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base + QStringLiteral("/history"));
}

QString HistoryManager::filePathFor(const QString &chatId) const
{
    QString safe = chatId;
    safe.replace(QRegularExpression(QStringLiteral("[^\\w\\-]")), QStringLiteral("_"));
    return historyDir().filePath(safe + QStringLiteral(".json"));
}

QVariantList HistoryManager::load(const QString &chatId)
{
    if (m_cache.contains(chatId))
        return m_cache.value(chatId);

    QVariantList result;
    QFile f(filePathFor(chatId));
    if (f.exists() && f.open(QIODevice::ReadOnly)) {
        const QByteArray raw = f.readAll();
        f.close();
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
        if (doc.isArray()) {
            result = doc.array().toVariantList();
        } else if (!raw.trimmed().isEmpty()) {
            // whatever is in there is still the user's log, so keep the chat
            // usable in memory but never write over the file again
            m_unreadable.insert(chatId);
            qCWarning(KOUTNET_LOG_CHAT) << "refusing to overwrite unreadable" << f.fileName()
                                        << err.errorString();
        }
    }
    m_cache.insert(chatId, result);
    return result;
}

void HistoryManager::append(const QString &chatId, const QVariantMap &entry)
{
    if (!m_savingEnabled)
        return;

    QVariantList msgs = load(chatId);
    msgs.append(entry);
    if (msgs.size() > kMaxMessagesPerChat)
        msgs = msgs.mid(msgs.size() - kMaxMessagesPerChat);

    m_cache.insert(chatId, msgs);
    writeChatFile(chatId, msgs);

    Q_EMIT historyAppended(chatId, entry);
}

void HistoryManager::replaceAll(const QString &chatId, const QVariantList &entries)
{
    if (!m_savingEnabled)
        return;

    QVariantList msgs = entries;
    if (msgs.size() > kMaxMessagesPerChat)
        msgs = msgs.mid(msgs.size() - kMaxMessagesPerChat);

    // load() first, for a chat this process has not read yet: reading the file
    // is what discovers that it will not parse, and writeChatFile() refuses to
    // touch one of those. Without this, a damaged log would be overwritten with
    // whatever the caller happens to be holding.
    load(chatId);
    m_cache.insert(chatId, msgs);
    writeChatFile(chatId, msgs);
}

void HistoryManager::writeChatFile(const QString &chatId, const QVariantList &msgs)
{
    if (m_unreadable.contains(chatId))
        return;

    // QSaveFile because this rewrites the whole log every message: a crash
    // halfway through the old QFile write left the chat truncated
    QSaveFile f(filePathFor(chatId));
    if (!f.open(QIODevice::WriteOnly)) {
        qCWarning(KOUTNET_LOG_CHAT) << "failed to write" << f.fileName() << f.errorString();
        return;
    }
    f.write(QJsonDocument(QJsonArray::fromVariantList(msgs)).toJson(QJsonDocument::Indented));
    if (!f.commit())
        qCWarning(KOUTNET_LOG_CHAT) << "commit failed" << f.fileName() << f.errorString();
}

QVariantList HistoryManager::loadCallLog()
{
    return load(QStringLiteral("__call_log__"));
}

void HistoryManager::addCall(const QVariantMap &entry)
{
    append(QStringLiteral("__call_log__"), entry);
}
