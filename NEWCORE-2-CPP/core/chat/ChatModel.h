#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QString>
#include <QVector>
#include <QPointer>

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

public:
    enum Roles {
        SenderRole = Qt::UserRole + 1,
        TextRole,
        IsOwnRole,
        ColorRole,
        EmojiRole,
        MsgTypeRole,
        ImageDataRole,
        IsSystemRole,
        IsEditedRole,
        IsForwardedRole,
        ForwardedFromRole,
        ReplyToTextRole,
        MsgIdRole,
        IsReadRole,
        ReactionsRole,
        TimeStringRole,
        IsFileRole,
        FilePathRole,
        IsImageRole,
    };
    Q_ENUM(Roles)

    explicit ChatModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString chatId() const { return m_chatId; }
    void setChatId(const QString &id);

    QObject *historyManagerObj() const;
    void setHistoryManagerObj(QObject *obj);
    QObject *reactionStoreObj() const;
    void setReactionStoreObj(QObject *obj);
    QObject *unreadManagerObj() const;
    void setUnreadManagerObj(QObject *obj);

    Q_INVOKABLE void sendMessage(const QString &text, const QString &replyToText = QString());
    Q_INVOKABLE void sendFile(const QString &filePath, bool isImage);
    Q_INVOKABLE void receiveMessage(const QString &text, const QString &sender = QString());
    Q_INVOKABLE void receiveFile(const QString &filePath, bool isImage, const QString &sender = QString());
    Q_INVOKABLE void appendSystemMessage(const QString &text);

    Q_INVOKABLE void toggleReaction(int row, const QString &emoji, const QString &username);
    // Marks every own outgoing message in this chat as read (called when
    // a "read" receipt arrives from the peer).
    Q_INVOKABLE void markOwnMessagesRead();
    Q_INVOKABLE void markAllRead();

signals:
    void chatIdChanged();
    void historyManagerChanged();
    void reactionStoreChanged();
    void unreadManagerChanged();

private:
    void reload();
    void appendEntry(MessageEntry e, bool persist);
    void refreshRow(int row);

    QString m_chatId;
    QVector<MessageEntry> m_messages;

    QPointer<HistoryManager> m_history;
    QPointer<ReactionStore> m_reactions;
    QPointer<UnreadManager> m_unread;
};
