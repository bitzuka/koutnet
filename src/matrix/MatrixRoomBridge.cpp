// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "MatrixRoomBridge.h"

#include "MatrixManager.h"
#include "MatrixTranslate.h"
#include "core/chat/ChatAddress.h"

#include <KLocalizedString>

#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QSize>
#include <QUrl>

#include <algorithm>
#include <memory>
#include <utility>

#include <QJsonObject>
#include <QRandomGenerator>
#include <QUuid>
#include <Quotient/connection.h>
#include <Quotient/csapi/create_room.h>
#include <Quotient/csapi/joining.h>
#include <Quotient/csapi/leaving.h>
#include <Quotient/csapi/typing.h>
#include <Quotient/events/callevents.h>
#include <Quotient/events/encryptionevent.h>
#include <Quotient/events/eventrelation.h>
#include <Quotient/events/reactionevent.h>
#include <Quotient/events/redactionevent.h>
#include <Quotient/events/roomavatarevent.h>
#include <Quotient/events/roomcanonicalaliasevent.h>
#include <Quotient/events/roomcreateevent.h>
#include <Quotient/events/roommemberevent.h>
#include <Quotient/events/roommessageevent.h>
#include <Quotient/events/roompowerlevelsevent.h>
#include <Quotient/events/simplestateevents.h>
#include <Quotient/events/stateevent.h>
#include <Quotient/room.h>
#include <Quotient/roommember.h>

#include "core/security/CryptoManager.h"
#include "network/NetworkManager.h"
#include "network/VoiceCallManager.h"

using namespace Quotient;

namespace
{
using koutnet::matrix::MediaKind;
using koutnet::matrix::StateChange;

bool isTextLike(RoomMessageEvent::MsgType type)
{
    return type == RoomMessageEvent::MsgType::Text || type == RoomMessageEvent::MsgType::Notice || type == RoomMessageEvent::MsgType::Emote;
}

MediaKind mediaKindOf(RoomMessageEvent::MsgType type)
{
    switch (type) {
    case RoomMessageEvent::MsgType::Image:
        return MediaKind::Image;
    case RoomMessageEvent::MsgType::Video:
        return MediaKind::Video;
    case RoomMessageEvent::MsgType::Audio:
        return MediaKind::Audio;
    case RoomMessageEvent::MsgType::File:
        return MediaKind::File;
    default:
        return MediaKind::None;
    }
}

// A name and not the enumerator's number, because the other end of this map is
// QML and a number there is unreadable.
QString mediaKindName(MediaKind kind)
{
    switch (kind) {
    case MediaKind::Image:
        return QStringLiteral("image");
    case MediaKind::Video:
        return QStringLiteral("video");
    case MediaKind::Audio:
        return QStringLiteral("audio");
    case MediaKind::File:
        return QStringLiteral("file");
    case MediaKind::None:
        break;
    }
    return QString();
}

// Whether a send into this room can be protected the way the room requires.
// An unencrypted room is always yes. An encrypted room is yes only while the
// session actually has its key store: libQuotient drops the event on the floor
// otherwise, and posting it in the clear instead is never the answer, because
// every other client in the room would show it with a warning.
bool canSendEncrypted(const Room *room)
{
    if (room == nullptr || !room->usesEncryption())
        return true;
    return room->connection() != nullptr && room->connection()->encryptionEnabled();
}

// Whether libQuotient's trust tables can be asked anything at all.
//
// A security invariant with teeth: Connection::isVerifiedDevice(),
// isUserVerified() and allSessionsSelfVerified() all run a query against
// Connection::database(), which is null whenever the key store was not set up,
// and none of them check. devicesForUser() dereferences the encryption data
// directly. Asking any of them too early is a crash, and answering "not
// verified" because the question could not be asked is a different lie from
// "not verified" because it is not - which is why the maps below carry
// trustKnown rather than a bare boolean.
bool canAskAboutTrust(const Connection *connection)
{
    return connection != nullptr && connection->encryptionEnabled() && connection->database() != nullptr;
}

QString encryptionUnavailableReason()
{
    return i18nc("@info:status a message for an encrypted room was not sent because the session has no keys",
                 "This room is end-to-end encrypted and this session could not open its encryption keys, so nothing was sent.");
}

// The room-local display name, falling back to the id. Never blank: a state
// line that begins with a space is what the fallback is here to prevent.
QString memberLabel(const Room *room, const QString &userId)
{
    if (room == nullptr || userId.isEmpty())
        return userId;
    const QString name = room->member(userId).displayName();
    return name.isEmpty() ? userId : name;
}

// The media URL of a member's avatar, or an empty string when the member has
// none or this session is looking at its own picture (which is a local file).
QString memberAvatarUrl(const Room *room, const QString &userId)
{
    if (room == nullptr || userId.isEmpty() || room->connection() == nullptr)
        return QString();
    if (userId == room->connection()->userId())
        return QString();
    const QUrl avatar = room->member(userId).avatarUrl();
    if (avatar.scheme() != QLatin1String("mxc"))
        return QString();
    return room->connection()->makeMediaUrl(avatar).toString();
}

// Whether a member is currently joined to the room. joinedMemberCount() would
// answer the count but not the identity, and a direct room is the room with
// exactly one other member: this is who.
bool isJoinedMember(const Room *room, const QString &userId)
{
    if (room == nullptr || userId.isEmpty())
        return false;
    return room->memberState(userId) == Membership::Join;
}

// The picture a conversation row should carry: the other member's avatar in a
// one-on-one, the room's own otherwise. Empty when there is none to show.
QString roomAvatarUrl(const Room *room)
{
    if (room == nullptr || room->connection() == nullptr)
        return QString();
    if (room->isDirectChat()) {
        const auto members = room->joinedMembers();
        for (const RoomMember &member : members) {
            if (member.id() == room->connection()->userId())
                continue;
            const QUrl avatar = member.avatarUrl();
            if (avatar.scheme() == QLatin1String("mxc"))
                return room->connection()->makeMediaUrl(avatar).toString();
        }
    }
    const QUrl avatar = room->avatarUrl();
    if (avatar.scheme() == QLatin1String("mxc"))
        return room->connection()->makeMediaUrl(avatar).toString();
    return QString();
}

// Which of the member-event cases this is. The distinctions - and which ones
// are worth drawing at all - are NeoChat's, from src/libneochat/eventhandler.cpp
// (EventHandler::genericBody) by James Graham.
StateChange classifyMember(const Room *room, const RoomMemberEvent &event, QString &subject)
{
    const bool aboutSomeoneElse = event.senderId() != event.userId();

    switch (event.membership()) {
    case Membership::Invite:
        subject = memberLabel(room, event.userId());
        return StateChange::Invited;

    case Membership::Join: {
        if (event.changesMembership())
            return StateChange::Joined;

        // Already joined, so this is a profile change rather than an arrival.
        // Names before pictures: a picture that changed in the same event is
        // the less interesting half and does not earn a line of its own.
        if (event.isRename()) {
            if (!event.newDisplayName())
                return StateChange::DisplayNameCleared;
            if (!event.prevContent() || !event.prevContent()->displayName)
                return StateChange::DisplayNameSet;
            subject = *event.prevContent()->displayName;
            return StateChange::DisplayNameChanged;
        }
        if (event.isAvatarUpdate())
            return StateChange::MemberAvatarChanged;
        // A join event that changed nothing. NeoChat says so out loud; here it
        // is dropped, because it is a sync artefact rather than an event.
        return StateChange::None;
    }

    case Membership::Leave:
        subject = memberLabel(room, event.userId());
        if (event.prevContent() && event.prevContent()->membership == Membership::Invite)
            return aboutSomeoneElse ? StateChange::InviteWithdrawn : StateChange::InviteRejected;
        if (event.prevContent() && event.prevContent()->membership == Membership::Ban)
            return aboutSomeoneElse ? StateChange::Unbanned : StateChange::SelfUnbanned;
        return aboutSomeoneElse ? StateChange::Kicked : StateChange::Left;

    case Membership::Ban:
        subject = memberLabel(room, event.userId());
        return aboutSomeoneElse ? StateChange::Banned : StateChange::SelfBanned;

    case Membership::Knock:
        return StateChange::KnockRequested;

    default:
        break;
    }
    return StateChange::Unknown;
}

// Which state change an event is, and what its one substituted string is. None
// means the event earns no line in the timeline.
StateChange classifyState(const Room *room, const RoomEvent *event, QString &subject)
{
    if (!event->isStateEvent())
        return StateChange::None;

    // A state event that restates what the room already said is a sync
    // artefact. Checked first: every branch below would otherwise print a
    // duplicate line after each reconnect.
    if (const auto *state = eventCast<const StateEvent>(event); state && state->repeatsState())
        return StateChange::None;

    if (const auto *member = eventCast<const RoomMemberEvent>(event))
        return classifyMember(room, *member, subject);

    if (const auto *name = eventCast<const RoomNameEvent>(event)) {
        subject = name->name();
        return subject.isEmpty() ? StateChange::RoomNameCleared : StateChange::RoomNameSet;
    }
    if (const auto *topic = eventCast<const RoomTopicEvent>(event)) {
        // Flattened: the topic gets one line of the timeline, and a topic of
        // three paragraphs would otherwise take three paragraphs of it.
        subject = topic->topic().simplified();
        return subject.isEmpty() ? StateChange::TopicCleared : StateChange::TopicSet;
    }
    if (const auto *alias = eventCast<const RoomCanonicalAliasEvent>(event)) {
        subject = alias->alias();
        return subject.isEmpty() ? StateChange::AliasCleared : StateChange::AliasSet;
    }
    if (is<RoomAvatarEvent>(*event))
        return StateChange::RoomAvatarChanged;
    if (is<EncryptionEvent>(*event))
        return StateChange::EncryptionEnabled;
    if (const auto *create = eventCast<const RoomCreateEvent>(event))
        return create->isUpgrade() ? StateChange::RoomUpgraded : StateChange::RoomCreated;
    if (is<RoomPowerLevelsEvent>(*event))
        return StateChange::PowerLevelsChanged;

    // Some other state event: a widget, a server ACL, a call member. Named as
    // unknown rather than skipped, so the timeline never has a silent gap.
    return StateChange::Unknown;
}

void fillMedia(const Room *room, const RoomMessageEvent &message, koutnet::matrix::RawEvent &raw)
{
    // Read off the raw JSON rather than the typed content accessors: get<T>()
    // is a template instantiated inside libQuotient for a fixed list of content
    // types, and a build whose library list differs from its headers' links
    // nothing. The JSON fields are the spec, so they read the same everywhere.
    const QJsonObject content = message.contentJson();
    const QJsonObject info = content.value(QStringLiteral("info")).toObject();

    raw.mediaName = content.value(QStringLiteral("body")).toString();
    raw.mediaMime = info.value(QStringLiteral("mimetype")).toString();
    raw.mediaSize = info.value(QStringLiteral("size")).toInteger();

    // The dimensions matter: without them a picture is laid out at whatever
    // the decoder reports once the download finishes, which moves the timeline
    // under whoever is reading it.
    raw.mediaWidth = info.value(QStringLiteral("w")).toInt();
    raw.mediaHeight = info.value(QStringLiteral("h")).toInt();
    raw.mediaDurationMs = info.value(QStringLiteral("duration")).toInt();

    // An encrypted attachment carries the address one level down, under file.
    const QJsonObject file = content.value(QStringLiteral("file")).toObject();
    const QUrl source(file.isEmpty() ? content.value(QStringLiteral("url")).toString() : file.value(QStringLiteral("url")).toString());
    // makeMediaUrl() asserts on the scheme, and an encrypted attachment or a
    // malformed event can carry something that is not an mxc URI.
    if (source.scheme() == QLatin1String("mxc") && room != nullptr && room->connection() != nullptr)
        raw.mediaUrl = room->makeMediaUrl(message.id(), source).toString();
}

koutnet::matrix::RawEvent flatten(const Room *room, const RoomEvent *event)
{
    koutnet::matrix::RawEvent raw;
    raw.eventId = event->id();
    raw.senderId = event->senderId();
    raw.senderName = room->member(event->senderId()).displayName();
    raw.originTimestampMs = event->originTimestamp().toMSecsSinceEpoch();
    raw.isOwn = !raw.senderId.isEmpty() && room->connection() != nullptr && raw.senderId == room->connection()->userId();
    raw.redacted = event->isRedacted();

    raw.state = classifyState(room, event, raw.stateSubject);
    if (raw.state != StateChange::None)
        return raw;
    // A state event classified as None has nothing to show. Cleared rather than
    // left to fall through, or it would be drawn as an empty message.
    if (event->isStateEvent()) {
        raw.eventId.clear();
        return raw;
    }

    if (const auto *message = eventCast<const RoomMessageEvent>(event)) {
        raw.body = message->plainBody();
        raw.textLike = isTextLike(message->msgtype());
        raw.media = mediaKindOf(message->msgtype());
        if (raw.media != MediaKind::None && !raw.redacted)
            fillMedia(room, *message, raw);
    } else if (is<EncryptedEvent>(*event)) {
        raw.encrypted = true;
    } else {
        // A reaction, a redaction event itself, a call signal: not a row.
        raw.eventId.clear();
    }
    return raw;
}

// The corrected text of an m.replace. The replacement's own body is the
// "* corrected text" fallback for clients that do not understand edits, and
// showing that is how the asterisk ends up in the timeline.
QString replacementBody(const RoomMessageEvent &message)
{
    const QJsonObject newContent = message.contentJson().value(QLatin1String("m.new_content")).toObject();
    const QString body = newContent.value(QLatin1String("body")).toString();
    return body.isEmpty() ? message.plainBody() : body;
}
} // namespace

namespace koutnet
{

MatrixRoomBridge::MatrixRoomBridge(MatrixManager *manager, QObject *parent)
    : ChatBackend(parent)
    , m_manager(manager)
{
    if (m_manager) {
        connect(m_manager, &MatrixManager::connectionChanged, this, [this]() {
            // Detached by hand rather than left to the rooms being destroyed: a
            // connection on its way out lives until the homeserver answers or
            // gives up, and until then its rooms would go on filing messages
            // into a session the user has already left.
            for (const QPointer<QObject> &room : std::as_const(m_tracked)) {
                if (room)
                    room->disconnect(this);
            }
            m_tracked.clear();
            attach(m_manager ? m_manager->connection() : nullptr);
        });
        attach(m_manager->connection());
    }
}

void MatrixRoomBridge::attach(Connection *connection)
{
    if (!connection)
        return;

    connect(connection, &Connection::joinedRoom, this, [this](Room *room) {
        trackRoom(room);
    });
    connect(connection, &Connection::invitedRoom, this, [this](Room *room, Room *) {
        // Nothing to track yet - see trackRoom's note. The invitation is
        // offered to the conversation list, which draws it with accept and
        // decline buttons; the join moves it through joinedRoom() and the
        // invite object is deleted, which is how the list hears it is gone.
        Q_EMIT roomInvited(chatid::matrixChatId(room->id()), matrix::conversationTitle(room->displayName(), room->id()));
    });
    connect(connection, &Connection::loadedRoomState, this, [this](Room *room) {
        // The room's name and members are only settled here; joinedRoom() fires
        // while the object is still nameless, which is how rows used to appear
        // in the list titled with their room id and stay that way.
        trackRoom(room);
    });
    connect(connection, &Connection::leftRoom, this, [this](Room *room, Room *prev) {
        const QString chatId = chatid::matrixChatId(room->id());
        m_tracked.remove(room->id());
        m_tsToEventId.remove(chatId);
        m_eventIdToTs.remove(chatId);
        m_lastTypingSent.remove(chatId);
        m_reactions.remove(chatId);
        m_calls.remove(chatId);
        Q_EMIT roomLeft(chatId);
        // Declining an invitation: the room this account was asked into matches
        // a new object (this one), and the invite object itself comes in prev.
        if (prev != nullptr && prev->joinState() == JoinState::Invite)
            Q_EMIT roomInviteGone(chatid::matrixChatId(prev->id()));
    });
    connect(connection, &Connection::aboutToDeleteRoom, this, [this](Room *room) {
        const QString chatId = chatid::matrixChatId(room->id());
        if (m_tracked.value(room->id()) == room) {
            m_tracked.remove(room->id());
            m_tsToEventId.remove(chatId);
            m_eventIdToTs.remove(chatId);
            m_lastTypingSent.remove(chatId);
            m_reactions.remove(chatId);
            m_calls.remove(chatId);
        }
        // The other way an invitation dies: the join landed, a stale invite
        // object was created for the same room, and it is being retired. No
        // leftRoom() precedes this - see Connection's room transition table.
        if (room->joinState() == JoinState::Invite)
            Q_EMIT roomInviteGone(chatId);
    });

    // A verification that just succeeded moved the trust table, and the room
    // column and the member card are drawing straight off that table. Without
    // this the padlock only catches up whenever something else happens to make
    // the column re-read, which for a quiet room is never.
    auto retellTrust = [this]() {
        const auto ids = m_tracked.keys();
        for (const QString &roomId : ids)
            Q_EMIT roomInfoChanged(chatid::matrixChatId(roomId));
    };
    connect(connection, &Connection::sessionVerified, this, retellTrust);
    connect(connection, &Connection::userVerified, this, retellTrust);

    // Whatever loadState() already put in place. Rooms that arrive later come
    // through the signals above.
    const auto rooms = connection->allRooms();
    for (Room *room : rooms)
        trackRoom(room);
}

void MatrixRoomBridge::trackRoom(Room *room)
{
    // An invitation is a room this account does not have a timeline in yet, so
    // it cannot be opened as a chat. The Connection owns the join-state
    // transitions and hands every one of them to a different Room object - an
    // invite is a Room in its own right that is deleted the moment the join
    // lands - so an invitation is announced by attach() on Connection's
    // invitedRoom()/leftRoom()/aboutToDeleteRoom() and never tracked here.
    if (!room || room->joinState() != JoinState::Join)
        return;

    if (m_tracked.value(room->id()) == room) {
        // Already wired; the name may still have moved.
        publishRoom(room);
        return;
    }
    m_tracked.insert(room->id(), room);

    // A direct room requested by openDirectChat() comes through the join after
    // the homeserver created it; the window asks for it to be opened, so the
    // answer is emitted the moment the room is known to be the one requested.
    if (!m_pendingDirectTarget.isEmpty() && room->isDirectChat() && isJoinedMember(room, m_pendingDirectTarget) && room->joinedCount() == 2) {
        m_pendingDirectTarget.clear();
        Q_EMIT directChatOpened(chatid::matrixChatId(room->id()));
    }

    connect(room, &Room::addedMessages, this, [this, room](int fromIndex, int toIndex) {
        publishRange(room, fromIndex, toIndex);
    });
    // This is how an outgoing message gets into the timeline, and the only way.
    // addedMessages() is emitted for the span of a sync that is *not* an echo of
    // something this client sent; Room::Private::mergePendingEvent() folds our
    // own message straight into its pending item and emits these two instead. A
    // message typed here was posted, accepted by the homeserver and seen by
    // every other client in the room, and never appeared on this screen.
    connect(room, &Room::pendingEventAboutToMerge, this, [this, room](RoomEvent *serverEvent, int) {
        publishEvent(room, serverEvent);
    });
    // The other half of the same silence: a send libQuotient gave up on.
    connect(room, &Room::pendingEventChanged, this, [this, room](int index) {
        reportPendingFailure(room, index);
    });
    connect(room, &Room::displaynameChanged, this, [this, room]() {
        publishRoom(room);
    });
    // A megolm key that turned up after the message it opens. libQuotient
    // swaps the decrypted event into the timeline in place and says so here;
    // without this the placeholder row stays a placeholder for the rest of the
    // run even though the text is sitting right there.
    connect(room, &Room::replacedEvent, this, [this, room](const RoomEvent *newEvent, const RoomEvent *) {
        revealEvent(room, newEvent);
    });

    // Someone in the room is typing. The window shows one indicator per
    // conversation and does not name who, so the answer is a boolean - and it
    // is otherMembersTyping(), not membersTyping(), because this session's own
    // typing must not light the indicator it set itself.
    connect(room, &Room::typingChanged, this, [this, room]() {
        Q_EMIT roomTyping(chatid::matrixChatId(room->id()), !room->otherMembersTyping().isEmpty());
    });

    // A read receipt moved. Reported only for somebody else's receipt, and
    // only when it is about this session's own message: reading a message of
    // a third member says nothing about whether this session's messages were
    // read, and claiming it did would be the same lie as the LAN path claiming
    // it for a message that was never on the screen.
    connect(room, &Room::lastReadEventChanged, this, [this, room](const QVector<QString> &userIds) {
        const QString ownId = room->connection() ? room->connection()->userId() : QString();
        for (const QString &userId : userIds) {
            if (userId == ownId)
                continue;
            const ReadReceipt receipt = room->lastReadReceipt(userId);
            const auto target = room->findInTimeline(receipt.eventId);
            if (target != room->historyEdge() && target->event()->senderId() == ownId) {
                Q_EMIT roomReadReceipt(chatid::matrixChatId(room->id()));
                return;
            }
        }
    });

    // The room's own column reads all of these through roomInfo(), so they
    // share one signal rather than getting a property each.
    const auto announceInfo = [this, room]() {
        Q_EMIT roomInfoChanged(chatid::matrixChatId(room->id()));
    };
    connect(room, &Room::topicChanged, this, announceInfo);
    // A room that has just turned encryption on changes what the room column
    // must say about it. The timeline line for the same event comes from the
    // m.room.encryption state event like any other state change.
    connect(room, &Room::encryption, this, announceInfo);
    connect(room, &Room::avatarChanged, this, announceInfo);
    connect(room, &Room::memberListChanged, this, announceInfo);
    connect(room, &Room::namesChanged, this, announceInfo);

    // m.call.invite/answer/hangup. libQuotient 0.9 hands the whole event over;
    // the caller's identity is in the sender, the rest is in the content.
    connect(room, &Room::callEvent, this, [this](Room *callRoom, const RoomEvent *event) {
        handleCallEvent(callRoom, event);
    });

    publishRoom(room);

    // Whatever is already in the timeline from the state cache. Indices run from
    // the oldest known event to the newest, both ends inclusive.
    if (!room->messageEvents().empty())
        publishRange(room, room->minTimelineIndex(), room->maxTimelineIndex());
}

void MatrixRoomBridge::publishRoom(Room *room)
{
    const QString chatId = chatid::matrixChatId(room->id());
    Q_EMIT roomListed(chatId, matrix::conversationTitle(room->displayName(), room->id()), roomAvatarUrl(room));
    Q_EMIT roomInfoChanged(chatId);
}

void MatrixRoomBridge::publishRange(Room *room, int fromIndex, int toIndex)
{
    for (int i = fromIndex; i <= toIndex; ++i) {
        const auto it = room->findInTimeline(i);
        // Out-of-range lookups hand back historyEdge() - the only sentinel the
        // reverse iterator has, equal to messageEvents().crend() - and
        // dereferencing it is undefined behaviour. The index range comes from
        // the room's own min/max, so this is normally an index between cached
        // events; the check is for the index that races an ongoing sync.
        if (it == room->historyEdge())
            continue;
        publishEvent(room, it->event());
    }
}

void MatrixRoomBridge::publishEvent(Room *room, const RoomEvent *event)
{
    if (room == nullptr || event == nullptr)
        return;

    const QString chatId = chatid::matrixChatId(room->id());

    // A reaction is never a row: it is a statement about a message that is
    // already on the screen, and it earns no line of its own. The ReactionStore
    // is keyed on the stamp of the message being reacted to, which is how the
    // LAN protocol files reactions too, so the reaction's own timestamp is
    // deliberately not what is sent - the target's is.
    if (const auto *reaction = eventCast<const ReactionEvent>(event)) {
        const QString reactionId = event->id();
        const double targetTs = m_eventIdToTs.value(chatId).value(reaction->eventId());
        // A reaction that was redacted loses its content, so the emoji is not
        // in the event any more; it is remembered here instead, from when the
        // reaction was first published.
        if (event->isRedacted()) {
            const auto it = m_reactions.value(chatId).constFind(reactionId);
            if (it != m_reactions.value(chatId).constEnd()) {
                Q_EMIT roomReaction(chatId, it->targetTs, it->emoji, it->sender, false);
                m_reactions[chatId].erase(it);
            }
            return;
        }
        if (targetTs > 0.0 && !reaction->key().isEmpty()) {
            // This session's own reaction also comes back through sync. The
            // local toggle already filed it under "me" when it was sent, so
            // the echo would badge the same reaction twice under two names -
            // and the sender label is the display name, not "me".
            const QString ownId = room->connection() ? room->connection()->userId() : QString();
            if (reaction->senderId() != ownId)
                Q_EMIT roomReaction(chatId, targetTs, reaction->key(), memberLabel(room, reaction->senderId()), true);
            m_reactions[chatId].insert(reactionId, {targetTs, reaction->key(), memberLabel(room, reaction->senderId())});
        }
        return;
    }

    // A redaction is the event that takes a message or a reaction back. The
    // redacted event itself stays in the timeline, so what the window already
    // shows has to be told apart by whoever redacted it: a message row is
    // removed, a reaction badge is taken down.
    if (const auto *redaction = eventCast<const RedactionEvent>(event)) {
        const QString redactedId = redaction->redactedEvent();
        const auto it = m_reactions.value(chatId).constFind(redactedId);
        if (it != m_reactions.value(chatId).constEnd()) {
            Q_EMIT roomReaction(chatId, it->targetTs, it->emoji, it->sender, false);
            m_reactions[chatId].erase(it);
            return;
        }
        if (m_eventIdToTs.value(chatId).contains(redactedId))
            Q_EMIT roomMessageRemoved(chatId, redactedId);
        return;
    }

    // An edit is dealt with before anything else and never becomes a row: it is
    // a statement about a message that is already on the screen.
    if (const auto *message = eventCast<const RoomMessageEvent>(event)) {
        const QString replaced = message->replacedEvent();
        if (!replaced.isEmpty()) {
            const QString body = replacementBody(*message);
            if (!body.isEmpty())
                Q_EMIT roomMessageEdited(chatId, replaced, body);
            return;
        }
    }

    const matrix::Row row = matrix::rowFor(flatten(room, event));
    switch (row.kind) {
    case matrix::RowKind::Skip:
        break;
    case matrix::RowKind::Text: {
        const QString avatar = memberAvatarUrl(room, row.senderId);
        Q_EMIT roomMessage(chatId, row.msgId, row.text, row.sender, row.isOwn, row.ts, false, avatar);
        break;
    }
    case matrix::RowKind::System: {
        const QString avatar = memberAvatarUrl(room, row.senderId);
        Q_EMIT roomMessage(chatId, row.msgId, row.text, row.sender, row.isOwn, row.ts, true, avatar);
        break;
    }
    case matrix::RowKind::Encrypted: {
        // Per event, not per room. The room is readable; this one message is
        // not, and saying it about the room would be a lie about the rest.
        const QString avatar = memberAvatarUrl(room, row.senderId);
        Q_EMIT roomMessage(chatId, row.msgId, row.text, row.sender, row.isOwn, row.ts, true, avatar);
        break;
    }
    case matrix::RowKind::Attachment: {
        QVariantMap media;
        media.insert(QStringLiteral("kind"), mediaKindName(row.media));
        media.insert(QStringLiteral("url"), row.mediaUrl);
        media.insert(QStringLiteral("name"), row.mediaName);
        media.insert(QStringLiteral("mime"), row.mediaMime);
        media.insert(QStringLiteral("size"), QVariant::fromValue(row.mediaSize));
        media.insert(QStringLiteral("width"), row.mediaWidth);
        media.insert(QStringLiteral("height"), row.mediaHeight);
        media.insert(QStringLiteral("duration"), row.mediaDurationMs);
        const QString avatar = memberAvatarUrl(room, row.senderId);
        Q_EMIT roomAttachment(chatId, row.msgId, media, row.sender, row.isOwn, row.ts, avatar);
        break;
    }
    }

    // The window addresses messages by stamp and the homeserver by event id;
    // both directions of that table are kept here so that a reaction, an edit
    // or an unsend can be resolved to the event it is about, and a reaction
    // arriving for an event already shown can be filed under the stamp the
    // ReactionStore expects. System rows carry a synthetic id that cannot be
    // reacted to, so they are not indexed - a reaction to a state line is not
    // a thing Matrix has an event for.
    if (!row.msgId.isEmpty() && row.kind != matrix::RowKind::Skip && row.kind != matrix::RowKind::System && row.ts > 0.0) {
        m_tsToEventId[chatId].insert(row.ts, row.msgId);
        m_eventIdToTs[chatId].insert(row.msgId, row.ts);
    }
}

void MatrixRoomBridge::revealEvent(Room *room, const RoomEvent *event)
{
    if (room == nullptr || event == nullptr)
        return;
    // Only a decryption. libQuotient emits replacedEvent() for an m.replace as
    // well, and that path is already handled where the replacement arrives; the
    // original event is kept only when the swap was a decryption.
    if (event->originalEvent() == nullptr)
        return;

    const matrix::Row row = matrix::rowFor(flatten(room, event));
    if (row.kind == matrix::RowKind::Skip || row.kind == matrix::RowKind::Encrypted || row.text.isEmpty())
        return;

    // Text for an attachment too. The row went into the model as a placeholder
    // line and a line is what it can become again; turning it into a picture
    // would mean replacing the entry, and an attachment that arrives with its
    // key late is rare enough not to earn that. It is named rather than left
    // saying there is no key for it, which would be untrue once there is.
    Q_EMIT roomMessageRevealed(chatid::matrixChatId(room->id()), row.msgId, row.text);
}

void MatrixRoomBridge::reportPendingFailure(Room *room, int pendingIndex)
{
    if (room == nullptr || pendingIndex < 0)
        return;
    const auto &pending = room->pendingEvents();
    if (size_t(pendingIndex) >= pending.size())
        return;

    const auto &item = pending[size_t(pendingIndex)];
    // Every other status is progress and says itself in the timeline.
    if (item.deliveryStatus() != EventStatus::SendingFailed)
        return;

    const QString reason = item.annotation();
    Q_EMIT sendFailed(
        chatid::matrixChatId(room->id()),
        reason.isEmpty()
            ? i18nc("@info:status a Matrix message was not accepted and nothing said why", "The message could not be sent to this room.")
            : i18nc("@info:status a Matrix message was not accepted, %1 is what the homeserver reported", "The message could not be sent: %1", reason));
}

Room *MatrixRoomBridge::roomFor(const QString &chatId) const
{
    const QString roomId = chatid::matrixRoomId(chatId);
    if (roomId.isEmpty() || !m_manager || !m_manager->connection())
        return nullptr;
    return m_manager->connection()->room(roomId, JoinState::Join);
}

bool MatrixRoomBridge::sendText(const QString &chatId, const QString &text)
{
    if (text.trimmed().isEmpty())
        return false;

    // Split from the lookup below so the two are not reported as one thing: a
    // session that is not up and a room that is not joined need different
    // answers from the user, and "not available" was the same sentence for both.
    if (!m_manager || !m_manager->connection()) {
        Q_EMIT sendFailed(chatId, i18nc("@info:status", "Not signed in to Matrix, so this message was not sent."));
        return false;
    }

    Room *room = roomFor(chatId);
    if (!room) {
        Q_EMIT sendFailed(chatId, i18nc("@info:status", "That Matrix room is not available in this session."));
        return false;
    }
    if (!canSendEncrypted(room)) {
        Q_EMIT sendFailed(chatId, encryptionUnavailableReason());
        return false;
    }

    // Nothing here encrypts anything. libQuotient does the whole of it inside
    // the send: Room::Private::doSendEvent() makes or rotates the outbound
    // megolm session, ships the session key to every device that has not got
    // it, and puts an m.room.encrypted on the wire instead of the message.
    room->postText(text);
    return true;
}

bool MatrixRoomBridge::sendFile(const QString &chatId, const QString &localFilePath)
{
    Room *room = roomFor(chatId);
    if (!room) {
        Q_EMIT sendFailed(chatId, i18nc("@info:status", "That Matrix room is not available in this session."));
        return false;
    }
    if (!canSendEncrypted(room)) {
        Q_EMIT sendFailed(chatId, encryptionUnavailableReason());
        return false;
    }

    const QFileInfo file(localFilePath);
    if (!file.exists() || !file.isReadable()) {
        Q_EMIT sendFailed(chatId, i18nc("@info:status %1 is a file name", "%1 could not be read, so nothing was sent.", file.fileName()));
        return false;
    }

    const QUrl localUrl = QUrl::fromLocalFile(file.absoluteFilePath());
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(file);
    const QString mimeName = mime.name();

    // The msgtype comes from the content type rather than the extension,
    // because that is what every other client in the room reads it back as. No
    // poster frame is generated for a video: doing that properly means decoding
    // a frame and uploading it as a second piece of media, and a video with no
    // poster still plays.
    std::unique_ptr<EventContent::FileContentBase> content;
    if (mimeName.startsWith(QLatin1String("image/"))) {
        const QImage image(file.absoluteFilePath());
        content = std::make_unique<EventContent::ImageContent>(localUrl, file.size(), mime, image.size(), file.fileName());
    } else if (mimeName.startsWith(QLatin1String("audio/"))) {
        auto audio = std::make_unique<EventContent::AudioContent>(localUrl, file.size(), mime, file.fileName());
        // Set by hand: the duration member belongs to libQuotient's playable
        // content and only its JSON constructor fills it in, so one built from
        // a local file starts out holding whatever was on the stack, and that
        // number goes out in the event.
        audio->duration = 0;
        content = std::move(audio);
    } else if (mimeName.startsWith(QLatin1String("video/"))) {
        auto video = std::make_unique<EventContent::VideoContent>(localUrl, file.size(), mime, QSize(), file.fileName());
        video->duration = 0;
        content = std::move(video);
    } else {
        content = std::make_unique<EventContent::FileContent>(localUrl, file.size(), mime, file.fileName());
    }

    room->postFile(file.fileName(), std::move(content));
    return true;
}

void MatrixRoomBridge::markRead(const QString &chatId)
{
    if (Room *room = roomFor(chatId))
        room->markAllMessagesAsRead();
}

bool MatrixRoomBridge::leaveChat(const QString &chatId)
{
    Room *room = roomFor(chatId);
    if (room == nullptr)
        return false;
    const auto job = room->leaveRoom();
    // The window must not wait for the sync to echo the leave back: join-state
    // transitions are driven by sync (see setJoinState's note in room.h), so
    // with the sync loop down or merely slow the row would linger, and "I
    // left and the chat is still here" was exactly the report. When the sync
    // does echo, Connection::leftRoom lands too and the handler is idempotent.
    if (job != nullptr) {
        connect(job.data(), &Quotient::BaseJob::success, this, [this, chatId]() {
            Q_EMIT roomLeft(chatId);
        });
    }
    return true;
}

chatid::Transport MatrixRoomBridge::transport() const
{
    return chatid::Transport::Matrix;
}

bool MatrixRoomBridge::canHandle(const QString &chatId) const
{
    return chatid::isMatrix(chatId);
}

bool MatrixRoomBridge::serverOwnsTimeline(const QString &) const
{
    return true;
}

bool MatrixRoomBridge::hasRooms(const QString &) const
{
    return true;
}

bool MatrixRoomBridge::supportsCalls(const QString &) const
{
    // A room call rides the LAN voice channel, so the answer is whether the
    // local side of that channel exists - which is every real session; voice
    // is not an optional installation.
    return m_net != nullptr && m_voice != nullptr;
}

bool MatrixRoomBridge::supportsTyping(const QString &) const
{
    return true;
}

bool MatrixRoomBridge::supportsEdits(const QString &) const
{
    return true;
}

bool MatrixRoomBridge::supportsReactions(const QString &) const
{
    return true;
}

void MatrixRoomBridge::sendTyping(const QString &chatId)
{
    // The LAN side re-sends a typing datagram for every keystroke and has no
    // stop packet; the homeserver side works the same way - a typing event
    // carries a timeout, and the server clears it on its own when it runs out,
    // so there is no "stopped typing" here either. What is different is the
    // cost: one HTTP round trip per packet, so the bridge sends one only when
    // the previous has been on the wire long enough to be taking the pressure
    // of real typing. 4 seconds is about a sentence at a normal pace.
    if (!m_manager || !m_manager->connection())
        return;
    Room *room = roomFor(chatId);
    if (room == nullptr)
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 last = m_lastTypingSent.value(chatId, 0);
    if (now - last < 4000)
        return;
    m_lastTypingSent.insert(chatId, now);

    // Sent to the server as the user "who is typing", which is this session's
    // own id; the room is where it appears. 10 seconds is the conventional
    // timeout every Matrix client keeps retyping under - short enough to clear
    // a typist who walked away, long enough to survive a pause mid-sentence.
    m_manager->connection()->callApi<SetTypingJob>(m_manager->connection()->userId(), room->id(), true, 10000);
}

void MatrixRoomBridge::sendReaction(const QString &chatId, double ts, const QString &emoji, bool added)
{
    Room *room = roomFor(chatId);
    // The event id is resolved from the stamp the window filed the row under;
    // an event older than the loaded timeline has no stamp here and no
    // reaction can be sent to it - the window offers none either, because a
    // row it never showed cannot be reacted to.
    const QString eventId = m_tsToEventId.value(chatId).value(ts);
    if (room == nullptr || eventId.isEmpty() || emoji.isEmpty())
        return;

    if (added) {
        // The local reaction store decides added/removed from what it has on
        // disk, and across a restart or an account switch that knowledge can
        // be wrong: the tap then arrives as "add" for a reaction this account
        // already sent, and the homeserver answers the second copy with a 400.
        // The room's own annotations are the truth - an existing one from this
        // account means the tap meant "off", which is a redaction, not a post.
        const auto &annotations = room->relatedEvents(eventId, EventRelation::AnnotationType);
        for (const RoomEvent *annotation : annotations) {
            const auto *reaction = eventCast<const ReactionEvent>(annotation);
            if (reaction && reaction->key() == emoji && !room->connection()->userId().isEmpty() && reaction->senderId() == room->connection()->userId()) {
                room->redactEvent(reaction->id());
                return;
            }
        }
        room->postReaction(eventId, emoji);
        return;
    }

    // Removing a reaction is redacting the reaction event this session sent,
    // and there may be several (one per emoji, or a retried send). All of them
    // are found and redacted; the homeserver does not care that redacting a
    // reaction is redacting an event, and neither does anyone else in the room.
    const auto &annotations = room->relatedEvents(eventId, EventRelation::AnnotationType);
    for (const RoomEvent *annotation : annotations) {
        const auto *reaction = eventCast<const ReactionEvent>(annotation);
        if (reaction && reaction->key() == emoji && !room->connection()->userId().isEmpty() && reaction->senderId() == room->connection()->userId())
            room->redactEvent(reaction->id());
    }
}

bool MatrixRoomBridge::sendEdit(const QString &chatId, double ts, const QString &newText)
{
    if (newText.trimmed().isEmpty())
        return false;
    Room *room = roomFor(chatId);
    const QString eventId = m_tsToEventId.value(chatId).value(ts);
    if (room == nullptr || eventId.isEmpty())
        return false;
    if (!canSendEncrypted(room)) {
        Q_EMIT sendFailed(chatId, encryptionUnavailableReason());
        return false;
    }

    // Everything an edit needs is in the related-to: the replacement body and
    // an m.relates_to with the serial of the event being corrected. libQuotient
    // stamps new_content with the type itself, which is what every other client
    // reads back as the corrected text.
    room->postText(newText, std::nullopt, EventRelation::replace(eventId));
    return true;
}

bool MatrixRoomBridge::sendDelete(const QString &chatId, double ts)
{
    Room *room = roomFor(chatId);
    const QString eventId = m_tsToEventId.value(chatId).value(ts);
    if (room == nullptr || eventId.isEmpty())
        return false;

    room->redactEvent(eventId);
    return true;
}

void MatrixRoomBridge::createRoom(const QString &name, const QString &topic, const QString &alias, const QStringList &invitedUsers, bool isPrivate)
{
    Connection *connection = m_manager->connection();
    if (connection == nullptr)
        return;

    // A name without an alias is what the "make it up as we go" rooms want; the
    // alias is the address somebody pastes to join, and cannot be handed out to
    // a private room, so it stays local when the room is not public.
    connection->createRoom(isPrivate ? Connection::UnpublishRoom : Connection::PublishRoom, isPrivate ? QString() : alias, name, topic, invitedUsers)
        .then(
            [this](const auto &job) {
                // The room arrives through joinedRoom() like any other, and the
                // conversation list hears about it there. Nothing else to do.
                Q_UNUSED(job);
            },
            [this](const auto &job) {
                Q_EMIT roomOperationFailed(QString(), job->errorString());
            });
}

void MatrixRoomBridge::joinRoom(const QString &aliasOrId)
{
    Connection *connection = m_manager->connection();
    if (connection == nullptr || aliasOrId.trimmed().isEmpty())
        return;

    // Join, then track the room the job created. A room that has never been in
    // this session's cache would otherwise wait for the next sync before the
    // conversation list hears about it.
    connection->joinRoom(aliasOrId.trimmed())
        .then(
            [this](const auto &job) {
                Room *room = m_manager->connection()->room(job->roomId(), JoinState::Join);
                trackRoom(room);
            },
            [this, aliasOrId](const auto &job) {
                Q_EMIT roomOperationFailed(QString(), job->errorString());
            });
}

void MatrixRoomBridge::acceptInvite(const QString &chatId)
{
    Room *room = roomFor(chatId);
    if (room == nullptr)
        return;
    // Joining is a connection-level call in libQuotient; the room object's own
    // state catches up when the join lands, and trackRoom() picks it up there.
    m_manager->connection()->joinRoom(room->id());
}

void MatrixRoomBridge::declineInvite(const QString &chatId)
{
    Room *room = roomFor(chatId);
    if (room == nullptr)
        return;
    room->leaveRoom();
}

void MatrixRoomBridge::inviteMember(const QString &chatId, const QString &userId)
{
    Room *room = roomFor(chatId);
    if (room == nullptr || userId.trimmed().isEmpty())
        return;
    room->inviteToRoom(userId.trimmed());
}

void MatrixRoomBridge::kickMember(const QString &chatId, const QString &userId)
{
    Room *room = roomFor(chatId);
    if (room == nullptr || userId.trimmed().isEmpty())
        return;
    room->kickMember(userId.trimmed());
}

void MatrixRoomBridge::banMember(const QString &chatId, const QString &userId)
{
    Room *room = roomFor(chatId);
    if (room == nullptr || userId.trimmed().isEmpty())
        return;
    room->ban(userId.trimmed());
}

void MatrixRoomBridge::unbanMember(const QString &chatId, const QString &userId)
{
    Room *room = roomFor(chatId);
    if (room == nullptr || userId.trimmed().isEmpty())
        return;
    room->unban(userId.trimmed());
}

void MatrixRoomBridge::openDirectChat(const QString &userId)
{
    const QString target = userId.trimmed();
    auto *connection = m_manager != nullptr ? m_manager->connection() : nullptr;
    if (target.isEmpty() || connection == nullptr)
        return;

    // An existing direct room with exactly the two of us is reused rather than
    // proliferated: the Matrix spec allows several, and neither this window
    // nor the peer wants to pick between lookalikes.
    const auto rooms = connection->allRooms();
    for (Room *room : rooms) {
        if (room == nullptr || room->joinState() != JoinState::Join || !room->isDirectChat())
            continue;
        if (room->joinedCount() == 2 && isJoinedMember(room, target)) {
            Q_EMIT directChatOpened(chatid::matrixChatId(room->id()));
            return;
        }
    }

    m_pendingDirectTarget = target;
    connection->requestDirectChat(target);
}

void MatrixRoomBridge::setCallStack(NetworkManager *net, VoiceCallManager *voice, CryptoManager *crypto)
{
    m_net = net;
    m_voice = voice;
    m_crypto = crypto;
}

MatrixRoomBridge::CallOffer MatrixRoomBridge::ownCallOffer() const
{
    CallOffer offer;
    // The address the LAN path already announces itself under; connectVoice()
    // dials the well-known voice port on it, so this is all the far side
    // needs to reach the media channel. The key is generated fresh per call,
    // so no two room calls share a keystream even between the same two ends.
    if (m_net != nullptr)
        offer.address = m_net->hostIp();
    offer.key.resize(CryptoManager::kKeyLen);
    // fillRange() fills integers, the key is bytes: the cast is sound because
    // the size is a multiple of four (kKeyLen is 32).
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(offer.key.data()), offer.key.size() / int(sizeof(quint32)));
    return offer;
}

MatrixRoomBridge::CallOffer MatrixRoomBridge::callOfferFromSdp(const QString &sdp)
{
    CallOffer offer;
    const QStringList parts = sdp.split(QLatin1Char(' '));
    if (parts.size() < 2 || parts.at(0) != QLatin1String("koutnet"))
        return offer;
    offer.address = parts.at(1);
    if (parts.size() >= 3)
        offer.key = QByteArray::fromBase64(parts.at(2).toLatin1());
    return offer;
}

void MatrixRoomBridge::callRoom(const QString &chatId)
{
    Room *room = roomFor(chatId);
    const CallOffer offer = ownCallOffer();
    if (room == nullptr || offer.address.isEmpty() || m_calls.contains(chatId))
        return;

    RoomCall call;
    call.callId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    call.role = QStringLiteral("caller");
    m_calls.insert(chatId, call);

    // The old "lifetime" field of a legacy m.call.invite. The homeserver
    // expects the event to look like a call even though nothing about this
    // call goes through it. The offer carries this side's media address and
    // the key the media is sealed with; both travel the same path as the
    // call, so the key is exactly as private as the room is.
    const QString sdp = QStringLiteral("koutnet %1 %2").arg(offer.address, QString::fromLatin1(offer.key.toBase64()));
    room->postJson(QStringLiteral("m.call.invite"),
                   QJsonObject{{QStringLiteral("call_id"), call.callId},
                               {QStringLiteral("version"), 0},
                               {QStringLiteral("party_id"), room->connection() ? room->connection()->userId() : QString()},
                               {QStringLiteral("lifetime"), 120000},
                               {QStringLiteral("offer"), QJsonObject{{QStringLiteral("type"), QStringLiteral("offer")}, {QStringLiteral("sdp"), sdp}}}});
}

void MatrixRoomBridge::acceptCall(const QString &chatId, const QString &callId)
{
    Room *room = roomFor(chatId);
    if (room == nullptr || m_pending.chatId != chatId || m_pending.callId != callId)
        return;

    const CallOffer offer = ownCallOffer();
    if (offer.address.isEmpty() || m_pending.peerKey.size() != CryptoManager::kKeyLen)
        return;

    RoomCall call;
    call.callId = callId;
    call.role = QStringLiteral("answerer");
    call.peerAddresses.append(m_pending.peerAddress);
    call.peerKeys.insert(m_pending.peerAddress, m_pending.peerKey);
    call.established = true;
    m_calls.insert(chatId, call);

    const QString sdp = QStringLiteral("koutnet %1 %2").arg(offer.address, QString::fromLatin1(offer.key.toBase64()));
    room->postJson(QStringLiteral("m.call.answer"),
                   QJsonObject{{QStringLiteral("call_id"), callId},
                               {QStringLiteral("party_id"), room->connection() ? room->connection()->userId() : QString()},
                               {QStringLiteral("answer"), QJsonObject{{QStringLiteral("type"), QStringLiteral("answer")}, {QStringLiteral("sdp"), sdp}}}});

    // The media channel comes up here and on the caller's side the moment its
    // answer lands, in exactly the direction a LAN call goes: both sides dial
    // each other's well-known voice port. The shared key is in place first,
    // so neither side has a moment of voice in the clear.
    if (m_crypto != nullptr)
        m_crypto->installSharedSession(m_pending.peerAddress, m_pending.peerKey);
    if (m_voice != nullptr)
        m_voice->call(m_pending.peerAddress);

    m_pending = PendingCall();
}

void MatrixRoomBridge::declineCall(const QString &chatId, const QString &callId)
{
    Room *room = roomFor(chatId);
    if (room == nullptr || m_pending.chatId != chatId || m_pending.callId != callId)
        return;

    room->postJson(QStringLiteral("m.call.hangup"),
                   QJsonObject{{QStringLiteral("call_id"), callId},
                               {QStringLiteral("party_id"), room->connection() ? room->connection()->userId() : QString()},
                               {QStringLiteral("reason"), QStringLiteral("decline")}});
    m_pending = PendingCall();
}

void MatrixRoomBridge::hangupRoomCall(const QString &chatId)
{
    Room *room = roomFor(chatId);
    const auto it = m_calls.find(chatId);
    if (it == m_calls.end())
        return;

    if (room != nullptr) {
        room->postJson(QStringLiteral("m.call.hangup"),
                       QJsonObject{{QStringLiteral("call_id"), it->callId},
                                   {QStringLiteral("party_id"), room->connection() ? room->connection()->userId() : QString()},
                                   {QStringLiteral("reason"), QStringLiteral("hangup")}});
    }

    const auto addresses = it->peerAddresses;
    for (const QString &address : addresses) {
        if (m_crypto != nullptr)
            m_crypto->dropSharedSession(address);
        if (m_voice != nullptr)
            m_voice->hangup(address);
    }
    m_calls.erase(it);
    Q_EMIT roomCallEnded(chatId);
}

void MatrixRoomBridge::handleCallEvent(Room *room, const RoomEvent *event)
{
    if (room == nullptr || event == nullptr)
        return;

    const QString chatId = chatid::matrixChatId(room->id());
    const QString ownId = room->connection() ? room->connection()->userId() : QString();
    if (event->senderId() == ownId)
        return;

    if (const auto *invite = eventCast<const CallInviteEvent>(event)) {
        const CallOffer offer = callOfferFromSdp(invite->sdp());
        // Non-const find: the answerer branch below appends to the peer list.
        const auto it = m_calls.find(chatId);
        if (it != m_calls.end() && it->callId == invite->callId()) {
            // The same caller re-inviting an established call: a group call in
            // which more than one member is answering the one invitation. On
            // the answerer side each such invite adds the member as a peer.
            if (it->role == QStringLiteral("answerer") && !offer.address.isEmpty()) {
                if (!it->peerAddresses.contains(offer.address)) {
                    if (m_crypto != nullptr && offer.key.size() == CryptoManager::kKeyLen)
                        m_crypto->installSharedSession(offer.address, offer.key);
                    it->peerAddresses.append(offer.address);
                    if (m_voice != nullptr)
                        m_voice->call(offer.address);
                }
            }
            return;
        }
        // A call this session has never seen: offer it to the window. Only one
        // pending invitation is carried, matching the single dialog.
        if (m_pending.chatId.isEmpty()) {
            m_pending = PendingCall{chatId, invite->callId(), offer.address, offer.key};
            Q_EMIT roomCallInvited(chatId, invite->callId(), room->member(event->senderId()).displayName());
        }
        return;
    }

    if (const auto *answer = eventCast<const CallAnswerEvent>(event)) {
        const auto it = m_calls.find(chatId);
        if (it == m_calls.end() || it->callId != answer->callId())
            return;
        const CallOffer offer = callOfferFromSdp(answer->sdp());
        if (!offer.address.isEmpty() && !it->peerAddresses.contains(offer.address)) {
            if (m_crypto != nullptr && offer.key.size() == CryptoManager::kKeyLen)
                m_crypto->installSharedSession(offer.address, offer.key);
            it->peerAddresses.append(offer.address);
            if (m_voice != nullptr)
                m_voice->call(offer.address);
        }
        it->established = true;
        // Every answer opens the caller's call window; the first one is the
        // call, further ones are group participants riding the same window.
        Q_EMIT roomCallAccepted(chatId);
        return;
    }

    if (const auto *hangup = eventCast<const CallHangupEvent>(event)) {
        (void)hangup;
        const auto it = m_calls.find(chatId);
        if (it != m_calls.end()) {
            const auto addresses = it->peerAddresses;
            for (const QString &address : addresses) {
                if (m_crypto != nullptr)
                    m_crypto->dropSharedSession(address);
                if (m_voice != nullptr)
                    m_voice->hangup(address);
            }
            m_calls.erase(it);
            Q_EMIT roomCallEnded(chatId);
        }
        if (m_pending.chatId == chatId)
            m_pending = PendingCall();
    }
}

QVariantMap MatrixRoomBridge::roomInfo(const QString &chatId) const
{
    QVariantMap info;
    const Room *room = roomFor(chatId);
    if (room == nullptr)
        return info;

    info.insert(QStringLiteral("roomId"), room->id());
    info.insert(QStringLiteral("displayName"), matrix::conversationTitle(room->displayName(), room->id()));
    info.insert(QStringLiteral("topic"), room->topic());
    info.insert(QStringLiteral("canonicalAlias"), room->canonicalAlias());
    info.insert(QStringLiteral("altAliases"), room->altAliases());
    info.insert(QStringLiteral("joinedCount"), room->joinedCount());
    info.insert(QStringLiteral("invitedCount"), room->invitedCount());
    info.insert(QStringLiteral("encrypted"), room->usesEncryption());
    // Whether this session can do encryption at all, which is not the same
    // question as whether the room asked for it. An encrypted room in a session
    // without a key store is unreadable and unwritable, and the column has to be
    // able to say that instead of drawing a padlock over it.
    const Connection *connection = room->connection();
    const bool trustKnown = canAskAboutTrust(connection);
    info.insert(QStringLiteral("encryptionActive"), connection != nullptr && connection->encryptionEnabled());
    info.insert(QStringLiteral("trustKnown"), trustKnown);
    // Only about this account's own other sessions, and only one query. Whether
    // every device of every member is verified is a question this column does
    // not ask, because it is one database round trip per member and the answer
    // for a room of any size is always no.
    info.insert(QStringLiteral("ownSessionsVerified"), trustKnown && connection->allSessionsSelfVerified(connection->userId()));
    // The room key backup is an account-level matter, but this is the one place
    // the account's encryption state is discussed, so it is reported here: does
    // the homeserver hold an encrypted copy of the room keys, and has this
    // session already unlocked it.
    info.insert(QStringLiteral("keyBackupAvailable"), m_manager->keyBackupAvailable());
    info.insert(QStringLiteral("keyBackupUnlocked"), m_manager->isKeyBackupUnlocked());
    info.insert(QStringLiteral("version"), room->version());
    info.insert(QStringLiteral("isDirect"), room->isDirectChat());
    // What this session is allowed to do here. The column hides the actions it
    // would only be refused for rather than offering them and reporting the
    // refusal afterwards.
    info.insert(QStringLiteral("ownPowerLevel"), room->memberEffectivePowerLevel());

    const QUrl avatar = room->avatarUrl();
    info.insert(QStringLiteral("avatarUrl"),
                (avatar.scheme() == QLatin1String("mxc") && room->connection() != nullptr) ? room->connection()->makeMediaUrl(avatar).toString() : QString());

    return info;
}

QVariantList MatrixRoomBridge::roomMembers(const QString &chatId) const
{
    QVariantList list;
    const Room *room = roomFor(chatId);
    if (room == nullptr)
        return list;

    // joinedMembers() rather than members(): a list that also holds everybody
    // who ever left is a list of a different thing.
    QList<RoomMember> members = room->joinedMembers();
    std::sort(members.begin(), members.end(), [](const RoomMember &left, const RoomMember &right) {
        if (left.powerLevel() != right.powerLevel())
            return left.powerLevel() > right.powerLevel();
        return left.displayName().localeAwareCompare(right.displayName()) < 0;
    });

    list.reserve(members.size());
    for (const RoomMember &member : std::as_const(members)) {
        QVariantMap entry;
        entry.insert(QStringLiteral("userId"), member.id());
        entry.insert(QStringLiteral("displayName"), member.displayName().isEmpty() ? member.id() : member.displayName());
        entry.insert(QStringLiteral("powerLevel"), member.powerLevel());
        const QUrl avatar = member.avatarUrl();
        entry.insert(QStringLiteral("avatarUrl"),
                     (avatar.scheme() == QLatin1String("mxc") && room->connection() != nullptr) ? room->connection()->makeMediaUrl(avatar).toString()
                                                                                                : QString());
        list.append(entry);
    }
    return list;
}

QVariantMap MatrixRoomBridge::memberInfo(const QString &chatId, const QString &userId) const
{
    QVariantMap info;
    const Room *room = roomFor(chatId);
    if (room == nullptr || userId.isEmpty())
        return info;

    const RoomMember member = room->member(userId);
    if (member.isEmpty())
        return info;

    info.insert(QStringLiteral("userId"), member.id());
    info.insert(QStringLiteral("displayName"), member.displayName().isEmpty() ? member.id() : member.displayName());
    info.insert(QStringLiteral("powerLevel"), member.powerLevel());
    info.insert(QStringLiteral("isLocalMember"), member.isLocalMember());
    // A ban is its own membership state on the server, and it outlives the
    // member list: a banned member is still listed, just flagged. The card
    // answers with the unban button instead of kick and ban on this flag.
    info.insert(QStringLiteral("isBanned"), room->memberState(member.id()) == Membership::Ban);

    // Trust, one member at a time, which is the only place it is cheap enough
    // to ask. trustKnown false means the question could not be put - no key
    // store, or a member of a room nobody has ever shared an encrypted room
    // with - and the card says that rather than reporting an unverified device
    // this session simply never looked at.
    const Connection *connection = room->connection();
    const bool trustKnown = canAskAboutTrust(connection);
    info.insert(QStringLiteral("trustKnown"), trustKnown);
    info.insert(QStringLiteral("userVerified"), trustKnown && connection->isUserVerified(member.id()));

    int deviceCount = 0;
    int verifiedDeviceCount = 0;
    if (trustKnown) {
        const QStringList devices = connection->devicesForUser(member.id());
        deviceCount = int(devices.size());
        for (const QString &device : devices) {
            if (connection->isVerifiedDevice(member.id(), device))
                ++verifiedDeviceCount;
        }
    }
    info.insert(QStringLiteral("deviceCount"), deviceCount);
    info.insert(QStringLiteral("verifiedDeviceCount"), verifiedDeviceCount);
    const QUrl avatar = member.avatarUrl();
    info.insert(QStringLiteral("avatarUrl"),
                (avatar.scheme() == QLatin1String("mxc") && room->connection() != nullptr) ? room->connection()->makeMediaUrl(avatar).toString() : QString());
    return info;
}

} // namespace koutnet
