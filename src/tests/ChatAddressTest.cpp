// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// The chat-id prefix table, which is the whole of the routing between the
// conversation list and the backends. Every transport's prefix is round-tripped
// here, including Telegram and Rocket.Chat whose helpers were merged into the
// table without a test until now - a wrong prefix files a conversation under the
// wrong backend and the logs never read back.
#include <QTest>

#include "../core/chat/ChatAddress.h"

using namespace koutnet::chatid;

class ChatAddressTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void matrixRoundTrip();
    void telegramRoundTrip();
    void rocketChatRoundTrip();

    void transportOfRoutesByPrefix();
    void transportNameNamesEveryTransport();

    void emptyInputYieldsEmpty();
    void crossTransportKeysAreNotConfused();
    void reservedAndLanAreNotTransports();
};

void ChatAddressTest::matrixRoundTrip()
{
    const QString room = QStringLiteral("!abc:matrix.org");
    const QString chat = matrixChatId(room);
    QCOMPARE(chat, QStringLiteral("mx:!abc:matrix.org"));
    QCOMPARE(matrixRoomId(chat), room);
    QCOMPARE(matrixRoomId(chat), matrixRoomId(matrixChatId(chat)));
    // Idempotent: a caller that prefixes twice must not produce "mx:mx:".
    QCOMPARE(matrixChatId(chat), chat);
}

void ChatAddressTest::telegramRoundTrip()
{
    const QString key = QStringLiteral("-100123456:bot");
    const QString chat = telegramChatId(key);
    QCOMPARE(chat, QStringLiteral("tg:-100123456:bot"));
    QCOMPARE(telegramChatKey(chat), key);
    QCOMPARE(telegramChatKey(chat), telegramChatKey(telegramChatId(key)));
    QCOMPARE(telegramChatId(chat), chat);
}

void ChatAddressTest::rocketChatRoundTrip()
{
    const QString channel = QStringLiteral("general");
    const QString chat = rocketChatId(channel);
    QCOMPARE(chat, QStringLiteral("rc:general"));
    QCOMPARE(rocketChannelId(chat), channel);
    QCOMPARE(rocketChannelId(chat), rocketChannelId(rocketChatId(channel)));
    QCOMPARE(rocketChatId(chat), chat);
}

void ChatAddressTest::transportOfRoutesByPrefix()
{
    QCOMPARE(transportOf(QStringLiteral("mx:!a:b.c")), Transport::Matrix);
    QCOMPARE(transportOf(QStringLiteral("tg:-1:bot")), Transport::Telegram);
    QCOMPARE(transportOf(QStringLiteral("rc:general")), Transport::RocketChat);
    QCOMPARE(transportOf(QStringLiteral("__self__")), Transport::Reserved);
    QCOMPARE(transportOf(QStringLiteral("192.168.1.5")), Transport::Lan);
    QCOMPARE(transportOf(QStringLiteral("fe80::1")), Transport::Lan);
}

void ChatAddressTest::transportNameNamesEveryTransport()
{
    QCOMPARE(transportName(QStringLiteral("mx:!a:b.c")), QStringLiteral("matrix"));
    QCOMPARE(transportName(QStringLiteral("tg:-1:bot")), QStringLiteral("telegram"));
    QCOMPARE(transportName(QStringLiteral("rc:general")), QStringLiteral("rocket.chat"));
    QCOMPARE(transportName(QStringLiteral("__self__")), QStringLiteral("reserved"));
    QCOMPARE(transportName(QStringLiteral("10.0.0.1")), QStringLiteral("lan"));
}

void ChatAddressTest::emptyInputYieldsEmpty()
{
    QVERIFY(matrixChatId(QString()).isEmpty());
    QVERIFY(matrixRoomId(QString()).isEmpty());
    QVERIFY(telegramChatId(QString()).isEmpty());
    QVERIFY(telegramChatKey(QString()).isEmpty());
    QVERIFY(rocketChatId(QString()).isEmpty());
    QVERIFY(rocketChannelId(QString()).isEmpty());
}

void ChatAddressTest::crossTransportKeysAreNotConfused()
{
    // A key that names another transport must not leak across when it is stripped.
    QVERIFY(telegramChatKey(QStringLiteral("mx:!a:b.c")).isEmpty());
    QVERIFY(rocketChannelId(QStringLiteral("mx:!a:b.c")).isEmpty());
    QVERIFY(matrixRoomId(QStringLiteral("tg:-1:bot")).isEmpty());
    // And the router sends each prefixed id to its own transport.
    QCOMPARE(transportOf(telegramChatId(QStringLiteral("-9:bot"))), Transport::Telegram);
    QCOMPARE(transportOf(rocketChatId(QStringLiteral("news"))), Transport::RocketChat);
}

void ChatAddressTest::reservedAndLanAreNotTransports()
{
    // Reserved logs and LAN addresses are explicit non-transports so a backend
    // never claims them as its own conversation.
    QVERIFY(!isMatrix(QStringLiteral("__self__")));
    QVERIFY(!isMatrix(QStringLiteral("192.168.0.1")));
}

QTEST_GUILESS_MAIN(ChatAddressTest)

#include "ChatAddressTest.moc"
