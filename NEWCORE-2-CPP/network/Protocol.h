// KOutNet — Network Protocol Constants
// Ported from gdf_network.py ( NT Server 1.8)
#pragma once

#include <QString>

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

// ── Defaults ─────────────────────────────────────────────────────────
// LAN discovery (broadcast + mDNS + ARP scan) works standalone, no server
// needed — see NetworkManager::onBroadcastTimer / scanArpTable.
inline constexpr quint16 kUdpPortDefault = 42000;
inline constexpr quint16 kTcpPortDefault = 42001;

// Relay / VDS — used only in "internet mode" for NAT traversal + discovery
// beyond LAN. Neither a default public relay nor self-hosted server config
// exists yet.
// I_Do_It_Latet.! — default public relay host/port (official KOutNet relay).
// I_Do_It_Latet.! — self-hosted / custom relay server support (user-supplied
//                    host:port, persisted via AppSettings once that lands).
inline constexpr auto kRelayHost = "I_Do_It_Latet.!";
inline constexpr quint16 kRelayTunnelPort = 0;
inline constexpr auto kRelayVoiceHost = "I_Do_It_Latet.!";
inline constexpr quint16 kRelayVoicePort = 0;

inline constexpr auto kAppName = "KOutNet";
inline constexpr int kProtocolVersion = 1;

} // namespace koutnet::protocol
