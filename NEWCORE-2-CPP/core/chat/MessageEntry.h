#pragma once

#include <QString>
#include <QJsonObject>
#include <QVariantMap>

// Data structure for a single chat message.
struct MessageEntry
{
    QString sender;
    QString text;
    double  ts        = 0.0;      // unix timestamp, seconds (fractional)
    bool    isOwn      = false;
    QString color      = QStringLiteral("#E0E0E0");
    QString emoji;
    QString msgType    = QStringLiteral("public"); // "public" | "private" | "group"
    QString imageData;            // base64 inline image, empty if none
    bool    isSystem   = false;
    bool    isEdited   = false;
    bool    isForwarded = false;
    QString forwardedFrom;
    QString replyToText;
    QString chatId;
    QString msgId;

    // File-transfer fields (match the FileTransferHandler flow already in use)
    bool    isFile     = false;
    QString filePath;             // local path once downloaded/attached
    bool    isImage    = false;

    // Read receipt for own outgoing messages
    bool    isRead     = false;

    MessageEntry() = default;

    static MessageEntry fromJson(const QJsonObject &o);
    QJsonObject toJson() const;
    QVariantMap toVariantMap() const;

    void ensureMsgId()
    {
        if (msgId.isEmpty())
            msgId = QString::number(ts, 'f', 3);
    }
};
