// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "MatrixRoomBridge.h"

#include "MatrixManager.h"
#include "MatrixTranslate.h"
#include "core/chat/ChatAddress.h"

#include <KLocalizedString>

#include <QDateTime>

#include <utility>

#include <Quotient/connection.h>
#include <Quotient/eventitem.h>
#include <Quotient/events/encryptedevent.h>
#include <Quotient/events/roommessageevent.h>
#include <Quotient/room.h>
#include <Quotient/roommember.h>

using namespace Quotient;

namespace
{
// Only the three text msgtypes render as a message in this pass. An m.image or
// an m.file is reported as an attachment rather than dropped, because a message
// that is simply missing from the timeline is the bug this whole class is meant
// not to have.
bool isTextLike(RoomMessageEvent::MsgType type)
{
    return type == RoomMessageEvent::MsgType::Text || type == RoomMessageEvent::MsgType::Notice
        || type == RoomMessageEvent::MsgType::Emote;
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

    if (const auto *message = eventCast<const RoomMessageEvent>(event)) {
        raw.body = message->plainBody();
        raw.textLike = isTextLike(message->msgtype());
    } else if (is<EncryptedEvent>(*event)) {
        raw.encrypted = true;
    }
    return raw;
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
            m_encryptionAnnounced.clear();
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
    connect(room, &Room::encryption, this, [this, room]() {
        announceEncryption(room);
    });

    publishRoom(room);

    // Whatever is already in the timeline from the state cache. Indices run from
    // the oldest known event to the newest, both ends inclusive.
    if (!room->messageEvents().empty())
        publishRange(room, room->minTimelineIndex(), room->maxTimelineIndex());
}

void MatrixRoomBridge::publishRoom(Room *room)
{
    Q_EMIT roomListed(chatid::matrixChatId(room->id()), matrix::conversationTitle(room->displayName(), room->id()));
    if (room->usesEncryption())
        announceEncryption(room);
}

void MatrixRoomBridge::announceEncryption(Room *room)
{
    // Once per room per run. ChatModel drops the repeat across runs by itself,
    // because the notice carries a msgId derived from the room id.
    if (m_encryptionAnnounced.contains(room->id()))
        return;
    m_encryptionAnnounced.insert(room->id());

    Q_EMIT roomMessage(chatid::matrixChatId(room->id()),
                       matrix::encryptionNoticeId(room->id()),
                       i18nc("@info in-timeline notice about a Matrix room",
                             "This room is end-to-end encrypted. KOutNet cannot read or send messages here yet."),
                       QString(),
                       false,
                       matrix::secondsFromMs(QDateTime::currentMSecsSinceEpoch()),
                       true);
}

void MatrixRoomBridge::publishRange(Room *room, int fromIndex, int toIndex)
{
    for (int i = fromIndex; i <= toIndex; ++i) {
        const auto it = room->findInTimeline(i);
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
    const matrix::Row row = matrix::rowFor(flatten(room, event));
    switch (row.kind) {
    case matrix::RowKind::Skip:
        break;
    case matrix::RowKind::Text:
        Q_EMIT roomMessage(chatId, row.msgId, row.text, row.sender, row.isOwn, row.ts, false);
        break;
    case matrix::RowKind::Encrypted:
        // One notice for the room rather than one per unreadable event: the
        // point is to say why the room looks quiet, not to fill it.
        announceEncryption(room);
        break;
    case matrix::RowKind::Unsupported:
        Q_EMIT roomMessage(chatId,
                           row.msgId,
                           row.text.isEmpty()
                               ? i18nc("@info in-timeline notice, a Matrix message this build cannot render", "Unsupported message")
                               : i18nc("@info in-timeline notice, %1 is the message description sent with an attachment",
                                       "Attachment (not supported yet): %1",
                                       row.text),
                           row.sender,
                           row.isOwn,
                           row.ts,
                           true);
        break;
    }
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
    Q_EMIT sendFailed(chatid::matrixChatId(room->id()),
                      reason.isEmpty()
                          ? i18nc("@info:status a Matrix message was not accepted and nothing said why", "The message could not be sent to this room.")
                          : i18nc("@info:status a Matrix message was not accepted, %1 is what the homeserver reported",
                                  "The message could not be sent: %1",
                                  reason));
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
    if (room->usesEncryption()) {
        // Refused rather than sent in the clear: posting an unencrypted event
        // into an encrypted room is a message the other clients will show with a
        // warning, and it is not what anybody asked for.
        Q_EMIT sendFailed(chatId, i18nc("@info:status", "This room is end-to-end encrypted. KOutNet cannot send here yet."));
        return false;
    }

    room->postText(text);
    return true;
}

void MatrixRoomBridge::markRead(const QString &chatId)
{
    if (Room *room = roomFor(chatId))
        room->markAllMessagesAsRead();
}

} // namespace koutnet
