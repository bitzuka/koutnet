// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Network Protocol Constants
#pragma once

#include <QByteArray>
#include <QCborMap>
#include <QJsonObject>
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

// Framing for the TCP streams: a 4-byte big-endian length in front of every
// message, because an XChaCha20-Poly1305 tag that starts one byte off never
// verifies.
inline constexpr int kFrameHeaderBytes = 4;
// The declared length comes from an untrusted peer, so each stream refuses
// anything larger than it could plausibly need and hangs up.
inline constexpr quint32 kMaxVoiceFrameBytes = 1u << 20; // 1 MiB

inline constexpr int kProtocolVersion = 1;

// Binary wire envelope. Replaces the old JSON-text datagrams so the data plane
// stops paying for text parsing, base64, and unbounded allocations. Format:
//   magic   : 4 bytes, 'K','O','N','1'
//   version : 1 byte  (kWireVersion)
//   type    : 1 byte  (messageTypeCode(typeName))
//   length  : 4 bytes big-endian, size of the CBOR payload that follows
//   payload : 'length' bytes, a CBOR map (the former QJsonObject)
// Voice media is intentionally NOT wrapped here: it is already opaque
// XChaCha20-Poly1305 ciphertext streamed under the kFrameHeaderBytes framing.
inline constexpr char kWireMagic[4] = {'K', 'O', 'N', '1'};
inline constexpr quint8 kWireVersion = 1;
// Hard ceiling on a single wire frame's CBOR payload (16 MiB). The old code
// bounded chat frames by kMaxChatPacketBytes; file chunks are the only type
// that can approach this, and 16 MiB covers them with room to spare.
inline constexpr quint32 kWireMaxPayload = 16u * 1024u * 1024u;

// Stable numeric codes for the message types above. Both ends must agree, so
// this mapping - not string hashing - is the contract. Presence stays 1 to
// keep the wire ugly-stable, the rest follow in dispatch order.
quint8 messageTypeCode(QLatin1StringView type);
QLatin1StringView messageTypeName(quint8 code);

// Encode a message object into a binary wire frame. The object MUST carry a
// "type" field matching one of the kMsg* constants. Returns an empty frame on
// an unknown type so a bad call is dropped rather than broadcast.
QByteArray encodeFrame(const QJsonObject &obj);
// Encode from a CBOR map directly. Use this for payloads that must carry binary
// (e.g. file_data "data" as a byte string instead of base64 in JSON).
QByteArray encodeFrame(const QCborMap &map);

// Decode a binary wire frame. Returns false on any malformation (bad magic,
// version, length, payload, or non-object CBOR) so the caller can drop it.
bool decodeFrame(const QByteArray &data, QString &outType, QJsonObject &outObj);
// Decode into the raw CBOR map - the canonical form the signer worked from, so
// byte-string fields survive intact instead of being forced through JSON.
bool decodeFrame(const QByteArray &data, QString &outType, QCborMap &outMap);

// Canonical bytes for HMAC sign/verify: CBOR of the payload with "_sig" removed.
// Both signer and verifier run this on the same logical payload, so identical
// key insertion order yields identical bytes. The QCborMap overload is the
// source of truth; the QJsonObject one routes through it.
QByteArray canonicalBytes(const QJsonObject &obj);
QByteArray canonicalBytes(const QCborMap &map);

// Convert a decoded CBOR map to a QJsonObject for the handlers that still speak
// JSON. Byte-string values become base64 strings here, so keep binary payloads
// (file_data) on the QCborMap path and never round-trip them.
QJsonObject qjsonFromCbor(const QCborMap &map);

} // namespace koutnet::protocol
