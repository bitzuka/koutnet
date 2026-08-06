// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Network Protocol Constants
#pragma once

#include <QString>
#include <QVector>

namespace koutnet::protocol
{

inline constexpr QLatin1StringView kMsgPresence("presence");
inline constexpr QLatin1StringView kMsgChat("chat");
inline constexpr QLatin1StringView kMsgPrivate("private");
inline constexpr QLatin1StringView kMsgGroup("group");
inline constexpr QLatin1StringView kMsgCallReq("call_req");
inline constexpr QLatin1StringView kMsgCallAccept("call_accept");
inline constexpr QLatin1StringView kMsgCallBusy("call_busy");
inline constexpr QLatin1StringView kMsgCallReject("call_reject");
inline constexpr QLatin1StringView kMsgCallEnd("call_end");
inline constexpr QLatin1StringView kMsgFileMeta("file_meta");
inline constexpr QLatin1StringView kMsgFileData("file_data");
inline constexpr QLatin1StringView kMsgGroupInv("group_invite");
inline constexpr QLatin1StringView kMsgTyping("typing");
inline constexpr QLatin1StringView kMsgReaction("reaction");
inline constexpr QLatin1StringView kMsgEdit("edit");
inline constexpr QLatin1StringView kMsgDelete("delete");
inline constexpr QLatin1StringView kMsgRead("read");
inline constexpr QLatin1StringView kMsgSticker("sticker");

// Fields on a peer record: the presence packet that arrived plus whatever
// handlePresence() adds to it. last_seen is stamped from the local clock on
// arrival, never read off the wire, and pruneStalePeers() judges staleness by it.
inline constexpr QLatin1StringView kFieldLastSeen("last_seen");
// The address a peer asked to be called, kept only when it differs from the one
// it was actually heard on. A delivery hint, never an identity.
inline constexpr QLatin1StringView kFieldAdvertisedIp("advertised_ip");

// LAN / VPN mode, the default and the path that actually works today.
inline constexpr quint16 kUdpPortDefault = 42000;
inline constexpr quint16 kTcpPortDefault = 42001;

struct RelayServer {
    const char *name;
    const char *host;
    quint16 tunnelPort;
    quint16 voicePort;
};

// TODO(VDS): populate once an official KOutNet relay is deployed, e.g.:
//   { "KOutNet Official", "relay.koutnet.example", 42010, 42011 },
// Until then this stays empty, and Relay mode requires the user to supply
// their own server via NetworkManager::setRelayServer().
inline const QVector<RelayServer> &builtinRelays()
{
    static const QVector<RelayServer> relays = {
    };
    return relays;
}

// Reconnect backoff for the relay tunnel, so an unreachable or unconfigured
// VDS does not hammer the network or the battery forever.
inline constexpr int kRelayReconnectBaseMs = 3000;
inline constexpr int kRelayReconnectMaxMs = 60000;

// Framing for the TCP streams: a 4-byte big-endian length in front of every
// message, because an AES-GCM tag that starts one byte off never verifies.
inline constexpr int kFrameHeaderBytes = 4;
// The declared length comes from an untrusted peer, so each stream refuses
// anything larger than it could plausibly need and hangs up.
inline constexpr quint32 kMaxVoiceFrameBytes = 1u << 20; // 1 MiB
inline constexpr quint32 kMaxRelayFrameBytes = 8u << 20; // 8 MiB

inline constexpr QLatin1StringView kAppName("KOutNet");
inline constexpr int kProtocolVersion = 1;

} // namespace koutnet::protocol
