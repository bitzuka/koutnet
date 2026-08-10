// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "ChatAddress.h"

namespace
{
const QLatin1String kMatrixPrefix("mx:");
const QLatin1String kRocketPrefix("rc:");
const QLatin1String kTelegramPrefix("tg:");
const QLatin1String kReservedPrefix("__");
} // namespace

namespace koutnet::chatid
{

Transport transportOf(const QString &chatId)
{
    if (chatId.startsWith(kReservedPrefix))
        return Transport::Reserved;
    if (chatId.startsWith(kMatrixPrefix))
        return Transport::Matrix;
    if (chatId.startsWith(kRocketPrefix))
        return Transport::RocketChat;
    if (chatId.startsWith(kTelegramPrefix))
        return Transport::Telegram;
    return Transport::Lan;
}

bool isMatrix(const QString &chatId)
{
    return transportOf(chatId) == Transport::Matrix;
}

QString matrixChatId(const QString &roomId)
{
    // An empty room id would produce a bare "mx:", which is a chat id that
    // resolves to no room and would still be routed at the homeserver.
    if (roomId.isEmpty())
        return {};
    if (roomId.startsWith(kMatrixPrefix))
        return roomId;
    return kMatrixPrefix + roomId;
}

QString matrixRoomId(const QString &chatId)
{
    if (!isMatrix(chatId))
        return {};
    return chatId.mid(kMatrixPrefix.size());
}

QString rocketChatId(const QString &channelId)
{
    if (channelId.isEmpty())
        return {};
    if (channelId.startsWith(kRocketPrefix))
        return channelId;
    return kRocketPrefix + channelId;
}

QString rocketChannelId(const QString &chatId)
{
    if (transportOf(chatId) != Transport::RocketChat)
        return {};
    return chatId.mid(kRocketPrefix.size());
}

QString telegramChatId(const QString &chatKey)
{
    if (chatKey.isEmpty())
        return {};
    if (chatKey.startsWith(kTelegramPrefix))
        return chatKey;
    return kTelegramPrefix + chatKey;
}

QString telegramChatKey(const QString &chatId)
{
    if (transportOf(chatId) != Transport::Telegram)
        return {};
    return chatId.mid(kTelegramPrefix.size());
}

QString transportName(const QString &chatId)
{
    switch (transportOf(chatId)) {
    case Transport::Matrix:
        return QStringLiteral("matrix");
    case Transport::RocketChat:
        return QStringLiteral("rocket.chat");
    case Transport::Telegram:
        return QStringLiteral("telegram");
    case Transport::Reserved:
        return QStringLiteral("reserved");
    case Transport::Lan:
        break;
    }
    return QStringLiteral("lan");
}

} // namespace koutnet::chatid
