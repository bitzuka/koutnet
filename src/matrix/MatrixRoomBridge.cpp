// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "MatrixRoomBridge.h"

#include "MatrixManager.h"
#include "MatrixTranslate.h"
#include "core/chat/ChatAddress.h"

#include <KLocalizedString>

#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QSize>
#include <QUrl>

#include <algorithm>
#include <memory>
#include <utility>

#include <Quotient/connection.h>
#include <Quotient/eventitem.h>
#include <Quotient/events/encryptedevent.h>
#include <Quotient/events/encryptionevent.h>
#include <Quotient/events/eventcontent.h>
#include <Quotient/events/eventrelation.h>
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
    const auto content = message.get<EventContent::FileContentBase>();
    if (!content)
        return;

    const EventContent::FileInfo info = content->commonInfo();
    raw.mediaName = info.originalName;
    raw.mediaMime = info.mimeType.name();
    raw.mediaSize = info.payloadSize;

    // makeMediaUrl() asserts on the scheme, and an encrypted attachment or a
    // malformed event can carry something that is not an mxc URI.
    const QUrl source = content->url();
    if (source.scheme() == QLatin1String("mxc") && room != nullptr && room->connection() != nullptr)
        raw.mediaUrl = room->makeMediaUrl(message.id(), source).toString();

    // The dimensions matter: without them a picture is laid out at whatever the
    // decoder reports once the download finishes, which moves the timeline
    // under whoever is reading it.
    if (const auto image = message.get<EventContent::ImageContent>()) {
        raw.mediaWidth = image->imageSize.width();
        raw.mediaHeight = image->imageSize.height();
    } else if (const auto video = message.get<EventContent::VideoContent>()) {
        raw.mediaWidth = video->imageSize.width();
        raw.mediaHeight = video->imageSize.height();
        raw.mediaDurationMs = video->duration;
    } else if (const auto audio = message.get<EventContent::AudioContent>()) {
        raw.mediaDurationMs = audio->duration;
    }
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
    : QObject(parent)
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
    connect(connection, &Connection::loadedRoomState, this, [this](Room *room) {
        // The room's name and members are only settled here; joinedRoom() fires
        // while the object is still nameless, which is how rows used to appear
        // in the list titled with their room id and stay that way.
        trackRoom(room);
    });
    connect(connection, &Connection::leftRoom, this, [this](Room *room) {
        m_tracked.remove(room->id());
        Q_EMIT roomLeft(chatid::matrixChatId(room->id()));
    });
    connect(connection, &Connection::aboutToDeleteRoom, this, [this](Room *room) {
        if (m_tracked.value(room->id()) == room)
            m_tracked.remove(room->id());
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
    if (!room || room->joinState() != JoinState::Join)
        return;

    if (m_tracked.value(room->id()) == room) {
        // Already wired; the name may still have moved.
        publishRoom(room);
        return;
    }
    m_tracked.insert(room->id(), room);

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

    publishRoom(room);

    // Whatever is already in the timeline from the state cache. Indices run from
    // the oldest known event to the newest, both ends inclusive.
    if (!room->messageEvents().empty())
        publishRange(room, room->minTimelineIndex(), room->maxTimelineIndex());
}

void MatrixRoomBridge::publishRoom(Room *room)
{
    const QString chatId = chatid::matrixChatId(room->id());
    Q_EMIT roomListed(chatId, matrix::conversationTitle(room->displayName(), room->id()));
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
    case matrix::RowKind::Text:
        Q_EMIT roomMessage(chatId, row.msgId, row.text, row.sender, row.isOwn, row.ts, false);
        break;
    case matrix::RowKind::System:
        Q_EMIT roomMessage(chatId, row.msgId, row.text, row.sender, row.isOwn, row.ts, true);
        break;
    case matrix::RowKind::Encrypted:
        // Per event, not per room. The room is readable; this one message is
        // not, and saying it about the room would be a lie about the rest.
        Q_EMIT roomMessage(chatId, row.msgId, row.text, row.sender, row.isOwn, row.ts, true);
        break;
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
        Q_EMIT roomAttachment(chatId, row.msgId, media, row.sender, row.isOwn, row.ts);
        break;
    }
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
        Q_EMIT sendFailed(chatId, i18nc("@info:status", "Not signed in to the K-Server, so this message was not sent."));
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

void MatrixRoomBridge::leaveRoom(const QString &chatId)
{
    if (Room *room = roomFor(chatId))
        room->leaveRoom();
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
