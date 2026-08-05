// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// Tests for the parts of CryptoManager that only misbehave under attack.
// A passphrase round trip is easy to check by hand; a replay window off by
// one second, or a tag that is not actually verified, is not.

#include <KLocalizedString>
#include <QDateTime>
#include <QFile>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include <openssl/evp.h>

#include "../core/security/CryptoManager.h"
#include "../core/security/SecretStore.h"

using koutnet::CryptoManager;

namespace
{
const QString kPass = QStringLiteral("correct horse battery staple");
const QString kPeer = QStringLiteral("192.0.2.10");
// The two addresses each side of a session believes the other one lives at.
const QString kIpA = QStringLiteral("192.0.2.1");
const QString kIpB = QStringLiteral("192.0.2.2");
// A third address, for the peer at kIpB turning up somewhere else - a VPN
// coming up, which is one interface more and the same person.
const QString kIpC = QStringLiteral("192.0.2.3");

// Two managers exchanging their real handshake payloads, which is what the
// presence packet carries between two running instances.
bool pairUp(CryptoManager &a, CryptoManager &b)
{
    const bool aOk = a.processHandshake(kIpB, b.handshakePayload());
    const bool bOk = b.processHandshake(kIpA, a.handshakePayload());
    return aOk && bOk;
}

// A handshake payload assembled by hand. processHandshake() refuses anything
// whose dh_pub is not signed by the id_pub in the same packet, so putting a
// chosen DH key in front of it - a garbage one, or the all-zero point - takes
// an Ed25519 key of our own to sign with.
class ForgedPeer
{
public:
    ~ForgedPeer()
    {
        if (m_priv)
            EVP_PKEY_free(m_priv);
    }

    bool generate()
    {
        EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
        if (!ctx)
            return false;
        const bool ok = EVP_PKEY_keygen_init(ctx) == 1 && EVP_PKEY_keygen(ctx, &m_priv) == 1;
        EVP_PKEY_CTX_free(ctx);
        if (!ok)
            return false;

        size_t len = 0;
        if (EVP_PKEY_get_raw_public_key(m_priv, nullptr, &len) != 1 || len == 0)
            return false;
        m_pub.resize(int(len));
        return EVP_PKEY_get_raw_public_key(m_priv, reinterpret_cast<unsigned char *>(m_pub.data()), &len) == 1;
    }

    QByteArray sign(const QByteArray &data) const
    {
        EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
        if (!mdctx)
            return {};
        QByteArray sig;
        size_t sigLen = 0;
        bool ok = EVP_DigestSignInit(mdctx, nullptr, nullptr, nullptr, m_priv) == 1
            && EVP_DigestSign(mdctx, nullptr, &sigLen, reinterpret_cast<const unsigned char *>(data.constData()), data.size()) == 1;
        if (ok) {
            sig.resize(int(sigLen));
            ok = EVP_DigestSign(mdctx,
                                reinterpret_cast<unsigned char *>(sig.data()),
                                &sigLen,
                                reinterpret_cast<const unsigned char *>(data.constData()),
                                data.size())
                == 1;
        }
        EVP_MD_CTX_free(mdctx);
        return ok ? sig.left(int(sigLen)) : QByteArray();
    }

    // A well-formed packet advertising whatever DH key it is handed.
    QJsonObject payloadFor(const QByteArray &dhPub) const
    {
        QJsonObject o;
        o[QStringLiteral("dh_pub")] = QString::fromLatin1(dhPub.toBase64());
        o[QStringLiteral("id_pub")] = QString::fromLatin1(m_pub.toBase64());
        o[QStringLiteral("dh_pub_sig")] = QString::fromLatin1(sign(dhPub).toBase64());
        return o;
    }

    QByteArray publicKey() const
    {
        return m_pub;
    }

private:
    EVP_PKEY *m_priv = nullptr;
    QByteArray m_pub;
};
}

class CryptoManagerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void keysLoad()
    {
        CryptoManager crypto;
        QVERIFY2(crypto.isValid(),
                 "no keypair, every other test would pass "
                 "trivially because the code falls through "
                 "to plaintext");
        QVERIFY(!crypto.fingerprint().isEmpty());
    }

    // The bug this guards against: the migration filled the wallet, called
    // remove() on the config file, and nobody checked that the removal reached
    // the disk. The keys are seeded the way the pre-wallet build wrote them -
    // setValue() of a QByteArray, so the file reads @ByteArray(...) - because
    // that is the form the deletion has to cope with.
    void plaintextKeysLeaveTheConfigFile()
    {
        const QStringList keys = {QStringLiteral("security/identity_priv_b64"), QStringLiteral("security/dh_priv_b64")};
        QString path;
        {
            QSettings settings;
            path = settings.fileName();
            settings.setValue(keys.at(0), QByteArrayLiteral("aWRlbnRpdHk="));
            settings.setValue(keys.at(1), QByteArrayLiteral("ZGlmZmllaGVsbG1hbg=="));
            settings.setValue(QStringLiteral("app/username"), QStringLiteral("tester"));
            settings.sync();
        }

        QString detail;
        QVERIFY2(koutnet::SecretStore::purgePlaintextConfigKeys(keys, &detail), qUtf8Printable(detail));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray contents = file.readAll();
        QVERIFY2(!contents.contains(QByteArrayLiteral("identity_priv_b64")), contents.constData());
        QVERIFY2(!contents.contains(QByteArrayLiteral("dh_priv_b64")), contents.constData());
        // everything else in the file has to survive the deletion
        QVERIFY2(contents.contains(QByteArrayLiteral("tester")), contents.constData());

        // Runs on every start, so an already clean file is a success, not a
        // warning the user would learn to ignore.
        QVERIFY(koutnet::SecretStore::purgePlaintextConfigKeys(keys, &detail));
    }

    void roundTrip_data()
    {
        QTest::addColumn<QString>("plain");
        QTest::newRow("ascii") << QStringLiteral("hello");
        QTest::newRow("empty") << QString();
        QTest::newRow("cyrillic") << QStringLiteral("привет, как дела");
        QTest::newRow("emoji") << QStringLiteral("ok \xF0\x9F\x91\x8D");
        QTest::newRow("long") << QString(200000, QLatin1Char('x'));
    }

    void roundTrip()
    {
        QFETCH(QString, plain);
        CryptoManager crypto;
        const QString sealed = crypto.encrypt(plain, kPass);
        QCOMPARE(crypto.decrypt(sealed, kPass), plain);
    }

    void cipherTextIsNotThePlainText()
    {
        CryptoManager crypto;
        const QString plain = QStringLiteral("meet me at seven");
        QVERIFY(!crypto.encrypt(plain, kPass).contains(plain));
    }

    void wrongPassphraseFails()
    {
        CryptoManager crypto;
        const QString plain = QStringLiteral("meet me at seven");
        const QString sealed = crypto.encrypt(plain, kPass);
        QVERIFY(crypto.decrypt(sealed, QStringLiteral("wrong")) != plain);
    }

    // The point of AES-GCM. A flipped bit has to be refused outright rather
    // than decrypted into something that merely looks wrong.
    //
    // The flip goes into the decoded bytes, not into the base64 text. Base64
    // carries spare bits in places, so editing a character there can leave
    // the bytes untouched and the test would then be measuring nothing.
    void tamperedCipherTextFails()
    {
        CryptoManager crypto;
        const QString plain = QStringLiteral("balance: 100");
        const QString sealed = crypto.encrypt(plain, kPass);
        const QString marker = QStringLiteral("KNC1:");
        QVERIFY(sealed.startsWith(marker));

        const QByteArray body = QByteArray::fromBase64(sealed.mid(marker.length()).toLatin1());
        QVERIFY(body.size() > 64);

        // Salt, nonce and tag all live in there, so hit each region once.
        for (qsizetype at : {qsizetype(2), body.size() / 2, body.size() - 2}) {
            QByteArray broken = body;
            broken[at] = char(broken.at(at) ^ 0x01);
            QCOMPARE(broken.size(), body.size());
            QVERIFY(broken != body);

            const QString rebuilt = marker + QString::fromLatin1(broken.toBase64());
            QVERIFY2(crypto.decrypt(rebuilt, kPass) != plain, qPrintable(QStringLiteral("a flipped bit at byte %1 was accepted").arg(at)));
        }
    }

    void garbageDoesNotCrash_data()
    {
        QTest::addColumn<QString>("junk");
        QTest::newRow("empty") << QString();
        QTest::newRow("plain word") << QStringLiteral("nonsense");
        QTest::newRow("bad base64") << QStringLiteral("!!!!not base64!!!!");
        QTest::newRow("short base64") << QStringLiteral("AAAA");
        QTest::newRow("nul bytes") << QStringLiteral("AA\0AA");
    }

    void garbageDoesNotCrash()
    {
        QFETCH(QString, junk);
        CryptoManager crypto;
        crypto.decrypt(junk, kPass);
        QByteArray out;
        crypto.decryptBytes(kPeer, junk.toUtf8(), &out);
    }

    // A nonce may be used once. The second sighting is an attacker replaying
    // a captured packet.
    void replayRejectsRepeats()
    {
        CryptoManager crypto;
        const double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
        const QString nonce = QStringLiteral("deadbeef");
        QVERIFY(crypto.checkReplay(kPeer, nonce, now));
        QVERIFY(!crypto.checkReplay(kPeer, nonce, now));
    }

    // Off by one here is invisible in normal use and fatal under attack, so
    // both sides of the boundary get checked.
    void replayWindowEdges()
    {
        CryptoManager crypto;
        const double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
        const double window = CryptoManager::kReplayWindowSec;

        QVERIFY2(crypto.checkReplay(kPeer, QStringLiteral("n1"), now - window + 2.0), "a packet inside the window was refused");
        QVERIFY2(!crypto.checkReplay(kPeer, QStringLiteral("n2"), now - window - 2.0), "a packet older than the window was accepted");
        QVERIFY2(!crypto.checkReplay(kPeer, QStringLiteral("n3"), now + window + 2.0), "a packet from the future was accepted");
    }

    // The replay cache is keyed on the source address and grew without limit in
    // both directions: one peer sending fresh nonces faster than the TTL retires
    // them, and a flood from spoofed addresses making a bucket each. Neither
    // needs anything but a UDP socket, and neither is visible from outside
    // except as a process that keeps getting bigger - so what is asserted here
    // is that recent nonces still work after the flood, since a cap implemented
    // by clearing the cache would let every one of them be replayed.
    void aNonceFloodDoesNotDefeatTheReplayGuard()
    {
        CryptoManager crypto;
        const double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
        const QString peer = QStringLiteral("203.0.113.5");

        // Enough to force the eviction to run more than once.
        const int flood = CryptoManager::kMaxNoncesPerPeer * 2 + 100;
        for (int i = 0; i < flood; ++i)
            crypto.checkReplay(peer, QStringLiteral("flood-%1").arg(i), now);

        // The most recent ones are the ones a real replay would target, and they
        // have to still be remembered.
        for (int i = flood - 8; i < flood; ++i) {
            QVERIFY2(!crypto.checkReplay(peer, QStringLiteral("flood-%1").arg(i), now),
                     "a nonce from the end of the flood was forgotten, so replaying "
                     "a just-captured packet would work");
        }

        // A fresh one still gets through, so the guard has not simply closed.
        QVERIFY(crypto.checkReplay(peer, QStringLiteral("brand-new"), now));
    }

    void aFloodOfSourceAddressesDoesNotDefeatIt()
    {
        CryptoManager crypto;
        const double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
        const QString real = QStringLiteral("203.0.113.9");

        QVERIFY(crypto.checkReplay(real, QStringLiteral("keep-me"), now));

        for (int i = 0; i < CryptoManager::kMaxNoncePeers * 2; ++i) {
            const QString spoofed = QStringLiteral("198.18.%1.%2").arg(i / 256).arg(i % 256);
            crypto.checkReplay(spoofed, QStringLiteral("n"), now);
            crypto.checkRate(spoofed);
        }

        // Eviction is oldest first, and the address above was touched before all
        // of them - so this is the one case where the guard is expected to have
        // forgotten. What must hold is that it still works from here on.
        QVERIFY(crypto.checkReplay(real, QStringLiteral("after-the-flood"), now));
        QVERIFY(!crypto.checkReplay(real, QStringLiteral("after-the-flood"), now));
    }

    void rateLimitTrips()
    {
        CryptoManager crypto;
        int accepted = 0;
        for (int i = 0; i < 50; ++i) {
            if (crypto.checkRate(kPeer, 10))
                ++accepted;
        }
        QVERIFY2(accepted <= 12, "the rate limit let a flood through");
        QVERIFY2(accepted >= 10, "the rate limit refused traffic under the cap");
    }

    // Handshake
    // Everything below needs two managers in one process. They would otherwise
    // load the same keypair out of the wallet - or the same plaintext leftovers
    // out of the config file - and end up as the same peer, which is a test that
    // passes while proving nothing. The storage scope keeps their identities in
    // separate entries; nothing else about them differs from the application's.
    void twoManagersHaveSeparateIdentities()
    {
        CryptoManager a(QStringLiteral("peer-a"));
        CryptoManager b(QStringLiteral("peer-b"));
        QVERIFY(a.isValid());
        QVERIFY(b.isValid());
        QVERIFY2(a.fingerprint() != b.fingerprint(),
                 "both managers came up as the same identity, so no test below "
                 "is actually exercising two peers");
    }

    void handshakeGivesBothSidesASession()
    {
        CryptoManager a(QStringLiteral("peer-a"));
        CryptoManager b(QStringLiteral("peer-b"));
        QVERIFY(!a.hasSession(kIpB));
        QVERIFY(pairUp(a, b));
        QVERIFY(a.hasSession(kIpB));
        QVERIFY(b.hasSession(kIpA));
        // Each side has to have pinned the key the other one actually holds.
        QCOMPARE(a.peerFingerprint(kIpB), b.fingerprint());
        QCOMPARE(b.peerFingerprint(kIpA), a.fingerprint());
        QCOMPARE(a.securityLevel(kIpB, false, false), koutnet::SecurityLevel::E2E);
    }

    // The session is only worth having if the key both sides derived is the
    // same one. ECDH failing quietly would leave two different keys and this is
    // the only place that would notice.
    void sessionKeyIsUsableBothWays()
    {
        CryptoManager a(QStringLiteral("peer-a"));
        CryptoManager b(QStringLiteral("peer-b"));
        QVERIFY(pairUp(a, b));

        const QString outbound = QStringLiteral("the key is under the mat");
        const QString sealed = a.encrypt(outbound, QString(), kIpB);
        QVERIFY(!sealed.isEmpty());
        QVERIFY(!sealed.contains(outbound));
        QCOMPARE(b.decrypt(sealed, QString(), kIpA), outbound);

        const QString reply = QStringLiteral("no it is not");
        QCOMPARE(a.decrypt(b.encrypt(reply, QString(), kIpA), QString(), kIpB), reply);

        // The voice path uses the same key without the base64 wrapper.
        const QByteArray frame = QByteArrayLiteral("\x01\x02\x03rawpcm");
        const QByteArray sealedFrame = a.encryptBytes(kIpB, frame);
        QVERIFY(!sealedFrame.isEmpty());
        QByteArray out;
        QVERIFY(b.decryptBytes(kIpA, sealedFrame, &out));
        QCOMPARE(out, frame);
    }

    // Runs for real now that a session exists. It used to QSKIP, which reads
    // like a pass in the ctest output.
    void packetSignaturesVerifyAcrossPeers()
    {
        CryptoManager a(QStringLiteral("peer-a"));
        CryptoManager b(QStringLiteral("peer-b"));
        QVERIFY(pairUp(a, b));

        const QByteArray payload = QByteArrayLiteral("{\"text\":\"hi\",\"type\":\"chat\"}");
        const QString sig = a.signPacket(kIpB, payload);
        QVERIFY(!sig.isEmpty());
        QVERIFY2(b.verifyPacket(kIpA, payload, sig),
                 "the peer could not verify a signature made with the session key "
                 "both sides derived");

        QVERIFY2(!b.verifyPacket(kIpA, payload + QByteArrayLiteral(" "), sig), "an edited payload verified against the original signature");
        QVERIFY2(!b.verifyPacket(kIpA, QByteArrayLiteral("{}"), sig), "a swapped payload verified");
        QVERIFY2(!b.verifyPacket(kIpA, payload, QStringLiteral("AAAA")), "a truncated signature verified");
        QVERIFY2(!b.verifyPacket(QStringLiteral("198.51.100.9"), payload, sig), "a signature verified against a peer we hold no session with");

        // A third host with a session of its own holds a different key, so its
        // signature over the same bytes must not pass as the first peer's.
        CryptoManager c(QStringLiteral("peer-c"));
        QVERIFY(c.processHandshake(kIpA, a.handshakePayload()));
        const QString foreignSig = c.signPacket(kIpA, payload);
        QVERIFY(!foreignSig.isEmpty());
        QVERIFY2(foreignSig != sig, "two different peers derived the same session key with A");
        QVERIFY2(!b.verifyPacket(kIpA, payload, foreignSig), "a third party's signature was accepted as the peer's");
    }

    // Trust on first use. Presence is unauthenticated, so without the pin any
    // host that can claim a known peer's address hands us its own identity key
    // and takes the session over silently.
    void identityPinSurvivesAnImpostor()
    {
        CryptoManager a(QStringLiteral("peer-a"));
        CryptoManager b(QStringLiteral("peer-b"));
        QVERIFY(pairUp(a, b));
        const QString pinned = a.peerFingerprint(kIpB);

        QSignalSpy spy(&a, &CryptoManager::peerIdentityChanged);
        QVERIFY(spy.isValid());

        CryptoManager impostor(QStringLiteral("peer-evil"));
        QVERIFY2(!a.processHandshake(kIpB, impostor.handshakePayload()), "a second identity claiming the same address was accepted");

        QCOMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.at(0);
        QCOMPARE(args.at(0).toString(), kIpB);
        QCOMPARE(args.at(1).toString(), pinned);
        QCOMPARE(args.at(2).toString(), impostor.fingerprint());

        // The refusal must not have disturbed what was already there.
        QVERIFY(a.hasSession(kIpB));
        QCOMPARE(a.peerFingerprint(kIpB), pinned);
        const QString text = QStringLiteral("still talking to the same peer");
        QCOMPARE(b.decrypt(a.encrypt(text, QString(), kIpB), QString(), kIpA), text);

        // Presence repeats every couple of seconds, so the warning is once per
        // offending key rather than once per packet.
        QVERIFY(!a.processHandshake(kIpB, impostor.handshakePayload()));
        QCOMPARE(spy.count(), 1);

        // The real peer is still welcome.
        QVERIFY(a.processHandshake(kIpB, b.handshakePayload()));
    }

    // The session belongs to the identity key, not to the address the handshake
    // happened over. Before this, a peer that sent from a second interface was a
    // stranger, and everything it sent was dropped as unauthenticated.
    void sessionsFollowTheIdentityNotTheAddress()
    {
        CryptoManager a(QStringLiteral("peer-a"));
        CryptoManager b(QStringLiteral("peer-b"));
        QVERIFY(pairUp(a, b));

        const QString bId = a.identityForAddress(kIpB);
        QVERIFY2(!bId.isEmpty(), "the handshake left no way to look the peer up by address");
        QCOMPARE(bId, b.ownIdentityId());

        const QByteArray payload = QByteArrayLiteral("{\"text\":\"hi\",\"type\":\"chat\"}");
        const QString sig = b.signPacket(kIpA, payload);
        QVERIFY(!sig.isEmpty());
        QVERIFY2(a.verifyPacket(bId, payload, sig), "a signature could not be checked against the identity that made it");

        // An address on its own still resolves to nothing, which is the point of
        // calling it a hint: it neither grants nor denies anything by itself.
        QVERIFY(a.identityForAddress(kIpC).isEmpty());
        QVERIFY(!a.verifyPacket(kIpC, payload, sig));

        // One handshake from the new interface and the address resolves as well,
        // while the session, the pin and the old address are all untouched.
        QVERIFY(a.processHandshake(kIpC, b.handshakePayload()));
        QCOMPARE(a.identityForAddress(kIpC), bId);
        QVERIFY(a.verifyPacket(kIpC, payload, sig));
        QVERIFY(a.hasSession(kIpB));
        QCOMPARE(a.peerFingerprint(kIpB), b.fingerprint());
        const QStringList seen = a.addressesFor(bId);
        QVERIFY(seen.contains(kIpB));
        QVERIFY(seen.contains(kIpC));
    }

    void replayStateFollowsTheIdentity()
    {
        CryptoManager a(QStringLiteral("peer-a"));
        CryptoManager b(QStringLiteral("peer-b"));
        QVERIFY(pairUp(a, b));
        const double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
        const QString bId = a.identityForAddress(kIpB);

        QVERIFY(a.checkReplay(kIpB, QStringLiteral("n1"), now));
        QVERIFY2(!a.checkReplay(bId, QStringLiteral("n1"), now), "the address and the identity had separate replay buckets");

        // The same captured nonce arriving from the peer's other interface, which
        // used to be a fresh bucket and one free replay per address.
        QVERIFY(a.processHandshake(kIpC, b.handshakePayload()));
        QVERIFY2(!a.checkReplay(kIpC, QStringLiteral("n1"), now), "a captured packet was accepted again from another address");
    }

    // The impostor case at this level: the identity key is public, it goes out in
    // every presence packet, so anyone can put a peer's id_pub in a handshake.
    // What they cannot do is sign the DH key with it.
    void aforgedSignatureUnderAKnownIdentityIsRefused()
    {
        CryptoManager a(QStringLiteral("peer-a"));
        CryptoManager b(QStringLiteral("peer-b"));
        QVERIFY(pairUp(a, b));
        const QString bId = a.identityForAddress(kIpB);
        const QString pinned = a.peerFingerprint(bId);

        ForgedPeer forged;
        QVERIFY2(forged.generate(), "could not build the forged identity, the test proves nothing");

        QJsonObject fake = b.handshakePayload();
        const QByteArray dh = QByteArray::fromBase64(fake.value(QStringLiteral("dh_pub")).toString().toLatin1());
        QVERIFY(!dh.isEmpty());
        fake[QStringLiteral("dh_pub_sig")] = QString::fromLatin1(forged.sign(dh).toBase64());

        QVERIFY2(!a.processHandshake(kIpC, fake), "a handshake signed by the wrong key was accepted under a known identity");
        QCOMPARE(a.peerFingerprint(bId), pinned);
        QVERIFY(a.hasSession(kIpB));
        QVERIFY2(a.identityForAddress(kIpC).isEmpty(), "an unproven packet taught the address index something");
    }

    // An address changing hands is refused while the peer sitting there is still
    // live, because that slot is what the interface shows the user. It is not a
    // verdict on the newcomer's identity, and anywhere else it is welcome.
    void anAddressChangingHandsIsRefusedButTheNewcomerIsNot()
    {
        CryptoManager a(QStringLiteral("peer-a"));
        CryptoManager b(QStringLiteral("peer-b"));
        QVERIFY(pairUp(a, b));

        CryptoManager newcomer(QStringLiteral("peer-newcomer"));
        QString who;
        QVERIFY2(a.processHandshakeFrom(kIpB, newcomer.handshakePayload(), &who) == CryptoManager::HandshakeOutcome::AddressTaken,
                 "a stranger was let into an address a live peer is using");
        QCOMPARE(who, newcomer.ownIdentityId());
        QVERIFY(!a.hasSession(who));
        QCOMPARE(a.identityForAddress(kIpB), b.ownIdentityId());

        who.clear();
        QVERIFY2(a.processHandshakeFrom(kIpC, newcomer.handshakePayload(), &who) == CryptoManager::HandshakeOutcome::Established,
                 "the same peer was refused at an address nobody was using");
        QCOMPARE(who, newcomer.ownIdentityId());
        QVERIFY(a.hasSession(who));
        QVERIFY(a.hasSession(kIpC));
        // And the peer that was there all along is exactly where it was.
        QVERIFY(a.hasSession(kIpB));
        QCOMPARE(a.peerFingerprint(kIpB), b.fingerprint());
    }

    void handshakeRefusesJunk_data()
    {
        QTest::addColumn<QJsonObject>("payload");

        QTest::newRow("empty") << QJsonObject();

        CryptoManager donor(QStringLiteral("peer-donor"));
        const QJsonObject good = donor.handshakePayload();

        for (const QString &field : {QStringLiteral("dh_pub"), QStringLiteral("id_pub"), QStringLiteral("dh_pub_sig")}) {
            QJsonObject missing = good;
            missing.remove(field);
            QTest::newRow(qPrintable(QStringLiteral("missing %1").arg(field))) << missing;

            QJsonObject blank = good;
            blank[field] = QString();
            QTest::newRow(qPrintable(QStringLiteral("blank %1").arg(field))) << blank;

            QJsonObject junk = good;
            junk[field] = QStringLiteral("!!!! not base64 !!!!");
            QTest::newRow(qPrintable(QStringLiteral("garbage %1").arg(field))) << junk;

            QJsonObject wrongType = good;
            wrongType[field] = 42;
            QTest::newRow(qPrintable(QStringLiteral("numeric %1").arg(field))) << wrongType;
        }

        // Right length, wrong bytes: the signature no longer matches the DH key
        // in front of it, which is the case the Ed25519 check exists for.
        QJsonObject swapped = good;
        swapped[QStringLiteral("dh_pub")] = QString::fromLatin1(QByteArray(32, 0x42).toBase64());
        QTest::newRow("dh_pub not the signed one") << swapped;

        QJsonObject wrongSig = good;
        wrongSig[QStringLiteral("dh_pub_sig")] = QString::fromLatin1(QByteArray(64, 0x11).toBase64());
        QTest::newRow("signature is not a signature") << wrongSig;
    }

    void handshakeRefusesJunk()
    {
        QFETCH(QJsonObject, payload);
        CryptoManager crypto(QStringLiteral("peer-a"));
        QVERIFY(!crypto.processHandshake(kIpB, payload));
        QVERIFY(!crypto.hasSession(kIpB));
    }

    // The all-zero X25519 public key is a small-subgroup point: the shared
    // secret it produces is all zeroes, identical on every machine, so a session
    // key derived from it is a constant an attacker knows. It gets a test of its
    // own because the packet carrying it is otherwise completely well formed -
    // the signature over dh_pub verifies, and only the derive step refuses.
    void handshakeRefusesDegenerateDhKeys_data()
    {
        QTest::addColumn<QByteArray>("dhPub");
        // The four low-order points, little-endian as X25519 encodes them. Each
        // one drives the shared secret to zero regardless of our private key.
        QTest::newRow("u = 0") << QByteArray(32, '\0');
        QTest::newRow("u = 1") << QByteArray::fromHex("0100000000000000000000000000000000000000000000000000000000000000");
        QTest::newRow("order 8") << QByteArray::fromHex("e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b800");
        QTest::newRow("u = p - 1") << QByteArray::fromHex("ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f");
        // Not 32 bytes at all, so the key never gets built in the first place.
        QTest::newRow("too short") << QByteArray(16, '\0');
        QTest::newRow("too long") << QByteArray(64, '\0');
    }

    void handshakeRefusesDegenerateDhKeys()
    {
        QFETCH(QByteArray, dhPub);
        ForgedPeer forged;
        QVERIFY2(forged.generate(), "could not build the forged identity, the test proves nothing");

        CryptoManager crypto(QStringLiteral("peer-a"));
        const QJsonObject payload = forged.payloadFor(dhPub);
        // Sanity: the packet really is well formed apart from the DH key, so a
        // refusal below is the derive check and not the signature check.
        QVERIFY(!payload.value(QStringLiteral("dh_pub_sig")).toString().isEmpty());

        QVERIFY2(!crypto.processHandshake(kIpB, payload), "a degenerate DH public key produced a session");
        QVERIFY(!crypto.hasSession(kIpB));
    }
};

// QTEST_MAIN would run the tests against the real user configuration,
// where CryptoManager keeps its keypair. Test mode moves QSettings and the
// rest of QStandardPaths into a scratch directory instead.
int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    // Without a domain ki18n warns on every single string, which buries
    // the actual test output.
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("koutnet"));
    QCoreApplication::setOrganizationName(QStringLiteral("koutnet-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("crypto-manager"));
    CryptoManagerTest tc;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tc, argc, argv);
}
#include "CryptoManagerTest.moc"
