#include "ChatModel.h"
#include "HistoryManager.h"
#include "ReactionStore.h"
#include "UnreadManager.h"

#include <QDateTime>

ChatModel::ChatModel(QObject *parent) : QAbstractListModel(parent) {}

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
    case SenderRole:         return m.sender;
    case TextRole:           return m.text;
    case IsOwnRole:          return m.isOwn;
    case ColorRole:          return m.color;
    case EmojiRole:          return m.emoji;
    case MsgTypeRole:        return m.msgType;
    case ImageDataRole:      return m.imageData;
    case IsSystemRole:       return m.isSystem;
    case IsEditedRole:       return m.isEdited;
    case IsForwardedRole:    return m.isForwarded;
    case ForwardedFromRole:  return m.forwardedFrom;
    case ReplyToTextRole:    return m.replyToText;
    case MsgIdRole:          return m.msgId;
    case IsReadRole:         return m.isRead;
    case IsFileRole:         return m.isFile;
    case FilePathRole:       return m.filePath;
    case IsImageRole:        return m.isImage;
    case ReactionsRole:
        return m_reactions
            ? m_reactions->summary(m.chatId.isEmpty() ? QStringLiteral("public") : m.chatId, m.ts)
            : QVariantList();
    case TimeStringRole:
        return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(m.ts)).toString("HH:mm");
    default:
        return {};
    }
}

QHash<int, QByteArray> ChatModel::roleNames() const
{
    return {
        {SenderRole, "sender"}, {TextRole, "text"}, {IsOwnRole, "isOwn"},
        {ColorRole, "color"}, {EmojiRole, "emoji"}, {MsgTypeRole, "msgType"},
        {ImageDataRole, "imageData"}, {IsSystemRole, "isSystem"},
        {IsEditedRole, "isEdited"}, {IsForwardedRole, "isForwarded"},
        {ForwardedFromRole, "forwardedFrom"}, {ReplyToTextRole, "replyToText"},
        {MsgIdRole, "msgId"}, {IsReadRole, "isRead"}, {ReactionsRole, "reactions"},
        {TimeStringRole, "timeString"}, {IsFileRole, "isFile"},
        {FilePathRole, "filePath"}, {IsImageRole, "isImage"},
    };
}

void ChatModel::setChatId(const QString &id)
{
    if (m_chatId == id)
        return;
    m_chatId = id;
    emit chatIdChanged();
    reload();
}

QObject *ChatModel::historyManagerObj() const { return m_history; }
void ChatModel::setHistoryManagerObj(QObject *obj)
{
    auto *h = qobject_cast<HistoryManager *>(obj);
    if (m_history == h) return;
    m_history = h;
    emit historyManagerChanged();
    reload();
}

QObject *ChatModel::reactionStoreObj() const { return m_reactions; }
void ChatModel::setReactionStoreObj(QObject *obj)
{
    auto *r = qobject_cast<ReactionStore *>(obj);
    if (m_reactions == r) return;
    if (m_reactions) disconnect(m_reactions, nullptr, this, nullptr);
    m_reactions = r;
    if (m_reactions) {
        connect(m_reactions, &ReactionStore::reactionsChanged, this,
                [this](const QString &chatId, double ts) {
                    const QString cid = m_chatId.isEmpty() ? QStringLiteral("public") : m_chatId;
                    if (chatId != cid) return;
                    for (int i = 0; i < m_messages.size(); ++i) {
                        if (m_messages.at(i).ts == ts) { refreshRow(i); break; }
                    }
                });
    }
    emit reactionStoreChanged();
}

QObject *ChatModel::unreadManagerObj() const { return m_unread; }
void ChatModel::setUnreadManagerObj(QObject *obj)
{
    auto *u = qobject_cast<UnreadManager *>(obj);
    if (m_unread == u) return;
    m_unread = u;
    emit unreadManagerChanged();
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
}

void ChatModel::sendMessage(const QString &text, const QString &replyToText)
{
    if (text.trimmed().isEmpty())
        return;
    MessageEntry e;
    e.text = text;
    e.ts = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    e.isOwn = true;
    e.replyToText = replyToText;
    appendEntry(e, true);
}

void ChatModel::sendFile(const QString &filePath, bool isImage)
{
    MessageEntry e;
    e.text = filePath.section('/', -1);
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
    e.text = filePath.section('/', -1);
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
            emit dataChanged(idx, idx, {IsReadRole});
        }
    }
    // Persist read-state by rewriting full history for this chat.
    if (any && m_history) {
        for (const auto &m : m_messages)
            Q_UNUSED(m); // history append-only API; full rewrite deferred to a future pass
    }
}

void ChatModel::markAllRead()
{
    if (m_unread && !m_chatId.isEmpty())
        m_unread->markRead(m_chatId);
}

void ChatModel::refreshRow(int row)
{
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {ReactionsRole});
}
