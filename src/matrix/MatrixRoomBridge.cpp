// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "MatrixRoomBridge.h"

#include "MatrixManager.h"
#include "MatrixTranslate.h"
#include "core/chat/ChatAddress.h"
#include "pollevent.h"

#include <KLocalizedString>

#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QPointer>
#include <QSize>
#include <QTextDocument>
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
#include <Quotient/user.h>

#include "core/security/CryptoManager.h"
#include "network/NetworkManager.h"
#include "network/VoiceCallManager.h"

using namespace Quotient;

namespace
{
using koutnet::matrix::MediaKind;
using koutnet::matrix::StateChange;

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

// a name, not the enum number; the other end is QML, where a number is unreadable.
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
    case MediaKind::Location:
        return QStringLiteral("location");
    case MediaKind::None:
        break;
    }
    return QString();
}

// Without a key store libQuotient drops the event, and sending in the clear
// is never right.
bool canSendEncrypted(const Room *room)
{
    if (room == nullptr || !room->usesEncryption())
        return true;
    return room->connection() != nullptr && room->connection()->encryptionEnabled();
}

// whether the libQuotient trust tables can be asked at all.
//
// a security invariant: isVerifiedDevice()/isUserVerified()/allSessionsSelfVerified()
// and devicesForUser() all touch Connection::database(), which is null when the
// key store is absent, and none of them check. asking early is a crash, and
// answering "not verified" when we could not ask is a lie, so we carry
// trustKnown instead of a bare boolean.
bool canAskAboutTrust(const Connection *connection)
{
    return connection != nullptr && connection->encryptionEnabled() && connection->database() != nullptr;
}

QString encryptionUnavailableReason()
{
    return i18nc("@info:status a message for an encrypted room was not sent because the session has no keys",
                 "This room is end-to-end encrypted and this session could not open its encryption keys, so nothing was sent.");
}

// never blank
QString memberLabel(const Room *room, const QString &userId)
{
    if (room == nullptr || userId.isEmpty())
        return userId;
    const QString name = room->member(userId).displayName();
    return name.isEmpty() ? userId : name;
}

// the media url of a member avatar, or empty when it is our own picture.
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

// whether a member is currently joined.
bool isJoinedMember(const Room *room, const QString &userId)
{
    if (room == nullptr || userId.isEmpty())
        return false;
    return room->memberState(userId) == Membership::Join;
}

// the picture a conversation row carries. empty when none.
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

// Classify member events using the same distinctions as NeoChat.
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

        // already joined, so a profile change not an arrival. names before
        // pictures: a picture change alone is the duller half, no line of its own.
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
        // a join event that changed nothing - a sync artefact, dropped.
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

StateChange classifyState(const Room *room, const RoomEvent *event, QString &subject)
{
    if (!event->isStateEvent())
        return StateChange::None;

    // a state event that just repeats the room state is a sync artefact.
    // checked first, or every branch prints a duplicate line after reconnect.
    if (const auto *state = eventCast<const StateEvent>(event); state && state->repeatsState())
        return StateChange::None;

    if (const auto *member = eventCast<const RoomMemberEvent>(event))
        return classifyMember(room, *member, subject);

    if (const auto *name = eventCast<const RoomNameEvent>(event)) {
        subject = name->name();
        return subject.isEmpty() ? StateChange::RoomNameCleared : StateChange::RoomNameSet;
    }
    if (const auto *topic = eventCast<const RoomTopicEvent>(event)) {
        // flattened: the topic gets one timeline line, not one per paragraph.
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
    // read the raw JSON, not the typed accessors. get<T>() is a template
    // libQuotient instantiates for a fixed content-type list, and a build whose
    // library list disagrees with its headers links nothing. the JSON fields
    // are the spec and read the same everywhere.
    const QJsonObject content = message.contentJson();
    const QJsonObject info = content.value(QStringLiteral("info")).toObject();

    raw.mediaName = content.value(QStringLiteral("body")).toString();
    raw.mediaMime = info.value(QStringLiteral("mimetype")).toString();
    raw.mediaSize = info.value(QStringLiteral("size")).toInteger();

    // the dimensions matter: without them a picture is laid out at whatever the
    // decoder reports after the download, shifting the timeline under the reader.
    raw.mediaWidth = info.value(QStringLiteral("w")).toInt();
    raw.mediaHeight = info.value(QStringLiteral("h")).toInt();
    raw.mediaDurationMs = info.value(QStringLiteral("duration")).toInt();

    // an encrypted attachment keeps the url one level down, under file.
    const QJsonObject file = content.value(QStringLiteral("file")).toObject();
    const QUrl source(file.isEmpty() ? content.value(QStringLiteral("url")).toString() : file.value(QStringLiteral("url")).toString());
    // makeMediaUrl() asserts on the scheme, and an encrypted attachment or a
    // malformed event can carry a non-mxc uri.
    if (source.scheme() == QLatin1String("mxc") && room != nullptr && room->connection() != nullptr)
        raw.mediaUrl = room->makeMediaUrl(message.id(), source).toString();
}

// A poll arrives under several event types and content shapes; detect by
// content so every client's poll renders instead of falling through to text.
static QVariantMap pollFromContent(const QJsonObject &content)
{
    QString pollKey;
    if (content.contains(QStringLiteral("org.matrix.msc3381.poll.start")))
        pollKey = QStringLiteral("org.matrix.msc3381.poll.start");
    else if (content.contains(QStringLiteral("poll")))
        pollKey = QStringLiteral("poll");
    else
        return {};

    const QJsonObject poll = content.value(pollKey).toObject();
    const QString textKey = QStringLiteral("org.matrix.msc1767.text");

    QString question = poll.value(QStringLiteral("question")).toObject().value(textKey).toString();
    if (question.isEmpty())
        question = poll.value(QStringLiteral("question")).toObject().value(QStringLiteral("body")).toString();
    if (question.isEmpty())
        question = content.value(textKey).toString();
    if (question.isEmpty())
        question = content.value(QStringLiteral("body")).toString();

    QVariantList answers;
    const QJsonArray answersJson = poll.value(QStringLiteral("answers")).toArray();
    for (const QJsonValue &answer : answersJson) {
        const QJsonObject answerObj = answer.toObject();
        QString body = answerObj.value(textKey).toString();
        if (body.isEmpty())
            body = answerObj.value(QStringLiteral("body")).toString();
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), answerObj.value(QStringLiteral("id")).toString());
        entry.insert(QStringLiteral("body"), body);
        answers.append(entry);
    }

    const QString kind = poll.value(QStringLiteral("kind")).toString();
    const bool disclosed = kind == QStringLiteral("disclosed") || kind == QStringLiteral("org.matrix.msc3381.poll.disclosed");

    QVariantMap out;
    out.insert(QStringLiteral("question"), question);
    out.insert(QStringLiteral("answers"), answers);
    out.insert(QStringLiteral("disclosed"), disclosed);
    return out;
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
    // a state event classified as None has nothing to show; cleared so it is
    // not drawn as an empty message.
    if (event->isStateEvent()) {
        raw.eventId.clear();
        return raw;
    }

    // Registered poll classes let libQuotient parse the unstable poll events
    // without guessing at their wire format.
    if (const auto *poll = eventCast<const Quotient::PollStartEvent>(event)) {
        QVariantList answers;
        const auto pollAnswers = poll->answers();
        for (const auto &a : pollAnswers) {
            QVariantMap e;
            e.insert(QStringLiteral("id"), a.id);
            e.insert(QStringLiteral("body"), a.text);
            answers.append(e);
        }
        QVariantMap pollMap;
        pollMap.insert(QStringLiteral("question"), poll->question());
        pollMap.insert(QStringLiteral("answers"), answers);
        pollMap.insert(QStringLiteral("disclosed"), poll->kind() == PollKind::Disclosed);
        raw.poll = pollMap;
        raw.body = poll->question();
        return raw;
    }
    if (const auto *vote = eventCast<const Quotient::PollResponseEvent>(event)) {
        const std::optional<Quotient::EventRelation> rel = vote->relatesTo();
        raw.pollStartId = rel ? rel->eventId : QString();
        const QStringList selections = vote->selections();
        raw.pollAnswerId = selections.isEmpty() ? QString() : selections.first();
        return raw;
    }

    if (const auto *message = eventCast<const RoomMessageEvent>(event)) {
        // Read the msgtype from the raw content: the typed MsgType enum does not
        // have every unstable kind (m.sticker, m.location), and a library whose
        // message list disagrees with its headers would link nothing.
        const QJsonObject content = message->contentJson();
        const QString msgtype = content.value(QStringLiteral("msgtype")).toString();
        raw.textLike = msgtype == QStringLiteral("m.text") || msgtype == QStringLiteral("m.notice") || msgtype == QStringLiteral("m.emote");
        // A poll may arrive under several msgtypes/event types; detect it by its
        // content so the voting UI renders instead of a bare question line.
        const QVariantMap pollData = pollFromContent(content);
        if (!pollData.isEmpty()) {
            raw.poll = pollData;
            raw.body = pollData.value(QStringLiteral("question")).toString();
            return raw;
        }
        if (msgtype == QStringLiteral("m.location")) {
            // A geo: point is opened as a map, not downloaded; keep the uri as
            // the body so the row shows where it points, and as the media url so
            // the attachment block can hand it straight to a map application.
            raw.media = MediaKind::Location;
            raw.body = content.value(QStringLiteral("geo_uri")).toString();
            if (raw.body.isEmpty())
                raw.body = message->plainBody();
            raw.mediaUrl = raw.body;
            raw.mediaName = content.value(QStringLiteral("body")).toString();
        } else if (msgtype == QStringLiteral("m.sticker")) {
            // A sticker is an image with a different msgtype; render it as one.
            raw.media = MediaKind::Image;
            if (!raw.redacted)
                fillMedia(room, *message, raw);
        } else if (msgtype == QStringLiteral("org.matrix.msc3381.poll.response")) {
            const QJsonObject relates = content.value(QStringLiteral("m.relates_to")).toObject();
            raw.pollStartId = relates.value(QStringLiteral("event_id")).toString();
            const QJsonArray answers = content.value(QStringLiteral("org.matrix.msc3381.poll.response")).toObject().value(QStringLiteral("answers")).toArray();
            if (!answers.isEmpty())
                raw.pollAnswerId = answers.at(0).isObject() ? answers.at(0).toObject().value(QStringLiteral("id")).toString() : answers.at(0).toString();
        } else {
            raw.body = message->plainBody();
            raw.media = mediaKindOf(message->msgtype());
            if (raw.media != MediaKind::None && !raw.redacted)
                fillMedia(room, *message, raw);
        }
        // MSC2446 spoiler: the hidden text rides in a "spoiler" array or as a
        // <span data-mx-spoiler> in the formatted body; the window hides it.
        if (msgtype == QStringLiteral("m.text")
            && (content.value(QStringLiteral("spoiler")).isArray()
                || content.value(QStringLiteral("format")).toString().contains(QLatin1String("data-mx-spoiler")))) {
            raw.spoiler = true;
            // Wrap in || so the shared TextHandler hides it the same way a LAN
            // writer's ||secret|| does; revealed, the inner text is what shows.
            raw.body = QStringLiteral("||%1||").arg(raw.body);
        }
    } else if (is<EncryptedEvent>(*event)) {
        raw.encrypted = true;
    } else {
        // a reaction, a redaction event, or a call signal: not a row.
        raw.eventId.clear();
    }
    return raw;
}

// The "* text" fallback for clients that do not understand edits.
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
            // detached by hand, not left to the rooms being destroyed: a
            // connection on its way out lives until the server answers or gives
            // up, and its rooms would keep filing messages into a session the
            // user already left.
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
        // nothing to track yet (see trackRoom). The join moves it through
        // joinedRoom() and the invite object is deleted, which is how the list
        // knows it is gone.
        QString inviterId;
        QString inviterName;
        inviterOf(room, &inviterId, &inviterName);
        Q_EMIT roomInvited(chatid::matrixChatId(room->id()), matrix::conversationTitle(room->displayName(), room->id()), inviterId, inviterName);
    });
    connect(connection, &Connection::loadedRoomState, this, [this](Room *room) {
        // the room name and members are only settled here; joinedRoom() fires
        // while the object is still nameless, which is why rows used to appear
        // titled with their room id and stay that way.
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
        // declining an invite: the room we were asked into matches a new object
        // (this one), and the invite object itself is in prev.
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
        // the other way an invite dies: the join landed, a stale invite object
        // was created and is being retired. no leftRoom() precedes this.
        if (room->joinState() == JoinState::Invite)
            Q_EMIT roomInviteGone(chatId);
    });

    // a verification that just succeeded moved the trust table, and the column
    // and card draw straight from it. without this the padlock only catches up
    // when something else makes the column re-read, which for a quiet room never
    // happens.
    auto retellTrust = [this]() {
        const auto ids = m_tracked.keys();
        for (const QString &roomId : ids)
            Q_EMIT roomInfoChanged(chatid::matrixChatId(roomId));
    };
    connect(connection, &Connection::sessionVerified, this, retellTrust);
    connect(connection, &Connection::userVerified, this, retellTrust);

    // whatever loadState() already put in place; later rooms come via the signals above.
    const auto rooms = connection->allRooms();
    for (Room *room : rooms)
        trackRoom(room);
}

void MatrixRoomBridge::trackRoom(Room *room)
{
    // an invitation is a room we have no timeline in yet, so it cannot be opened
    // as a chat. the Connection hands every join-state transition to a different
    // Room object - an invite is its own Room, deleted the moment the join lands
    // - so invites are announced by attach() and never tracked here.
    if (!room || room->joinState() != JoinState::Join)
        return;

    if (m_tracked.value(room->id()) == room) {
        // Already wired; the name may still have moved.
        publishRoom(room);
        return;
    }
    m_tracked.insert(room->id(), room);

    // a direct room asked for by openDirectChat() comes through the join after
    // the homeserver made it; emit the moment it is the one requested.
    if (!m_pendingDirectTarget.isEmpty() && room->isDirectChat() && isJoinedMember(room, m_pendingDirectTarget) && room->joinedCount() == 2) {
        m_pendingDirectTarget.clear();
        Q_EMIT directChatOpened(chatid::matrixChatId(room->id()));
    }

    connect(room, &Room::addedMessages, this, [this, room](int fromIndex, int toIndex) {
        publishRange(room, fromIndex, toIndex);
    });
    // Standalone poll events bypass addedMessages(); publish them from this signal.
    connect(room, &Room::aboutToAddNewMessages, this, [this, room](const Quotient::RoomEventsRange &events) {
        for (const auto &ev : events) {
            const RoomEvent *event = ev.get();
            // Registered event types identify polls without inspecting raw JSON.
            if (eventCast<const Quotient::PollStartEvent>(event) || eventCast<const Quotient::PollResponseEvent>(event)) {
                publishEvent(room, event);
            }
        }
    });
    // this is how an outgoing message reaches the timeline, and the only way.
    // addedMessages() fires for a sync span that is not an echo of our own send;
    // mergePendingEvent() folds our message into its pending item and emits
    // this instead. a message typed here was posted, accepted by the server and
    // seen by every other client, but never shown on this screen.
    connect(room, &Room::pendingEventAboutToMerge, this, [this, room](RoomEvent *serverEvent, int) {
        publishEvent(room, serverEvent);
    });
    // the other half of the silence: a send libQuotient gave up on.
    connect(room, &Room::pendingEventChanged, this, [this, room](int index) {
        reportPendingFailure(room, index);
    });
    connect(room, &Room::displaynameChanged, this, [this, room]() {
        publishRoom(room);
    });
    // a megolm key that turned up after the message it opens. libQuotient swaps
    // the decrypted event into the timeline in place; without this the
    // placeholder row stays one for the rest of the run despite the text.
    connect(room, &Room::replacedEvent, this, [this, room](const RoomEvent *newEvent, const RoomEvent *) {
        revealEvent(room, newEvent);
    });

    // someone in the room is typing. the window shows one indicator per chat
    // and does not name who, so a boolean; and it is otherMembersTyping(), not
    // membersTyping(), since our own typing must not light the indicator we set.
    connect(room, &Room::typingChanged, this, [this, room]() {
        Q_EMIT roomTyping(chatid::matrixChatId(room->id()), !room->otherMembersTyping().isEmpty());
    });

    // a read receipt moved. reported only for someone else receipt, and only
    // when it is about our own message: a third member read says nothing about
    // whether our messages were read, and claiming it would be the same lie as
    // the LAN path for a message that was never on screen.
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

    // the room column reads all of these through roomInfo(), so they share one signal.
    const auto announceInfo = [this, room]() {
        Q_EMIT roomInfoChanged(chatid::matrixChatId(room->id()));
    };
    connect(room, &Room::topicChanged, this, announceInfo);
    // a room that just turned encryption on changes what the column says. the
    // timeline line for the same event comes from the m.room.encryption state
    // event like any other.
    connect(room, &Room::encryption, this, announceInfo);
    connect(room, &Room::avatarChanged, this, announceInfo);
    connect(room, &Room::memberListChanged, this, announceInfo);
    connect(room, &Room::namesChanged, this, announceInfo);
    connect(room, &Room::pinnedEventsChanged, this, [this, room]() {
        Q_EMIT roomPinnedChanged(chatid::matrixChatId(room->id()), pinnedRows(room));
    });

    // m.call.invite/answer/hangup. libQuotient hands the whole event over; the
    // caller is in the sender, the rest in the content.
    connect(room, &Room::callEvent, this, [this](Room *callRoom, const RoomEvent *event) {
        handleCallEvent(callRoom, event);
    });

    publishRoom(room);

    // whatever is already in the timeline from the state cache. indices run from
    // the oldest known event to the newest, both inclusive.
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
        // out-of-range lookups hand back historyEdge(), the only sentinel the
        // reverse iterator has (= crend()), and dereferencing it is UB. the
        // range comes from the room min/max, so this is usually in range;
        // the check is for the index that races an ongoing sync.
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

    // a reaction is never a row: it is about a message already on screen and
    // earns no line of its own. the ReactionStore keys on the target message
    // stamp, like the LAN protocol, so the reaction timestamp itself is not sent.
    if (const auto *reaction = eventCast<const ReactionEvent>(event)) {
        const QString reactionId = event->id();
        const double targetTs = m_eventIdToTs.value(chatId).value(reaction->eventId());
        // a redacted reaction loses its content, so the emoji is gone from the
        // event; it is remembered here from when the reaction was first sent.
        if (event->isRedacted()) {
            auto &reactions = m_reactions[chatId];
            const auto it = reactions.constFind(reactionId);
            if (it != reactions.constEnd()) {
                Q_EMIT roomReaction(chatId, it->targetTs, it->emoji, it->sender, false);
                reactions.erase(it);
            }
            return;
        }
        if (targetTs > 0.0 && !reaction->key().isEmpty()) {
            // our own reaction also comes back through sync, but the local
            // toggle already filed it under "me"; the echo would badge the
            // same reaction twice under two names - and the label is the
            // display name, not "me".
            const QString ownId = room->connection() ? room->connection()->userId() : QString();
            if (reaction->senderId() != ownId)
                Q_EMIT roomReaction(chatId, targetTs, reaction->key(), memberLabel(room, reaction->senderId()), true);
            m_reactions[chatId].insert(reactionId, {targetTs, reaction->key(), memberLabel(room, reaction->senderId())});
        }
        return;
    }

    // a redaction takes a message or a reaction back. the redacted event stays
    // in the timeline, so what the window shows is told apart by who redacted:
    // a message row is removed, a reaction badge taken down.
    if (const auto *redaction = eventCast<const RedactionEvent>(event)) {
        const QString redactedId = redaction->redactedEvent();
        auto &reactions = m_reactions[chatId];
        const auto it = reactions.constFind(redactedId);
        if (it != reactions.constEnd()) {
            Q_EMIT roomReaction(chatId, it->targetTs, it->emoji, it->sender, false);
            reactions.erase(it);
            return;
        }
        if (m_eventIdToTs.value(chatId).contains(redactedId))
            Q_EMIT roomMessageRemoved(chatId, redactedId);
        return;
    }

    // an edit is dealt with first and never becomes a row: it is about a message
    // already on screen.
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
        // per event, not per room. the room is readable; this message is not,
        // and saying so about the room would be a lie about the rest.
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
    case matrix::RowKind::Poll: {
        // The question and options ride as structured data, not as a string the
        // window would have to re-parse; votes go back through sendPollVote().
        const QString avatar = memberAvatarUrl(room, row.senderId);
        Q_EMIT roomPoll(chatId,
                        row.msgId,
                        row.poll.value(QStringLiteral("question")).toString(),
                        row.poll.value(QStringLiteral("answers")).toList(),
                        row.poll.value(QStringLiteral("disclosed")).toBool(),
                        row.sender,
                        row.isOwn,
                        row.ts,
                        avatar);
        break;
    }
    case matrix::RowKind::PollVote: {
        // Not a row: an amendment to the poll named by row.msgId. The window keeps
        // the tally, and the event id here is the poll it votes on, not a message.
        Q_EMIT roomPollVote(chatId, row.msgId, row.pollAnswerId, row.senderId, row.isOwn);
        break;
    }
    }

    // the window addresses messages by stamp, the homeserver by event id; both
    // directions are kept so a reaction/edit/unsend resolves to the event it is
    // about. system rows carry a synthetic id that cannot be reacted to, so
    // they are not indexed - a reaction to a state line has no Matrix event. A
    // poll vote names the poll it answers, not itself, so it is not indexed either.
    if (!row.msgId.isEmpty() && row.kind != matrix::RowKind::Skip && row.kind != matrix::RowKind::System && row.kind != matrix::RowKind::PollVote
        && row.ts > 0.0) {
        m_tsToEventId[chatId].insert(row.ts, row.msgId);
        m_eventIdToTs[chatId].insert(row.msgId, row.ts);
    }
}

void MatrixRoomBridge::revealEvent(Room *room, const RoomEvent *event)
{
    if (room == nullptr || event == nullptr)
        return;
    // only a decryption. libQuotient also emits replacedEvent() for an m.replace,
    // handled where the replacement arrives; the original is kept only when the
    // swap was a decryption.
    if (event->originalEvent() == nullptr)
        return;

    const matrix::Row row = matrix::rowFor(flatten(room, event));
    if (row.kind == matrix::RowKind::Skip || row.kind == matrix::RowKind::Encrypted || row.text.isEmpty())
        return;

    // text for an attachment too. the row went in as a placeholder line and a
    // line is what it can become again; turning it into a picture would replace
    // the entry, and a late-keyed attachment is rare enough not to earn that.
    // it is named rather than left saying there is no key, which would be untrue.
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
    // every other status is progress and says itself in the timeline.
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

Room *MatrixRoomBridge::invitedRoomFor(const QString &chatId) const
{
    const QString roomId = chatid::matrixRoomId(chatId);
    if (roomId.isEmpty() || !m_manager || !m_manager->connection())
        return nullptr;
    // invites are JoinState::Invite rooms, roomFor() only sees Join, so look here
    return m_manager->connection()->room(roomId, JoinState::Invite);
}

void MatrixRoomBridge::inviterOf(Room *room, QString *inviterId, QString *inviterName) const
{
    if (inviterId != nullptr)
        inviterId->clear();
    if (inviterName != nullptr)
        inviterName->clear();
    if (room == nullptr || room->connection() == nullptr)
        return;

    // the inviter is whoever sent our own m.room.member invite event
    const QString ownId = room->connection()->userId();
    const auto *memberEvent = room->currentState().get<RoomMemberEvent>(ownId);
    if (memberEvent == nullptr)
        return;

    const QString sender = memberEvent->senderId();
    if (sender.isEmpty())
        return;
    if (inviterId != nullptr)
        *inviterId = sender;
    if (inviterName != nullptr) {
        const auto member = room->member(sender);
        const QString name = member.displayName();
        *inviterName = name.isEmpty() ? sender : name;
    }
}

bool MatrixRoomBridge::sendText(const QString &chatId, const QString &text)
{
    if (text.trimmed().isEmpty())
        return false;

    // split from the lookup so the two errors are not reported as one: a session
    // that is down and a room that is not joined need different answers, and
    // "not available" used to be the same sentence for both.
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

    // nothing here encrypts. libQuotient does it all inside the send:
    // doSendEvent() makes or rotates the outbound megolm session, ships the key
    // to every device without it, and puts an m.room.encrypted on the wire.
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

    // the msgtype comes from the content type, not the extension, since that is
    // what every other client reads it back as. no poster frame for a video:
    // making one means decoding a frame and uploading it as a second media, and
    // a video with no poster still plays.
    std::unique_ptr<EventContent::FileContentBase> content;
    if (mimeName.startsWith(QLatin1String("image/"))) {
        const QImage image(file.absoluteFilePath());
        content = std::make_unique<EventContent::ImageContent>(localUrl, file.size(), mime, image.size(), file.fileName());
    } else if (mimeName.startsWith(QLatin1String("audio/"))) {
        auto audio = std::make_unique<EventContent::AudioContent>(localUrl, file.size(), mime, file.fileName());
        // set by hand: the duration member belongs to the libQuotient playable
        // content and only its JSON constructor fills it, so a local-file one
        // starts holding whatever was on the stack and that goes out in the event.
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
    // the window must not wait for sync to echo the leave: join-state changes
    // are driven by sync, so with the loop down or slow the row would linger -
    // "I left and the chat is still here" was the report. when sync does echo,
    // Connection::leftRoom lands too and the handler is idempotent.
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
    // a room call rides the LAN voice channel, so the answer is whether the
    // local side exists - which is every real session, voice is not optional.
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
    // No stop packet: a typing event carries a timeout the server clears.
    // Cost: one HTTP round trip per packet, so the bridge sends one only when
    // the previous has been on the wire long enough. 4s ≈ a sentence.
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

    // 10s timeout: short enough to clear a walk-away, long enough to survive a pause.
    m_manager->connection()->callApi<SetTypingJob>(m_manager->connection()->userId(), room->id(), true, 10000);
}

void MatrixRoomBridge::sendReaction(const QString &chatId, const QVariant &identifier, const QString &emoji, bool added)
{
    Room *room = roomFor(chatId);
    // identifier is either a string (event id) or a double (stamp).  The string
    // path is preferred: it is exact and cannot collide.
    QString eventId;
    if (identifier.canConvert<QString>())
        eventId = identifier.toString();
    else
        eventId = m_tsToEventId.value(chatId).value(identifier.toDouble());
    if (room == nullptr || eventId.isEmpty() || emoji.isEmpty())
        return;

    if (added) {
        // the local reaction store decides added/removed from disk, and across
        // a restart or account switch that can be wrong: the tap then arrives as
        // "add" for a reaction this account already sent, and the server answers
        // the second copy with a 400. the room annotations are the truth -
        // an existing one from this account means the tap meant "off", a redaction.
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

    // Redact all matching reaction events from this session.
    const auto &annotations = room->relatedEvents(eventId, EventRelation::AnnotationType);
    for (const RoomEvent *annotation : annotations) {
        const auto *reaction = eventCast<const ReactionEvent>(annotation);
        if (reaction && reaction->key() == emoji && !room->connection()->userId().isEmpty() && reaction->senderId() == room->connection()->userId())
            room->redactEvent(reaction->id());
    }
}

bool MatrixRoomBridge::sendEdit(const QString &chatId, const QVariant &identifier, const QString &newText)
{
    if (newText.trimmed().isEmpty())
        return false;
    Room *room = roomFor(chatId);
    QString eventId;
    if (identifier.canConvert<QString>())
        eventId = identifier.toString();
    else
        eventId = m_tsToEventId.value(chatId).value(identifier.toDouble());
    if (room == nullptr || eventId.isEmpty())
        return false;
    if (!canSendEncrypted(room)) {
        Q_EMIT sendFailed(chatId, encryptionUnavailableReason());
        return false;
    }

    // everything an edit needs is in the related-to: the replacement body and
    // an m.relates_to with the corrected event serial. libQuotient stamps
    // new_content itself, which is what every other client reads back.
    room->postText(newText, std::nullopt, EventRelation::replace(eventId));
    return true;
}

bool MatrixRoomBridge::sendDelete(const QString &chatId, const QVariant &identifier)
{
    Room *room = roomFor(chatId);
    QString eventId;
    if (identifier.canConvert<QString>())
        eventId = identifier.toString();
    else
        eventId = m_tsToEventId.value(chatId).value(identifier.toDouble());
    if (room == nullptr || eventId.isEmpty())
        return false;

    room->redactEvent(eventId);
    return true;
}

// Build a timeline row map for a single event, the shape roomMessage() uses, so
// pin and search results can show what they point at without another round trip.
static QVariantMap rowForEvent(const RoomEvent *ev)
{
    QVariantMap m;
    m.insert(QStringLiteral("eventId"), ev->id());
    m.insert(QStringLiteral("ts"), ev->originTimestamp().toMSecsSinceEpoch() / 1000.0);
    m.insert(QStringLiteral("sender"), ev->senderId());
    if (const auto *me = eventCast<const RoomMessageEvent>(ev))
        m.insert(QStringLiteral("text"), me->plainBody());
    return m;
}

bool MatrixRoomBridge::sendReply(const QString &chatId, const QVariant &identifier, const QString &plainText)
{
    const double ts = identifier.toDouble();
    if (plainText.trimmed().isEmpty())
        return false;
    Room *room = roomFor(chatId);
    const QString eventId = m_tsToEventId.value(chatId).value(ts);
    if (room == nullptr || eventId.isEmpty())
        return false;
    if (!canSendEncrypted(room)) {
        Q_EMIT sendFailed(chatId, encryptionUnavailableReason());
        return false;
    }
    room->postText(plainText, std::nullopt, EventRelation::replyTo(eventId));
    return true;
}

// QTextDocument wraps a whole page, so the <body> inner is pulled out as the
// inline fragment the homeserver expects.
static QString markdownToHtml(const QString &markdown)
{
    QTextDocument doc;
    doc.setMarkdown(markdown);
    QString html = doc.toHtml();
    const int bodyOpen = html.indexOf(QLatin1String("<body"));
    const int bodyClose = html.lastIndexOf(QLatin1String("</body>"));
    if (bodyOpen >= 0 && bodyClose > bodyOpen) {
        const int tagEnd = html.indexOf(QLatin1String(">"), bodyOpen);
        html = html.mid(tagEnd + 1, bodyClose - tagEnd - 1);
    }
    return html.trimmed();
}

// Only build an HTML body when there is something to format, so plain messages
// stay plain and the fallback body is the only thing on the wire.
static bool looksFormatted(const QString &text)
{
    return text.contains(QLatin1String("**")) || text.contains(QLatin1String("__")) || text.contains(QLatin1String("`")) || text.contains(QLatin1String("~~"))
        || text.contains(QLatin1String("# ")) || text.contains(QLatin1String("\n"));
}

bool MatrixRoomBridge::sendRichText(const QString &chatId, const QString &plainText, const QString &html)
{
    if (plainText.trimmed().isEmpty())
        return false;
    Room *room = roomFor(chatId);
    if (room == nullptr) {
        Q_EMIT sendFailed(chatId, i18nc("@info:status", "That Matrix room is not available in this session."));
        return false;
    }
    if (!canSendEncrypted(room)) {
        Q_EMIT sendFailed(chatId, encryptionUnavailableReason());
        return false;
    }
    // The composer passes an empty html when it only has markdown; derive the
    // formatted body here so the window never has to know the markup dialect.
    QString htmlBody = html;
    if (htmlBody.isEmpty() && looksFormatted(plainText))
        htmlBody = markdownToHtml(plainText);
    const std::optional<QString> htmlOpt = htmlBody.isEmpty() ? std::nullopt : std::optional<QString>(htmlBody);
    room->postText(plainText, htmlOpt);
    return true;
}

void MatrixRoomBridge::setRoomName(const QString &chatId, const QString &name)
{
    if (Room *room = roomFor(chatId))
        room->setName(name);
}

void MatrixRoomBridge::setRoomTopic(const QString &chatId, const QString &topic)
{
    if (Room *room = roomFor(chatId))
        room->setTopic(topic);
}

void MatrixRoomBridge::setRoomAvatar(const QString &chatId, const QString &localFilePath)
{
    Room *room = roomFor(chatId);
    if (room == nullptr || room->connection() == nullptr) {
        Q_EMIT roomOperationFailed(chatId, i18nc("@info:status", "That Matrix room is not available in this session."));
        return;
    }
    // Upload the picture, then point the room's avatar state at the mxc id the
    // homeserver returns - the same two-step the file transfer uses, so an avatar
    // and an attachment fail and recover the same way.
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(localFilePath);
    auto job = room->connection()->uploadFile(localFilePath, mime.name());
    connect(job.operator->(), &Quotient::BaseJob::success, this, [this, chatId, job]() {
        // Re-resolve: the room may have been left while the upload was in flight.
        Room *room = roomFor(chatId);
        if (!room)
            return;
        room->setState(Quotient::RoomAvatarEvent(job->contentUri()));
        Q_EMIT roomOperationSucceeded(chatId);
    });
    connect(job.operator->(), &Quotient::BaseJob::failure, this, [this, chatId, job]() {
        Q_EMIT roomOperationFailed(chatId, job->errorString());
    });
}

void MatrixRoomBridge::pinMessage(const QString &chatId, const QVariant &identifier)
{
    const double ts = identifier.toDouble();
    Room *room = roomFor(chatId);
    if (room == nullptr)
        return;
    const QString eventId = m_tsToEventId.value(chatId).value(ts);
    if (eventId.isEmpty())
        return;
    QStringList pinned = room->pinnedEventIds();
    if (!pinned.contains(eventId)) {
        pinned.append(eventId);
        room->setPinnedEvents(pinned);
    }
}

void MatrixRoomBridge::unpinMessage(const QString &chatId, const QVariant &identifier)
{
    const double ts = identifier.toDouble();
    Room *room = roomFor(chatId);
    if (room == nullptr)
        return;
    const QString eventId = m_tsToEventId.value(chatId).value(ts);
    if (eventId.isEmpty())
        return;
    QStringList pinned = room->pinnedEventIds();
    pinned.removeAll(eventId);
    room->setPinnedEvents(pinned);
}

void MatrixRoomBridge::ignoreUser(const QString &userId)
{
    if (m_manager && m_manager->connection())
        if (User *u = m_manager->connection()->user(userId))
            u->ignore();
}

void MatrixRoomBridge::unignoreUser(const QString &userId)
{
    if (m_manager && m_manager->connection())
        if (User *u = m_manager->connection()->user(userId))
            u->unmarkIgnore();
}

void MatrixRoomBridge::searchMessages(const QString &chatId, const QString &query)
{
    QVariantList results;
    if (Room *room = roomFor(chatId)) {
        const QString q = query.toLower();
        for (const auto &item : room->messageEvents()) {
            const RoomEvent *ev = item.event();
            const auto *me = eventCast<const RoomMessageEvent>(ev);
            if (!me)
                continue;
            if (me->plainBody().toLower().contains(q)) {
                results.append(rowForEvent(ev));
                if (results.size() >= 50)
                    break;
            }
        }
    }
    Q_EMIT roomSearchResults(chatId, results);
}

void MatrixRoomBridge::sendLocation(const QString &chatId, double latitude, double longitude, const QString &label)
{
    Room *room = roomFor(chatId);
    if (room == nullptr) {
        Q_EMIT sendFailed(chatId, i18nc("@info:status", "That Matrix room is not available in this session."));
        return;
    }
    if (!canSendEncrypted(room)) {
        Q_EMIT sendFailed(chatId, encryptionUnavailableReason());
        return;
    }
    // geo: takes lat,lon; the label is what shows before the map opens.
    const QString geo = QStringLiteral("geo:%1,%2").arg(latitude, 0, 'f', 6).arg(longitude, 0, 'f', 6);
    QJsonObject content;
    content.insert(QStringLiteral("msgtype"), QStringLiteral("m.location"));
    content.insert(QStringLiteral("geo_uri"), geo);
    content.insert(QStringLiteral("body"), label.isEmpty() ? geo : label);
    room->postJson(QStringLiteral("m.room.message"), content);
}

void MatrixRoomBridge::sendSticker(const QString &chatId, const QString &mxcUrl, const QString &description)
{
    Room *room = roomFor(chatId);
    if (room == nullptr) {
        Q_EMIT sendFailed(chatId, i18nc("@info:status", "That Matrix room is not available in this session."));
        return;
    }
    if (!canSendEncrypted(room)) {
        Q_EMIT sendFailed(chatId, encryptionUnavailableReason());
        return;
    }
    QJsonObject content;
    content.insert(QStringLiteral("msgtype"), QStringLiteral("m.sticker"));
    content.insert(QStringLiteral("url"), mxcUrl);
    content.insert(QStringLiteral("body"), description.isEmpty() ? i18nc("@item a sticker with no caption", "sticker") : description);
    room->postJson(QStringLiteral("m.room.message"), content);
}

void MatrixRoomBridge::sendSpoiler(const QString &chatId, const QString &text)
{
    if (text.trimmed().isEmpty())
        return;
    Room *room = roomFor(chatId);
    if (room == nullptr) {
        Q_EMIT sendFailed(chatId, i18nc("@info:status", "That Matrix room is not available in this session."));
        return;
    }
    if (!canSendEncrypted(room)) {
        Q_EMIT sendFailed(chatId, encryptionUnavailableReason());
        return;
    }
    // MSC2446: the hidden text rides in a <span data-mx-spoiler> in the html
    // body, while the plain body stays readable for clients that ignore it.
    const QString escaped = text.toHtmlEscaped();
    const QString html = QStringLiteral("<span data-mx-spoiler>%1</span>").arg(escaped);
    QJsonObject content;
    content.insert(QStringLiteral("msgtype"), QStringLiteral("m.text"));
    content.insert(QStringLiteral("format"), QStringLiteral("org.matrix.custom.html"));
    content.insert(QStringLiteral("formatted_body"), html);
    content.insert(QStringLiteral("body"), text);
    room->postJson(QStringLiteral("m.room.message"), content);
}

// The composer has only a path, so the mxc id comes from the homeserver first.
void MatrixRoomBridge::sendStickerFile(const QString &chatId, const QString &localFilePath)
{
    Room *room = roomFor(chatId);
    if (room == nullptr || room->connection() == nullptr) {
        Q_EMIT sendFailed(chatId, i18nc("@info:status", "That Matrix room is not available in this session."));
        return;
    }
    if (!canSendEncrypted(room)) {
        Q_EMIT sendFailed(chatId, encryptionUnavailableReason());
        return;
    }
    // The composer hands over a file:// URL straight from a dialog or the
    // recorder; QFileInfo will not stat that, so turn it into a local path first.
    const QString path = localFilePath.startsWith(QStringLiteral("file://")) ? QUrl(localFilePath).toLocalFile() : localFilePath;
    const QFileInfo file(path);
    if (!file.exists() || !file.isReadable()) {
        Q_EMIT sendFailed(chatId, i18nc("@info:status %1 is a file name", "%1 could not be read, so nothing was sent.", file.fileName()));
        return;
    }
    const QString mimeName = QMimeDatabase().mimeTypeForFile(file).name();
    auto job = room->connection()->uploadFile(file.absoluteFilePath(), mimeName);
    connect(job.operator->(), &Quotient::BaseJob::success, this, [this, chatId, file, mimeName, job]() {
        Room *room = roomFor(chatId);
        if (!room)
            return;
        QJsonObject content;
        content.insert(QStringLiteral("msgtype"), QStringLiteral("m.sticker"));
        content.insert(QStringLiteral("url"), job->contentUri().toString());
        content.insert(QStringLiteral("body"), file.fileName());
        QJsonObject info;
        info.insert(QStringLiteral("mimetype"), mimeName);
        info.insert(QStringLiteral("size"), file.size());
        content.insert(QStringLiteral("info"), info);
        room->postJson(QStringLiteral("m.room.message"), content);
    });
    connect(job.operator->(), &Quotient::BaseJob::failure, this, [this, chatId, job]() {
        Q_EMIT sendFailed(chatId, job->errorString());
    });
}

// MSC3245 voice: an m.audio with the voice flag so clients render a player.
void MatrixRoomBridge::sendVoice(const QString &chatId, const QString &localFilePath, int durationMs)
{
    Room *room = roomFor(chatId);
    if (room == nullptr || room->connection() == nullptr) {
        Q_EMIT sendFailed(chatId, i18nc("@info:status", "That Matrix room is not available in this session."));
        return;
    }
    if (!canSendEncrypted(room)) {
        Q_EMIT sendFailed(chatId, encryptionUnavailableReason());
        return;
    }
    const QString path = localFilePath.startsWith(QStringLiteral("file://")) ? QUrl(localFilePath).toLocalFile() : localFilePath;
    const QFileInfo file(path);
    if (!file.exists() || !file.isReadable()) {
        Q_EMIT sendFailed(chatId, i18nc("@info:status %1 is a file name", "%1 could not be read, so nothing was sent.", file.fileName()));
        return;
    }
    const QString ext = file.suffix().toLower();
    QString mimeName = QStringLiteral("audio/ogg");
    if (ext == QStringLiteral("m4a") || ext == QStringLiteral("mp4"))
        mimeName = QStringLiteral("audio/mp4");
    else if (ext == QStringLiteral("aac"))
        mimeName = QStringLiteral("audio/aac");
    else if (ext == QStringLiteral("ogg"))
        mimeName = QStringLiteral("audio/ogg");
    else if (ext == QStringLiteral("wav"))
        mimeName = QStringLiteral("audio/wav");
    else if (ext == QStringLiteral("flac"))
        mimeName = QStringLiteral("audio/flac");
    else if (ext == QStringLiteral("mp3"))
        mimeName = QStringLiteral("audio/mpeg");
    auto job = room->connection()->uploadFile(file.absoluteFilePath(), mimeName);
    connect(job.operator->(), &Quotient::BaseJob::success, this, [this, chatId, file, mimeName, durationMs, job]() {
        Room *room = roomFor(chatId);
        if (!room)
            return;
        const QString url = job->contentUri().toString();
        QJsonObject content;
        content.insert(QStringLiteral("msgtype"), QStringLiteral("m.audio"));
        content.insert(QStringLiteral("body"), QStringLiteral("Voice message"));
        content.insert(QStringLiteral("url"), url);
        QJsonObject fileInfo;
        fileInfo.insert(QStringLiteral("mimetype"), mimeName);
        fileInfo.insert(QStringLiteral("name"), QStringLiteral("Voice Message"));
        fileInfo.insert(QStringLiteral("size"), file.size());
        fileInfo.insert(QStringLiteral("url"), url);
        content.insert(QStringLiteral("org.matrix.msc1767.file"), fileInfo);
        QJsonObject info;
        info.insert(QStringLiteral("mimetype"), mimeName);
        info.insert(QStringLiteral("size"), file.size());
        info.insert(QStringLiteral("duration"), durationMs);
        content.insert(QStringLiteral("info"), info);
        QJsonObject audio;
        audio.insert(QStringLiteral("duration"), durationMs);
        audio.insert(QStringLiteral("waveform"), QJsonArray());
        content.insert(QStringLiteral("org.matrix.msc1767.audio"), audio);
        content.insert(QStringLiteral("org.matrix.msc3245.voice"), QJsonObject());
        room->postJson(QStringLiteral("m.room.message"), content);
    });
    connect(job.operator->(), &Quotient::BaseJob::failure, this, [this, chatId, job]() {
        Q_EMIT sendFailed(chatId, job->errorString());
    });
}

// Send a poll in the MSC3381 form used by NeoChat and current servers.
void MatrixRoomBridge::sendPoll(const QString &chatId, const QString &question, const QStringList &answers, bool disclosed)
{
    Room *room = roomFor(chatId);
    if (room == nullptr) {
        Q_EMIT sendFailed(chatId, i18nc("@info:status", "That Matrix room is not available in this session."));
        return;
    }
    if (!canSendEncrypted(room)) {
        Q_EMIT sendFailed(chatId, encryptionUnavailableReason());
        return;
    }
    QJsonArray answersJson;
    int index = 0;
    for (const QString &answer : answers) {
        if (answer.trimmed().isEmpty())
            continue;
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), QString::number(index));
        entry.insert(QStringLiteral("org.matrix.msc1767.text"), answer);
        answersJson.append(entry);
        ++index;
    }
    // Keep the unstable shape for compatibility with existing readers.
    QJsonObject inner;
    inner.insert(QStringLiteral("kind"),
                 disclosed ? QStringLiteral("org.matrix.msc3381.poll.disclosed") : QStringLiteral("org.matrix.msc3381.poll.undisclosed"));
    inner.insert(QStringLiteral("max_selections"), 1);
    inner.insert(QStringLiteral("question"), QJsonObject{{QStringLiteral("org.matrix.msc1767.text"), question}});
    inner.insert(QStringLiteral("answers"), answersJson);
    QString fallback = question;
    for (int i = 0; i < answers.count(); ++i)
        fallback += QStringLiteral("\n%1. %2").arg(i + 1).arg(answers.at(i));
    QJsonObject content;
    content.insert(QStringLiteral("org.matrix.msc1767.text"), fallback);
    content.insert(QStringLiteral("org.matrix.msc3381.poll.start"), inner);
    room->postJson(QStringLiteral("org.matrix.msc3381.poll.start"), content);
}

void MatrixRoomBridge::sendPollVote(const QString &chatId, const QString &msgId, const QString &answerId)
{
    Room *room = roomFor(chatId);
    if (room == nullptr) {
        Q_EMIT sendFailed(chatId, i18nc("@info:status", "That Matrix room is not available in this session."));
        return;
    }
    if (!canSendEncrypted(room)) {
        Q_EMIT sendFailed(chatId, encryptionUnavailableReason());
        return;
    }
    QJsonArray answers;
    answers.append(answerId);
    QJsonObject response;
    response.insert(QStringLiteral("answers"), answers);
    QJsonObject content;
    content.insert(QStringLiteral("body"), i18nc("@info a poll vote with no readable text", "Voted."));
    content.insert(QStringLiteral("org.matrix.msc3381.poll.response"), response);
    content.insert(QStringLiteral("m.relates_to"),
                   QJsonObject{{QStringLiteral("rel_type"), QStringLiteral("m.reference")}, {QStringLiteral("event_id"), msgId}});
    room->postJson(QStringLiteral("org.matrix.msc3381.poll.response"), content);
}

QVariantList MatrixRoomBridge::pinnedRows(Room *room) const
{
    QVariantList out;
    const QStringList ids = room->pinnedEventIds();
    for (const QString &id : ids) {
        const auto it = room->findInTimeline(id);
        if (it == room->historyEdge())
            continue;
        out.append(rowForEvent(it->event()));
    }
    return out;
}

void MatrixRoomBridge::createRoom(const QString &name, const QString &topic, const QString &alias, const QStringList &invitedUsers, bool isPrivate)
{
    Connection *connection = m_manager->connection();
    if (connection == nullptr)
        return;

    // a name without an alias is what the "make it up as we go" rooms want; the
    // alias is the address pasted to join, and cannot be handed out to a private
    // room, so it stays local when the room is not public.
    connection->createRoom(isPrivate ? Connection::UnpublishRoom : Connection::PublishRoom, isPrivate ? QString() : alias, name, topic, invitedUsers)
        .then(
            [](const auto &job) {
                // the room arrives through joinedRoom() like any other; the list hears about it there.
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

    // join, then track the room the job created. a room never in this session
    // cache would otherwise wait for the next sync before the list hears it.
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
    // invite is a JoinState::Invite room, find it through invitedRoomFor()
    Room *room = invitedRoomFor(chatId);
    if (room == nullptr)
        return;
    // joining is a connection-level call in libQuotient; the room state
    // catches up when the join lands, and trackRoom() picks it up there.
    m_manager->connection()->joinRoom(room->id());
}

void MatrixRoomBridge::declineInvite(const QString &chatId)
{
    Room *room = invitedRoomFor(chatId);
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

    // an existing direct room with just the two of us is reused, not multiplied:
    // the spec allows several, and neither this window nor the peer wants to
    // pick between lookalikes.
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
    // the address the LAN path already announces; connectVoice() dials the
    // well-known voice port on it, so this is all the far side needs. the key
    // is fresh per call, so no two room calls share a keystream even on the
    // same pair of ends.
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

    // the legacy "lifetime" field of m.call.invite. the server expects the event
    // to look like a call even though nothing about it goes through the server.
    // the offer carries this side media address and the key the media is
    // sealed with; both travel with the call, so the key is as private as the room.
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
    call.peerKeys.insert(m_pending.peerAddress, offer.key);
    call.established = true;
    m_calls.insert(chatId, call);

    const QString sdp = QStringLiteral("koutnet %1 %2").arg(offer.address, QString::fromLatin1(offer.key.toBase64()));
    room->postJson(QStringLiteral("m.call.answer"),
                   QJsonObject{{QStringLiteral("call_id"), callId},
                               {QStringLiteral("party_id"), room->connection() ? room->connection()->userId() : QString()},
                               {QStringLiteral("answer"), QJsonObject{{QStringLiteral("type"), QStringLiteral("answer")}, {QStringLiteral("sdp"), sdp}}}});

    // the media channel comes up here and on the caller side when its answer
    // lands, exactly like a LAN call: both sides dial each other voice port.
    // the shared key is in place first, so neither side has a moment in the clear.
    if (m_crypto != nullptr)
        m_crypto->installSharedSession(m_pending.peerAddress, offer.key);
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
        // non-const find: the answerer branch below appends to the peer list.
        const auto it = m_calls.find(chatId);
        if (it != m_calls.end() && it->callId == invite->callId()) {
            // the same caller re-inviting an established call: a group call where
            // more than one member answers the one invite. each such invite adds
            // the member as a peer on the answerer side.
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
        // a call this session has never seen: offer it to the window. only one
        // pending invite is carried, matching the single dialog.
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
        // every answer opens the caller call window; the first is the call,
        // the rest are group participants on the same window.
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
    // whether this session can do encryption at all, which is not the same as
    // whether the room asked for it. an encrypted room without a key store is
    // unreadable and unwritable, and the column must say so, not draw a padlock.
    const Connection *connection = room->connection();
    const bool trustKnown = canAskAboutTrust(connection);
    info.insert(QStringLiteral("encryptionActive"), connection != nullptr && connection->encryptionEnabled());
    info.insert(QStringLiteral("trustKnown"), trustKnown);
    // only about this account own other sessions, and only one query. whether
    // every member every device is verified is a question the column does not
    // ask - one database round trip per member, and the answer is always no.
    info.insert(QStringLiteral("ownSessionsVerified"), trustKnown && connection->allSessionsSelfVerified(connection->userId()));
    // the room key backup is account-level, but this is where encryption state
    // is discussed, so it is reported here: does the server hold an encrypted
    // copy of the room keys, and has this session unlocked it.
    info.insert(QStringLiteral("keyBackupAvailable"), m_manager->keyBackupAvailable());
    info.insert(QStringLiteral("keyBackupUnlocked"), m_manager->isKeyBackupUnlocked());
    info.insert(QStringLiteral("version"), room->version());
    info.insert(QStringLiteral("isDirect"), room->isDirectChat());
    // what this session is allowed to do here. the column hides actions it would
    // only be refused for, rather than offering them and reporting the refusal.
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

    // joinedMembers() rather than members(): a list that also holds everyone
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
    // a ban is its own membership state on the server and outlives the member
    // list: a banned member is still listed, just flagged. the card answers with
    // the unban button instead of kick and ban on this flag.
    info.insert(QStringLiteral("isBanned"), room->memberState(member.id()) == Membership::Ban);

    // trust, one member at a time, which is the only place it is cheap enough
    // to ask. trustKnown false means the question could not be put - no key
    // store, or a member of a room nobody has shared an encrypted room with -
    // and the card says that rather than reporting an unverified device this
    // session never looked at.
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
