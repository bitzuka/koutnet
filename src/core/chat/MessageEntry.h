// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

// Data structure for a single chat message.
struct MessageEntry {
    QString sender;
    QString text;
    double ts = 0.0; // unix timestamp, seconds (fractional)
    bool isOwn = false;
    QString color = QStringLiteral("#E0E0E0");
    QString msgType = QStringLiteral("public"); // "public" | "private" | "group"
    bool isSystem = false;
    bool isEdited = false;
    QString replyToText;
    QString chatId;
    QString msgId;

    // File-transfer fields (match the FileTransferHandler flow already in use)
    bool isFile = false;
    QString filePath; // local path once downloaded/attached
    bool isImage = false;

    // Read receipt for own outgoing messages
    bool isRead = false;

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
