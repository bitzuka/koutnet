// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "ChatAddress.h"

namespace
{
const QLatin1String kMatrixPrefix("mx:");
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

QString transportName(const QString &chatId)
{
    switch (transportOf(chatId)) {
    case Transport::Matrix:
        return QStringLiteral("matrix");
    case Transport::Reserved:
        return QStringLiteral("reserved");
    case Transport::Lan:
        break;
    }
    return QStringLiteral("lan");
}

} // namespace koutnet::chatid
