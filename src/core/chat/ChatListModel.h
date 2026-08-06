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

// Deliberately not the peer table: NetworkManager's m_peers is a scanner of who
// is broadcasting presence now, while a conversation goes on existing when the
// peer is switched off. Reachability is an attribute of a row here, never the
// reason the row is in the list.
//
// A chat is keyed on an address because that is what chat_id is everywhere else,
// so a peer back on a new DHCP lease arrives as a second row; keying on peer
// identity needs a migration of the history on disk, which is why it is not here.
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
        // Where the row came from, worked out from the chat id alone rather than
        // stored: it is a property of the id, and a stored copy could disagree
        // with it. The only thing the interface is told about transport, and it
        // spends it on a badge.
        TransportRole,
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

    // Idempotent, and it never moves an existing chat up the list: opening an old
    // conversation is not activity in it.
    Q_INVOKABLE void openChat(const QString &chatId, const QString &displayName = QString());
    Q_INVOKABLE void noteMessage(const QString &chatId, const QString &preview, bool isOwn, double ts);
    Q_INVOKABLE void setPresence(const QString &chatId, bool online, double lastSeenSecs, const QString &displayName = QString());
    Q_INVOKABLE void removeChat(const QString &chatId);
    Q_INVOKABLE bool hasChat(const QString &chatId) const;
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
        // Never persisted: a stored "online" would be a lie for the whole of
        // startup.
        bool online = false;
    };

    static bool isTrackable(const QString &chatId);

    int indexOfChat(const QString &chatId) const;
    int destinationFor(double activity, int excludeRow) const;
    void moveToSortedPosition(int row);
    void refreshRow(int row, const QVector<int> &roles);
    void load();
    void scheduleSave();
    void save();

    QVector<Entry> m_rows;
    QPointer<HistoryManager> m_history;
    QPointer<UnreadManager> m_unread;
    // Coalesces the writes: a burst of messages is one rewrite of the index.
    QTimer m_saveTimer;
    bool m_loaded = false;
};
