// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "HistoryManager.h"

#include "koutnet_chat_debug.h"
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace
{
// How much of the readable part of a chat id survives into the file name. A
// room alias can be longer than a file name is allowed to be, and the digest is
// what carries the identity, so the readable part is free to be cut.
constexpr int kMaxStemChars = 48;
// Hex characters of SHA-256. Forty-eight bits is far past the point where a
// collision is a realistic way to lose a conversation.
constexpr int kDigestChars = 12;
} // namespace

HistoryManager::HistoryManager(QObject *parent)
    : QObject(parent)
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

QString HistoryManager::legacyStemFor(const QString &chatId)
{
    static const QRegularExpression unsafe(QStringLiteral("[^\\w\\-]"));
    QString safe = chatId;
    safe.replace(unsafe, QStringLiteral("_"));
    return safe;
}

QString HistoryManager::stemFor(const QString &chatId)
{
    const QString safe = legacyStemFor(chatId);
    // Nothing was replaced and the name is short enough to be one: this id is
    // already unique among file names. Returning it untouched is what keeps
    // every log written before the digest existed readable.
    if (safe == chatId && safe.size() <= kMaxStemChars)
        return safe;

    const QString digest = QString::fromLatin1(QCryptographicHash::hash(chatId.toUtf8(), QCryptographicHash::Sha256).toHex().left(kDigestChars));
    return safe.left(kMaxStemChars) + QLatin1Char('-') + digest;
}

QString HistoryManager::filePathFor(const QString &chatId)
{
    const QDir dir = historyDir();
    const QString path = dir.filePath(stemFor(chatId) + QStringLiteral(".json"));

    if (m_namesChecked.contains(chatId))
        return path;
    m_namesChecked.insert(chatId);

    const QString legacy = dir.filePath(legacyStemFor(chatId) + QStringLiteral(".json"));
    // A log that has stopped reading back is indistinguishable from a lost one,
    // so the file moves with the scheme rather than being abandoned by it. Once
    // per chat per run, and never over a file that is already there - two ids
    // that used to share a name cannot both claim it.
    if (legacy != path && QFile::exists(legacy) && !QFile::exists(path)) {
        if (!QFile::rename(legacy, path))
            qCWarning(KOUTNET_LOG_CHAT) << "could not move" << legacy << "to" << path;
    }
    return path;
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
            // still the user's log: never write over the file again
            m_unreadable.insert(chatId);
            qCWarning(KOUTNET_LOG_CHAT) << "refusing to overwrite unreadable" << f.fileName() << err.errorString();
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

    // load() first, for a chat this process has not read yet: reading is what
    // discovers a file that will not parse, and writeChatFile() refuses those
    load(chatId);
    m_cache.insert(chatId, msgs);
    writeChatFile(chatId, msgs);
}

void HistoryManager::writeChatFile(const QString &chatId, const QVariantList &msgs)
{
    if (m_unreadable.contains(chatId))
        return;

    // QSaveFile because a crash halfway through the old QFile write left the
    // chat truncated
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

QVariantList HistoryManager::loadChatIndex()
{
    return load(QStringLiteral("__chat_index__"));
}

void HistoryManager::saveChatIndex(const QVariantList &entries)
{
    replaceAll(QStringLiteral("__chat_index__"), entries);
}
