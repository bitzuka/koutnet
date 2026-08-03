// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "MessageEntry.h"

MessageEntry MessageEntry::fromJson(const QJsonObject &o)
{
    MessageEntry e;
    e.sender        = o.value(QStringLiteral("sender")).toString();
    e.text          = o.value(QStringLiteral("text")).toString();
    e.ts            = o.value(QStringLiteral("ts")).toDouble();
    e.isOwn         = o.value(QStringLiteral("is_own")).toBool();
    e.color         = o.value(QStringLiteral("color")).toString(QStringLiteral("#E0E0E0"));
    e.msgType       = o.value(QStringLiteral("msg_type")).toString(QStringLiteral("public"));
    e.isSystem      = o.value(QStringLiteral("is_system")).toBool();
    e.isEdited      = o.value(QStringLiteral("is_edited")).toBool();
    e.replyToText   = o.value(QStringLiteral("reply_to_text")).toString();
    e.chatId        = o.value(QStringLiteral("chat_id")).toString();
    e.msgId         = o.value(QStringLiteral("msg_id")).toString();
    e.isFile        = o.value(QStringLiteral("is_file")).toBool();
    e.filePath      = o.value(QStringLiteral("file_path")).toString();
    e.isImage       = o.value(QStringLiteral("is_image")).toBool();
    e.isRead        = o.value(QStringLiteral("is_read")).toBool();
    e.ensureMsgId();
    return e;
}

QJsonObject MessageEntry::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("sender")]         = sender;
    o[QStringLiteral("text")]           = text;
    o[QStringLiteral("ts")]             = ts;
    o[QStringLiteral("is_own")]         = isOwn;
    o[QStringLiteral("color")]          = color;
    o[QStringLiteral("msg_type")]       = msgType;
    o[QStringLiteral("is_system")]      = isSystem;
    o[QStringLiteral("is_edited")]      = isEdited;
    o[QStringLiteral("reply_to_text")]  = replyToText;
    o[QStringLiteral("chat_id")]        = chatId;
    o[QStringLiteral("msg_id")]         = msgId;
    o[QStringLiteral("is_file")]        = isFile;
    o[QStringLiteral("file_path")]      = filePath;
    o[QStringLiteral("is_image")]       = isImage;
    o[QStringLiteral("is_read")]        = isRead;
    return o;
}

QVariantMap MessageEntry::toVariantMap() const
{
    return toJson().toVariantMap();
}
