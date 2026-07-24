#pragma once

#include <QString>
#include <QJsonObject>
#include <QVariantMap>
#include <QDateTime>

// Data structure for a single chat message.
// Mirrors legacy Python MessageEntry, but with real types instead of duck typing.
struct MessageEntry
{
    QString sender;
    QString text;
    double  ts        = 0.0;      // unix timestamp, seconds (fractional)
    bool    isOwn      = false;
    QString color      = QStringLiteral("#E0E0E0");
    QString emoji;
    QString msgType    = QStringLiteral("public"); // "public" | "private" | "group"
    QString imageData;            // base64, empty if none
    bool    isSystem   = false;
    bool    isEdited   = false;
    bool    isForwarded = false;
    QString forwardedFrom;
    QString replyToText;
    QString chatId;
    QString msgId;                // auto-generated from ts if empty

    MessageEntry() = default;

    static MessageEntry fromJson(const QJsonObject &o);
    QJsonObject toJson() const;

    // Convenience for QML (ListModel-style roles / delegate binding)
    QVariantMap toVariantMap() const;

    void ensureMsgId()
    {
        if (msgId.isEmpty())
            msgId = QString::number(ts, 'f', 3);
    }
};
