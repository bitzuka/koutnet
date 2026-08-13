// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - the window's only door to the transports.
//
// Every action a chat id can be asked for goes through this one object, and
// the chat id decides which backend does it. A chat no backend claims gets a
// quiet refusal - the window guards the reserved chats ("__self__") before it
// ever asks. Exposed to QML as "chatTransport"; nothing in qml/ knows a prefix.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "core/backend/ChatBackend.h"

namespace koutnet
{

class ChatBackendRegistry : public QObject
{
    Q_OBJECT

public:
    explicit ChatBackendRegistry(QObject *parent = nullptr);

    // Registration order is the order canHandle() is asked in, which is the
    // order the backends are listed in main.cpp. A chat id can only match one
    // transport (the prefixes are disjoint), so order only matters while the
    // prefix table is growing.
    void registerBackend(ChatBackend *backend);
    ChatBackend *backendFor(const QString &chatId) const;

    // Everything below forwards to backendFor(chatId) or answers false/empty
    // when no backend claims the id. QML-facing, so they are Q_INVOKABLE and
    // return value types QML can hold.
    Q_INVOKABLE bool sendText(const QString &chatId, const QString &text);
    Q_INVOKABLE bool sendFile(const QString &chatId, const QString &localFilePath);
    Q_INVOKABLE void markRead(const QString &chatId);
    Q_INVOKABLE void sendTyping(const QString &chatId);
    Q_INVOKABLE void sendReaction(const QString &chatId, double ts, const QString &emoji, bool added);
    Q_INVOKABLE bool leaveChat(const QString &chatId);
    Q_INVOKABLE QVariantMap roomInfo(const QString &chatId) const;
    Q_INVOKABLE QVariantList roomMembers(const QString &chatId) const;
    Q_INVOKABLE QVariantMap memberInfo(const QString &chatId, const QString &userId) const;
    Q_INVOKABLE bool serverOwnsTimeline(const QString &chatId) const;
    Q_INVOKABLE bool hasRooms(const QString &chatId) const;
    Q_INVOKABLE bool supportsCalls(const QString &chatId) const;
    Q_INVOKABLE bool supportsTyping(const QString &chatId) const;
    Q_INVOKABLE bool supportsEdits(const QString &chatId) const;
    Q_INVOKABLE bool supportsReactions(const QString &chatId) const;
    // What the badge on a conversation row says, for the transport the chat
    // id belongs to - chatid::transportName()'s answer, made QML-visible.
    Q_INVOKABLE QString transportName(const QString &chatId) const;

private:
    QList<ChatBackend *> m_backends;
};

} // namespace koutnet
