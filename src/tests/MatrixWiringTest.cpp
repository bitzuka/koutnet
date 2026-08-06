// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// The Matrix path without a homeserver, without a network and without
// libQuotient: the addressing scheme, the file naming that scheme has to
// survive, and the translation from a flattened timeline event to a row.
//
// Everything here is a pure function on purpose. The parts of the Matrix
// support that need a server are not testable and are not tested; the parts
// that decide where a conversation is filed and what a message turns into are
// the parts that lose data when they are wrong.
#include <QTest>

#include "../core/chat/ChatAddress.h"
#include "../core/chat/HistoryManager.h"
#include "../matrix/MatrixTranslate.h"

using namespace koutnet;

class MatrixWiringTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void addressRoundTrip();
    void addressKeepsLanAndReservedIdsAlone();
    void addressRefusesEmptyRoomId();

    void fileStemLeavesSafeIdsAlone();
    void fileStemSeparatesIdsThatUsedToCollide();
    void fileStemIsBounded();
    void fileStemIsStable();

    void rowForPlainText();
    void rowForOwnMessage();
    void rowFallsBackToTheSenderId();
    void rowSkipsRedacted();
    void rowSkipsEventsWithNoId();
    void rowSkipsEmptyText();
    void rowReportsEncrypted();
    void rowReportsUnsupported();

    void timestampConversion();
    void conversationTitleFallsBackToTheRoomId();
    void encryptionNoticeIdIsPerRoom();
};

void MatrixWiringTest::addressRoundTrip()
{
    const QString roomId = QStringLiteral("!abc:matrix.org");
    const QString chatId = chatid::matrixChatId(roomId);

    QCOMPARE(chatId, QStringLiteral("mx:!abc:matrix.org"));
    QVERIFY(chatid::isMatrix(chatId));
    QCOMPARE(chatid::transportOf(chatId), chatid::Transport::Matrix);
    QCOMPARE(chatid::matrixRoomId(chatId), roomId);
    QCOMPARE(chatid::transportName(chatId), QStringLiteral("matrix"));

    // Idempotent, so a caller that prefixes twice does not produce "mx:mx:".
    QCOMPARE(chatid::matrixChatId(chatId), chatId);
}

void MatrixWiringTest::addressKeepsLanAndReservedIdsAlone()
{
    // An IPv6 address is full of colons and must not be mistaken for a prefixed
    // id; the prefix is checked at the front, which is why it cannot be.
    for (const QString &lan : {QStringLiteral("192.168.1.5"), QStringLiteral("fe80::1"), QStringLiteral("10.0.0.1")}) {
        QCOMPARE(chatid::transportOf(lan), chatid::Transport::Lan);
        QVERIFY(!chatid::isMatrix(lan));
        QVERIFY(chatid::matrixRoomId(lan).isEmpty());
        QCOMPARE(chatid::transportName(lan), QStringLiteral("lan"));
    }

    QCOMPARE(chatid::transportOf(QStringLiteral("__self__")), chatid::Transport::Reserved);
    QCOMPARE(chatid::transportName(QStringLiteral("__self__")), QStringLiteral("reserved"));
}

void MatrixWiringTest::addressRefusesEmptyRoomId()
{
    // A bare "mx:" would be a chat id that names no room and would still be
    // routed at the homeserver.
    QVERIFY(chatid::matrixChatId(QString()).isEmpty());
    QVERIFY(chatid::matrixRoomId(QStringLiteral("mx:")).isEmpty());
}

void MatrixWiringTest::fileStemLeavesSafeIdsAlone()
{
    // The whole point of the exception: an id that needed no substitution keeps
    // the file name it has always had, so no log written before the digest
    // existed goes missing.
    for (const QString &id : {QStringLiteral("__self__"), QStringLiteral("__calls"), QStringLiteral("peer-01"), QStringLiteral("abc123")}) {
        QCOMPARE(HistoryManager::stemFor(id), id);
        QCOMPARE(HistoryManager::stemFor(id), HistoryManager::legacyStemFor(id));
    }
}

void MatrixWiringTest::fileStemSeparatesIdsThatUsedToCollide()
{
    struct Pair {
        QString a;
        QString b;
    };
    const QList<Pair> pairs = {
        // The collisions the old scheme already had, before Matrix: an address
        // and the file name it flattens to are the same file, and so are two
        // spellings of one IPv6 address.
        {QStringLiteral("192.168.1.5"), QStringLiteral("192_168_1_5")},
        {QStringLiteral("fe80::1"), QStringLiteral("fe80..1")},
        // The ones Matrix would have added: a room id is a colon and a dot away
        // from another room id.
        {chatid::matrixChatId(QStringLiteral("!a:b.c")), chatid::matrixChatId(QStringLiteral("!a.b:c"))},
        {chatid::matrixChatId(QStringLiteral("!abc:matrix.org")), chatid::matrixChatId(QStringLiteral("!abc.matrix:org"))},
    };

    for (const Pair &pair : pairs) {
        QCOMPARE(HistoryManager::legacyStemFor(pair.a), HistoryManager::legacyStemFor(pair.b));
        QVERIFY2(HistoryManager::stemFor(pair.a) != HistoryManager::stemFor(pair.b), qPrintable(pair.a + QStringLiteral(" / ") + pair.b));
    }
}

void MatrixWiringTest::fileStemIsBounded()
{
    // A room alias can be longer than a file name is allowed to be.
    const QString huge = chatid::matrixChatId(QStringLiteral("!") + QString(400, QLatin1Char('x')) + QStringLiteral(":example.org"));
    const QString stem = HistoryManager::stemFor(huge);
    QVERIFY(stem.size() < 100);
    // Still not the same file as another very long id with the same prefix.
    const QString other = chatid::matrixChatId(QStringLiteral("!") + QString(400, QLatin1Char('x')) + QStringLiteral(":example.com"));
    QVERIFY(HistoryManager::stemFor(other) != stem);
}

void MatrixWiringTest::fileStemIsStable()
{
    // The name has to be the same next run or the log does not read back.
    const QString id = chatid::matrixChatId(QStringLiteral("!room:example.org"));
    QCOMPARE(HistoryManager::stemFor(id), HistoryManager::stemFor(id));
    // And it stays a legal file name.
    QVERIFY(!HistoryManager::stemFor(id).contains(QLatin1Char('/')));
    QVERIFY(!HistoryManager::stemFor(id).contains(QLatin1Char(':')));
}

namespace
{
matrix::RawEvent textEvent()
{
    matrix::RawEvent e;
    e.eventId = QStringLiteral("$evt1:example.org");
    e.senderId = QStringLiteral("@alice:example.org");
    e.senderName = QStringLiteral("Alice");
    e.body = QStringLiteral("hello");
    e.originTimestampMs = 1700000000000LL;
    e.textLike = true;
    return e;
}
} // namespace

void MatrixWiringTest::rowForPlainText()
{
    const matrix::Row row = matrix::rowFor(textEvent());

    QCOMPARE(row.kind, matrix::RowKind::Text);
    QCOMPARE(row.msgId, QStringLiteral("$evt1:example.org"));
    QCOMPARE(row.text, QStringLiteral("hello"));
    QCOMPARE(row.sender, QStringLiteral("Alice"));
    QCOMPARE(row.ts, 1700000000.0);
    QVERIFY(!row.isOwn);
}

void MatrixWiringTest::rowForOwnMessage()
{
    matrix::RawEvent e = textEvent();
    e.isOwn = true;
    const matrix::Row row = matrix::rowFor(e);
    QCOMPARE(row.kind, matrix::RowKind::Text);
    QVERIFY(row.isOwn);
}

void MatrixWiringTest::rowFallsBackToTheSenderId()
{
    matrix::RawEvent e = textEvent();
    e.senderName.clear();
    // A blank author is a message that looks like it came from nobody.
    QCOMPARE(matrix::rowFor(e).sender, QStringLiteral("@alice:example.org"));
}

void MatrixWiringTest::rowSkipsRedacted()
{
    matrix::RawEvent e = textEvent();
    e.redacted = true;
    QCOMPARE(matrix::rowFor(e).kind, matrix::RowKind::Skip);
}

void MatrixWiringTest::rowSkipsEventsWithNoId()
{
    matrix::RawEvent e = textEvent();
    e.eventId.clear();
    // Without an id there is no duplicate check, and every reconnect would add
    // the message again.
    QCOMPARE(matrix::rowFor(e).kind, matrix::RowKind::Skip);
}

void MatrixWiringTest::rowSkipsEmptyText()
{
    matrix::RawEvent e = textEvent();
    e.body.clear();
    QCOMPARE(matrix::rowFor(e).kind, matrix::RowKind::Skip);
}

void MatrixWiringTest::rowReportsEncrypted()
{
    matrix::RawEvent e = textEvent();
    e.textLike = false;
    e.body.clear();
    e.encrypted = true;

    const matrix::Row row = matrix::rowFor(e);
    // Reported rather than skipped: a room that is simply quiet and a room this
    // build cannot read must not look the same.
    QCOMPARE(row.kind, matrix::RowKind::Encrypted);
    QCOMPARE(row.msgId, e.eventId);
}

void MatrixWiringTest::rowReportsUnsupported()
{
    matrix::RawEvent e = textEvent();
    e.textLike = false;
    e.body = QStringLiteral("holiday.png");

    const matrix::Row row = matrix::rowFor(e);
    QCOMPARE(row.kind, matrix::RowKind::Unsupported);
    QCOMPARE(row.text, QStringLiteral("holiday.png"));
}

void MatrixWiringTest::timestampConversion()
{
    QCOMPARE(matrix::secondsFromMs(1500), 1.5);
    // Zero rather than 1970: the conversation list sorts on this, and a room
    // with no timestamp must not outrank one with a real message in it.
    QCOMPARE(matrix::secondsFromMs(0), 0.0);
    QCOMPARE(matrix::secondsFromMs(-1), 0.0);
}

void MatrixWiringTest::conversationTitleFallsBackToTheRoomId()
{
    QCOMPARE(matrix::conversationTitle(QStringLiteral("Kitchen"), QStringLiteral("!k:e.org")), QStringLiteral("Kitchen"));
    QCOMPARE(matrix::conversationTitle(QStringLiteral("  "), QStringLiteral("!k:e.org")), QStringLiteral("!k:e.org"));
    QCOMPARE(matrix::conversationTitle(QString(), QStringLiteral("!k:e.org")), QStringLiteral("!k:e.org"));
}

void MatrixWiringTest::encryptionNoticeIdIsPerRoom()
{
    const QString a = matrix::encryptionNoticeId(QStringLiteral("!a:e.org"));
    const QString b = matrix::encryptionNoticeId(QStringLiteral("!b:e.org"));
    QVERIFY(a != b);
    // Stable, which is what stops the notice stacking a copy per restart.
    QCOMPARE(a, matrix::encryptionNoticeId(QStringLiteral("!a:e.org")));
}

QTEST_GUILESS_MAIN(MatrixWiringTest)

#include "MatrixWiringTest.moc"
