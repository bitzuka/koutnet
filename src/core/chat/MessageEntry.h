// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

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
    QString replyToSender;
    QString replyToId;
    QString chatId;
    QString msgId;

    bool isFile = false;
    QString filePath; // local path once downloaded/attached
    bool isImage = false;

    // An attachment that lives on a server rather than on this disk. A LAN
    // transfer has already put its file here and fills filePath instead; these
    // are the Matrix side, where the bytes are fetched by whatever loads the
    // URL and there is no local path to point at. Never both at once.
    QString mediaKind; // "", "image", "video", "audio" or "file"
    QString mediaUrl;
    QString mediaMime;
    qint64 mediaSize = 0;
    // As the sender reported them. Used to reserve the right space before the
    // download finishes, so the timeline does not jump under whoever is reading.
    int mediaWidth = 0;
    int mediaHeight = 0;
    int mediaDurationMs = 0;

    bool isRead = false;

    // True between an outgoing message appearing in the timeline and its
    // datagram being written. Deliberately not serialised - a "sending" state
    // that survived a restart would never resolve.
    bool pending = false;

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
