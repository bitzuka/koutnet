// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Matrix rooms as conversations, and as rooms.
//
// This is the whole seam. It turns libQuotient's room and timeline signals into
// the statements the LAN path already makes about a conversation - this chat
// exists, it is called this, something arrived in it, it went away - and
// Main.qml feeds those to the same ChatListModel and ChatModel as everything
// else. Nothing below this class has a Quotient type in it.
//
// What a room has and a LAN peer has not - a topic, an address, members with
// power levels - does not fit through those signals and is not forced through
// them. It is asked for instead, by roomInfo() and roomMembers(), which the
// room's own column calls and calls again when roomInfoChanged() says to. A
// pull rather than a push because that column is usually shut, and a member
// list nobody is looking at should cost nothing.
//
// The reverse direction is ChatBackend's: sendText(), sendFile(), markRead()
// and leaveChat(), reached through ChatBackendRegistry under the "mx:" prefix
// and implemented here as libQuotient calls.
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "core/backend/ChatBackend.h"

namespace Quotient
{
class Connection;
class Room;
class RoomEvent;
}

namespace koutnet
{

class MatrixManager;

class MatrixRoomBridge : public ChatBackend
{
    Q_OBJECT

public:
    explicit MatrixRoomBridge(MatrixManager *manager, QObject *parent = nullptr);

    // All of these refuse quietly when the chat id is not a Matrix one or the
    // room is not in this session: the window branches on the id prefix, and a
    // session that has not synced yet must not turn a keystroke into an error.
    // They are ChatBackend's; Main.qml reaches them through chatTransport, and
    // the Q_INVOKABLE surface is kept for the receive-side handlers and tests.
    Q_INVOKABLE bool sendText(const QString &chatId, const QString &text) override;
    // The upload and the event that follows it are libQuotient's to sequence;
    // this only decides which content type the file becomes. False means it was
    // refused here, and sendFailed() has already said why.
    Q_INVOKABLE bool sendFile(const QString &chatId, const QString &localFilePath) override;
    Q_INVOKABLE void markRead(const QString &chatId) override;
    Q_INVOKABLE bool leaveChat(const QString &chatId) override;

    // Everything the room column shows, in one map, because a dozen properties
    // would be a dozen change signals for one sync. An empty map when the chat
    // id names no room in this session, which QML tests by looking at "roomId".
    Q_INVOKABLE QVariantMap roomInfo(const QString &chatId) const override;
    // Members, most powerful first and then by name - the order a member list
    // is read in. Invited members are included and marked, because "who is in
    // this room" and "who has been asked" are both answers it has to give.
    Q_INVOKABLE QVariantList roomMembers(const QString &chatId) const override;
    // One member, in the shape the member card wants. An empty map when the
    // user is not known to the room.
    Q_INVOKABLE QVariantMap memberInfo(const QString &chatId, const QString &userId) const override;

    // The rest of the ChatBackend contract: this is the Matrix transport, it
    // owns the "mx:" prefix, the homeserver echoes our rows back through sync
    // (so the window must not invent a local row before sending), and rooms
    // have furniture - members, an info column, a leave action. Calls, typing
    // and message edits are the LAN protocol's own; there is no Matrix form of
    // them here.
    chatid::Transport transport() const override;
    bool canHandle(const QString &chatId) const override;
    bool serverOwnsTimeline(const QString &chatId) const override;
    bool hasRooms(const QString &chatId) const override;
    bool supportsCalls(const QString &chatId) const override;
    bool supportsTyping(const QString &chatId) const override;
    bool supportsEdits(const QString &chatId) const override;
    void sendTyping(const QString &chatId) override;

Q_SIGNALS:
    // A room the conversation list has not been told about yet, or one whose
    // name changed. Idempotent on the receiving end - ChatListModel::openChat()
    // already is.
    void roomListed(QString chatId, QString displayName);
    void roomLeft(QString chatId);

    // One signal for every kind of text row, because ChatModel takes them all
    // through one call and the duplicate check is keyed on eventId. System rows
    // carry a synthetic id so that a restart does not stack another copy.
    void roomMessage(QString chatId, QString eventId, QString text, QString sender, bool isOwn, double ts, bool isSystem);

    // A picture, a recording or a file. A map rather than nine parameters: the
    // shape is ChatModel::ingestRemoteAttachment()'s, and both ends move
    // together. Keys: kind, url, name, mime, size, width, height, duration.
    void roomAttachment(QString chatId, QString eventId, QVariantMap media, QString sender, bool isOwn, double ts);

    // An m.replace arrived for an event already in the timeline. Never a row of
    // its own: showing it as one is how a corrected typo becomes two messages.
    void roomMessageEdited(QString chatId, QString eventId, QString newText);

    // A message that went into the timeline as "no key for this" has been
    // decrypted, because the key turned up afterwards. Separate from
    // roomMessageEdited() for one reason: nobody edited anything, and marking
    // the row as edited would be this interface telling a small lie about a
    // message whose whole point is that it is now being told truthfully.
    void roomMessageRevealed(QString chatId, QString eventId, QString text);

    // The topic, the name, the member list, the address or the picture moved.
    // Deliberately coarse - whoever is showing the room asks again.
    void roomInfoChanged(QString chatId);

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
    // A timeline event libQuotient has swapped for its decrypted self.
    void revealEvent(Quotient::Room *room, const Quotient::RoomEvent *event);
    Quotient::Room *roomFor(const QString &chatId) const;

    QPointer<MatrixManager> m_manager;
    // Room id to the object whose timeline signals are already connected.
    // libQuotient replaces the object when a room changes join state, so the id
    // alone cannot say whether the connections are still live - the pointer can.
    // Guarded, because a room is a child of its Connection and a Connection can
    // be deleted from under this without leftRoom() ever being emitted.
    QHash<QString, QPointer<QObject>> m_tracked;
};

} // namespace koutnet
