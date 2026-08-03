// SPDX-FileCopyrightText: 2026 bitzuka <matveypotyzhno@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Network Protocol Constants
#pragma once

#include <QString>
#include <QVector>

namespace koutnet::protocol {

// Message types
inline constexpr QLatin1StringView kMsgPresence  ("presence");
inline constexpr QLatin1StringView kMsgChat      ("chat");
inline constexpr QLatin1StringView kMsgPrivate   ("private");
inline constexpr QLatin1StringView kMsgGroup     ("group");
inline constexpr QLatin1StringView kMsgCallReq   ("call_req");
inline constexpr QLatin1StringView kMsgCallAccept ("call_accept");
inline constexpr QLatin1StringView kMsgCallBusy  ("call_busy");
inline constexpr QLatin1StringView kMsgCallReject ("call_reject");
inline constexpr QLatin1StringView kMsgCallEnd   ("call_end");
inline constexpr QLatin1StringView kMsgFileMeta  ("file_meta");
inline constexpr QLatin1StringView kMsgFileData  ("file_data");
inline constexpr QLatin1StringView kMsgGroupInv  ("group_invite");
inline constexpr QLatin1StringView kMsgTyping    ("typing");
inline constexpr QLatin1StringView kMsgReaction  ("reaction");
inline constexpr QLatin1StringView kMsgEdit      ("edit");
inline constexpr QLatin1StringView kMsgDelete    ("delete");
inline constexpr QLatin1StringView kMsgRead      ("read");
inline constexpr QLatin1StringView kMsgSticker   ("sticker");

// LAN / VPN mode, the default and the path that actually works today.
// Broadcast, mDNS and ARP discovery with no server, see
// NetworkManager::onBroadcastTimer and scanArpTable. A VPN adapter is just
// another local interface, see NetworkManager::refreshLocalIps.
inline constexpr quint16 kUdpPortDefault = 42000;
inline constexpr quint16 kTcpPortDefault = 42001;

// VDS / relay mode
// Used by the Relay and MaintainerVds modes, where a relay server
// handles discovery and NAT traversal beyond the LAN.
struct RelayServer {
    const char *name;
    const char *host;
    quint16 tunnelPort;
    quint16 voicePort;
};

// TODO(VDS): populate once an official KOutNet relay is deployed, e.g.:
//   { "KOutNet Official", "relay.koutnet.example", 42010, 42011 },
// Until then this stays empty, and Vds mode requires the user to supply
// their own server via NetworkManager::setRelayServer().
inline const QVector<RelayServer> &builtinRelays()
{
    static const QVector<RelayServer> relays = {
        // (empty - no built-in relay ships yet)
    };
    return relays;
}

// Reconnect backoff for the relay/tunnel connection - starts fast, doubles
// up to a ceiling, so an unreachable/unconfigured VDS doesn't hammer the
// network or battery forever.
inline constexpr int kRelayReconnectBaseMs = 3000;
inline constexpr int kRelayReconnectMaxMs = 60000;

inline constexpr QLatin1StringView kAppName ("KOutNet");
inline constexpr int kProtocolVersion = 1;

} // namespace koutnet::protocol
