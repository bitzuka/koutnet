// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Matrix rooms as ordinary conversations.
//
// This is the whole seam. It does not own a model and it does not know one
// exists: it turns libQuotient's room and timeline signals into the same four
// statements the LAN path already makes about a conversation - this chat
// exists, it is called this, a message arrived in it, it went away - and
// Main.qml feeds those to the same ChatListModel and ChatModel as everything
// else. Nothing below this class has a Quotient type in it.
//
// The reverse direction is sendText(), which Main.qml calls instead of
// NetworkManager::sendPrivate() when the chat id carries the "mx:" prefix.
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>

namespace Quotient
{
class Connection;
class Room;
class RoomEvent;
}

namespace koutnet
{

class MatrixManager;

class MatrixRoomBridge : public QObject
{
    Q_OBJECT

public:
    explicit MatrixRoomBridge(MatrixManager *manager, QObject *parent = nullptr);

    // Both refuse quietly when the chat id is not a Matrix one or the room is
    // not in this session: the window branches on the id prefix, and a session
    // that has not synced yet must not turn a keystroke into an error.
    Q_INVOKABLE bool sendText(const QString &chatId, const QString &text);
    Q_INVOKABLE void markRead(const QString &chatId);

Q_SIGNALS:
    // A room the conversation list has not been told about yet, or one whose
    // name changed. Idempotent on the receiving end - ChatListModel::openChat()
    // already is.
    void roomListed(QString chatId, QString displayName);
    void roomLeft(QString chatId);

    // One signal for every kind of row, because ChatModel takes them all
    // through one call and the duplicate check is keyed on eventId. System rows
    // carry a synthetic id so that a restart does not stack another copy.
    void roomMessage(QString chatId, QString eventId, QString text, QString sender, bool isOwn, double ts, bool isSystem);

    void sendFailed(QString chatId, QString reason);

private:
    void attach(Quotient::Connection *connection);
    void trackRoom(Quotient::Room *room);
    void publishRoom(Quotient::Room *room);
    void publishRange(Quotient::Room *room, int fromIndex, int toIndex);
    void publishEvent(Quotient::Room *room, const Quotient::RoomEvent *event);
    // Turns a pending event libQuotient has given up on into something the user
    // sees. Without it a refused send is exactly as quiet as a delivered one.
    void reportPendingFailure(Quotient::Room *room, int pendingIndex);
    void announceEncryption(Quotient::Room *room);
    Quotient::Room *roomFor(const QString &chatId) const;

    QPointer<MatrixManager> m_manager;
    // Room id to the object whose timeline signals are already connected.
    // libQuotient replaces the object when a room changes join state, so the id
    // alone cannot say whether the connections are still live - the pointer can.
    // Guarded, because a room is a child of its Connection and a Connection can
    // be deleted from under this without leftRoom() ever being emitted.
    QHash<QString, QPointer<QObject>> m_tracked;
    QSet<QString> m_encryptionAnnounced;
};

} // namespace koutnet
