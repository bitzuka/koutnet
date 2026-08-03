// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// Tests for the packet path: parse, authenticate, replay-check, dispatch.
//
// Every byte that reaches handleDatagram() came from whoever felt like sending
// a UDP packet to port 42000. The interesting cases are the ones a real peer
// never produces - a missing signature, a replayed packet, a 10 MB blob - so
// none of this is reachable by running two copies of the application.
//
// No sockets are bound and no peers are contacted: handleDatagram() is fed
// directly, which is the whole reason it exists as a function.

#include <QTest>
#include <KLocalizedString>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>

#include <limits>

#include "../core/security/CryptoManager.h"
#include "../network/NetworkManager.h"
#include "../network/Protocol.h"

using koutnet::CryptoManager;
using koutnet::NetworkManager;
namespace protocol = koutnet::protocol;

namespace {

// The address the peer under test claims. TEST-NET-2, so it can never collide
// with an address this machine actually holds - which would make the packet
// look like our own broadcast echoed back and get it dropped for that reason
// instead of the one the test is about.
const QString kPeerIp = QStringLiteral("198.51.100.7");
// The label the peer files its session with us under. Session keys are looked
// up by string; both sides derived the same one, so what it is called on the
// far side does not matter.
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

// Has to match NetworkManager's own signableBytes() byte for byte, or nothing
// signed here would ever verify there.
QByteArray signableBytes(QJsonObject obj)
{
    obj.remove(QStringLiteral("_sig"));
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray toDatagram(const QJsonObject &obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

// A presence packet as the peer would broadcast it, handshake bundle included.
QJsonObject presenceFrom(const CryptoManager &peer, const QString &username = QStringLiteral("peer"))
{
    QJsonObject o = peer.handshakePayload();
    o[QStringLiteral("type")] = protocol::kMsgPresence;
    o[QStringLiteral("ip")] = kPeerIp;
    o[QStringLiteral("ts")] = nowEpoch();
    o[QStringLiteral("nonce")] = freshNonce();
    o[QStringLiteral("username")] = username;
    return o;
}

// The signing half of sendUdp(): nonce, timestamp, then the HMAC over
// everything else.
QJsonObject signedPacket(const CryptoManager &peer, QJsonObject o, double ts = -1.0)
{
    o[QStringLiteral("nonce")] = freshNonce();
    o[QStringLiteral("ts")] = ts < 0.0 ? nowEpoch() : ts;
    o[QStringLiteral("_sig")] = peer.signPacket(kSelfLabel, signableBytes(o));
    return o;
}

// One NetworkManager, the CryptoManager it was given, and a second one standing
// in for the peer. Separate storage scopes, or both would come up as the same
// identity and every signature would verify for the wrong reason.
class Harness
{
public:
    Harness()
        : mine(QStringLiteral("nm-self")), peer(QStringLiteral("nm-peer")), net(&mine)
    {
    }

    // Runs the real handshake in both directions, the way a presence packet
    // does it, and leaves both sides holding the session key.
    bool establishSession()
    {
        net.handleDatagram(kPeerIp, toDatagram(presenceFrom(peer)));
        return peer.processHandshake(kSelfLabel, mine.handshakePayload())
            && mine.hasSession(kPeerIp);
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
        QTest::newRow("nul in the middle")
            << QByteArray("{\"type\":\"chat\"\x00,\"text\":\"x\"}", 27);

        // Past whatever the parser's nesting limit is, which is the point: it
        // has to refuse rather than recurse until the stack runs out.
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

        // Ten megabytes of valid JSON. Nothing here is allowed to keep it, and
        // nothing is allowed to act on it either - there is no session, so the
        // signature policy refuses it before any handler sees it.
        QJsonObject huge;
        huge[QStringLiteral("type")] = protocol::kMsgChat;
        huge[QStringLiteral("text")] = QString(10 * 1024 * 1024, QLatin1Char('A'));
        QTest::newRow("ten megabytes of json") << toDatagram(huge);

        // And ten megabytes that is not JSON at all.
        QTest::newRow("ten megabytes of junk")
            << (QByteArrayLiteral("{\"type\":") + QByteArray(10 * 1024 * 1024, 'z'));
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

    // A type nobody implements is not an error to report and not a packet to
    // act on. It falls off the end of the dispatch chain, and the test is here
    // so that adding a branch later cannot silently start accepting one
    // unsigned.
    void anUnknownTypeIsIgnored_data()
    {
        QTest::addColumn<QString>("type");
        QTest::newRow("invented") << QStringLiteral("sudo_make_me_a_sandwich");
        QTest::newRow("near miss") << QStringLiteral("presence ");
        QTest::newRow("case wrong") << QStringLiteral("Presence");
        QTest::newRow("sticker, declared but unhandled") << QString(protocol::kMsgSticker);
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
        // Correctly signed, so that a packet getting through would be the
        // dispatch chain's doing and not a failure to authenticate.
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o)));

        QCOMPARE(messages.count(), 0);
        QCOMPARE(typing.count(), 0);
        QCOMPARE(online.count(), 0);
        QCOMPARE(h.net.peers().size(), peersBefore);
    }

    // Presence is the packet that carries the handshake, so it is the one thing
    // that may arrive unauthenticated. Nothing else may.
    void onlyPresencePassesBeforeASession()
    {
        Harness h;
        QSignalSpy messages(&h.net, &NetworkManager::message);
        QSignalSpy online(&h.net, &NetworkManager::userOnline);
        QSignalSpy errors(&h.net, &NetworkManager::errorOccurred);

        for (const QLatin1StringView type : { protocol::kMsgChat, protocol::kMsgPrivate,
                                              protocol::kMsgGroup, protocol::kMsgFileMeta,
                                              protocol::kMsgFileData, protocol::kMsgTyping,
                                              protocol::kMsgCallReq, protocol::kMsgCallAccept,
                                              protocol::kMsgCallEnd, protocol::kMsgGroupInv,
                                              protocol::kMsgReaction, protocol::kMsgEdit,
                                              protocol::kMsgDelete, protocol::kMsgRead }) {
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

        // The one exception, and it is what gets the session going.
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

        // An empty one is no better than a missing one.
        QJsonObject emptySig = bare;
        emptySig[QStringLiteral("nonce")] = freshNonce();
        emptySig[QStringLiteral("_sig")] = QString();
        h.net.handleDatagram(kPeerIp, toDatagram(emptySig));
        QCOMPARE(messages.count(), 0);

        // Nor is a wrong one, of any length.
        for (const QString &sig : { QStringLiteral("AAAA"),
                                    QString::fromLatin1(QByteArray(32, '\0').toBase64()),
                                    QString::fromLatin1(QByteArray(32, 'x').toBase64()),
                                    QStringLiteral("!!! not base64 !!!") }) {
            QJsonObject badSig = bare;
            badSig[QStringLiteral("nonce")] = freshNonce();
            badSig[QStringLiteral("_sig")] = sig;
            h.net.handleDatagram(kPeerIp, toDatagram(badSig));
            QCOMPARE(messages.count(), 0);
        }

        // A signature over different bytes than the ones that arrived.
        QJsonObject tampered = signedPacket(h.peer, bare);
        tampered[QStringLiteral("text")] = QStringLiteral("transfer 1000 to me");
        h.net.handleDatagram(kPeerIp, toDatagram(tampered));
        QCOMPARE(messages.count(), 0);
    }

    // The counterpart: a correctly signed packet has to get through, and its
    // body has to come out of the session key. If this fails, the fail-closed
    // policy above is fail-always and the application does not work at all.
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
    // serialise/parse round trip unchanged - a fractional timestamp included,
    // since that is what sendUdp() puts in every packet.
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

    // The regression this exists for: presence carries no signature, because a
    // broadcast has no single peer to sign it for. Requiring one once a session
    // existed meant the peer went stale 25 seconds after the handshake and the
    // session key kept it away permanently.
    void presenceKeepsFlowingAfterASession()
    {
        Harness h;
        QVERIFY(h.establishSession());
        QVERIFY(h.net.peers().contains(kPeerIp));

        QSignalSpy errors(&h.net, &NetworkManager::errorOccurred);
        const double firstSeen = h.net.peers().value(kPeerIp)
                                     .value(QStringLiteral("last_seen")).toDouble();
        QVERIFY(firstSeen > 0.0);

        // Same peer, same identity, a later broadcast. No _sig, because the
        // application never puts one on a presence packet.
        const QJsonObject again = presenceFrom(h.peer, QStringLiteral("peer-renamed"));
        QVERIFY(!again.contains(QStringLiteral("_sig")));
        h.net.handleDatagram(kPeerIp, toDatagram(again));

        QVERIFY2(errors.isEmpty(),
                 "presence from an established peer was reported as unauthenticated");
        QCOMPARE(h.net.peers().value(kPeerIp).value(QStringLiteral("username")).toString(),
                 QStringLiteral("peer-renamed"));
        QVERIFY(h.net.peers().value(kPeerIp).value(QStringLiteral("e2e")).toBool());
    }

    void aReplayedPresencePacketIsRefused()
    {
        Harness h;
        QSignalSpy online(&h.net, &NetworkManager::userOnline);

        const QJsonObject first = presenceFrom(h.peer, QStringLiteral("original"));
        h.net.handleDatagram(kPeerIp, toDatagram(first));
        QCOMPARE(online.count(), 1);
        QCOMPARE(h.net.peers().value(kPeerIp).value(QStringLiteral("username")).toString(),
                 QStringLiteral("original"));

        // Same nonce, edited contents - which is exactly what a captured packet
        // being resent with a tweak looks like.
        QJsonObject replay = first;
        replay[QStringLiteral("username")] = QStringLiteral("attacker");
        h.net.handleDatagram(kPeerIp, toDatagram(replay));

        QCOMPARE(online.count(), 1);
        QCOMPARE(h.net.peers().value(kPeerIp).value(QStringLiteral("username")).toString(),
                 QStringLiteral("original"));
    }

    // Signed packets used to get no replay check at all: the nonce and the
    // timestamp are inside the signature, so resending the captured bytes
    // verifies exactly as well as the original did.
    void aReplayedSignedPacketIsRefused()
    {
        Harness h;
        QVERIFY(h.establishSession());

        QSignalSpy messages(&h.net, &NetworkManager::message);
        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgChat;
        o[QStringLiteral("text")] = h.peer.encrypt(QStringLiteral("pay the invoice"),
                                                   QString(), kSelfLabel);
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
        o[QStringLiteral("text")] = h.peer.encrypt(QStringLiteral("on time?"),
                                                   QString(), kSelfLabel);
        h.net.handleDatagram(kPeerIp, toDatagram(signedPacket(h.peer, o, nowEpoch() + offset)));

        QCOMPARE(messages.count(), accepted ? 1 : 0);
    }

    // Fields the peer fills in and a handler then passes on. None of them is
    // interpreted here, and that is the property worth pinning down: the chat
    // id becomes a filename in HistoryManager::filePathFor(), which sanitises
    // it there, and nothing in this layer may do so half way.
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
        // Verbatim, or something along the way decided to reinterpret it.
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
        QTest::newRow("object") << QJsonValue(QJsonObject{ { QStringLiteral("a"), 1 } });
        QTest::newRow("array") << QJsonValue(QJsonArray{ 1, 2, 3 });
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

        // This layer hands the field on untouched; nothing here narrows it to an
        // integer, which is where a value the integer cannot hold would stop
        // being a large number and start being undefined behaviour.
        QCOMPARE(messages.count(), 1);
    }

    // all_ips drives the alternate-address fan-out in sendPrivate(), so a peer
    // gets to choose how long that list is.
    void ahostileAllIpsListIsSurvivable_data()
    {
        QTest::addColumn<QJsonValue>("allIps");
        QTest::newRow("not an array") << QJsonValue(QStringLiteral("192.0.2.1"));
        QTest::newRow("empty array") << QJsonValue(QJsonArray());
        QTest::newRow("nested") << QJsonValue(QJsonArray{ QJsonArray{ QStringLiteral("a") } });
        QTest::newRow("junk entries")
            << QJsonValue(QJsonArray{ QStringLiteral("not an ip"), 42, QJsonValue::Null,
                                      QStringLiteral("") });

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
        // Recorded against the peer and nowhere else: one presence packet may
        // never create entries for the addresses it lists.
        QCOMPARE(h.net.peers().size(), 1);
        QVERIFY(h.net.peers().contains(kPeerIp));
    }

    // Our own broadcast comes back to us on every interface we sent it on, and
    // a peer claiming one of our addresses gets the same treatment.
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

        // And a presence packet claiming to be us is not a peer.
        QJsonObject presence = presenceFrom(h.peer);
        presence[QStringLiteral("ip")] = h.net.hostIp();
        h.net.handleDatagram(kPeerIp, toDatagram(presence));
        QVERIFY(h.net.peers().isEmpty());
    }

    // A different identity arriving for an address we have already pinned is
    // refused by CryptoManager, and the peer entry must not be taken over by it
    // either.
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
        // The peer record is what the interface shows next to that warning, so
        // the refused packet may not have rewritten it either.
        QCOMPARE(h.net.peers().value(kPeerIp).value(QStringLiteral("username")).toString(),
                 QStringLiteral("peer"));
        QCOMPARE(h.net.peers().value(kPeerIp).value(QStringLiteral("id_pub")).toString(),
                 h.peer.handshakePayload().value(QStringLiteral("id_pub")).toString());

        // The session key still belongs to the peer we handshook with, so the
        // impostor cannot sign anything we will accept.
        QSignalSpy messages(&h.net, &NetworkManager::message);
        QJsonObject o;
        o[QStringLiteral("type")] = protocol::kMsgChat;
        o[QStringLiteral("text")] = QStringLiteral("it is me, your peer");
        QJsonObject signedByImpostor = o;
        signedByImpostor[QStringLiteral("nonce")] = freshNonce();
        signedByImpostor[QStringLiteral("ts")] = nowEpoch();
        signedByImpostor[QStringLiteral("_sig")] =
            impostor.signPacket(kSelfLabel, signableBytes(signedByImpostor));
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
        const QString delivered = messages.at(0).at(0).toJsonObject()
                                      .value(QStringLiteral("text")).toString();
        QVERIFY2(delivered != QStringLiteral("meet me at seven"),
                 "an unencrypted body was passed through on a channel with a session key");
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
};

// Test mode so CryptoManager's config lookups stay in a scratch directory, and
// no sockets are bound anywhere in here - QCoreApplication is only needed for
// the timers the two classes own.
int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    // Without a domain ki18n warns on every single string, which buries
    // the actual test output.
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("koutnet"));
    QCoreApplication::setOrganizationName(QStringLiteral("koutnet-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("network-manager"));

    NetworkManagerTest tc;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tc, argc, argv);
}
#include "NetworkManagerTest.moc"
