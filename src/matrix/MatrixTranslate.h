// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Matrix timeline event to conversation row.
//
// Deliberately free of every libQuotient type. MatrixRoomBridge flattens an
// event into RawEvent and this decides what the timeline shows, which is the
// half of the Matrix path that can be tested without a homeserver - or, for
// that matter, without libQuotient installed at all.
#pragma once

#include <QString>

namespace koutnet::matrix
{

struct RawEvent {
    QString eventId;
    QString senderId;
    QString senderName; // room-local display name, may be empty
    QString body; // plain text body, whatever the msgtype
    qint64 originTimestampMs = 0;
    bool isOwn = false;
    bool redacted = false;
    bool encrypted = false; // an m.room.encrypted this build cannot open
    bool textLike = false; // m.text, m.notice or m.emote
};

enum class RowKind {
    Skip, //!< nothing goes in the timeline for this event
    Text, //!< an ordinary message
    Encrypted, //!< say so; never show an empty bubble
    Unsupported, //!< an attachment or a msgtype this pass does not render
};

struct Row {
    RowKind kind = RowKind::Skip;
    QString msgId;
    QString text;
    QString sender;
    double ts = 0.0;
    bool isOwn = false;
};

// The stable msgId of a room's one "this room is encrypted" notice. Stable so
// that ChatModel's duplicate check swallows it on every run after the first
// rather than stacking a copy per restart.
QString encryptionNoticeId(const QString &roomId);

Row rowFor(const RawEvent &event);

// A room with no name and no canonical alias comes back from libQuotient with
// a display name already worked out; this is only the last resort, so that a
// row is never blank.
QString conversationTitle(const QString &displayName, const QString &roomId);

// Milliseconds since the epoch to the fractional seconds every other model in
// this tree stores. Zero maps to zero rather than to 1970: the conversation
// list sorts on it, and a room with no timestamp must not outrank a real one.
double secondsFromMs(qint64 ms);

} // namespace koutnet::matrix
