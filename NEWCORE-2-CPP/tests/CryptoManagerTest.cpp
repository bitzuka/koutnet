// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// Tests for the parts of CryptoManager that only misbehave under attack.
// A passphrase round trip is easy to check by hand; a replay window off by
// one second, or a tag that is not actually verified, is not.

#include <QTest>
#include <QDateTime>
#include <QStandardPaths>

#include "../core/security/CryptoManager.h"

using koutnet::CryptoManager;

namespace {
const QString kPass = QStringLiteral("correct horse battery staple");
const QString kPeer = QStringLiteral("192.0.2.10");
}

class CryptoManagerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void keysLoad()
    {
        CryptoManager crypto;
        QVERIFY2(crypto.isValid(), "no keypair, every other test would pass "
                                   "trivially because the code falls through "
                                   "to plaintext");
        QVERIFY(!crypto.fingerprint().isEmpty());
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
    void tamperedCipherTextFails()
    {
        CryptoManager crypto;
        const QString plain = QStringLiteral("balance: 100");
        QString sealed = crypto.encrypt(plain, kPass);
        QVERIFY(sealed.length() > 8);

        for (qsizetype at : {qsizetype(4), sealed.length() / 2, sealed.length() - 3}) {
            QString broken = sealed;
            const QChar c = broken.at(at);
            broken[at] = (c == QLatin1Char('A')) ? QLatin1Char('B') : QLatin1Char('A');
            QVERIFY2(crypto.decrypt(broken, kPass) != plain,
                     qPrintable(QStringLiteral("tampering at %1 went through").arg(at)));
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

        QVERIFY2(crypto.checkReplay(kPeer, QStringLiteral("n1"), now - window + 2.0),
                 "a packet inside the window was refused");
        QVERIFY2(!crypto.checkReplay(kPeer, QStringLiteral("n2"), now - window - 2.0),
                 "a packet older than the window was accepted");
        QVERIFY2(!crypto.checkReplay(kPeer, QStringLiteral("n3"), now + window + 2.0),
                 "a packet from the future was accepted");
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

    void signatureCatchesEditedPayload()
    {
        CryptoManager crypto;
        const QByteArray payload = QByteArrayLiteral("{\"type\":\"chat\"}");
        const QString sig = crypto.signPacket(kPeer, payload);
        if (sig.isEmpty())
            QSKIP("no session with this peer, signing is not available");
        QVERIFY(crypto.verifyPacket(kPeer, payload, sig));
        QVERIFY(!crypto.verifyPacket(kPeer, payload + QByteArrayLiteral(" "), sig));
    }
};

// QTEST_MAIN would run the tests against the real user configuration,
// where CryptoManager keeps its keypair. Test mode moves QSettings and the
// rest of QStandardPaths into a scratch directory instead.
int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("koutnet-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("crypto-manager"));
    CryptoManagerTest tc;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tc, argc, argv);
}
#include "CryptoManagerTest.moc"
