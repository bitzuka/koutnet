// KOutNet — Network Protocol Constants
// Ported from gdf_network.py ( NT Server 1.8)
#pragma once

#include <QString>
#include <QVector>

namespace koutnet::protocol {

// ── Message types ───────────────────────────────────────────────────
inline constexpr auto kMsgPresence  = "presence";
inline constexpr auto kMsgChat      = "chat";
inline constexpr auto kMsgPrivate   = "private";
inline constexpr auto kMsgGroup     = "group";
inline constexpr auto kMsgCallReq   = "call_req";
inline constexpr auto kMsgCallAccept = "call_accept";
inline constexpr auto kMsgCallBusy  = "call_busy";
inline constexpr auto kMsgCallReject = "call_reject";
inline constexpr auto kMsgCallEnd   = "call_end";
inline constexpr auto kMsgFileMeta  = "file_meta";
inline constexpr auto kMsgFileData  = "file_data";
inline constexpr auto kMsgGroupInv  = "group_invite";
inline constexpr auto kMsgTyping    = "typing";
inline constexpr auto kMsgReaction  = "reaction";
inline constexpr auto kMsgEdit      = "edit";
inline constexpr auto kMsgDelete    = "delete";
inline constexpr auto kMsgRead      = "read";
inline constexpr auto kMsgSticker   = "sticker";

// ── LAN / VPN mode (default) ────────────────────────────────────────
// Broadcast + mDNS + ARP scan discovery, works standalone, no server
// needed — see NetworkManager::onBroadcastTimer / scanArpTable. A VPN
// adapter is just another local interface here, no special-casing needed
// — see NetworkManager::refreshLocalIps. Primary supported path today.
inline constexpr quint16 kUdpPortDefault = 42000;
inline constexpr quint16 kTcpPortDefault = 42001;

// ── VDS / relay mode ─────────────────────────────────────────────────
// Used only when NetworkManager::ConnectionMode::Vds is selected — relay
// server handles discovery + NAT traversal beyond LAN.
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
        // (empty — no built-in relay ships yet)
    };
    return relays;
}

// Reconnect backoff for the relay/tunnel connection — starts fast, doubles
// up to a ceiling, so an unreachable/unconfigured VDS doesn't hammer the
// network or battery forever.
inline constexpr int kRelayReconnectBaseMs = 3000;
inline constexpr int kRelayReconnectMaxMs = 60000;

inline constexpr auto kAppName = "KOutNet";
inline constexpr int kProtocolVersion = 1;

} // namespace koutnet::protocol
