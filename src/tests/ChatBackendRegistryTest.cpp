// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// The registry is the one door every chat action goes through, so the routing
// table is the thing to test: which backend a chat id lands on, what happens
// when none claims it, and that the capability flags and queries are the
// backend's answers and not the registry's invention. Two fake backends stand
// in for NetworkManager and MatrixRoomBridge - the routing code must not care
// what a backend is made of.
#include <QSignalSpy>
#include <QTest>

#include "core/backend/ChatBackendRegistry.h"
#include "core/chat/ChatAddress.h"

using namespace koutnet;

namespace
{

class FakeBackend : public ChatBackend
{
public:
    explicit FakeBackend(chatid::Transport t, QString prefix, QObject *parent = nullptr)
        : ChatBackend(parent)
        , m_transport(t)
        , m_prefix(std::move(prefix))
    {
    }

    chatid::Transport transport() const override
    {
        return m_transport;
    }
    bool canHandle(const QString &chatId) const override
    {
        return chatId.startsWith(m_prefix);
    }
    bool serverOwnsTimeline(const QString &) const override
    {
        return m_serverOwnsTimeline;
    }
    bool hasRooms(const QString &) const override
    {
        return m_hasRooms;
    }
    bool supportsCalls(const QString &) const override
    {
        return m_supportsCalls;
    }
    bool supportsTyping(const QString &) const override
    {
        return m_supportsTyping;
    }
    bool supportsEdits(const QString &) const override
    {
        return m_supportsEdits;
    }
    bool sendText(const QString &chatId, const QString &text) override
    {
        m_lastSend = text;
        return m_sendOk;
    }
    bool sendFile(const QString &chatId, const QString &localFilePath) override
    {
        m_lastFile = localFilePath;
        return m_sendOk;
    }
    void markRead(const QString &chatId) override
    {
        m_readCount++;
    }
    void sendTyping(const QString &chatId) override
    {
        m_typingCount++;
    }
    bool leaveChat(const QString &chatId) override
    {
        return m_leaveOk;
    }
    QVariantMap roomInfo(const QString &chatId) const override
    {
        return m_info;
    }
    QVariantList roomMembers(const QString &chatId) const override
    {
        return m_members;
    }
    QVariantMap memberInfo(const QString &chatId, const QString &userId) const override
    {
        QVariantMap m;
        m[QStringLiteral("userId")] = userId;
        return m;
    }

    // The switchboard the test pokes to make the fake answer a certain way.
    bool m_sendOk = true;
    bool m_leaveOk = true;
    bool m_serverOwnsTimeline = false;
    bool m_hasRooms = false;
    bool m_supportsCalls = false;
    bool m_supportsTyping = false;
    bool m_supportsEdits = false;
    QVariantMap m_info;
    QVariantList m_members;
    QString m_lastSend;
    QString m_lastFile;
    int m_readCount = 0;
    int m_typingCount = 0;

private:
    chatid::Transport m_transport;
    QString m_prefix;
};

} // namespace

class ChatBackendRegistryTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aChatIdLandsOnItsBackend()
    {
        FakeBackend lan(chatid::Transport::Lan, QStringLiteral("192."));
        FakeBackend matrix(chatid::Transport::Matrix, QStringLiteral("mx:"));
        ChatBackendRegistry registry;
        registry.registerBackend(&lan);
        registry.registerBackend(&matrix);

        QCOMPARE(registry.backendFor(QStringLiteral("192.168.1.7")), &lan);
        QCOMPARE(registry.backendFor(QStringLiteral("mx:!ab:c")), &matrix);
        // A transport with no registered backend and a reserved chat both find
        // nobody - the window guards "__self__" before it ever asks.
        QCOMPARE(registry.backendFor(QStringLiteral("rc:#general")), nullptr);
        QCOMPARE(registry.backendFor(QStringLiteral("__self__")), nullptr);
    }

    void actionsRouteToTheOwningBackend()
    {
        FakeBackend lan(chatid::Transport::Lan, QStringLiteral("192."));
        FakeBackend matrix(chatid::Transport::Matrix, QStringLiteral("mx:"));
        ChatBackendRegistry registry;
        registry.registerBackend(&lan);
        registry.registerBackend(&matrix);

        QVERIFY(registry.sendText(QStringLiteral("192.168.1.7"), QStringLiteral("hi")));
        QCOMPARE(lan.m_lastSend, QStringLiteral("hi"));
        QVERIFY(matrix.m_lastSend.isEmpty());

        QVERIFY(registry.sendText(QStringLiteral("mx:!a:b"), QStringLiteral("yo")));
        QCOMPARE(matrix.m_lastSend, QStringLiteral("yo"));
        // The LAN fake was not asked twice.
        QCOMPARE(lan.m_lastSend, QStringLiteral("hi"));

        registry.markRead(QStringLiteral("mx:!a:b"));
        registry.markRead(QStringLiteral("192.168.1.7"));
        QCOMPARE(matrix.m_readCount, 1);
        QCOMPARE(lan.m_readCount, 1);

        // The send returns false when the owning backend refuses - a chat no
        // backend claims is the same refusal.
        lan.m_sendOk = false;
        QVERIFY(!registry.sendText(QStringLiteral("192.168.1.7"), QStringLiteral("no")));
        QVERIFY(!registry.sendText(QStringLiteral("tg:123"), QStringLiteral("no")));
        QVERIFY(!registry.sendFile(QStringLiteral("tg:123"), QStringLiteral("/tmp/x")));
    }

    void capabilitiesAreTheBackendsAnswers()
    {
        FakeBackend lan(chatid::Transport::Lan, QStringLiteral("192."));
        FakeBackend matrix(chatid::Transport::Matrix, QStringLiteral("mx:"));
        lan.m_supportsCalls = true;
        lan.m_supportsTyping = true;
        lan.m_supportsEdits = true;
        matrix.m_serverOwnsTimeline = true;
        matrix.m_hasRooms = true;
        ChatBackendRegistry registry;
        registry.registerBackend(&lan);
        registry.registerBackend(&matrix);

        QVERIFY(registry.supportsCalls(QStringLiteral("192.168.1.7")));
        QVERIFY(!registry.supportsCalls(QStringLiteral("mx:!a:b")));
        QVERIFY(registry.serverOwnsTimeline(QStringLiteral("mx:!a:b")));
        QVERIFY(!registry.serverOwnsTimeline(QStringLiteral("192.168.1.7")));
        QVERIFY(registry.hasRooms(QStringLiteral("mx:!a:b")));
        QVERIFY(!registry.hasRooms(QStringLiteral("192.168.1.7")));
        QVERIFY(registry.supportsTyping(QStringLiteral("192.168.1.7")));
        QVERIFY(registry.supportsEdits(QStringLiteral("192.168.1.7")));
        // A chat no backend claims answers no to everything.
        QVERIFY(!registry.supportsCalls(QStringLiteral("tg:123")));
        QVERIFY(!registry.serverOwnsTimeline(QStringLiteral("tg:123")));
    }

    void queriesComeBackFromTheOwningBackend()
    {
        FakeBackend matrix(chatid::Transport::Matrix, QStringLiteral("mx:"));
        matrix.m_info = {{QStringLiteral("roomId"), QStringLiteral("!a:b")}};
        matrix.m_members = {QVariantMap{{QStringLiteral("userId"), QStringLiteral("@u:b")}}};
        ChatBackendRegistry registry;
        registry.registerBackend(&matrix);

        QCOMPARE(registry.roomInfo(QStringLiteral("mx:!a:b")).value(QStringLiteral("roomId")).toString(),
                 QStringLiteral("!a:b"));
        QCOMPARE(registry.roomMembers(QStringLiteral("mx:!a:b")).size(), 1);
        QCOMPARE(registry.memberInfo(QStringLiteral("mx:!a:b"), QStringLiteral("@u:b")).value(QStringLiteral("userId")).toString(),
                 QStringLiteral("@u:b"));
        // A LAN chat has no room furniture, whatever the backend's map says.
        QVERIFY(registry.roomInfo(QStringLiteral("192.168.1.7")).isEmpty());
        QVERIFY(registry.roomMembers(QStringLiteral("192.168.1.7")).isEmpty());
    }

    void transportNamesFollowThePrefixTable()
    {
        QCOMPARE(koutnet::chatid::transportName(QStringLiteral("192.168.1.7")), QStringLiteral("lan"));
        QCOMPARE(koutnet::chatid::transportName(QStringLiteral("mx:!a:b")), QStringLiteral("matrix"));
        QCOMPARE(koutnet::chatid::transportName(QStringLiteral("rc:#general")), QStringLiteral("rocket.chat"));
        QCOMPARE(koutnet::chatid::transportName(QStringLiteral("tg:123")), QStringLiteral("telegram"));
        QCOMPARE(koutnet::chatid::transportName(QStringLiteral("__self__")), QStringLiteral("reserved"));
    }

    void chatIdHelpersRoundTrip()
    {
        QCOMPARE(koutnet::chatid::matrixChatId(QStringLiteral("!a:b")), QStringLiteral("mx:!a:b"));
        QCOMPARE(koutnet::chatid::matrixRoomId(QStringLiteral("mx:!a:b")), QStringLiteral("!a:b"));
        QCOMPARE(koutnet::chatid::rocketChatId(QStringLiteral("#general")), QStringLiteral("rc:#general"));
        QCOMPARE(koutnet::chatid::rocketChannelId(QStringLiteral("rc:#general")), QStringLiteral("#general"));
        QCOMPARE(koutnet::chatid::telegramChatId(QStringLiteral("123")), QStringLiteral("tg:123"));
        QCOMPARE(koutnet::chatid::telegramChatKey(QStringLiteral("tg:123")), QStringLiteral("123"));
        // The unwrap refuses a foreign or bare id rather than guessing.
        QVERIFY(koutnet::chatid::matrixRoomId(QStringLiteral("rc:#general")).isEmpty());
        QVERIFY(koutnet::chatid::rocketChannelId(QStringLiteral("mx:!a:b")).isEmpty());
        QVERIFY(koutnet::chatid::telegramChatKey(QStringLiteral("192.168.1.7")).isEmpty());
        // Empty input never produces a bare prefix.
        QVERIFY(koutnet::chatid::telegramChatId(QString()).isEmpty());
        QVERIFY(koutnet::chatid::rocketChatId(QString()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(ChatBackendRegistryTest)

#include "ChatBackendRegistryTest.moc"
