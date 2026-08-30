// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QPointer>
#include <QQmlEngine>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include "MessageEntry.h"

class HistoryManager;
class ReactionStore;
class UnreadManager;

class ChatModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString chatId READ chatId WRITE setChatId NOTIFY chatIdChanged)
    Q_PROPERTY(QObject *historyManager READ historyManagerObj WRITE setHistoryManagerObj NOTIFY historyManagerChanged)
    Q_PROPERTY(QObject *reactionStore READ reactionStoreObj WRITE setReactionStoreObj NOTIFY reactionStoreChanged)
    Q_PROPERTY(QObject *unreadManager READ unreadManagerObj WRITE setUnreadManagerObj NOTIFY unreadManagerChanged)
    // Mirrors UnreadManager's count for this chat: "jump to unread" needs both
    // the number and a change signal, which QML cannot get from the manager.
    Q_PROPERTY(int unreadCount READ unreadCount NOTIFY unreadCountChanged)

public:
    enum Roles {
        SenderRole = Qt::UserRole + 1,
        SenderAvatarRole,
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
        IsPendingRole,
        ReactionsRole,
        TimeStringRole,
        IsFileRole,
        FilePathRole,
        IsImageRole,
        // An attachment that is still on a server. Kept apart from
        // FilePathRole because that one is a path and this one is a URL, and
        // the delegate has to know which of the two it was handed.
        MediaKindRole,
        MediaUrlRole,
        MediaMimeRole,
        MediaSizeRole,
        MediaWidthRole,
        MediaHeightRole,
        MediaDurationRole,
        StampSecsRole,
        // Worked out here because it is a statement about the row before this
        // one, and a delegate cannot see its neighbour without reaching back
        // into the model by index - which breaks once the list is reordered.
        ShowAuthorRole,
        ShowDayRole,
        // A poll this row carries (empty map otherwise): question, answers as a
        // list of {id, body}, and disclosed. The bridge tracks the votes.
        PollRole,
    };
    Q_ENUM(Roles)

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

    Q_INVOKABLE double
    sendMessage(const QString &text, const QString &replyToText = QString(), const QString &replyToSender = QString(), const QString &replyToId = QString());
    Q_INVOKABLE int rowForMsgId(const QString &msgId) const;
    Q_INVOKABLE double sendFile(const QString &filePath, bool isImage);
    // UDP has nothing to confirm a delivery with, so "sent" here means only that
    // this end wrote the datagram - the most an outgoing mark can claim.
    Q_INVOKABLE void markSent(double stamp);
    Q_INVOKABLE void receiveMessage(const QString &text, const QString &sender = QString());
    Q_INVOKABLE void receiveFile(const QString &filePath, bool isImage, const QString &sender = QString());
    Q_INVOKABLE void appendSystemMessage(const QString &text);

    // A message that arrived with an identity and a time of its own, which a LAN
    // datagram has neither of. The only entry point the Matrix path uses, and
    // the only one that is idempotent: remoteId is the event id, sync replays
    // the tail of a timeline on every reconnect, and the log on disk carries the
    // same ids back, so a restart does not double the conversation. Returns
    // false when the message was already here.
    Q_INVOKABLE bool ingestRemoteMessage(const QString &remoteId,
                                         const QString &text,
                                         const QString &sender,
                                         bool isOwn,
                                         double ts,
                                         bool isSystem = false,
                                         const QString &senderAvatar = QString());

    // The same contract for an attachment. media carries the keys
    // MatrixRoomBridge::roomAttachment() fills: kind, url, name, mime, size,
    // width, height, duration.
    Q_INVOKABLE bool ingestRemoteAttachment(const QString &remoteId,
                                            const QVariantMap &media,
                                            const QString &sender,
                                            bool isOwn,
                                            double ts,
                                            const QString &senderAvatar = QString());

    // Fold one m.poll.response into the tally of the poll it answers. voterId is
    // the sender of the vote; isOwn marks a vote cast by this account (so the
    // window can highlight it). Returns false when the poll is unknown here.
    Q_INVOKABLE bool applyPollResponse(const QString &pollStartId, const QString &answerId, const QString &voterId, bool isOwn);

    // A poll (Matrix m.poll.start). answers is a list of {id, body} maps; the
    // window renders it as a question with a button per option and votes through
    // the chat's transport. Idempotent like ingestRemoteMessage.
    Q_INVOKABLE bool ingestRemotePoll(const QString &remoteId,
                                      const QString &question,
                                      const QVariantList &answers,
                                      bool disclosed,
                                      const QString &sender,
                                      bool isOwn,
                                      double ts,
                                      const QString &senderAvatar = QString());

    // An m.replace that arrived for a message already here. False when the
    // original is not in this conversation, which is the normal case for an
    // edit of something older than the loaded backlog.
    //
    // markEdited false replaces the text without the "edited" mark, which is
    // for the one caller that is not an edit: an encrypted message whose key
    // arrived late, where the row had a placeholder in it and now has what was
    // always there. Nobody changed that message and the interface must not say
    // somebody did.
    Q_INVOKABLE bool applyRemoteEdit(const QString &remoteId, const QString &newText, bool markEdited = true);

    // Editing and unsending are local only: the caller tells the peer, because
    // only the window knows this chat's address and a model reaching for
    // NetworkManager could not be tested without a socket. The stamp is the
    // identifier on the wire, so rowForStamp() resolves a peer's edit.
    Q_INVOKABLE double stampForRow(int row) const;
    Q_INVOKABLE int rowForStamp(double ts) const;
    Q_INVOKABLE bool editMessage(int row, const QString &newText);
    Q_INVOKABLE bool deleteMessage(int row);

    Q_INVOKABLE void toggleReaction(int row, const QString &emoji, const QString &username);
    Q_INVOKABLE void markOwnMessagesRead();
    Q_INVOKABLE void markAllRead();

    // Empties the conversation and the file behind it. The row stays: the
    // saved messages chat is pinned and cannot be forgotten.
    Q_INVOKABLE void clearMessages();

    int unreadCount() const;
    // Counted back from the end, because the tally lives in UnreadManager and
    // the messages carry no incoming read flag.
    Q_INVOKABLE int firstUnreadRow() const;

Q_SIGNALS:
    void unreadCountChanged();

    // The conversation list is built from this rather than
    // HistoryManager::historyAppended, which is silent when saving is off.
    void messageAdded(const QString &chatId, const QString &preview, bool isOwn, double ts);

    void chatIdChanged();
    void historyManagerChanged();
    void reactionStoreChanged();
    void unreadManagerChanged();

    // A reaction toggled on a message in this store; the window routes it
    // to the chat's transport when the backend supports reactions.
    void reactionToggledLocally(double ts, const QString &emoji, bool added);

private:
    void reload();
    void appendEntry(MessageEntry e, bool persist);
    void refreshRow(int row);
    // Rewrites the whole log from m_messages - the only way to change a stored
    // entry, since HistoryManager appends.
    void persistAll();
    bool startsRun(int row) const;
    bool startsDay(int row) const;

    QString m_chatId;
    QVector<MessageEntry> m_messages;

    struct PendingPollVote {
        QString answerId;
        QString voterId;
        bool isOwn = false;
    };
    // Sync can deliver a response before the referenced start event. Keep it
    // until the poll arrives instead of silently losing the vote.
    QHash<QString, QVector<PendingPollVote>> m_pendingPollVotes;

    QPointer<HistoryManager> m_history;
    QPointer<ReactionStore> m_reactions;
    QPointer<UnreadManager> m_unread;
};
