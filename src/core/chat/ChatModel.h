// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#pragma once

#include <QAbstractListModel>
#include <QPointer>
#include <QQmlEngine>
#include <QString>
#include <QVector>

#include "MessageEntry.h"

class HistoryManager;
class ReactionStore;
class UnreadManager;

// QML-facing list model for a single chat's messages.
class ChatModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString chatId READ chatId WRITE setChatId NOTIFY chatIdChanged)
    Q_PROPERTY(QObject *historyManager READ historyManagerObj WRITE setHistoryManagerObj NOTIFY historyManagerChanged)
    Q_PROPERTY(QObject *reactionStore READ reactionStoreObj WRITE setReactionStoreObj NOTIFY reactionStoreChanged)
    Q_PROPERTY(QObject *unreadManager READ unreadManagerObj WRITE setUnreadManagerObj NOTIFY unreadManagerChanged)
    // Mirrors UnreadManager's count for this chat. The timeline's "jump to
    // unread" needs both the number and a change signal, and reaching into the
    // manager from QML gives it neither.
    Q_PROPERTY(int unreadCount READ unreadCount NOTIFY unreadCountChanged)

public:
    enum Roles {
        SenderRole = Qt::UserRole + 1,
        TextRole,
        IsOwnRole,
        ColorRole,
        MsgTypeRole,
        IsSystemRole,
        IsEditedRole,
        ReplyToTextRole,
        ReplyToSenderRole,
        ReplyToIdRole,
        MsgIdRole,
        IsReadRole,
        // Whether an outgoing message is still between the timeline and the
        // socket. Transient and never loaded from the log; see MessageEntry.
        IsPendingRole,
        ReactionsRole,
        TimeStringRole,
        IsFileRole,
        FilePathRole,
        IsImageRole,
        // The raw stamp behind TimeStringRole. The date separator and the
        // "sent at" tooltip both need the instant, not the "HH:mm" of it.
        StampSecsRole,
        // Whether this message opens a run rather than continuing one. Worked
        // out here because it is a statement about the row before this one, and
        // a delegate cannot see its neighbour without reaching back into the
        // model by index - which goes wrong the moment the list is filtered or
        // reordered.
        ShowAuthorRole,
        // Whether this message is the first of its calendar day, which is where
        // the date separator goes. Same reason as above.
        ShowDayRole,
    };
    Q_ENUM(Roles)

    // A pause longer than this breaks a run even when the sender has not
    // changed: five minutes on, a message is a new thought and wants its own
    // header and its own time.
    static constexpr double kRunGapSecs = 300.0;

    explicit ChatModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString chatId() const
    {
        return m_chatId;
    }
    void setChatId(const QString &id);

    QObject *historyManagerObj() const;
    void setHistoryManagerObj(QObject *obj);
    QObject *reactionStoreObj() const;
    void setReactionStoreObj(QObject *obj);
    QObject *unreadManagerObj() const;
    void setUnreadManagerObj(QObject *obj);

    // Both return the stamp of the row they appended, which is what the
    // caller hands back to markSent() once the datagram is actually written.
    // Zero means nothing was appended.
    Q_INVOKABLE double
    sendMessage(const QString &text, const QString &replyToText = QString(), const QString &replyToSender = QString(), const QString &replyToId = QString());
    // Where a message with this id sits, or -1. What the quote above a reply is
    // clicked to reach.
    Q_INVOKABLE int rowForMsgId(const QString &msgId) const;
    Q_INVOKABLE double sendFile(const QString &filePath, bool isImage);
    // Resolves the hourglass to one tick. UDP has nothing to confirm a
    // delivery with, so "sent" here means only that this end wrote the
    // datagram - which is the most an outgoing mark is entitled to claim.
    Q_INVOKABLE void markSent(double stamp);
    Q_INVOKABLE void receiveMessage(const QString &text, const QString &sender = QString());
    Q_INVOKABLE void receiveFile(const QString &filePath, bool isImage, const QString &sender = QString());
    Q_INVOKABLE void appendSystemMessage(const QString &text);

    // Changing and unsending a message. Both are local only: the caller tells
    // the peer, because only the window knows which address this chat is with,
    // and a model that reached for NetworkManager would be one that could not be
    // tested without a socket.
    //
    // The stamp is the identifier on the wire - see NetworkManager's
    // sendMessageEdit() - so rowForStamp() is how an edit arriving from the peer
    // finds the message it is about.
    Q_INVOKABLE double stampForRow(int row) const;
    Q_INVOKABLE int rowForStamp(double ts) const;
    // False rather than silently nothing when the row cannot take it: a file has
    // no text to change, and a system line is nobody's message.
    Q_INVOKABLE bool editMessage(int row, const QString &newText);
    Q_INVOKABLE bool deleteMessage(int row);

    Q_INVOKABLE void toggleReaction(int row, const QString &emoji, const QString &username);
    // Marks every own outgoing message in this chat as read (called when
    // a "read" receipt arrives from the peer).
    Q_INVOKABLE void markOwnMessagesRead();
    Q_INVOKABLE void markAllRead();

    int unreadCount() const;
    // Row of the oldest message the user has not read, or -1 when there is
    // none. Counted back from the end, because the unread tally is kept by
    // UnreadManager and the messages themselves carry no incoming read flag.
    Q_INVOKABLE int firstUnreadRow() const;

Q_SIGNALS:
    void unreadCountChanged();

    // A message joined this chat, in either direction. The conversation list is
    // built from this rather than from HistoryManager::historyAppended, because
    // that one is silent when history saving is off and the sidebar still has to
    // show the chat.
    void messageAdded(const QString &chatId, const QString &preview, bool isOwn, double ts);

    void chatIdChanged();
    void historyManagerChanged();
    void reactionStoreChanged();
    void unreadManagerChanged();

private:
    void reload();
    void appendEntry(MessageEntry e, bool persist);
    void refreshRow(int row);
    // Rewrites the whole chat log from m_messages. The only way to change an
    // entry that is already on disk, since HistoryManager appends.
    void persistAll();
    // Both read m_messages[row - 1], so both are only ever called with a row
    // this model actually holds.
    bool startsRun(int row) const;
    bool startsDay(int row) const;

    QString m_chatId;
    QVector<MessageEntry> m_messages;

    QPointer<HistoryManager> m_history;
    QPointer<ReactionStore> m_reactions;
    QPointer<UnreadManager> m_unread;
};
