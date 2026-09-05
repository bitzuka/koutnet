// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - the Rocket.Chat transport, scaffolded.
//
// Registered as a preview backend so the interface lists Rocket.Chat among the
// five unified transports and opens a placeholder page for its chats. The real
// client (WebSocket/REST to a Rocket.Chat server, or its federation) grows in
// here and flips isPreview() off once it carries traffic.
#pragma once

#include "core/backend/PreviewChatBackend.h"

namespace koutnet
{

class RocketChatBackend : public PreviewChatBackend
{
public:
    explicit RocketChatBackend(QObject *parent = nullptr)
        : PreviewChatBackend(chatid::Transport::RocketChat, parent)
    {
    }
};

} // namespace koutnet
