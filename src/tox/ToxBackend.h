// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - the Tox transport, scaffolded.
//
// Registered as a preview backend so the interface lists Tox among the five
// unified transports and opens a placeholder page for its chats. The real
// client (a Tox core library bound to Qt, carrying friend/Group/contact
// traffic) grows in here and flips isPreview() off once it carries traffic.
#pragma once

#include "core/backend/PreviewChatBackend.h"

namespace koutnet
{

class ToxBackend : public PreviewChatBackend
{
public:
    explicit ToxBackend(QObject *parent = nullptr)
        : PreviewChatBackend(chatid::Transport::Tox, parent)
    {
    }
};

} // namespace koutnet
