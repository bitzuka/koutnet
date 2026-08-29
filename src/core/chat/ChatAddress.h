// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - what a chat id is allowed to be.
//
// A chat id used to be an address and nothing else. A Matrix conversation is a
// room id instead, and the transports now share one namespace: HistoryManager,
// UnreadManager, ReactionStore and the conversation list are all keyed on this
// string and none of them should learn a transport. The prefix table here is
// the whole of the routing - ChatBackendRegistry asks it who a chat id belongs
// to, and a backend answers with canHandle().
//
// A prefix rather than a guess at the shape of the string, because guessing is
// how "!a:b.c" ends up being sent to a UDP socket. None of the prefixes can
// begin an IPv4 or an IPv6 address, and "__" was already reserved for
// HistoryManager's own logs before any of this.
#pragma once

#include <QString>

namespace koutnet::chatid
{

enum class Transport {
    Lan, //!< an address NetworkManager can put a datagram on
    Matrix, //!< a room id behind the "mx:" prefix
    RocketChat, //!< a channel behind the "rc:" prefix
    Telegram, //!< a chat behind the "tg:" prefix
    Tox, //!< a friend/Group/chat behind the "tox:" prefix
    Reserved, //!< "__self__" and the other logs that are not conversations
};

// The five user-facing transports KOutNet unifies. Ordered the way the
// transport picker shows them; nothing else relies on the order.
inline constexpr Transport kAllTransports[] = {
    Transport::Lan,
    Transport::Matrix,
    Transport::RocketChat,
    Transport::Telegram,
    Transport::Tox,
};

Transport transportOf(const QString &chatId);
bool isMatrix(const QString &chatId);

// Rocket.Chat, Telegram and Tox ids are formatted the same way Matrix ones are:
// the prefix is the only thing that says which server/network the chat lives
// on, and the remainder is the id that transport knows.
QString rocketChatId(const QString &channelId);
QString rocketChannelId(const QString &chatId);
QString telegramChatId(const QString &chatKey);
QString telegramChatKey(const QString &chatId);
QString toxChatId(const QString &chatKey);
QString toxChatKey(const QString &chatId);

// Human label for a transport, used by the picker and the conversation badge.
QString transportLabel(Transport transport);

// matrixRoomId() is empty for anything that is not a Matrix chat id, so a
// caller cannot hand a LAN address to a homeserver by forgetting a check.
QString matrixChatId(const QString &roomId);
QString matrixRoomId(const QString &chatId);

// The only thing the interface is told about where a row came from. A string
// rather than the enum above because it crosses into QML as a model role, and a
// namespaced enum would have to become a QObject to get there.
QString transportName(const QString &chatId);

} // namespace koutnet::chatid
