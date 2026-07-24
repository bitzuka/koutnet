#include "HistoryManager.h"

#include <QStandardPaths>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>

HistoryManager::HistoryManager(QObject *parent) : QObject(parent)
{
    historyDir().mkpath(".");
}

void HistoryManager::setHistorySavingEnabled(bool enabled)
{
    if (m_savingEnabled == enabled)
        return;
    m_savingEnabled = enabled;
    emit historySavingEnabledChanged();
}

QDir HistoryManager::historyDir() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base + "/history");
}

QString HistoryManager::filePathFor(const QString &chatId) const
{
    QString safe = chatId;
    safe.replace(QRegularExpression("[^\\w\\-]"), "_");
    return historyDir().filePath(safe + ".json");
}

QVariantList HistoryManager::load(const QString &chatId)
{
    if (m_cache.contains(chatId))
        return m_cache.value(chatId);

    QVariantList result;
    QFile f(filePathFor(chatId));
    if (f.exists() && f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (doc.isArray())
            result = doc.array().toVariantList();
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

    QFile f(filePathFor(chatId));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(QJsonArray::fromVariantList(msgs)).toJson(QJsonDocument::Indented));
        f.close();
    } else {
        qWarning() << "[HistoryManager] failed to write" << f.fileName();
    }

    emit historyAppended(chatId, entry);
}

QVariantList HistoryManager::loadCallLog()
{
    return load(QStringLiteral("__call_log__"));
}

void HistoryManager::addCall(const QVariantMap &entry)
{
    append(QStringLiteral("__call_log__"), entry);
}
