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
inline constexpr quint16 kUdpPortDefault = 42000;
inline constexpr quint16 kTcpPortDefault = 42001;

// Relay / VDS (discovery + NAT traversal only — see project architecture)
inline constexpr auto kRelayHost = "koutnet.ru";
inline constexpr quint16 kRelayTunnelPort = 9090;
inline constexpr auto kRelayVoiceHost = "157.22.199.8"; // TODO: move to config
inline constexpr quint16 kRelayVoicePort = 9091;

inline constexpr auto kAppName = "KOutNet";
inline constexpr int kProtocolVersion = 1;

} // namespace koutnet::protocol
