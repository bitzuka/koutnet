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
class NetworkManager;
class VoiceCallManager;
class CryptoManager;

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
    // have furniture - members, an info column, a leave action. Typing,
    // reactions and edits ride the standard Matrix endpoints; the bridge
    // resolves the window's ts-keyed stamps to the event ids the homeserver
    // knows, because the window keys everything on the stamp a row was filed
    // under and the homeserver keys everything on an event id.
    chatid::Transport transport() const override;
    bool canHandle(const QString &chatId) const override;
    bool serverOwnsTimeline(const QString &chatId) const override;
    bool hasRooms(const QString &chatId) const override;
    bool supportsCalls(const QString &chatId) const override;
    bool supportsTyping(const QString &chatId) const override;
    bool supportsEdits(const QString &chatId) const override;
    bool supportsReactions(const QString &chatId) const override;
    void sendTyping(const QString &chatId) override;
    void sendReaction(const QString &chatId, double ts, const QString &emoji, bool added) override;
    bool sendEdit(const QString &chatId, double ts, const QString &newText) override;
    bool sendDelete(const QString &chatId, double ts) override;

    // Rooms are made and joined, not "sent", so these live outside the
    // ChatBackend contract - the LAN transport has no answer to them. The
    // window calls them straight on the bridge when a signed-in account is
    // looking, and every one of them is a homeserver round trip that answers
    // asynchronously; success surfaces as the usual roomListed()/roomLeft(),
    // and a refusal comes back as roomOperationFailed().
    Q_INVOKABLE void createRoom(const QString &name, const QString &topic, const QString &alias, const QStringList &invitedUsers, bool isPrivate);
    // By canonical alias ("#room:server") or by full room id - both are the
    // same homeserver call.
    Q_INVOKABLE void joinRoom(const QString &aliasOrId);
    Q_INVOKABLE void acceptInvite(const QString &chatId);
    Q_INVOKABLE void declineInvite(const QString &chatId);
    Q_INVOKABLE void inviteMember(const QString &chatId, const QString &userId);
    Q_INVOKABLE void kickMember(const QString &chatId, const QString &userId);
    Q_INVOKABLE void banMember(const QString &chatId, const QString &userId);
    Q_INVOKABLE void unbanMember(const QString &chatId, const QString &userId);

    // A one-on-one with a member of this homeserver. Existing direct rooms are
    // reused; requestDirectChat() creates one when there is none, and the join
    // chimes directChatOpened() so the window can open it without watching
    // roomListed().
    Q_INVOKABLE void openDirectChat(const QString &userId);

    // Calls between KOutNet sessions in a Matrix room. A voice call is media
    // between the two local networks, and the homeserver is only the
    // signaller: m.call.invite/answer/hangup carry the LAN address each side
    // is listening for voice on, and the voice itself flows peer to peer over
    // the same TCP voice channel a LAN call uses. The last two arguments are
    // given by main.cpp, which also owns both objects - the bridge may not
    // exist without them, but it also must not construct them.
    void setCallStack(NetworkManager *net, VoiceCallManager *voice, CryptoManager *crypto);
    // Start a room call as the caller. Anything already in the room is
    // invited; the first member whose client answers becomes the peer.
    Q_INVOKABLE void callRoom(const QString &chatId);
    // Answer an invitation the window is showing (roomCallInvited()).
    Q_INVOKABLE void acceptCall(const QString &chatId, const QString &callId);
    Q_INVOKABLE void declineCall(const QString &chatId, const QString &callId);
    Q_INVOKABLE void hangupRoomCall(const QString &chatId);

Q_SIGNALS:
    // A room the conversation list has not been told about yet, or one whose
    // name changed. Idempotent on the receiving end - ChatListModel::openChat()
    // already is.
    void roomListed(QString chatId, QString displayName, QString avatarUrl = QString());
    void roomLeft(QString chatId);

    // One signal for every kind of text row, because ChatModel takes them all
    // through one call and the duplicate check is keyed on eventId. System rows
    // carry a synthetic id so that a restart does not stack another copy.
    void roomMessage(QString chatId, QString eventId, QString text, QString sender, bool isOwn, double ts, bool isSystem, QString senderAvatar = QString());

    // A picture, a recording or a file. A map rather than nine parameters: the
    // shape is ChatModel::ingestRemoteAttachment()'s, and both ends move
    // together. Keys: kind, url, name, mime, size, width, height, duration.
    void roomAttachment(QString chatId, QString eventId, QVariantMap media, QString sender, bool isOwn, double ts, QString senderAvatar = QString());

    // A reaction to a message. ts is the stamp of the message being reacted
    // to, never the reaction's own - the ReactionStore keys on the target,
    // exactly as the LAN protocol's reaction packet does.
    void roomReaction(QString chatId, double ts, QString emoji, QString sender, bool added);

    // An m.replace arrived for an event already in the timeline. Never a row of
    // its own: showing it as one is how a corrected typo becomes two messages.
    void roomMessageEdited(QString chatId, QString eventId, QString newText);

    // Someone typed, or stopped typing, in a room that is open. A boolean, not
    // a name: the window shows one indicator per conversation, and which member
    // it is hardly matters for the purpose the indicator serves.
    void roomTyping(QString chatId, bool typing);

    // A read receipt moved past one of this session's own messages. The window
    // marks those messages read the same way it does when a LAN receipt lands.
    void roomReadReceipt(QString chatId);

    // A message this window already shows was redacted. The row is removed,
    // which is what the LAN protocol does with an unsend - and what every
    // client in the room will do with the message too, once the server
    // processes the redaction.
    void roomMessageRemoved(QString chatId, QString eventId);

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

    // Somebody asked this account into a room. The conversation list shows the
    // invitation with accept and decline buttons rather than as a chat, because
    // a room this account is not in has no timeline to open. The name may be
    // empty until the room's summary state has arrived, and the window deals
    // with that by calling roomInfo() when the room opens.
    void roomInvited(QString chatId, QString displayName);
    // The invitation is gone - accepted, declined, or the sender withdrew it.
    void roomInviteGone(QString chatId);

    // A room operation the homeserver refused: creation, joining, an invite, a
    // kick. One channel for all of them, because the window answers each with
    // the same toast.
    void roomOperationFailed(QString chatId, QString reason);

    // A one-on-one requested by openDirectChat() is ready: an existing direct
    // room was found, or the one requestDirectChat() created has come through
    // the join. The window opens this room as a chat.
    void directChatOpened(QString chatId);

    // Somebody in the room asked this session to join a call. The call id
    // travels with it: the answer to an invitation names the call.
    void roomCallInvited(QString chatId, QString callId, QString sender);
    // The invitation this session sent was answered. The caller's window opens
    // the call UI on this, and the media channel was already brought up.
    void roomCallAccepted(QString chatId);
    // The call in the room is over, either way. The window closes its call UI.
    void roomCallEnded(QString chatId);

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
    // A m.call.* event arrived. Voice signalling only - the media is peer to
    // peer and never passes through the homeserver, so everything here is a
    // handshake about who to connect to.
    void handleCallEvent(Quotient::Room *room, const Quotient::RoomEvent *event);
    // What one side of a room call hands the other in a m.call SDP field.
    // Both ends are in there on purpose: the address is where the media
    // channel dials, and the key is what encrypts it. The key travels the
    // same path as the call itself, so it is as private as the room is.
    struct CallOffer {
        QString address;
        QByteArray key;
    };
    static CallOffer callOfferFromSdp(const QString &sdp);
    CallOffer ownCallOffer() const;

    QPointer<MatrixManager> m_manager;
    // Room id to the object whose timeline signals are already connected.
    // libQuotient replaces the object when a room changes join state, so the id
    // alone cannot say whether the connections are still live - the pointer can.
    // Guarded, because a room is a child of its Connection and a Connection can
    // be deleted from under this without leftRoom() ever being emitted.
    QHash<QString, QPointer<QObject>> m_tracked;

    // The window addresses everything by the stamp a row was filed under;
    // the homeserver addresses everything by event id. Both directions of that
    // correspondence, per room, so a reaction, an edit or an unsend can be
    // resolved to the event it is about, and a reaction that arrives for an
    // event already shown can be filed under the stamp the ReactionStore
    // expects. One entry per row the window has been told about; a stamp that
    // collides (two events in the same millisecond) keeps the later entry,
    // which is the row the window shows as later.
    QHash<QString, QHash<double, QString>> m_tsToEventId;
    QHash<QString, QHash<QString, double>> m_eventIdToTs;

    // When each room last got a typing packet, in wall-clock time, so that a
    // typist does not cost one HTTP round trip per keystroke.
    QHash<QString, qint64> m_lastTypingSent;

    // Reactions this bridge has published, keyed by the reaction event id. The
    // redaction of a reaction wipes its content, so the badge cannot be taken
    // down from the redacted event - it can only be remembered from the
    // original, and a redaction is only meaningful about an event already on
    // the screen.
    struct PublishedReaction {
        double targetTs = 0.0;
        QString emoji;
        QString sender;
    };
    QHash<QString, QHash<QString, PublishedReaction>> m_reactions;

    // Voice call state per room. One call per room, and a call is one call id;
    // a member who answers the same invitation becomes an extra peer on the
    // caller's mixer, which is the same shape a LAN group call has. The media
    // side lives in VoiceCallManager and is keyed by the member's LAN address,
    // which is why this table stores addresses rather than member ids.
    struct RoomCall {
        QString callId;
        QString role; // "caller" or "answerer"
        QStringList peerAddresses;
        // The shared key each peer address's media is sealed with, both
        // directions. Dropped when the call ends, like the sessions.
        QHash<QString, QByteArray> peerKeys;
        bool established = false;
    };
    QHash<QString, RoomCall> m_calls;

    // The invitation the window is currently showing, if any: the call id, the
    // address to ring when it is accepted, and the key the ringing comes
    // encrypted under. Only one is offered at a time, exactly as the LAN
    // incoming-call dialog is a single object.
    struct PendingCall {
        QString chatId;
        QString callId;
        QString peerAddress;
        QByteArray peerKey;
    };
    PendingCall m_pending;

    // Set by openDirectChat() while the homeserver is creating the room; the
    // join of a room that matches it chimes directChatOpened().
    QString m_pendingDirectTarget;

    QPointer<NetworkManager> m_net;
    QPointer<VoiceCallManager> m_voice;
    QPointer<CryptoManager> m_crypto;
};

} // namespace koutnet
