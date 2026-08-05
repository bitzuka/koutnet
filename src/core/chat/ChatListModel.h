// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#pragma once

#include <QAbstractListModel>
#include <QPointer>
#include <QQmlEngine>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QVector>

class HistoryManager;
class UnreadManager;

// The conversation list behind the sidebar: one row per chat the user actually
// has, newest activity first.
//
// This is deliberately not the peer table. NetworkManager's m_peers is whoever
// is broadcasting presence on the network right now, which is a scanner - a row
// appeared because a stranger's laptop woke up and vanished again 25 seconds
// later. A conversation exists because the user started one or because somebody
// wrote to them, and it goes on existing when the peer is switched off.
//
// Reachability is therefore an attribute of a row here, never the reason the row
// is in the list.
//
// A chat is still keyed on an address, because that is what chat_id is everywhere
// else - HistoryManager's filenames, ChatModel, sendPrivate(). A peer that comes
// back on a different address after a DHCP lease expires therefore arrives as a
// second row. Keying a conversation on the peer identity is the right answer and
// belongs with finding a peer by handle, which is the next piece of work; it needs
// a migration for the history already on disk, which is why it is not in here.
class ChatListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject *historyManager READ historyManagerObj WRITE setHistoryManagerObj NOTIFY historyManagerChanged)
    Q_PROPERTY(QObject *unreadManager READ unreadManagerObj WRITE setUnreadManagerObj NOTIFY unreadManagerChanged)

public:
    enum Roles {
        ChatIdRole = Qt::UserRole + 1,
        DisplayNameRole,
        AvatarLetterRole,
        PreviewRole,
        StampSecsRole,
        LastSeenSecsRole,
        UnreadCountRole,
        OnlineRole,
    };
    Q_ENUM(Roles)

    explicit ChatListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QObject *historyManagerObj() const;
    void setHistoryManagerObj(QObject *obj);
    QObject *unreadManagerObj() const;
    void setUnreadManagerObj(QObject *obj);

    // Puts a chat in the list because the user asked for one, with no message in
    // it yet. Idempotent, and it never moves an existing chat up the list:
    // opening an old conversation is not activity in it.
    Q_INVOKABLE void openChat(const QString &chatId, const QString &displayName = QString());
    // A message joined a chat, in either direction. This is what creates the row
    // for a peer who wrote first, and what re-sorts the list.
    Q_INVOKABLE void noteMessage(const QString &chatId, const QString &preview, bool isOwn, double ts);
    // Reachability and the name the peer advertises. Called for peers whether or
    // not there is a conversation with them - one that is not in the list is
    // remembered here only as far as the next call, never added.
    Q_INVOKABLE void setPresence(const QString &chatId, bool online, double lastSeenSecs, const QString &displayName = QString());
    Q_INVOKABLE void removeChat(const QString &chatId);
    Q_INVOKABLE bool hasChat(const QString &chatId) const;
    // One row as a plain map, for the parts of the interface that want a single
    // chat rather than a list of them - the chat header needs the last-seen
    // stamp of a peer that is not on the network to say anything at all about it.
    // Empty map when there is no such chat.
    Q_INVOKABLE QVariantMap chatInfo(const QString &chatId) const;

Q_SIGNALS:
    void historyManagerChanged();
    void unreadManagerChanged();

private:
    struct Entry {
        QString chatId;
        QString displayName;
        QString preview;
        bool previewIsOwn = false;
        double lastActivity = 0.0;
        double lastSeen = 0.0;
        int unread = 0;
        // Never persisted: nothing is reachable until presence says so again,
        // and a stored "online" would be a lie for the whole of startup.
        bool online = false;
    };

    // The self-chat is a local scratchpad rather than a conversation with
    // anybody, and the sidebar pins it above the list, so it stays out of here.
    static bool isTrackable(const QString &chatId);

    int indexOfChat(const QString &chatId) const;
    // Where a row with this activity belongs, counted in a list with excludeRow
    // taken out. -1 excludes nothing.
    int destinationFor(double activity, int excludeRow) const;
    void moveToSortedPosition(int row);
    void refreshRow(int row, const QVector<int> &roles);
    void load();
    void scheduleSave();
    void save();

    QVector<Entry> m_rows;
    QPointer<HistoryManager> m_history;
    QPointer<UnreadManager> m_unread;
    // Coalesces the writes: a burst of messages is one rewrite of the index and
    // not one per message.
    QTimer m_saveTimer;
    bool m_loaded = false;
};
