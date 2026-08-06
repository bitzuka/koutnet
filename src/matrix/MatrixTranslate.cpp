// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "MatrixTranslate.h"

namespace koutnet::matrix
{

QString encryptionNoticeId(const QString &roomId)
{
    return QStringLiteral("mx-encryption:") + roomId;
}

double secondsFromMs(qint64 ms)
{
    if (ms <= 0)
        return 0.0;
    return double(ms) / 1000.0;
}

Row rowFor(const RawEvent &event)
{
    Row row;
    // Without an id there is no duplicate check, and sync replays the tail of a
    // timeline on every reconnect.
    if (event.eventId.isEmpty())
        return row;

    row.msgId = event.eventId;
    row.ts = secondsFromMs(event.originTimestampMs);
    row.isOwn = event.isOwn;
    row.sender = event.senderName.isEmpty() ? event.senderId : event.senderName;

    // A redacted event still occupies its place in the timeline, but its content
    // is gone; showing the tombstone as a message would show an empty bubble.
    if (event.redacted)
        return row;

    if (event.encrypted) {
        row.kind = RowKind::Encrypted;
        return row;
    }

    if (!event.textLike) {
        row.kind = RowKind::Unsupported;
        row.text = event.body;
        return row;
    }

    // An m.text with an empty body is legal and says nothing.
    if (event.body.isEmpty())
        return row;

    row.kind = RowKind::Text;
    row.text = event.body;
    return row;
}

QString conversationTitle(const QString &displayName, const QString &roomId)
{
    const QString trimmed = displayName.trimmed();
    return trimmed.isEmpty() ? roomId : trimmed;
}

} // namespace koutnet::matrix
