// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// The Matrix sign-in helpers, tested without a homeserver and without
// libQuotient: the SSO redirect URL is a pure string build, and the recovery-key
// shape is a pure check. These are the parts that break a sign-in when they are
// wrong and the parts a server round trip cannot cover.
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

#include "../matrix/MatrixLoginUtils.h"

using namespace koutnet::matrix;

class MatrixLoginTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void ssoRedirectUrlTargetsTheHomeserverEndpoint();
    void ssoRedirectUrlKeepsTheRedirectEncoded();
    void ssoRedirectUrlReplacesAnyExistingPath();

    void recoveryKeyShape_data();
    void recoveryKeyShape();
};

void MatrixLoginTest::ssoRedirectUrlTargetsTheHomeserverEndpoint()
{
    const QUrl url = ssoRedirectUrl(QUrl(QStringLiteral("https://matrix.example.org")), QUrl(QStringLiteral("koutnet://sso")));
    QCOMPARE(url.scheme(), QStringLiteral("https"));
    QCOMPARE(url.host(), QStringLiteral("matrix.example.org"));
    QVERIFY(url.path().endsWith(QStringLiteral("/_matrix/client/r0/login/sso/redirect")));
}

void MatrixLoginTest::ssoRedirectUrlKeepsTheRedirectEncoded()
{
    const QUrl url = ssoRedirectUrl(QUrl(QStringLiteral("https://matrix.example.org")), QUrl(QStringLiteral("koutnet://sso")));
    const QUrlQuery query(url);
    // The redirect is percent-encoded into the query, so the colon and slashes of
    // the custom scheme do not break the URL the browser is sent to.
    QCOMPARE(query.queryItemValue(QStringLiteral("redirectUrl")), QStringLiteral("koutnet://sso"));
}

void MatrixLoginTest::ssoRedirectUrlReplacesAnyExistingPath()
{
    // A homeserver the user typed with a path of its own still lands on the
    // redirect endpoint, not on whatever it was already serving.
    const QUrl url = ssoRedirectUrl(QUrl(QStringLiteral("https://matrix.example.org/_matrix/client/")), QUrl(QStringLiteral("koutnet://sso")));
    QCOMPARE(url.path(), QStringLiteral("/_matrix/client/r0/login/sso/redirect"));
}

void MatrixLoginTest::recoveryKeyShape_data()
{
    QTest::addColumn<QString>("candidate");
    QTest::addColumn<bool>("expected");

    // Twelve groups of four base58 characters, dashes between: the only form a
    // Megolm recovery key takes.
    QTest::newRow("twelve base58 groups") << QStringLiteral("AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA") << true;
    QTest::newRow("mixed base58 groups") << QStringLiteral("Wxyz-1234-bcde-fghj-kmnp-qrst-uvwx-yzAB-CDEF-GHJk-LMNP-PQRS") << true;

    // Anything else is a passphrase, or not a key at all.
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("a passphrase") << QStringLiteral("correct horse battery staple") << false;
    QTest::newRow("too few groups") << QStringLiteral("AAAA-AAAA-AAAA") << false;
    QTest::newRow("group too short") << QStringLiteral("AAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA") << false;
    QTest::newRow("group too long") << QStringLiteral("AAAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA") << false;
    // base58 excludes the four characters a human confuses: 0, O, I, l.
    QTest::newRow("ambiguous characters") << QStringLiteral("IIII-0000-OOOO-llll-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA") << false;
}

void MatrixLoginTest::recoveryKeyShape()
{
    QFETCH(QString, candidate);
    QFETCH(bool, expected);
    QCOMPARE(looksLikeRecoveryKey(candidate), expected);
}

QTEST_GUILESS_MAIN(MatrixLoginTest)

#include "MatrixLoginTest.moc"
