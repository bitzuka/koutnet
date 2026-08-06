// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "MessageEntry.h"

MessageEntry MessageEntry::fromJson(const QJsonObject &o)
{
    MessageEntry e;
    e.sender = o.value(QStringLiteral("sender")).toString();
    e.text = o.value(QStringLiteral("text")).toString();
    e.ts = o.value(QStringLiteral("ts")).toDouble();
    e.isOwn = o.value(QStringLiteral("is_own")).toBool();
    e.color = o.value(QStringLiteral("color")).toString(QStringLiteral("#E0E0E0"));
    e.msgType = o.value(QStringLiteral("msg_type")).toString(QStringLiteral("public"));
    e.isSystem = o.value(QStringLiteral("is_system")).toBool();
    e.isEdited = o.value(QStringLiteral("is_edited")).toBool();
    e.replyToText = o.value(QStringLiteral("reply_to_text")).toString();
    e.replyToSender = o.value(QStringLiteral("reply_to_sender")).toString();
    e.replyToId = o.value(QStringLiteral("reply_to_id")).toString();
    e.chatId = o.value(QStringLiteral("chat_id")).toString();
    e.msgId = o.value(QStringLiteral("msg_id")).toString();
    e.isFile = o.value(QStringLiteral("is_file")).toBool();
    e.filePath = o.value(QStringLiteral("file_path")).toString();
    e.isImage = o.value(QStringLiteral("is_image")).toBool();
    e.mediaKind = o.value(QStringLiteral("media_kind")).toString();
    e.mediaUrl = o.value(QStringLiteral("media_url")).toString();
    e.mediaMime = o.value(QStringLiteral("media_mime")).toString();
    // Through a double, because QJsonValue has no 64-bit integer of its own and
    // a file larger than two gigabytes must not come back negative.
    e.mediaSize = qint64(o.value(QStringLiteral("media_size")).toDouble());
    e.mediaWidth = o.value(QStringLiteral("media_width")).toInt();
    e.mediaHeight = o.value(QStringLiteral("media_height")).toInt();
    e.mediaDurationMs = o.value(QStringLiteral("media_duration_ms")).toInt();
    e.isRead = o.value(QStringLiteral("is_read")).toBool();
    e.ensureMsgId();
    return e;
}

QJsonObject MessageEntry::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("sender")] = sender;
    o[QStringLiteral("text")] = text;
    o[QStringLiteral("ts")] = ts;
    o[QStringLiteral("is_own")] = isOwn;
    o[QStringLiteral("color")] = color;
    o[QStringLiteral("msg_type")] = msgType;
    o[QStringLiteral("is_system")] = isSystem;
    o[QStringLiteral("is_edited")] = isEdited;
    o[QStringLiteral("reply_to_text")] = replyToText;
    o[QStringLiteral("reply_to_sender")] = replyToSender;
    o[QStringLiteral("reply_to_id")] = replyToId;
    o[QStringLiteral("chat_id")] = chatId;
    o[QStringLiteral("msg_id")] = msgId;
    o[QStringLiteral("is_file")] = isFile;
    o[QStringLiteral("file_path")] = filePath;
    o[QStringLiteral("is_image")] = isImage;
    o[QStringLiteral("media_kind")] = mediaKind;
    o[QStringLiteral("media_url")] = mediaUrl;
    o[QStringLiteral("media_mime")] = mediaMime;
    o[QStringLiteral("media_size")] = double(mediaSize);
    o[QStringLiteral("media_width")] = mediaWidth;
    o[QStringLiteral("media_height")] = mediaHeight;
    o[QStringLiteral("media_duration_ms")] = mediaDurationMs;
    o[QStringLiteral("is_read")] = isRead;
    return o;
}

QVariantMap MessageEntry::toVariantMap() const
{
    return toJson().toVariantMap();
}
