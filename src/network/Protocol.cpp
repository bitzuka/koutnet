// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Binary wire envelope implementation
#include "Protocol.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborStreamReader>
#include <QCborStreamWriter>
#include <QCborValue>
#include <QJsonArray>
#include <QtEndian>

#include <cstring>

namespace koutnet::protocol
{

namespace
{

constexpr quint8 kCodePresence = 1;
constexpr quint8 kCodeChat = 2;
constexpr quint8 kCodePrivate = 3;
constexpr quint8 kCodeGroup = 4;
constexpr quint8 kCodeCallReq = 5;
constexpr quint8 kCodeCallAccept = 6;
constexpr quint8 kCodeCallBusy = 7;
constexpr quint8 kCodeCallReject = 8;
constexpr quint8 kCodeCallEnd = 9;
constexpr quint8 kCodeFileMeta = 10;
constexpr quint8 kCodeFileData = 11;
constexpr quint8 kCodeGroupInv = 12;
constexpr quint8 kCodeTyping = 13;
constexpr quint8 kCodeReaction = 14;
constexpr quint8 kCodeEdit = 15;
constexpr quint8 kCodeDelete = 16;
constexpr quint8 kCodeRead = 17;

// QCborValue::toJsonValue() quietly turns NaN/Infinity into null, which both
// loses hostile-but-legal numbers and - worse - breaks HMAC verification, since
// the signer serialised the real NaN and the verifier would serialise null.
// QJsonValue can hold NaN/Inf fine, so recover doubles directly and recurse for
// containers. Map keys in our protocol are always strings.
QJsonValue cborToJsonValue(const QCborValue &v)
{
    if (v.isDouble())
        return QJsonValue(v.toDouble());
    if (v.isArray()) {
        QJsonArray out;
        for (const QCborValue &e : v.toArray())
            out.append(cborToJsonValue(e));
        return out;
    }
    if (v.isMap()) {
        QJsonObject out;
        const QCborMap map = v.toMap();
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            const QCborValue key = it.key();
            const QString keyStr = key.isString() ? key.toString() : key.toJsonValue().toString();
            out.insert(keyStr, cborToJsonValue(it.value()));
        }
        return out;
    }
    return v.toJsonValue();
}

} // namespace

quint8 messageTypeCode(const QString &type)
{
    if (type == kMsgPresence)
        return kCodePresence;
    if (type == kMsgChat)
        return kCodeChat;
    if (type == kMsgPrivate)
        return kCodePrivate;
    if (type == kMsgGroup)
        return kCodeGroup;
    if (type == kMsgCallReq)
        return kCodeCallReq;
    if (type == kMsgCallAccept)
        return kCodeCallAccept;
    if (type == kMsgCallBusy)
        return kCodeCallBusy;
    if (type == kMsgCallReject)
        return kCodeCallReject;
    if (type == kMsgCallEnd)
        return kCodeCallEnd;
    if (type == kMsgFileMeta)
        return kCodeFileMeta;
    if (type == kMsgFileData)
        return kCodeFileData;
    if (type == kMsgGroupInv)
        return kCodeGroupInv;
    if (type == kMsgTyping)
        return kCodeTyping;
    if (type == kMsgReaction)
        return kCodeReaction;
    if (type == kMsgEdit)
        return kCodeEdit;
    if (type == kMsgDelete)
        return kCodeDelete;
    if (type == kMsgRead)
        return kCodeRead;
    return 0;
}

QLatin1StringView messageTypeName(quint8 code)
{
    switch (code) {
    case kCodePresence:
        return kMsgPresence;
    case kCodeChat:
        return kMsgChat;
    case kCodePrivate:
        return kMsgPrivate;
    case kCodeGroup:
        return kMsgGroup;
    case kCodeCallReq:
        return kMsgCallReq;
    case kCodeCallAccept:
        return kMsgCallAccept;
    case kCodeCallBusy:
        return kMsgCallBusy;
    case kCodeCallReject:
        return kMsgCallReject;
    case kCodeCallEnd:
        return kMsgCallEnd;
    case kCodeFileMeta:
        return kMsgFileMeta;
    case kCodeFileData:
        return kMsgFileData;
    case kCodeGroupInv:
        return kMsgGroupInv;
    case kCodeTyping:
        return kMsgTyping;
    case kCodeReaction:
        return kMsgReaction;
    case kCodeEdit:
        return kMsgEdit;
    case kCodeDelete:
        return kMsgDelete;
    case kCodeRead:
        return kMsgRead;
    default:
        return QLatin1StringView("");
    }
}

QByteArray canonicalBytes(const QJsonObject &obj)
{
    return canonicalBytes(QCborValue::fromJsonValue(obj).toMap());
}

QByteArray canonicalBytes(const QCborMap &map)
{
    QCborMap copy = map;
    copy.remove(QCborValue(QStringLiteral("_sig")));
    QByteArray out;
    {
        QCborStreamWriter writer(&out);
        QCborValue(copy).toCbor(writer);
    }
    return out;
}

QByteArray encodeFrame(const QJsonObject &obj)
{
    return encodeFrame(QCborValue::fromJsonValue(obj).toMap());
}

QByteArray encodeFrame(const QCborMap &map)
{
    const quint8 typeCode = messageTypeCode(map.value(QStringLiteral("type")).toString());
    if (typeCode == 0)
        return {};

    QByteArray payload;
    {
        QCborStreamWriter writer(&payload);
        QCborValue(map).toCbor(writer);
    }
    if (quint32(payload.size()) > kWireMaxPayload)
        return {};

    QByteArray out;
    out.reserve(10 + payload.size());
    out.append(kWireMagic, 4);
    out.append(char(kWireVersion));
    out.append(char(typeCode));

    const quint32 len = quint32(payload.size());
    quint32 be = qToBigEndian(len);
    out.append(reinterpret_cast<const char *>(&be), 4);
    out.append(payload);
    return out;
}

QJsonObject qjsonFromCbor(const QCborMap &map)
{
    return cborToJsonValue(QCborValue(map)).toObject();
}

bool decodeFrame(const QByteArray &data, QString &outType, QJsonObject &outObj)
{
    QCborMap map;
    if (!decodeFrame(data, outType, map))
        return false;
    outObj = qjsonFromCbor(map);
    return true;
}

bool decodeFrame(const QByteArray &data, QString &outType, QCborMap &outMap)
{
    if (data.size() < 10)
        return false;
    if (std::memcmp(data.constData(), kWireMagic, 4) != 0)
        return false;

    const quint8 version = quint8(data.at(4));
    if (version != kWireVersion)
        return false;

    const quint8 typeCode = quint8(data.at(5));
    if (typeCode == 0)
        return false;

    quint32 len = 0;
    std::memcpy(&len, data.constData() + 6, 4);
    len = qFromBigEndian(len);
    if (len > kWireMaxPayload)
        return false;
    if (quint32(data.size()) != 10u + len)
        return false;

    QCborParserError err;
    const QCborValue value = QCborValue::fromCbor(data.constData() + 10, int(len), &err);
    if (err.error != QCborError::NoError)
        return false;
    if (!value.isMap())
        return false;

    outMap = value.toMap();
    outType = messageTypeName(typeCode);
    return true;
}

} // namespace koutnet::protocol
