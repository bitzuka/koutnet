// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - the Telegram transport, scaffolded.
//
// Registered as a preview backend so the interface lists Telegram among the
// five unified transports and opens a placeholder page for its chats. The real
// client (the Telegram API through a local tg protocol library or a gateway)
// grows in here and flips isPreview() off once it carries traffic.
#pragma once

#include "core/backend/PreviewChatBackend.h"

namespace koutnet
{

class TelegramBackend : public PreviewChatBackend
{
public:
    explicit TelegramBackend(QObject *parent = nullptr)
        : PreviewChatBackend(chatid::Transport::Telegram, parent)
    {
    }
};

} // namespace koutnet
