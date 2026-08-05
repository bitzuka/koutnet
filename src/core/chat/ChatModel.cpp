// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "ChatModel.h"
#include "HistoryManager.h"
#include "ReactionStore.h"
#include "UnreadManager.h"

#include <QDateTime>

ChatModel::ChatModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ChatModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_messages.size();
}

QVariant ChatModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_messages.size())
        return {};

    const MessageEntry &m = m_messages.at(index.row());
    switch (role) {
    case SenderRole:
        return m.sender;
    case TextRole:
        return m.text;
    case IsOwnRole:
        return m.isOwn;
    case ColorRole:
        return m.color;
    case MsgTypeRole:
        return m.msgType;
    case IsSystemRole:
        return m.isSystem;
    case IsEditedRole:
        return m.isEdited;
    case ReplyToTextRole:
        return m.replyToText;
    case ReplyToSenderRole:
        return m.replyToSender;
    case ReplyToIdRole:
        return m.replyToId;
    case MsgIdRole:
        return m.msgId;
    case IsReadRole:
        return m.isRead;
    case IsFileRole:
        return m.isFile;
    case FilePathRole:
        return m.filePath;
    case IsImageRole:
        return m.isImage;
    case ReactionsRole:
        return m_reactions ? m_reactions->summary(m.chatId.isEmpty() ? QStringLiteral("public") : m.chatId, m.ts) : QVariantList();
    case TimeStringRole:
        return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(m.ts)).toString(QStringLiteral("HH:mm"));
    case StampSecsRole:
        return m.ts;
    case ShowAuthorRole:
        return startsRun(index.row());
    case ShowDayRole:
        return startsDay(index.row());
    default:
        return {};
    }
}

bool ChatModel::startsRun(int row) const
{
    const MessageEntry &m = m_messages.at(row);
    // A system line is nobody's message and never carries a header.
    if (m.isSystem)
        return false;
    if (row == 0)
        return true;

    const MessageEntry &prev = m_messages.at(row - 1);
    // A run that a system line interrupted has to start again, or the message
    // under the notice looks like it came before it.
    if (prev.isSystem)
        return true;
    if (prev.isOwn != m.isOwn || prev.sender != m.sender)
        return true;
    if (m.ts - prev.ts > kRunGapSecs)
        return true;
    return startsDay(row);
}

bool ChatModel::startsDay(int row) const
{
    if (row == 0)
        return true;
    const QDate today = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(m_messages.at(row).ts)).date();
    const QDate before = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(m_messages.at(row - 1).ts)).date();
    return today != before;
}

QHash<int, QByteArray> ChatModel::roleNames() const
{
    return {
        {SenderRole, "sender"},
        {TextRole, "text"},
        {IsOwnRole, "isOwn"},
        {ColorRole, "color"},
        {MsgTypeRole, "msgType"},
        {IsSystemRole, "isSystem"},
        {IsEditedRole, "isEdited"},
        {ReplyToTextRole, "replyToText"},
        {ReplyToSenderRole, "replyToSender"},
        {ReplyToIdRole, "replyToId"},
        {MsgIdRole, "msgId"},
        {IsReadRole, "isRead"},
        {ReactionsRole, "reactions"},
        {TimeStringRole, "timeString"},
        {IsFileRole, "isFile"},
        {FilePathRole, "filePath"},
        {IsImageRole, "isImage"},
        {StampSecsRole, "stampSecs"},
        {ShowAuthorRole, "showAuthor"},
        {ShowDayRole, "showDay"},
    };
}

void ChatModel::setChatId(const QString &id)
{
    if (m_chatId == id)
        return;
    m_chatId = id;
    Q_EMIT chatIdChanged();
    reload();
    Q_EMIT unreadCountChanged();
}

QObject *ChatModel::historyManagerObj() const
{
    return m_history;
}
void ChatModel::setHistoryManagerObj(QObject *obj)
{
    auto *h = qobject_cast<HistoryManager *>(obj);
    if (m_history == h)
        return;
    m_history = h;
    Q_EMIT historyManagerChanged();
    reload();
}

QObject *ChatModel::reactionStoreObj() const
{
    return m_reactions;
}
void ChatModel::setReactionStoreObj(QObject *obj)
{
    auto *r = qobject_cast<ReactionStore *>(obj);
    if (m_reactions == r)
        return;
    if (m_reactions)
        disconnect(m_reactions, nullptr, this, nullptr);
    m_reactions = r;
    if (m_reactions) {
        connect(m_reactions, &ReactionStore::reactionsChanged, this, [this](const QString &chatId, double ts) {
            const QString cid = m_chatId.isEmpty() ? QStringLiteral("public") : m_chatId;
            if (chatId != cid)
                return;
            for (int i = 0; i < m_messages.size(); ++i) {
                if (m_messages.at(i).ts == ts) {
                    refreshRow(i);
                    break;
                }
            }
        });
    }
    Q_EMIT reactionStoreChanged();
}

QObject *ChatModel::unreadManagerObj() const
{
    return m_unread;
}
void ChatModel::setUnreadManagerObj(QObject *obj)
{
    auto *u = qobject_cast<UnreadManager *>(obj);
    if (m_unread == u)
        return;
    if (m_unread)
        disconnect(m_unread, nullptr, this, nullptr);
    m_unread = u;
    if (m_unread) {
        connect(m_unread, &UnreadManager::unreadChanged, this, [this](const QString &chatId, int) {
            if (chatId == m_chatId)
                Q_EMIT unreadCountChanged();
        });
    }
    Q_EMIT unreadManagerChanged();
    Q_EMIT unreadCountChanged();
}

int ChatModel::unreadCount() const
{
    if (!m_unread || m_chatId.isEmpty())
        return 0;
    return m_unread->get(m_chatId);
}

int ChatModel::firstUnreadRow() const
{
    int remaining = unreadCount();
    if (remaining <= 0)
        return -1;

    // Own messages and system lines were never counted into the tally, so they
    // are skipped rather than consumed - counting them would land the marker
    // some way past the message the user has actually not read.
    int row = -1;
    for (int i = m_messages.size() - 1; i >= 0 && remaining > 0; --i) {
        const MessageEntry &m = m_messages.at(i);
        if (m.isOwn || m.isSystem)
            continue;
        row = i;
        --remaining;
    }
    return row;
}

void ChatModel::reload()
{
    beginResetModel();
    m_messages.clear();
    if (m_history && !m_chatId.isEmpty()) {
        const QVariantList raw = m_history->load(m_chatId);
        m_messages.reserve(raw.size());
        for (const QVariant &v : raw)
            m_messages.append(MessageEntry::fromJson(QJsonObject::fromVariantMap(v.toMap())));
    }
    endResetModel();
}

void ChatModel::appendEntry(MessageEntry e, bool persist)
{
    e.chatId = m_chatId;
    e.ensureMsgId();

    const int row = m_messages.size();
    beginInsertRows(QModelIndex(), row, row);
    m_messages.append(e);
    endInsertRows();

    if (persist && m_history)
        m_history->append(m_chatId, e.toVariantMap());

    if (!e.isOwn && !e.isSystem && m_unread && !m_chatId.isEmpty())
        m_unread->increment(m_chatId);

    // After the unread bump, so a listener reading the count sees the new one.
    // System messages are the app talking to itself and have no place in a
    // conversation preview.
    if (!e.isSystem && !m_chatId.isEmpty())
        Q_EMIT messageAdded(m_chatId, e.text, e.isOwn, e.ts);
}

void ChatModel::sendMessage(const QString &text, const QString &replyToText, const QString &replyToSender, const QString &replyToId)
{
    if (text.trimmed().isEmpty())
        return;
    MessageEntry e;
    e.text = text;
    e.ts = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    e.isOwn = true;
    e.replyToText = replyToText;
    e.replyToSender = replyToSender;
    e.replyToId = replyToId;
    appendEntry(e, true);
}

int ChatModel::rowForMsgId(const QString &msgId) const
{
    if (msgId.isEmpty())
        return -1;
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages.at(i).msgId == msgId)
            return i;
    }
    return -1;
}

void ChatModel::sendFile(const QString &filePath, bool isImage)
{
    MessageEntry e;
    e.text = filePath.section(QLatin1Char('/'), -1);
    e.ts = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    e.isOwn = true;
    e.isFile = true;
    e.filePath = filePath;
    e.isImage = isImage;
    appendEntry(e, true);
}

void ChatModel::receiveMessage(const QString &text, const QString &sender)
{
    MessageEntry e;
    e.text = text;
    e.sender = sender;
    e.ts = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    e.isOwn = false;
    appendEntry(e, true);
}

void ChatModel::receiveFile(const QString &filePath, bool isImage, const QString &sender)
{
    MessageEntry e;
    e.text = filePath.section(QLatin1Char('/'), -1);
    e.sender = sender;
    e.ts = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    e.isOwn = false;
    e.isFile = true;
    e.filePath = filePath;
    e.isImage = isImage;
    appendEntry(e, true);
}

void ChatModel::appendSystemMessage(const QString &text)
{
    MessageEntry e;
    e.text = text;
    e.ts = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    e.isSystem = true;
    appendEntry(e, false);
}

void ChatModel::toggleReaction(int row, const QString &emoji, const QString &username)
{
    if (row < 0 || row >= m_messages.size() || !m_reactions)
        return;
    const MessageEntry &m = m_messages.at(row);
    m_reactions->toggle(m.chatId.isEmpty() ? QStringLiteral("public") : m.chatId, m.ts, emoji, username);
}

void ChatModel::markOwnMessagesRead()
{
    bool any = false;
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages[i].isOwn && !m_messages[i].isRead) {
            m_messages[i].isRead = true;
            any = true;
            const QModelIndex idx = index(i);
            Q_EMIT dataChanged(idx, idx, {IsReadRole});
        }
    }
    if (!any || !m_history || m_chatId.isEmpty())
        return;

    // Persist the read flags. The loop that used to be here had Q_UNUSED(m) for
    // a body, because there was no way to rewrite an entry already stored - so a
    // receipt lasted until the next reload and then the ticks came back.
    // HistoryManager::replaceAll() is that way.
    //
    // System messages are skipped rather than written out: appendEntry() never
    // persisted them, so they are in this list but not in the log, and a rewrite
    // for an unrelated reason is not the place to start storing them.
    QVariantList persisted;
    persisted.reserve(m_messages.size());
    for (int i = 0; i < m_messages.size(); ++i) {
        if (!m_messages.at(i).isSystem)
            persisted.append(m_messages.at(i).toVariantMap());
    }
    m_history->replaceAll(m_chatId, persisted);
}

void ChatModel::markAllRead()
{
    if (m_unread && !m_chatId.isEmpty())
        m_unread->markRead(m_chatId);
}

void ChatModel::refreshRow(int row)
{
    const QModelIndex idx = index(row);
    Q_EMIT dataChanged(idx, idx, {ReactionsRole});
}
