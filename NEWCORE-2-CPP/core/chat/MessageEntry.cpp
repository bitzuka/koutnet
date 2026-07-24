#include "MessageEntry.h"

MessageEntry MessageEntry::fromJson(const QJsonObject &o)
{
    MessageEntry e;
    e.sender        = o.value("sender").toString();
    e.text          = o.value("text").toString();
    e.ts            = o.value("ts").toDouble();
    e.isOwn         = o.value("is_own").toBool();
    e.color         = o.value("color").toString(QStringLiteral("#E0E0E0"));
    e.emoji         = o.value("emoji").toString();
    e.msgType       = o.value("msg_type").toString(QStringLiteral("public"));
    e.imageData     = o.value("image_data").toString();
    e.isSystem      = o.value("is_system").toBool();
    e.isEdited      = o.value("is_edited").toBool();
    e.isForwarded   = o.value("is_forwarded").toBool();
    e.forwardedFrom = o.value("forwarded_from").toString();
    e.replyToText   = o.value("reply_to_text").toString();
    e.chatId        = o.value("chat_id").toString();
    e.msgId         = o.value("msg_id").toString();
    e.isFile        = o.value("is_file").toBool();
    e.filePath      = o.value("file_path").toString();
    e.isImage       = o.value("is_image").toBool();
    e.isRead        = o.value("is_read").toBool();
    e.ensureMsgId();
    return e;
}

QJsonObject MessageEntry::toJson() const
{
    QJsonObject o;
    o["sender"]         = sender;
    o["text"]           = text;
    o["ts"]             = ts;
    o["is_own"]         = isOwn;
    o["color"]          = color;
    o["emoji"]          = emoji;
    o["msg_type"]       = msgType;
    o["image_data"]     = imageData;
    o["is_system"]      = isSystem;
    o["is_edited"]      = isEdited;
    o["is_forwarded"]   = isForwarded;
    o["forwarded_from"] = forwardedFrom;
    o["reply_to_text"]  = replyToText;
    o["chat_id"]        = chatId;
    o["msg_id"]         = msgId;
    o["is_file"]        = isFile;
    o["file_path"]      = filePath;
    o["is_image"]       = isImage;
    o["is_read"]        = isRead;
    return o;
}

QVariantMap MessageEntry::toVariantMap() const
{
    return toJson().toVariantMap();
}
