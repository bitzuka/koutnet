// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet Matrix rooms, as chats and as rooms.
//
// the whole seam between Quotient and the rest of the app: room/timeline
// signals become the same conversation events the LAN path makes, and
// Main.qml feeds both into the same models. nothing below here knows Quotient.
//
// a room has things a peer does not - topic, address, members - and those are
// pulled on demand via roomInfo()/roomMembers(), not pushed through the signals.
//
// sending goes the other way through ChatBackend: sendText/sendFile/markRead/
// leaveChat, reached via chatTransport under mx:.
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>
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

    // these refuse quietly when the chat id is not a Matrix one or the room is
    // not in the session; the window branches on the prefix, so a keystroke
    // must not turn into an error before sync. reached via chatTransport.
    Q_INVOKABLE bool sendText(const QString &chatId, const QString &text) override;
    // libQuotient sequences the upload and the event; this only picks the
    // content type. false means it was refused here, sendFailed already fired.
    Q_INVOKABLE bool sendFile(const QString &chatId, const QString &localFilePath) override;
    Q_INVOKABLE void markRead(const QString &chatId) override;
    Q_INVOKABLE bool leaveChat(const QString &chatId) override;

    // everything the room column shows, in one map - a dozen properties would be
    // a dozen signals for one sync. empty map when the chat id names no room.
    Q_INVOKABLE QVariantMap roomInfo(const QString &chatId) const override;
    // members, most powerful first then by name. invited ones are included and
    // marked, since the column asks both "who is in" and "who was asked".
    Q_INVOKABLE QVariantList roomMembers(const QString &chatId) const override;
    // one member, as the member card wants it. empty map when unknown to room.
    Q_INVOKABLE QVariantMap memberInfo(const QString &chatId, const QString &userId) const override;

    // the rest of the ChatBackend contract: this is the mx: transport, the
    // homeserver echoes rows back through sync (so the window does not invent
    // a local row first), and rooms have members/info/leave. the bridge maps
    // the ts stamps the window keeps to the event ids the homeserver uses.
    chatid::Transport transport() const override;
    bool canHandle(const QString &chatId) const override;
    bool serverOwnsTimeline(const QString &chatId) const override;
    bool hasRooms(const QString &chatId) const override;
    bool supportsCalls(const QString &chatId) const override;
    bool supportsTyping(const QString &chatId) const override;
    bool supportsEdits(const QString &chatId) const override;
    bool supportsReactions(const QString &chatId) const override;
    void sendTyping(const QString &chatId) override;
    void sendReaction(const QString &chatId, const QVariant &identifier, const QString &emoji, bool added) override;
    bool sendEdit(const QString &chatId, const QVariant &identifier, const QString &newText) override;
    bool sendDelete(const QString &chatId, const QVariant &identifier) override;

    // ---- features the interface must offer at parity with other clients ----
    // quote-reply to a message already on the timeline; ts is the row stamp the
    // window keeps, not the event id.
    // identifier accepts a Matrix event id or the legacy timestamp used by the
    // existing window. Event ids are preferred because timestamps can collide.
    Q_INVOKABLE bool sendReply(const QString &chatId, const QVariant &identifier, const QString &plainText);
    // send formatted (HTML) text; plainText is the fallback body. the composer
    // derives the HTML from whatever markdown the user typed.
    Q_INVOKABLE bool sendRichText(const QString &chatId, const QString &plainText, const QString &html);
    // rewrite the room's name or topic. avatar is set through setRoomAvatar().
    Q_INVOKABLE void setRoomName(const QString &chatId, const QString &name);
    Q_INVOKABLE void setRoomTopic(const QString &chatId, const QString &topic);
    Q_INVOKABLE void setRoomAvatar(const QString &chatId, const QString &localFilePath);
    // pin / unpin a message by its row stamp; the room re-emits pinnedEventsChanged.
    Q_INVOKABLE void pinMessage(const QString &chatId, const QVariant &identifier);
    Q_INVOKABLE void unpinMessage(const QString &chatId, const QVariant &identifier);
    // mute / unmute a user across the whole account.
    Q_INVOKABLE void ignoreUser(const QString &userId);
    Q_INVOKABLE void unignoreUser(const QString &userId);
    // search the loaded timeline of a room for a substring; results arrive via
    // roomSearchResults(). a server /search is not used - the local history is.
    Q_INVOKABLE void searchMessages(const QString &chatId, const QString &query);
    // share a geo: point (Matrix m.location). label is the human text; the uri
    // is built from the coordinates.
    Q_INVOKABLE void sendLocation(const QString &chatId, double latitude, double longitude, const QString &label);
    // send a sticker (Matrix m.sticker). mxcUrl is an already-uploaded media id;
    // the composer uploads the image and passes the id back.
    Q_INVOKABLE void sendSticker(const QString &chatId, const QString &mxcUrl, const QString &description);
    // upload a local image and send it as a sticker; the composer only has a path.
    Q_INVOKABLE void sendStickerFile(const QString &chatId, const QString &localFilePath);
    // send a recorded voice message (MSC3245): an m.audio carrying the voice flag.
    Q_INVOKABLE void sendVoice(const QString &chatId, const QString &localFilePath, int durationMs);
    // send a poll (MSC3386 stable m.poll.start). disclosed shows results as they
    // come; undisclosed hides them until the writer closes the poll.
    Q_INVOKABLE void sendPoll(const QString &chatId, const QString &question, const QStringList &answers, bool disclosed);
    // cast a vote in a poll (m.poll.response). msgId is the poll's event id; the
    // window resolves a row stamp to it the same way replies do.
    Q_INVOKABLE void sendPollVote(const QString &chatId, const QString &msgId, const QString &answerId);
    // send text hidden until revealed (MSC2446 spoiler), as a formatted m.text.
    Q_INVOKABLE void sendSpoiler(const QString &chatId, const QString &text);

    // rooms are made and joined, not sent, so these sit outside ChatBackend.
    // the window calls them on the bridge directly; each is a sync round trip
    // that answers via roomListed()/roomLeft() or roomOperationFailed().
    Q_INVOKABLE void createRoom(const QString &name, const QString &topic, const QString &alias, const QStringList &invitedUsers, bool isPrivate);
    // by canonical alias or by full room id, both the same homeserver call.
    Q_INVOKABLE void joinRoom(const QString &aliasOrId);
    Q_INVOKABLE void acceptInvite(const QString &chatId);
    Q_INVOKABLE void declineInvite(const QString &chatId);
    Q_INVOKABLE void inviteMember(const QString &chatId, const QString &userId);
    Q_INVOKABLE void kickMember(const QString &chatId, const QString &userId);
    Q_INVOKABLE void banMember(const QString &chatId, const QString &userId);
    Q_INVOKABLE void unbanMember(const QString &chatId, const QString &userId);

    // a one-on-one with a member of this homeserver. existing direct rooms are
    // reused; requestDirectChat() makes one otherwise, and the join chimes
    // directChatOpened() so the window opens it.
    Q_INVOKABLE void openDirectChat(const QString &userId);

    // calls between KOutNet sessions inside a Matrix room. the voice media goes
    // peer to peer over the same TCP channel a LAN call uses; the homeserver
    // only signals it via m.call.* carrying each side LAN address. net, voice
    // and crypto are handed in by main.cpp, which owns them.
    void setCallStack(NetworkManager *net, VoiceCallManager *voice, CryptoManager *crypto);
    // start a room call as the caller; everyone already in the room is invited,
    // the first to answer becomes the peer.
    Q_INVOKABLE void callRoom(const QString &chatId);
    // Answer an invitation the window is showing (roomCallInvited()).
    Q_INVOKABLE void acceptCall(const QString &chatId, const QString &callId);
    Q_INVOKABLE void declineCall(const QString &chatId, const QString &callId);
    Q_INVOKABLE void hangupRoomCall(const QString &chatId);

Q_SIGNALS:
    // a room the list has not heard about, or one whose name changed.
    // idempotent, like ChatListModel::openChat().
    void roomListed(QString chatId, QString displayName, QString avatarUrl = QString());
    void roomLeft(QString chatId);

    // one signal for every text row; ChatModel takes them all in one call and
    // de-dupes by eventId. system rows carry a synthetic id so a restart
    // does not stack a second copy.
    void roomMessage(QString chatId, QString eventId, QString text, QString sender, bool isOwn, double ts, bool isSystem, QString senderAvatar = QString());

    // a picture, a recording or a file, as a map (not nine args) shaped like
    // ChatModel::ingestRemoteAttachment() expects. keys: kind, url, name,
    // mime, size, width, height, duration.
    void roomAttachment(QString chatId, QString eventId, QVariantMap media, QString sender, bool isOwn, double ts, QString senderAvatar = QString());
    // a poll (m.poll.start) arrived. answers is a list of {id, body} maps; the
    // window renders the voter and sends choices back through sendPollVote().
    void roomPoll(QString chatId,
                  QString eventId,
                  QString question,
                  QVariantList answers,
                  bool disclosed,
                  QString sender,
                  bool isOwn,
                  double ts,
                  QString senderAvatar = QString());

    // one vote on a poll (m.poll.response). eventId is the poll start it answers,
    // answerId the chosen option; the window folds it into that poll's tally.
    void roomPollVote(QString chatId, QString eventId, QString answerId, QString voterId, bool isOwn);

    // a reaction to a message. ts is the target message stamp, never the
    // reaction itself - the ReactionStore keys on the target, like the LAN one.
    void roomReaction(QString chatId, double ts, QString emoji, QString sender, bool added);

    // an m.replace for an event already shown. never a row of its own, or a
    // corrected typo would show up as two messages.
    void roomMessageEdited(QString chatId, QString eventId, QString newText);

    // someone typed or stopped, in an open room. a boolean not a name: the
    // window shows one indicator per chat, the member hardly matters.
    void roomTyping(QString chatId, bool typing);

    // a read receipt passed one of our own messages; the window marks it read
    // like it does for a LAN receipt.
    void roomReadReceipt(QString chatId);

    // a message this window already shows was redacted. the row is removed,
    // like the LAN unsend does, and like every client in the room will.
    void roomMessageRemoved(QString chatId, QString eventId);

    // a message that was "no key for this" has now decrypted. separate from
    // roomMessageEdited() because nothing was edited; marking it edited would
    // lie about a message whose whole point is it is now readable.
    void roomMessageRevealed(QString chatId, QString eventId, QString text);

    // the topic, name, members, address or picture moved. deliberately coarse,
    // so whoever shows the room just asks again.
    void roomInfoChanged(QString chatId);

    // the pinned set moved. each entry is a map shaped like roomMessage()'s
    // row - eventId, ts, text, sender - resolved from the timeline so the pin
    // sheet can show what it pins without another round trip.
    void roomPinnedChanged(QString chatId, QVariantList pinned);

    // results of searchMessages(): a list of maps (eventId, ts, text, sender),
    // newest first, capped to a sane number.
    void roomSearchResults(QString chatId, QVariantList results);

    // the account ignore list changed (ignoreUser/unignoreUser). the member list
    // and the room re-read it to grey out muted users.
    void ignoreListChanged();

    void sendFailed(QString chatId, QString reason);

    // an invitation arrived; the list shows it with accept/decline, not as a chat.
    // inviterId/inviterName say who asked; either may be empty at first.
    void roomInvited(QString chatId, QString displayName, QString inviterId, QString inviterName);
    // the invitation is gone: accepted, declined, or withdrawn.
    void roomInviteGone(QString chatId);

    // a room operation the homeserver refused: create, join, invite, kick.
    // one channel for all, since the window answers each with the same toast.
    void roomOperationFailed(QString chatId, QString reason);
    // a room operation the homeserver accepted (e.g. an avatar was set).
    void roomOperationSucceeded(QString chatId);

    // a one-on-one requested by openDirectChat() is ready, whether an existing
    // direct room was found or the one requestDirectChat() made came through.
    // the window opens it as a chat.
    void directChatOpened(QString chatId);

    // somebody in the room asked us to join a call. the call id travels with it,
    // since the answer to an invite names the call.
    void roomCallInvited(QString chatId, QString callId, QString sender);
    // the invite this session sent was answered. the caller opens the call UI
    // here, the media channel is already up.
    void roomCallAccepted(QString chatId);
    // the call in the room is over. the window closes its call UI.
    void roomCallEnded(QString chatId);

private:
    void attach(Quotient::Connection *connection);
    void trackRoom(Quotient::Room *room);
    bool isCurrentRoom(const Quotient::Room *room, quint64 generation) const;
    void publishRoom(Quotient::Room *room);
    void publishRange(Quotient::Room *room, int fromIndex, int toIndex);
    void publishEvent(Quotient::Room *room, const Quotient::RoomEvent *event);
    // turns a pending event libQuotient gave up on into something the user sees.
    // without it a refused send is as quiet as a delivered one.
    void reportPendingFailure(Quotient::Room *room, int pendingIndex);
    // a timeline event libQuotient swapped for its decrypted self.
    void revealEvent(Quotient::Room *room, const Quotient::RoomEvent *event);
    Quotient::Room *roomFor(const QString &chatId) const;
    // the pinned event ids, resolved to timeline rows (see rowForEvent).
    QVariantList pinnedRows(Quotient::Room *room) const;
    // the invite object for a room we were asked into but have not joined yet
    Quotient::Room *invitedRoomFor(const QString &chatId) const;
    // who sent the invite and their display name; either may be empty early
    void inviterOf(Quotient::Room *room, QString *inviterId, QString *inviterName) const;
    // a m.call.* event arrived. voice signalling only, the media is peer to
    // peer and never touches the homeserver, so this is a handshake on who
    // to connect to.
    void handleCallEvent(Quotient::Room *room, const Quotient::RoomEvent *event);
    // what one side hands the other in a m.call SDP field. the address is
    // where the media dials, the key encrypts it - same path as the call, so
    // as private as the room.
    struct CallOffer {
        QString address;
        QByteArray key;
    };
    static CallOffer callOfferFromSdp(const QString &sdp);
    CallOffer ownCallOffer() const;

    QPointer<MatrixManager> m_manager;
    // room id to the object whose timeline signals are connected. libQuotient
    // replaces the object on join-state change, so the id alone cannot say the
    // connections are still live, the pointer can. guarded because a room is a
    // child of its Connection, which can vanish without leftRoom() firing.
    QHash<QString, QPointer<QObject>> m_tracked;
    quint64 m_roomGeneration = 0;

    // the window addresses rows by stamp, the homeserver by event id. both
    // directions are kept per room, so a reaction/edit/unsend resolves to the
    // event it is about, and an echo files under the stamp ReactionStore wants.
    // one entry per told row; a colliding stamp (two events in one ms) keeps
    // the later, which is the one shown later.
    QHash<QString, QHash<double, QString>> m_tsToEventId;
    QHash<QString, QHash<QString, double>> m_eventIdToTs;

    // when each room last got a typing packet, so a typist costs one round trip
    // per sentence, not per keystroke.
    QHash<QString, qint64> m_lastTypingSent;

    // reactions this bridge published, keyed by reaction event id. a redaction
    // wipes the reaction content, so the badge is remembered from the
    // original - and a redaction only matters for an event already on screen.
    struct PublishedReaction {
        double targetTs = 0.0;
        QString emoji;
        QString sender;
    };
    QHash<QString, QHash<QString, PublishedReaction>> m_reactions;

    // voice call state per room. one call id per room; a member who answers the
    // same invite becomes another peer on the caller mixer, like a LAN group
    // call. the media side lives in VoiceCallManager keyed by LAN address, so
    // this table stores addresses not member ids.
    struct RoomCall {
        QString callId;
        QString role; // "caller" or "answerer"
        QStringList peerAddresses;
        // the shared key each peer address media is sealed with, in both
        // directions. dropped when the call ends, like the sessions.
        QHash<QString, QByteArray> peerKeys;
        bool established = false;
    };
    QHash<QString, RoomCall> m_calls;

    // the invite the window is currently showing, if any: the call id, the
    // address to ring when accepted, and the key the ringing is encrypted
    // under. only one at a time, like the LAN incoming-call dialog.
    struct PendingCall {
        QString chatId;
        QString callId;
        QString peerAddress;
        QByteArray peerKey;
    };
    PendingCall m_pending;

    // set by openDirectChat() while the homeserver makes the room; a matching
    // join chimes directChatOpened().
    QString m_pendingDirectTarget;

    QPointer<NetworkManager> m_net;
    QPointer<VoiceCallManager> m_voice;
    QPointer<CryptoManager> m_crypto;
};

} // namespace koutnet
