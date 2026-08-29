// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Matrix timeline event to conversation row.
//
// Deliberately free of every libQuotient type. MatrixRoomBridge flattens an
// event into RawEvent and this decides what the timeline shows, which is the
// half of the Matrix path that can be tested without a homeserver - or, for
// that matter, without libQuotient installed at all.
//
// The state-change wording lives here for the same reason. "Who did what to
// this room" is a sentence; sentences are the part that gets said wrong, and a
// sentence can be checked where a Quotient::RoomMemberEvent cannot.
#pragma once

#include <QString>
#include <QVariantMap>

namespace koutnet::matrix
{

// What an m.room.message with a non-text msgtype carries.
enum class MediaKind {
    None,
    Image,
    Video,
    Audio,
    File,
    Location, //!< a geo: point, opened as a map rather than downloaded
};

// The state changes worth a line in a conversation. Anything a room can do to
// itself that is not named here becomes Unknown rather than nothing: a timeline
// with a silent gap in it is what this enum exists to prevent.
//
// The classification is the bridge's, because only it has the event and its
// prev_content. The wording is stateSentence()'s.
enum class StateChange {
    None,
    Joined,
    Left,
    Invited,
    InviteWithdrawn,
    InviteRejected,
    Kicked,
    Banned,
    SelfBanned,
    Unbanned,
    SelfUnbanned,
    KnockRequested,
    DisplayNameSet,
    DisplayNameChanged,
    DisplayNameCleared,
    MemberAvatarChanged,
    RoomCreated,
    RoomUpgraded,
    RoomNameSet,
    RoomNameCleared,
    TopicSet,
    TopicCleared,
    AliasSet,
    AliasCleared,
    RoomAvatarChanged,
    EncryptionEnabled,
    PowerLevelsChanged,
    Unknown,
};

struct RawEvent {
    QString eventId;
    QString senderId;
    QString senderName; // room-local display name, may be empty
    QString body; // plain text body, whatever the msgtype
    qint64 originTimestampMs = 0;
    bool isOwn = false;
    bool redacted = false;
    bool encrypted = false; // an m.room.encrypted no key arrived for
    bool textLike = false; // m.text, m.notice or m.emote
    bool spoiler = false; // text hidden until the reader chooses to reveal it

    // An attachment, once the bridge has turned the event's mxc:// source into
    // a URL the interface can load. A media kind with an empty mediaUrl means
    // the event named a file this session cannot reach, which is said in words
    // rather than drawn as a broken picture.
    MediaKind media = MediaKind::None;
    QString mediaUrl;
    QString mediaName;
    QString mediaMime;
    qint64 mediaSize = 0;
    int mediaWidth = 0;
    int mediaHeight = 0;
    int mediaDurationMs = 0;

    // Room state. subject is the member a change is about when that is somebody
    // other than the sender: a kick, a ban, a withdrawn invitation.
    StateChange state = StateChange::None;
    QString stateSubject;

    // A poll this event carries (empty map unless it is m.poll.start). Keys:
    // question (string), answers (list of {id, body}), disclosed (bool).
    QVariantMap poll;

    // A vote on a poll (m.poll.response): the event id of the poll it answers and
    // the answer id chosen. Both empty unless this is a poll response.
    QString pollStartId;
    QString pollAnswerId;
};

enum class RowKind {
    Skip, //!< nothing goes in the timeline for this event
    Text, //!< an ordinary message
    Encrypted, //!< there is a message here and no key for it; say so
    Attachment, //!< a picture, a recording or a file
    System, //!< the room talking about itself
    Poll, //!< a question with answer options the reader can vote on
    PollVote, //!< a vote on a poll, updating the tally of the poll it answers
};

struct Row {
    RowKind kind = RowKind::Skip;
    QString msgId;
    QString text;
    QString sender;
    // Who the sender is on the homeserver: a display name like sender, but the
    // id, so an avatar lookup gets a stable key even mid-rename.
    QString senderId;
    double ts = 0.0;
    bool isOwn = false;

    MediaKind media = MediaKind::None;
    QString mediaUrl;
    QString mediaName;
    QString mediaMime;
    qint64 mediaSize = 0;
    int mediaWidth = 0;
    int mediaHeight = 0;
    int mediaDurationMs = 0;

    // A poll this event carries (empty map unless it is m.poll.start). Keys:
    // question (string), answers (list of {id, body}), disclosed (bool).
    QVariantMap poll;

    // A vote on a poll (m.poll.response): the event id of the poll it answers and
    // the answer id chosen. Both empty unless this is a poll response.
    QString pollStartId;
    QString pollAnswerId;
};

Row rowFor(const RawEvent &event);

// One sentence per state change, in the room's own voice. actor is who did it;
// subject is who it was done to, where the two differ.
QString stateSentence(StateChange change, const QString &actor, const QString &subject);

// The name an attachment goes by when it has none of its own, and what the
// conversation list shows for a row with no words in it.
QString mediaLabel(MediaKind kind, const QString &name);

// A room with no name and no canonical alias comes back from libQuotient with
// a display name already worked out; this is only the last resort, so that a
// row is never blank.
QString conversationTitle(const QString &displayName, const QString &roomId);

// Milliseconds since the epoch to the fractional seconds every other model in
// this tree stores. Zero maps to zero rather than to 1970: the conversation
// list sorts on it, and a room with no timestamp must not outrank a real one.
double secondsFromMs(qint64 ms);

} // namespace koutnet::matrix
