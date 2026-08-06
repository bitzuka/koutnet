// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

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
const QString kIpA = QStringLiteral("192.0.2.1");
const QString kIpB = QStringLiteral("192.0.2.2");
const QString kIpC = QStringLiteral("192.0.2.3");

bool pairUp(CryptoManager &a, CryptoManager &b)
{
    const bool aOk = a.processHandshake(kIpB, b.handshakePayload());
    const bool bOk = b.processHandshake(kIpA, a.handshakePayload());
    return aOk && bOk;
}

// processHandshake() refuses any payload whose dh_pub is not signed by the id_pub
// beside it, so offering a chosen DH key needs an Ed25519 key of our own.
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
    // First, because everything under it makes keys and this is the promise that
    // none of them reach the user's keyring. These suites used to fill the real
    // KWallet with identity_priv_b64_peer-a and its friends, and the unscoped
    // cases wrote over the developer's own entries with test keys.
    void secretsStayOutOfTheRealWallet()
    {
        QVERIFY2(koutnet::SecretStore::isInMemoryOnly(),
                 "SecretStore is talking to KWallet; this run would write test keys "
                 "into the session keyring and leave them there");
        QVERIFY(koutnet::SecretStore::isAvailable());

        const QString key = QStringLiteral("wallet_isolation_probe");
        QVERIFY(koutnet::SecretStore::write(key, QStringLiteral("value")));
        QString readBack;
        QVERIFY(koutnet::SecretStore::read(key, &readBack));
        QCOMPARE(readBack, QStringLiteral("value"));
        QVERIFY(koutnet::SecretStore::remove(key));
        QVERIFY2(!koutnet::SecretStore::read(key, &readBack), "a removed secret was still readable");
    }

    void keysLoad()
    {
        CryptoManager crypto;
        QVERIFY2(crypto.isValid(),
                 "no keypair, every other test would pass "
                 "trivially because the code falls through "
                 "to plaintext");
        QVERIFY(!crypto.fingerprint().isEmpty());
    }

    // Guards a real bug: the migration filled the wallet and called remove() on
    // the config file, and nobody checked the removal reached the disk. The keys
    // are seeded in the pre-wallet form (@ByteArray(...)) the deletion must cope
    // with.
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
        QVERIFY2(contents.contains(QByteArrayLiteral("tester")), contents.constData());

        // Runs on every start, so an already clean file has to count as success.
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

    // The point of AES-GCM: a flipped bit has to be refused outright rather than
    // decrypted into something that merely looks wrong. The flip goes into the
    // decoded bytes, not the base64 text, whose spare bits can absorb an edit.
    void tamperedCipherTextFails()
    {
        CryptoManager crypto;
        const QString plain = QStringLiteral("balance: 100");
        const QString sealed = crypto.encrypt(plain, kPass);
        const QString marker = QStringLiteral("KNC1:");
        QVERIFY(sealed.startsWith(marker));

        const QByteArray body = QByteArray::fromBase64(sealed.mid(marker.length()).toLatin1());
        QVERIFY(body.size() > 64);

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

    void replayRejectsRepeats()
    {
        CryptoManager crypto;
        const double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
        const QString nonce = QStringLiteral("deadbeef");
        QVERIFY(crypto.checkReplay(kPeer, nonce, now));
        QVERIFY(!crypto.checkReplay(kPeer, nonce, now));
    }

    // Off by one here is invisible in normal use and fatal under attack.
    void replayWindowEdges()
    {
        CryptoManager crypto;
        const double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
        const double window = CryptoManager::kReplayWindowSec;

        QVERIFY2(crypto.checkReplay(kPeer, QStringLiteral("n1"), now - window + 2.0), "a packet inside the window was refused");
        QVERIFY2(!crypto.checkReplay(kPeer, QStringLiteral("n2"), now - window - 2.0), "a packet older than the window was accepted");
        QVERIFY2(!crypto.checkReplay(kPeer, QStringLiteral("n3"), now + window + 2.0), "a packet from the future was accepted");
    }

    // The replay cache is keyed on the source address and grew without limit,
    // from one peer outrunning the TTL or a flood of spoofed addresses. Asserting
    // that recent nonces still fail proves the cap was not a wholesale cache
    // clear.
    void aNonceFloodDoesNotDefeatTheReplayGuard()
    {
        CryptoManager crypto;
        const double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
        const QString peer = QStringLiteral("203.0.113.5");

        const int flood = CryptoManager::kMaxNoncesPerPeer * 2 + 100;
        for (int i = 0; i < flood; ++i)
            crypto.checkReplay(peer, QStringLiteral("flood-%1").arg(i), now);

        for (int i = flood - 8; i < flood; ++i) {
            QVERIFY2(!crypto.checkReplay(peer, QStringLiteral("flood-%1").arg(i), now),
                     "a nonce from the end of the flood was forgotten, so replaying "
                     "a just-captured packet would work");
        }

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

    // Two managers in one process would otherwise load the same keypair from the
    // wallet - or the same plaintext leftovers - and end up as the same peer, a
    // test that passes proving nothing. The storage scope separates their
    // entries.
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
        QCOMPARE(a.peerFingerprint(kIpB), b.fingerprint());
        QCOMPARE(b.peerFingerprint(kIpA), a.fingerprint());
        QCOMPARE(a.securityLevel(kIpB, false, false), koutnet::SecurityLevel::E2E);
    }

    // ECDH failing quietly would leave the two sides with different keys, and
    // this is the only place that would notice.
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

        const QByteArray frame = QByteArrayLiteral("\x01\x02\x03rawpcm");
        const QByteArray sealedFrame = a.encryptBytes(kIpB, frame);
        QVERIFY(!sealedFrame.isEmpty());
        QByteArray out;
        QVERIFY(b.decryptBytes(kIpA, sealedFrame, &out));
        QCOMPARE(out, frame);
    }

    // This used to QSKIP, which reads like a pass in the ctest output.
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

        CryptoManager c(QStringLiteral("peer-c"));
        QVERIFY(c.processHandshake(kIpA, a.handshakePayload()));
        const QString foreignSig = c.signPacket(kIpA, payload);
        QVERIFY(!foreignSig.isEmpty());
        QVERIFY2(foreignSig != sig, "two different peers derived the same session key with A");
        QVERIFY2(!b.verifyPacket(kIpA, payload, foreignSig), "a third party's signature was accepted as the peer's");
    }

    // Trust on first use. Presence is unauthenticated, so without the pin any
    // host that can claim a known peer's address takes the session over silently.
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

        QVERIFY(a.hasSession(kIpB));
        QCOMPARE(a.peerFingerprint(kIpB), pinned);
        const QString text = QStringLiteral("still talking to the same peer");
        QCOMPARE(b.decrypt(a.encrypt(text, QString(), kIpB), QString(), kIpA), text);

        QVERIFY(!a.processHandshake(kIpB, impostor.handshakePayload()));
        QCOMPARE(spy.count(), 1);

        QVERIFY(a.processHandshake(kIpB, b.handshakePayload()));
    }

    // The session belongs to the identity key, not the address. Before this, a
    // peer sending from a second interface was a stranger and its traffic was
    // dropped.
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

        QVERIFY(a.identityForAddress(kIpC).isEmpty());
        QVERIFY(!a.verifyPacket(kIpC, payload, sig));

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

        // The same captured nonce from the peer's other interface used to land in
        // a fresh bucket, which was one free replay per address.
        QVERIFY(a.processHandshake(kIpC, b.handshakePayload()));
        QVERIFY2(!a.checkReplay(kIpC, QStringLiteral("n1"), now), "a captured packet was accepted again from another address");
    }

    // The identity key is public - it ships in every presence packet - so anyone
    // can put a peer's id_pub in a handshake. What they cannot do is sign the DH
    // key.
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
    // live, because that slot is what the interface shows the user.
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

    // The all-zero X25519 public key is a small-subgroup point: the shared secret
    // is all zeroes on every machine, so the session key is a constant an
    // attacker knows. Its packet is otherwise well formed - only the derive step
    // refuses.
    void handshakeRefusesDegenerateDhKeys_data()
    {
        QTest::addColumn<QByteArray>("dhPub");
        QTest::newRow("u = 0") << QByteArray(32, '\0');
        QTest::newRow("u = 1") << QByteArray::fromHex("0100000000000000000000000000000000000000000000000000000000000000");
        QTest::newRow("order 8") << QByteArray::fromHex("e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b800");
        QTest::newRow("u = p - 1") << QByteArray::fromHex("ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f");
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
        QVERIFY(!payload.value(QStringLiteral("dh_pub_sig")).toString().isEmpty());

        QVERIFY2(!crypto.processHandshake(kIpB, payload), "a degenerate DH public key produced a session");
        QVERIFY(!crypto.hasSession(kIpB));
    }
};

// QTEST_MAIN would run against the real user configuration, where CryptoManager
// keeps its keypair; test mode moves QSettings into a scratch directory.
int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    // Test mode does not move KWallet - there is one per session - so this is
    // what keeps a run of these tests out of the user's real keyring. Said out
    // loud rather than left to the default, because the whole suite constructs
    // CryptoManagers whose keys would otherwise be filed next to the real ones.
    koutnet::SecretStore::setInMemoryOnly(true);
    QCoreApplication app(argc, argv);
    // Without a domain ki18n warns on every string, burying the test output.
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("koutnet"));
    QCoreApplication::setOrganizationName(QStringLiteral("koutnet-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("crypto-manager"));
    CryptoManagerTest tc;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tc, argc, argv);
}
#include "CryptoManagerTest.moc"
