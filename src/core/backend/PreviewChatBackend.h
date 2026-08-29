// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - a ChatBackend that is wired in but not yet carrying live traffic.
//
// A preview backend answers the registry's routing questions (which prefix is
// its, what furniture a conversation on it would have) and refuses every
// outgoing action, so the interface can list it as a real transport and open a
// placeholder page instead of offering controls nothing is behind. It is the
// seam the Rocket.Chat, Telegram and Tox supports are grown into; when one of
// them ships it replaces this base's refusals with the protocol call and flips
// isPreview() back to false.
#pragma once

#include "core/backend/ChatBackend.h"
#include "core/chat/ChatAddress.h"

namespace koutnet
{

class PreviewChatBackend : public ChatBackend
{
public:
    explicit PreviewChatBackend(chatid::Transport transport, QObject *parent = nullptr)
        : ChatBackend(parent)
        , m_transport(transport)
    {
    }

    chatid::Transport transport() const override
    {
        return m_transport;
    }
    bool canHandle(const QString &chatId) const override
    {
        return chatid::transportOf(chatId) == m_transport;
    }

    // The shape a server-backed conversation takes, so the placeholder page
    // and the conversation list can treat it like any other room transport.
    bool serverOwnsTimeline(const QString &chatId) const override
    {
        Q_UNUSED(chatId)
        return true;
    }
    bool hasRooms(const QString &chatId) const override
    {
        Q_UNUSED(chatId)
        return true;
    }
    bool supportsCalls(const QString &chatId) const override
    {
        Q_UNUSED(chatId)
        return false;
    }
    bool supportsTyping(const QString &chatId) const override
    {
        Q_UNUSED(chatId)
        return false;
    }
    bool supportsEdits(const QString &chatId) const override
    {
        Q_UNUSED(chatId)
        return false;
    }
    bool supportsReactions(const QString &chatId) const override
    {
        Q_UNUSED(chatId)
        return false;
    }

    bool isPreview() const override
    {
        return true;
    }

    bool sendText(const QString &chatId, const QString &text) override
    {
        Q_UNUSED(chatId)
        Q_UNUSED(text)
        return false;
    }
    bool sendFile(const QString &chatId, const QString &localFilePath) override
    {
        Q_UNUSED(chatId)
        Q_UNUSED(localFilePath)
        return false;
    }
    void markRead(const QString &chatId) override
    {
        Q_UNUSED(chatId)
    }
    void sendTyping(const QString &chatId) override
    {
        Q_UNUSED(chatId)
    }
    void sendReaction(const QString &chatId, double ts, const QString &emoji, bool added) override
    {
        Q_UNUSED(chatId)
        Q_UNUSED(ts)
        Q_UNUSED(emoji)
        Q_UNUSED(added)
    }
    bool leaveChat(const QString &chatId) override
    {
        Q_UNUSED(chatId)
        return false;
    }

    QVariantMap roomInfo(const QString &chatId) const override
    {
        Q_UNUSED(chatId)
        return {};
    }
    QVariantList roomMembers(const QString &chatId) const override
    {
        Q_UNUSED(chatId)
        return {};
    }
    QVariantMap memberInfo(const QString &chatId, const QString &userId) const override
    {
        Q_UNUSED(chatId)
        Q_UNUSED(userId)
        return {};
    }

private:
    chatid::Transport m_transport;
};

} // namespace koutnet
