// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// The interesting cases are the ones a real peer never produces - a missing
// signature, a replayed packet, a 10 MB blob - so handleDatagram() is fed
// directly, with no sockets bound and no peers contacted.

#include <KLocalizedString>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include <limits>

#include "../core/security/CryptoManager.h"
#include "../core/security/SecretStore.h"
#include "../network/NetworkManager.h"
#include "../network/Protocol.h"

using koutnet::CryptoManager;
using koutnet::NetworkManager;
namespace protocol = koutnet::protocol;

namespace
{

// TEST-NET-2, so the peer's claimed address can never collide with one this
// machine holds, which would drop the packet as our own echoed broadcast.
const QString kPeerIp = QStringLiteral("198.51.100.7");
// A second address the same peer sends from - the whole bug: discovery happens
// over one interface, a VPN comes up, and the message leaves by the other.
const QString kPeerAltIp = QStringLiteral("198.51.100.8");
const QString kOtherIp = QStringLiteral("198.51.100.20");
const QString kSelfLabel = QStringLiteral("198.51.100.1");

double nowEpoch()
{
    return QDateTime::currentMSecsSinceEpoch() / 1000.0;
}

QString freshNonce()
{
    static int counter = 0;
    return QStringLiteral("nonce-%1").arg(++counter);
}

// Has to match NetworkManager's signableBytes() byte for byte or nothing verifies.
QByteArray signableBytes(QJsonObject obj)
{
    obj.remove(QStringLiteral("_sig"));
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray toDatagram(const QJsonObject &obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QJsonObject presenceFrom(const CryptoManager &peer, const QString &username = QStringLiteral("peer"), const QString &ip = kPeerIp)
{
    QJsonObject o = peer.handshakePayload();
    o[QStringLiteral("type")] = protocol::kMsgPresence;
    o[QStringLiteral("ip")] = ip;
    o[QStringLiteral("ts")] = nowEpoch();
    o[QStringLiteral("nonce")] = freshNonce();
    o[QStringLiteral("username")] = username;
    return o;
}

QJsonObject signedPacket(const CryptoManager &peer, QJsonObject o, double ts = -1.0)
{
    o[QStringLiteral("nonce")] = freshNonce();
    o[QStringLiteral("ts")] = ts < 0.0 ? nowEpoch() : ts;
    o[QStringLiteral("from_id")] = peer.ownIdentityId();
    o[QStringLiteral("_sig")] = peer.signPacket(kSelfLabel, signableBytes(o));
    return o;
}

// Separate storage scopes, or both CryptoManagers would come up as the same
// identity and every signature would verify for the wrong reason.
class Harness
{
public:
    Harness()
        : mine(QStringLiteral("nm-self"))
        , peer(QStringLiteral("nm-peer"))
        , net(&mine)
    {
    }

    bool establishSession()
    {
        net.handleDatagram(kPeerIp, toDatagram(presenceFrom(peer)));
        return peer.processHandshake(kSelfLabel, mine.handshakePayload()) && mine.hasSession(kPeerIp);
    }

    CryptoManager mine;
    CryptoManager peer;
    NetworkManager net;
};

} // namespace

class NetworkManagerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void malformedBytesAreRefused_data()
    {
        QTest::addColumn<QByteArray>("raw");

        QTest::newRow("empty") << QByteArray();
        QTest::newRow("whitespace") << QByteArrayLiteral("   \n\t ");
        QTest::newRow("not json") << QByteArrayLiteral("hello there");
        QTest::newRow("binary") << QByteArray("\x00\x01\x02\xFF\xFE", 5);
        QTest::newRow("open brace") << QByteArrayLiteral("{");
        QTest::newRow("truncated object") << QByteArrayLiteral("{\"type\":\"chat\"");
        QTest::newRow("truncated string") << QByteArrayLiteral("{\"type\":\"cha");
        QTest::newRow("trailing comma") << QByteArrayLiteral("{\"type\":\"chat\",}");
        QTest::newRow("array not object") << QByteArrayLiteral("[{\"type\":\"chat\"}]");
        QTest::newRow("bare null") << QByteArrayLiteral("null");
        QTest::newRow("bare number") << QByteArrayLiteral("12345");
        QTest::newRow("nul in the middle") << QByteArray("{\"type\":\"chat\"\x00,\"text\":\"x\"}", 27);

        QByteArray deep;
        deep.reserve(8192);
        deep.append(QByteArrayLiteral("{\"type\":\"chat\",\"text\":"));
        deep.append(QByteArray(2000, '['));
        deep.append('1');
        deep.append(QByteArray(2000, ']'));
        deep.append('}');
        QTest::newRow("deeply nested") << deep;

        QByteArray deepObjects;
        for (int i = 0; i < 2000; ++i)
            deepObjects.append(QByteArrayLiteral("{\"a\":"));
        deepObjects.append('1');
        deepObjects.append(QByteArray(2000, '}'));
        QTest::newRow("deeply nested objects") << deepObjects;

        QJsonObject huge;
        huge[QStringLiteral("type")] = protocol::kMsgChat;
        huge[QStringLiteral("text")] = QString(10 * 1024 * 1024, QLatin1Char('A'));
        QTest::newRow("ten megabytes of json") << toDatagram(huge);

        QTest::newRow("ten megabytes of junk") << (QByteArrayLiteral("{\"type\":") + QByteArray(10 * 1024 * 1024, 'z'));
    }

    void malformedBytesAreRefused()
    {
        QFETCH(QByteArray, raw);

        Harness h;
        QSignalSpy messages(&h.net, &NetworkManager::message);
        QSignalSpy online(&h.net, &NetworkManager::userOnline);
        QSignalSpy typing(&h.net, &NetworkManager::typing);
        QSignalSpy files(&h.net, &NetworkManager::fileMeta);

        h.net.handleDatagram(kPeerIp, raw);

        QCOMPARE(messages.count(), 0);
        QCOMPARE(online.count(), 0);
        QCOMPARE(typing.count(), 0);
        QCOMPARE(files.count(), 0);
        QVERIFY2(h.net.peers().isEmpty(), "a peer was recorded from a packet that never parsed");
    }

    // The test is here so adding a branch later cannot silently start accepting
    // one unsigned.
    void anUnknownTypeIsIgnored_data()
    {
        QTest::addColumn<QString>("type");
        QTest::newRow("invented") << QStringLiteral("sudo_make_me_a_sandwich");
        QTest::newRow("near miss") << QStringLiteral("presence ");
        QTest::newRow("case wrong") << QStringLiteral("Presence");
        QTest::newRow("sticker, a kind this build declares nowhere") << QStringLiteral("sticker");
        QTest::newRow("empty") << QString();
    }

    void anUnknownTypeIsIgnored()
    {
        QFETCH(QString, type);

        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy messages(&h.net, &NetworkManager::message);
        QSignalSpy online(&h.net, &NetworkManager::userOnline);
        QSignalSpy typing(&h.net, &NetworkManager::typing);
        const int peersBefore = h.net.peers().size();

        QJsonObject o;
        o[QStringLiteral("type")] = type;
        o[QStringLiteral("text")] = QStringLiteral("payload");
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));

        QCOMPARE(messages.count(), 0);
        QCOMPARE(typing.count(), 0);
        QCOMPARE(online.count(), 0);
        QCOMPARE(h.net.peers().size(), peersBefore);
    }

    // Presence carries the handshake, so it is the one packet that may arrive
    // unauthenticated.
    void onlyPresencePassesBeforeASession()
    {
        Harness h;
        QSignalSpy messages(&h.net, &NetworkManager::message);
        QSignalSpy online(&h.net, &NetworkManager::userOnline);
        QSignalSpy errors(&h.net, &NetworkManager::errorOccurred);

        for (const QLatin1StringView type : {protocol::kMsgChat,
                                             protocol::kMsgPrivate,
                                             protocol::kMsgGroup,
                                             protocol::kMsgFileMeta,
                                             protocol::kMsgFileData,
                                             protocol::kMsgTyping,
                                             protocol::kMsgCallReq,
                                             protocol::kMsgCallAccept,
                                             protocol::kMsgCallEnd,
                                             protocol::kMsgGroupInv,
                                             protocol::kMsgReaction,
                                             protocol::kMsgEdit,
                                             protocol::kMsgDelete,
                                             protocol::kMsgRead}) {
            QJsonObject o;
            o[QStringLiteral("type")] = QString(type);
            o[QStringLiteral("text")] = QStringLiteral("trust me");
            o[QStringLiteral("to")] = h.net.hostIp();
            o[QStringLiteral("nonce")] = freshNonce();
            o[QStringLiteral("ts")] = nowEpoch();
            h.net.handleDatagram(kPeerIp, toDatagram(o));
        }
        QCOMPARE(messages.count(), 0);
        QVERIFY(!errors.isEmpty());
        QVERIFY(!h.mine.hasSession(kPeerIp));

        h.net.handleDatagram(kPeerIp, toDatagram(presenceFrom(h.peer)));
        QCOMPARE(online.count(), 1);
        QVERIFY(h.mine.hasSession(kPeerIp));
    }

    void afterASessionAnUnsignedPacketIsDropped()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy messages(&h.net, &NetworkManager::message);
        QSignalSpy errors(&h.net, &NetworkManager::errorOccurred);

        QJsonObject bare;
        bare[QStringLiteral("type")] = protocol::kMsgChat;
        bare[QStringLiteral("text")] = QStringLiteral("i am your peer, honest");
        bare[QStringLiteral("nonce")] = freshNonce();
        bare[QStringLiteral("ts")] = nowEpoch();
        h.net.handleDatagram(kPeerIp, toDatagram(bare));
        QCOMPARE(messages.count(), 0);
        QCOMPARE(errors.count(), 1);

        QJsonObject emptySig = bare;
        emptySig[QStringLiteral("nonce")] = freshNonce();
        emptySig[QStringLiteral("_sig")] = QString();
        h.net.handleDatagram(kPeerIp, toDatagram(emptySig));
        QCOMPARE(messages.count(), 0);

        for (const QString &sig : {QStringLiteral("AAAA"),
                                   QString::fromLatin1(QByteArray(32, '\0').toBase64()),
                                   QString::fromLatin1(QByteArray(32, 'x').toBase64()),
                                   QStringLiteral("!!! not base64 !!!")}) {
            QJsonObject badSig = bare;
            badSig[QStringLiteral("nonce")] = freshNonce();
            badSig[QStringLiteral("_sig")] = sig;
            h.net.handleDatagram(kPeerIp, toDatagram(badSig));
            QCOMPARE(messages.count(), 0);
        }

        QJsonObject tampered = signedPacket(h.peer, bare);
        tampered[QStringLiteral("text")] = QStringLiteral("transfer 1000 to me");
        h.net.handleDatagram(kPeerIp, toDatagram(tampered));
        QCOMPARE(messages.count(), 0);
    }

    // The counterpart: if a correctly signed packet does not get through, the
    // fail-closed policy above is fail-always and the application does not work.
    void aCorrectlySignedPacketIsAccepted()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy messages(&h.net, &NetworkManager::message);
        const QString text = QStringLiteral("dinner at eight");

        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgChat;
        o[QStringLiteral("text")] = h.peer.encrypt(text, QString(), kSelfLabel);
        o[QStringLiteral("encrypted")] = true;
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));

        QCOMPARE(messages.count(), 1);
        const QJsonObject got = messages.at(0).at(0).toJsonObject();
        QCOMPARE(got.value(QStringLiteral("text")).toString(), text);
    }

    // Signing happens over the JSON text, so the packet has to survive a
    // serialise/parse round trip unchanged, fractional timestamp included.
    void aSignatureSurvivesTheJsonRoundTrip()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy messages(&h.net, &NetworkManager::message);
        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgReaction;
        o[QStringLiteral("chat_id")] = QStringLiteral("public");
        o[QStringLiteral("msg_ts")] = 1774038261.4567890123;
        o[QStringLiteral("emoji")] = QStringLiteral(":)");
        o[QStringLiteral("added")] = true;
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));

        QVERIFY2(messages.count() == 1,
                 "a packet signed over its own JSON did not verify after a round "
                 "trip through the parser, so no real packet carrying a fractional "
                 "number can either");
    }

    // A reaction is a message packet like any other, and the receive branch
    // in the window is keyed on these exact fields.
    void aReactionPacketCarriesItsFields()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy messages(&h.net, &NetworkManager::message);
        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgReaction;
        o[QStringLiteral("chat_id")] = QStringLiteral("public");
        o[QStringLiteral("msg_ts")] = 1774038261.5;
        o[QStringLiteral("emoji")] = QStringLiteral(":)");
        o[QStringLiteral("added")] = false;
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));

        QCOMPARE(messages.count(), 1);
        const QJsonObject got = messages.at(0).at(0).toJsonObject();
        QCOMPARE(got.value(QStringLiteral("type")).toString(), QStringLiteral("reaction"));
        QCOMPARE(got.value(QStringLiteral("emoji")).toString(), QStringLiteral(":)"));
        QCOMPARE(got.value(QStringLiteral("added")).toBool(), false);
        QCOMPARE(got.value(QStringLiteral("msg_ts")).toDouble(), 1774038261.5);
        QCOMPARE(got.value(QStringLiteral("chat_id")).toString(), QStringLiteral("public"));
    }

    // Regression: presence carries no signature, a broadcast having no single peer
    // to sign for. Requiring one once a session existed made the peer go stale.
    void presenceKeepsFlowingAfterASession()
    {
        Harness h;
        QVERIFY(h.establishSession());
        QVERIFY(h.net.peers().contains(kPeerIp));

        QSignalSpy errors(&h.net, &NetworkManager::errorOccurred);
        const double firstSeen = h.net.peers().value(kPeerIp).value(QStringLiteral("last_seen")).toDouble();
        QVERIFY(firstSeen > 0.0);

        const QJsonObject again = presenceFrom(h.peer, QStringLiteral("peer-renamed"));
        QVERIFY(!again.contains(QStringLiteral("_sig")));
        h.net.handleDatagram(kPeerIp, toDatagram(again));

        QVERIFY2(errors.isEmpty(), "presence from an established peer was reported as unauthenticated");
        QCOMPARE(h.net.peers().value(kPeerIp).value(QStringLiteral("username")).toString(), QStringLiteral("peer-renamed"));
        QVERIFY(h.net.peers().value(kPeerIp).value(QStringLiteral("e2e")).toBool());
    }

    void aReplayedPresencePacketIsRefused()
    {
        Harness h;
        QSignalSpy online(&h.net, &NetworkManager::userOnline);

        const QJsonObject first = presenceFrom(h.peer, QStringLiteral("original"));
        h.net.handleDatagram(kPeerIp, toDatagram(first));
        QCOMPARE(online.count(), 1);
        QCOMPARE(h.net.peers().value(kPeerIp).value(QStringLiteral("username")).toString(), QStringLiteral("original"));

        QJsonObject replay = first;
        replay[QStringLiteral("username")] = QStringLiteral("attacker");
        h.net.handleDatagram(kPeerIp, toDatagram(replay));

        QCOMPARE(online.count(), 1);
        QCOMPARE(h.net.peers().value(kPeerIp).value(QStringLiteral("username")).toString(), QStringLiteral("original"));
    }

    // Signed packets used to get no replay check: the nonce and timestamp are
    // inside the signature, so resending the captured bytes verifies as well as
    // the original.
    void aReplayedSignedPacketIsRefused()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy messages(&h.net, &NetworkManager::message);
        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgChat;
        o[QStringLiteral("text")] = h.peer.encrypt(QStringLiteral("pay the invoice"), QString(), kSelfLabel);
        const QByteArray captured = toDatagram(signedPacket(h.peer, o));

        h.net.handleDatagram(kPeerIp, captured);
        QCOMPARE(messages.count(), 1);
        h.net.handleDatagram(kPeerIp, captured);
        QVERIFY2(messages.count() == 1, "a captured packet was accepted a second time");
        h.net.handleDatagram(kPeerIp, captured);
        QCOMPARE(messages.count(), 1);
    }

    void theReplayWindowEdgesBehave_data()
    {
        QTest::addColumn<double>("offset");
        QTest::addColumn<bool>("accepted");
        const double window = CryptoManager::kReplayWindowSec;

        QTest::newRow("now") << 0.0 << true;
        QTest::newRow("a moment ago") << -2.0 << true;
        QTest::newRow("just inside the window") << -(window - 2.0) << true;
        QTest::newRow("just outside the window") << -(window + 2.0) << false;
        QTest::newRow("long ago") << -3600.0 << false;
        QTest::newRow("slightly ahead") << 2.0 << true;
        QTest::newRow("from the future") << (window + 2.0) << false;
        QTest::newRow("far future") << 3600.0 << false;
    }

    void theReplayWindowEdgesBehave()
    {
        QFETCH(double, offset);
        QFETCH(bool, accepted);

        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy messages(&h.net, &NetworkManager::message);
        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgChat;
        o[QStringLiteral("text")] = h.peer.encrypt(QStringLiteral("on time?"), QString(), kSelfLabel);
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o, nowEpoch() + offset)));

        QCOMPARE(messages.count(), accepted ? 1 : 0);
    }

    // None of these fields is interpreted here, and that is the property pinned
    // down: chat ids are sanitised in HistoryManager, so no layer may do it half
    // way.
    void hostileFieldsStayInsideTheirHandler_data()
    {
        QTest::addColumn<QString>("chatId");
        QTest::newRow("traversal") << QStringLiteral("../../../../etc/passwd");
        QTest::newRow("absolute") << QStringLiteral("/etc/shadow");
        QTest::newRow("windows") << QStringLiteral("..\\..\\hosts");
        QTest::newRow("nul") << (QStringLiteral("public") + QChar(u'\0') + QStringLiteral("x"));
        QTest::newRow("newlines") << QStringLiteral("public\n[section]\nkey=value");
        QTest::newRow("format string") << QStringLiteral("%s%s%s%n%p");
        QTest::newRow("very long") << QString(100000, QLatin1Char('c'));
        QTest::newRow("empty") << QString();
    }

    void hostileFieldsStayInsideTheirHandler()
    {
        QFETCH(QString, chatId);

        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy typing(&h.net, &NetworkManager::typing);
        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgTyping;
        o[QStringLiteral("chat_id")] = chatId;
        o[QStringLiteral("username")] = QStringLiteral("peer");
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));

        QCOMPARE(typing.count(), 1);
        QCOMPARE(typing.at(0).at(1).toString(), chatId);
    }

    void hostileNumbersDoNotEscapeTheirHandler_data()
    {
        QTest::addColumn<QJsonValue>("msgTs");
        QTest::newRow("absurd") << QJsonValue(1e300);
        QTest::newRow("negative absurd") << QJsonValue(-1e300);
        QTest::newRow("infinity") << QJsonValue(std::numeric_limits<double>::infinity());
        QTest::newRow("nan") << QJsonValue(std::numeric_limits<double>::quiet_NaN());
        QTest::newRow("zero") << QJsonValue(0);
        QTest::newRow("string") << QJsonValue(QStringLiteral("not a timestamp"));
        QTest::newRow("object") << QJsonValue(QJsonObject{{QStringLiteral("a"), 1}});
        QTest::newRow("array") << QJsonValue(QJsonArray{1, 2, 3});
        QTest::newRow("null") << QJsonValue(QJsonValue::Null);
    }

    void hostileNumbersDoNotEscapeTheirHandler()
    {
        QFETCH(QJsonValue, msgTs);

        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy messages(&h.net, &NetworkManager::message);
        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgDelete;
        o[QStringLiteral("chat_id")] = QStringLiteral("public");
        o[QStringLiteral("msg_ts")] = msgTs;
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));

        QCOMPARE(messages.count(), 1);
    }

    void ahostileAllIpsListIsSurvivable_data()
    {
        QTest::addColumn<QJsonValue>("allIps");
        QTest::newRow("not an array") << QJsonValue(QStringLiteral("192.0.2.1"));
        QTest::newRow("empty array") << QJsonValue(QJsonArray());
        QTest::newRow("nested") << QJsonValue(QJsonArray{QJsonArray{QStringLiteral("a")}});
        QTest::newRow("junk entries") << QJsonValue(QJsonArray{QStringLiteral("not an ip"), 42, QJsonValue::Null, QString()});

        QJsonArray many;
        for (int i = 0; i < 5000; ++i)
            many.append(QStringLiteral("10.0.%1.%2").arg(i / 256).arg(i % 256));
        QTest::newRow("five thousand addresses") << QJsonValue(many);
    }

    void ahostileAllIpsListIsSurvivable()
    {
        QFETCH(QJsonValue, allIps);

        Harness h;
        QSignalSpy online(&h.net, &NetworkManager::userOnline);

        QJsonObject presence = presenceFrom(h.peer);
        presence[QStringLiteral("all_ips")] = allIps;
        h.net.handleDatagram(kPeerIp, toDatagram(presence));

        QCOMPARE(online.count(), 1);
        QCOMPARE(h.net.peers().size(), 1);
        QVERIFY(h.net.peers().contains(kPeerIp));
    }

    // Our own broadcast comes back on every interface we sent it on.
    void ourOwnPacketsAreDropped()
    {
        Harness h;
        QSignalSpy messages(&h.net, &NetworkManager::message);

        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgChat;
        o[QStringLiteral("text")] = QStringLiteral("echo");
        o[QStringLiteral("from_ip")] = h.net.hostIp();
        o[QStringLiteral("nonce")] = freshNonce();
        o[QStringLiteral("ts")] = nowEpoch();
        h.net.handleDatagram(kPeerIp, toDatagram(o));
        QCOMPARE(messages.count(), 0);

        h.net.handleDatagram(h.net.hostIp(), toDatagram(o));
        QCOMPARE(messages.count(), 0);

        QJsonObject presence = presenceFrom(h.peer);
        presence[QStringLiteral("ip")] = h.net.hostIp();
        h.net.handleDatagram(kPeerIp, toDatagram(presence));
        QVERIFY(h.net.peers().isEmpty());
    }

    // A different identity for an already pinned address is refused by
    // CryptoManager, and the peer entry must not be taken over by it either.
    void animpostorDoesNotTakeOverThePeerEntry()
    {
        Harness h;
        QVERIFY(h.establishSession());
        const QString pinned = h.mine.peerFingerprint(kPeerIp);

        CryptoManager impostor(QStringLiteral("nm-impostor"));
        QSignalSpy identityChanged(&h.mine, &CryptoManager::peerIdentityChanged);

        QJsonObject fake = presenceFrom(impostor, QStringLiteral("not-the-peer"));
        h.net.handleDatagram(kPeerIp, toDatagram(fake));

        QCOMPARE(identityChanged.count(), 1);
        QCOMPARE(h.mine.peerFingerprint(kPeerIp), pinned);
        QVERIFY(h.mine.hasSession(kPeerIp));
        QCOMPARE(h.net.peers().value(kPeerIp).value(QStringLiteral("username")).toString(), QStringLiteral("peer"));
        QCOMPARE(h.net.peers().value(kPeerIp).value(QStringLiteral("id_pub")).toString(), h.peer.handshakePayload().value(QStringLiteral("id_pub")).toString());

        QSignalSpy messages(&h.net, &NetworkManager::message);
        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgChat;
        o[QStringLiteral("text")] = QStringLiteral("it is me, your peer");
        QJsonObject signedByImpostor = o;
        signedByImpostor[QStringLiteral("nonce")] = freshNonce();
        signedByImpostor[QStringLiteral("ts")] = nowEpoch();
        signedByImpostor[QStringLiteral("_sig")] = impostor.signPacket(kSelfLabel, signableBytes(signedByImpostor));
        h.net.handleDatagram(kPeerIp, toDatagram(signedByImpostor));
        QCOMPARE(messages.count(), 0);
    }

    // Cleartext on a channel we hold a key for is a downgrade attempt: strip
    // the tag and the text used to be handed to the interface unchecked.
    void cleartextOnAKeyedChannelIsNotPassedThrough()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy messages(&h.net, &NetworkManager::message);
        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgChat;
        o[QStringLiteral("text")] = QStringLiteral("meet me at seven");
        o[QStringLiteral("encrypted")] = false; // the sender's own claim, which decides nothing
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));

        QCOMPARE(messages.count(), 1);
        const QString delivered = messages.at(0).at(0).toJsonObject().value(QStringLiteral("text")).toString();
        QVERIFY2(delivered != QStringLiteral("meet me at seven"), "an unencrypted body was passed through on a channel with a session key");
    }

    // The bug this pass is about, seen with one client in a network namespace: the
    // session was established over the advertised address while the datagram
    // arrived from the veth, so the address-keyed lookup found nothing and dropped
    // it.
    void aPacketFromAnotherAddressOfTheSamePeerIsAccepted()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy messages(&h.net, &NetworkManager::message);
        QSignalSpy errors(&h.net, &NetworkManager::errorOccurred);
        const QString text = QStringLiteral("sent down the tunnel");

        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgChat;
        o[QStringLiteral("text")] = h.peer.encrypt(text, QString(), kSelfLabel);
        h.net.handleDatagram(kPeerAltIp, toDatagram(signedPacket(h.peer, o)));

        QVERIFY2(errors.isEmpty(), "a correctly signed packet from a second address of a known peer was called unauthenticated");
        QCOMPARE(messages.count(), 1);
        const QJsonObject got = messages.at(0).at(0).toJsonObject();
        QCOMPARE(got.value(QStringLiteral("text")).toString(), text);
        QCOMPARE(got.value(QStringLiteral("from_ip")).toString(), kPeerIp);
    }

    // A private message was what was actually being lost, and it has one more way
    // to go missing: the "to" field used to be compared against our primary
    // address alone.
    void aPrivateMessageToAnyOfOurAddressesArrives()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy messages(&h.net, &NetworkManager::message);
        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgPrivate;
        o[QStringLiteral("to")] = h.net.hostIp();
        o[QStringLiteral("text")] = h.peer.encrypt(QStringLiteral("still there?"), QString(), kSelfLabel);
        h.net.handleDatagram(kPeerAltIp, toDatagram(signedPacket(h.peer, o)));
        QCOMPARE(messages.count(), 1);

        QJsonObject elsewhere = o;
        elsewhere[QStringLiteral("to")] = kOtherIp;
        h.net.handleDatagram(kPeerAltIp, toDatagram(signedPacket(h.peer, elsewhere)));
        QCOMPARE(messages.count(), 1);
    }

    // A read receipt is routed to a chat by from_ip, so one filed under the
    // address the peer believes it lives at marks up a chat with nobody in it and
    // the sent arrow never changes.
    void areadReceiptLandsInTheChatThePeerIsFiledUnder()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy messages(&h.net, &NetworkManager::message);
        QSignalSpy errors(&h.net, &NetworkManager::errorOccurred);

        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgRead;
        o[QStringLiteral("chat_id")] = QStringLiteral("dm");
        o[QStringLiteral("from_ip")] = kOtherIp;
        h.net.handleDatagram(kPeerAltIp, toDatagram(signedPacket(h.peer, o)));

        QVERIFY2(errors.isEmpty(), "a correctly signed read receipt was refused");
        QCOMPARE(messages.count(), 1);
        const QJsonObject got = messages.at(0).at(0).toJsonObject();
        QVERIFY(got.value(QStringLiteral("type")).toString() == protocol::kMsgRead);
        QVERIFY2(got.value(QStringLiteral("from_ip")).toString() == kPeerIp, "the receipt reached the interface under an address no chat is keyed on");
    }

    // Unsigned, anybody could mark somebody else's messages as read.
    void anunsignedReadReceiptIsRefused()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy messages(&h.net, &NetworkManager::message);
        QJsonObject bare;
        bare[QStringLiteral("type")] = protocol::kMsgRead;
        bare[QStringLiteral("chat_id")] = QStringLiteral("dm");
        bare[QStringLiteral("nonce")] = freshNonce();
        bare[QStringLiteral("ts")] = nowEpoch();
        bare[QStringLiteral("from_id")] = h.peer.ownIdentityId();
        h.net.handleDatagram(kPeerIp, toDatagram(bare));
        QCOMPARE(messages.count(), 0);
    }

    // Taking a packet from any address is only safe because the signature decides.
    void abadSignatureIsRefusedFromEitherAddress()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy messages(&h.net, &NetworkManager::message);
        for (const QString &from : {kPeerIp, kPeerAltIp}) {
            QJsonObject o;
            o[QStringLiteral("type")] = protocol::kMsgChat;
            o[QStringLiteral("text")] = QStringLiteral("trust me");
            QJsonObject tampered = signedPacket(h.peer, o);
            tampered[QStringLiteral("text")] = QStringLiteral("send money");
            h.net.handleDatagram(from, toDatagram(tampered));

            QJsonObject bare = o;
            bare[QStringLiteral("nonce")] = freshNonce();
            bare[QStringLiteral("ts")] = nowEpoch();
            bare[QStringLiteral("from_id")] = h.peer.ownIdentityId();
            h.net.handleDatagram(from, toDatagram(bare));
        }
        QCOMPARE(messages.count(), 0);
    }

    // The identity in a packet is a claim anyone can copy out of a presence
    // broadcast. What makes it true is the HMAC, which needs the session key
    // behind the identity.
    void animpostorClaimingAKnownIdentityIsRefused()
    {
        Harness h;
        QVERIFY(h.establishSession());
        const QString pinned = h.mine.peerFingerprint(kPeerIp);

        // Its own handshake, so it holds a real session key - just not that
        // name's.
        CryptoManager impostor(QStringLiteral("nm-impostor-id"));
        h.net.handleDatagram(kOtherIp, toDatagram(presenceFrom(impostor, QStringLiteral("someone-else"), kOtherIp)));
        QVERIFY(impostor.processHandshake(kSelfLabel, h.mine.handshakePayload()));
        QVERIFY(h.mine.hasSession(kOtherIp));

        QSignalSpy messages(&h.net, &NetworkManager::message);
        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgChat;
        o[QStringLiteral("text")] = QStringLiteral("it is me, your peer");
        o[QStringLiteral("nonce")] = freshNonce();
        o[QStringLiteral("ts")] = nowEpoch();
        o[QStringLiteral("from_id")] = h.peer.ownIdentityId(); // the peer's name
        o[QStringLiteral("_sig")] = impostor.signPacket(kSelfLabel, signableBytes(o)); // the impostor's key
        QVERIFY2(!o.value(QStringLiteral("_sig")).toString().isEmpty(), "the impostor could not sign anything, so the refusal below proves nothing");
        h.net.handleDatagram(kPeerAltIp, toDatagram(o));
        QCOMPARE(messages.count(), 0);

        QCOMPARE(h.mine.peerFingerprint(kPeerIp), pinned);
        QVERIFY(h.mine.hasSession(kPeerIp));
        QJsonObject real;
        real[QStringLiteral("type")] = protocol::kMsgChat;
        real[QStringLiteral("text")] = h.peer.encrypt(QStringLiteral("no it is not"), QString(), kSelfLabel);
        h.net.handleDatagram(kPeerAltIp, toDatagram(signedPacket(h.peer, real)));
        QCOMPARE(messages.count(), 1);
    }

    // Replay state hangs off the identity, not the source address.
    void areplayedPacketIsRefusedFromAnotherAddress()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy messages(&h.net, &NetworkManager::message);
        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgChat;
        o[QStringLiteral("text")] = h.peer.encrypt(QStringLiteral("pay the invoice"), QString(), kSelfLabel);
        const QByteArray captured = toDatagram(signedPacket(h.peer, o));

        h.net.handleDatagram(kPeerIp, captured);
        QCOMPARE(messages.count(), 1);
        h.net.handleDatagram(kPeerAltIp, captured);
        QVERIFY2(messages.count() == 1, "a captured packet was accepted again from a different source address");
    }

    // The peer decides how many addresses it lists; one message may not become one
    // each.
    void thefanOutIsCapped()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QJsonArray many;
        for (int i = 0; i < 5000; ++i)
            many.append(QStringLiteral("10.0.%1.%2").arg(i / 256).arg(i % 256));
        QJsonObject presence = presenceFrom(h.peer);
        presence[QStringLiteral("all_ips")] = many;
        h.net.handleDatagram(kPeerIp, toDatagram(presence));

        const QVector<QString> targets = h.net.deliveryAddresses(kPeerIp);
        QVERIFY2(targets.size() <= NetworkManager::kMaxDeliveryAddresses, "an advertised address list decided how many datagrams one message becomes");
        QVERIFY(targets.contains(kPeerIp));

        h.net.handleDatagram(kPeerAltIp, toDatagram(presenceFrom(h.peer, QStringLiteral("peer"), kPeerAltIp)));
        const QVector<QString> withAlt = h.net.deliveryAddresses(kPeerIp);
        QVERIFY(withAlt.size() <= NetworkManager::kMaxDeliveryAddresses);
        QVERIFY2(withAlt.contains(kPeerAltIp), "an address the peer was heard on did not make it into the delivery set");
    }

    // One peer on two interfaces is one person, and the peer table records the
    // address its packets came from, not the one it asked to be called.
    void amultiHomedPeerIsOneContact()
    {
        Harness h;
        QSignalSpy online(&h.net, &NetworkManager::userOnline);

        h.net.handleDatagram(kPeerIp, toDatagram(presenceFrom(h.peer, QStringLiteral("peer"), kOtherIp)));
        QCOMPARE(online.count(), 1);
        QCOMPARE(h.net.peers().size(), 1);
        QVERIFY2(h.net.peers().contains(kPeerIp), "the peer was filed under the address it asked for rather than the one it sent from");
        QVERIFY(!h.net.peers().contains(kOtherIp));
        QCOMPARE(h.net.peers().value(kPeerIp).value(QStringLiteral("ip")).toString(), kPeerIp);
        QCOMPARE(h.net.peers().value(kPeerIp).value(QStringLiteral("advertised_ip")).toString(), kOtherIp);

        h.net.handleDatagram(kPeerAltIp, toDatagram(presenceFrom(h.peer, QStringLiteral("peer"), kPeerAltIp)));
        QCOMPARE(h.net.peers().size(), 1);
        QVERIFY(h.net.peers().contains(kPeerIp));
        QCOMPARE(online.count(), 1);
    }
    // No socket exists before start(), so test the decision only:
    // which configured addresses get the unicast presence.
    void staticPeerTargetsAreChosenWithoutANetwork()
    {
        const QMap<QString, QJsonObject> none;
        QVERIFY(NetworkManager::staticUnicastTargets({}, none, kSelfLabel).isEmpty());

        const QStringList configured = {QStringLiteral("10.13.4.9"), QStringLiteral("not an address"), QStringLiteral("172.16.0.1")};
        QCOMPARE(NetworkManager::staticUnicastTargets(configured, none, kSelfLabel), QStringList({QStringLiteral("10.13.4.9"), QStringLiteral("172.16.0.1")}));

        QMap<QString, QJsonObject> known;
        known.insert(QStringLiteral("10.13.4.9"), QJsonObject());
        const QStringList targets = NetworkManager::staticUnicastTargets(configured, known, kSelfLabel);
        QVERIFY(!targets.contains(QStringLiteral("10.13.4.9")));
        QVERIFY2(targets == QStringList({QStringLiteral("172.16.0.1")}), "a known peer was sent a presence reminder");

        QCOMPARE(NetworkManager::staticUnicastTargets(configured, none, QStringLiteral("172.16.0.1")), QStringList({QStringLiteral("10.13.4.9")}));
    }

    void theRateLimitStopsAFlood()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy typing(&h.net, &NetworkManager::typing);
        for (int i = 0; i < 600; ++i) {
            QJsonObject o;
            o[QStringLiteral("type")] = protocol::kMsgTyping;
            o[QStringLiteral("chat_id")] = QStringLiteral("public");
            h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));
        }
        QVERIFY2(typing.count() < 600, "the per-IP rate limit let a whole flood through");
    }

    // The microphone attack: any session-holder can send a signed call_accept.
    // With nothing to accept it must not open a call, or the attacker hears the
    // victim's room.
    void aCallAcceptWithoutARequestWeSentIsDropped()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy accepted(&h.net, &NetworkManager::callAccepted);
        QSignalSpy ended(&h.net, &NetworkManager::callEnded);

        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgCallAccept;
        o[QStringLiteral("username")] = QStringLiteral("peer");
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));

        QVERIFY2(accepted.isEmpty(), "a call_accept for a call nobody asked for completed a call");
        QVERIFY2(ended.isEmpty(), "the forged accept left ringing state behind");
    }

    void aCallAcceptAnswersOnlyTheRequestWeSent()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy accepted(&h.net, &NetworkManager::callAccepted);
        h.net.sendCallRequest(kPeerIp);

        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgCallAccept;
        o[QStringLiteral("username")] = QStringLiteral("peer");
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));
        QCOMPARE(accepted.count(), 1);

        // The pending slot is spent: a second accept, and an accept from a peer
        // we never called, both complete nothing.
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));
        QCOMPARE(accepted.count(), 1);
    }

    void aCallEndForACallThatDoesNotExistIsDropped()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy ended(&h.net, &NetworkManager::callEnded);
        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgCallEnd;
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));
        QVERIFY2(ended.isEmpty(), "a call_end for a call in no state cancelled something");

        // But it cancels a ring: a call_req we let through, then the caller
        // giving up, is exactly the "caller abandoned the ring" case.
        QSignalSpy ringing(&h.net, &NetworkManager::callRequest);
        QJsonObject req;
        req[QStringLiteral("type")] = protocol::kMsgCallReq;
        req[QStringLiteral("username")] = QStringLiteral("peer");
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, req)));
        QCOMPARE(ringing.count(), 1);
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));
        QCOMPARE(ended.count(), 1);

        // And a call_end that never matched a ring must not fire twice.
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));
        QCOMPARE(ended.count(), 1);
    }

    void aCallEndCancelsAnActiveCall()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy ended(&h.net, &NetworkManager::callEnded);
        h.net.setActiveCalls({kPeerIp});
        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgCallEnd;
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));
        QCOMPARE(ended.count(), 1);
    }

    void aRejectAnswersOnlyTheRequestWeSent()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy rejected(&h.net, &NetworkManager::callRejected);
        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgCallReject;
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));
        QVERIFY2(rejected.isEmpty(), "a call_reject for a call nobody asked for surfaced a rejection");

        h.net.sendCallRequest(kPeerIp);
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));
        QCOMPARE(rejected.count(), 1);
        // The slot is spent; an accept arriving for the same call is stale.
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));
        QCOMPARE(rejected.count(), 1);
    }

    void aCallRequestWhileInACallIsMetWithBusyInsteadOfRinging()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy ringing(&h.net, &NetworkManager::callRequest);
        QSignalSpy rejected(&h.net, &NetworkManager::callRejected);
        h.net.setActiveCalls({kOtherIp});

        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgCallReq;
        o[QStringLiteral("username")] = QStringLiteral("peer");
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));
        QVERIFY2(ringing.isEmpty(), "a second call rang while one was live");
        QVERIFY2(rejected.isEmpty(), "the busy reply leaked out of the state machine as a rejection");

        // With no call live the same packet rings normally again.
        h.net.setActiveCalls({});
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));
        QCOMPARE(ringing.count(), 1);
    }

    // The Argon2id receive path: a passphrase-sealed message with a fresh salt
    // is decrypted on the worker thread and arrives through the queue. The text
    // is sealed under the passphrase rather than the session - that is what a
    // group message, or any message sent before the handshake, carries.
    void aPassphraseMessageArrivesThroughTheAsyncPipeline()
    {
        Harness h;
        QVERIFY(h.establishSession());
        h.net.setGroupPassphrase(QStringLiteral("gruppo"));

        QSignalSpy messages(&h.net, &NetworkManager::message);
        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgGroup;
        // A peerRef with no session forces the passphrase path in encrypt(),
        // even though a session exists - otherwise the text would be sealed
        // under the session key and never touch the async pipeline.
        o[QStringLiteral("text")] = h.peer.encrypt(QStringLiteral("il segreto"), QStringLiteral("gruppo"), QStringLiteral("nobody-here"));
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));

        QTRY_VERIFY_WITH_TIMEOUT(messages.count() == 1, 30000);
        const QJsonObject got = messages.at(0).at(0).toJsonObject();
        QCOMPARE(got.value(QStringLiteral("text")).toString(), QStringLiteral("il segreto"));
    }

    // A session-keyed message arriving behind a passphrase one must not
    // overtake it: the chat renders in arrival order, whatever the decryption
    // cost of the message in front.
    void aFastMessageWaitsBehindAnAsyncOne()
    {
        Harness h;
        QVERIFY(h.establishSession());
        h.net.setGroupPassphrase(QStringLiteral("gruppo"));

        QSignalSpy messages(&h.net, &NetworkManager::message);

        QJsonObject slow;
        slow[QStringLiteral("type")] = protocol::kMsgChat;
        slow[QStringLiteral("text")] = h.peer.encrypt(QStringLiteral("first"), QStringLiteral("gruppo"), QStringLiteral("nobody-here"));
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, slow)));

        QJsonObject fast;
        fast[QStringLiteral("type")] = protocol::kMsgChat;
        fast[QStringLiteral("text")] = h.peer.encrypt(QStringLiteral("second"), QString(), kSelfLabel);
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, fast)));

        QTRY_VERIFY_WITH_TIMEOUT(messages.count() == 2, 30000);
        QCOMPARE(messages.at(0).at(0).toJsonObject().value(QStringLiteral("text")).toString(), QStringLiteral("first"));
        QCOMPARE(messages.at(1).at(0).toJsonObject().value(QStringLiteral("text")).toString(), QStringLiteral("second"));
    }

    // A flood of fresh-salt messages costs queue slots, not GUI time and not
    // memory: one derivation in flight, eight queued, the rest refused. The
    // exact survivors do not matter, only that the queue drains and the GUI
    // thread was never the one doing the hashing.
    void anAsyncFloodDropsMessagesInsteadOfMemory()
    {
        Harness h;
        QVERIFY(h.establishSession());
        h.net.setGroupPassphrase(QStringLiteral("gruppo"));

        QSignalSpy messages(&h.net, &NetworkManager::message);
        for (int i = 0; i < 30; ++i) {
            QJsonObject o;
            o[QStringLiteral("type")] = protocol::kMsgGroup;
            o[QStringLiteral("text")] = h.peer.encrypt(QStringLiteral("spam-%1").arg(i), QStringLiteral("gruppo"), QStringLiteral("nobody-here"));
            h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));
        }

        // One in flight plus kMaxPendingDecrypts queued can get through; the
        // in-flight head holds a queue slot, so that is kMaxPendingDecrypts
        // arrivals in total. The rest were refused at the queue gate.
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() >= 1, 60000);
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() == koutnet::CryptoManager::kMaxPendingDecrypts, 60000);
    }
};

// Test mode keeps CryptoManager's config lookups in a scratch directory. No
// sockets are bound; QCoreApplication is only needed for the classes' timers.
int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    // See CryptoManagerTest's main(): test mode does not move KWallet, and the
    // impostor identities below are not things to leave in a real keyring.
    koutnet::SecretStore::setInMemoryOnly(true);
    QCoreApplication app(argc, argv);
    // Without a domain ki18n warns on every string, burying the test output.
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("koutnet"));
    QCoreApplication::setOrganizationName(QStringLiteral("koutnet-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("network-manager"));

    NetworkManagerTest tc;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tc, argc, argv);
}
#include "NetworkManagerTest.moc"
