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
#include <QSet>
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
    void fileStemCannotEscapeTheHistoryDir();

    void rowForPlainText();
    void rowForOwnMessage();
    void rowFallsBackToTheSenderId();
    void rowReportsRedacted();
    void rowSkipsEventsWithNoId();
    void rowSkipsEmptyText();
    void rowReportsEncrypted();
    void redactionOutranksAMissingKey();
    void rowReportsUnsupported();
    void rowCarriesAnAttachment();
    void rowNamesAnAttachmentThatHasNoName();
    void rowRendersStateEvents();
    void stateSentencesAreDistinct();
    void stateSentencesNameTheActor();

    void timestampConversion();
    void conversationTitleFallsBackToTheRoomId();
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

void MatrixWiringTest::fileStemCannotEscapeTheHistoryDir()
{
    // a hostile chatId must never produce a stem that climbs out of the
    // history directory, no matter what it contains
    const QStringList hostile = {
        QStringLiteral("../../etc/passwd"),
        QStringLiteral("../../../../etc/shadow"),
        QStringLiteral("/etc/passwd"),
        QStringLiteral("..\\..\\Windows\\System32"),
        QStringLiteral("../.."),
        QStringLiteral(".."),
        QStringLiteral("."),
        QStringLiteral("foo/../../../bar"),
        QStringLiteral("mx:!room/../../../steal:a.b"),
        QString(QChar(u'\0')) + QStringLiteral("/../etc/passwd"),
    };
    const QString base = QStringLiteral("/fake/history");
    for (const QString &id : hostile) {
        const QString stem = HistoryManager::stemFor(id);
        QVERIFY2(!stem.contains(QLatin1Char('/')), qPrintable(stem));
        QVERIFY2(!stem.contains(QLatin1Char('\\')), qPrintable(stem));
        QVERIFY2(stem != QLatin1String("."), qPrintable(stem));
        QVERIFY2(stem != QLatin1String(".."), qPrintable(stem));
        // the joined path must stay inside the directory
        const QString joined = QDir::cleanPath(base + QLatin1Char('/') + stem + QStringLiteral(".json"));
        QVERIFY2(joined.startsWith(base + QLatin1Char('/')), qPrintable(QStringLiteral("%1 escapes %2 (from chatId %3)").arg(joined, base, id)));
    }
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

void MatrixWiringTest::rowReportsRedacted()
{
    matrix::RawEvent e = textEvent();
    e.redacted = true;

    const matrix::Row row = matrix::rowFor(e);
    // Said out loud rather than dropped: a message that disappears without a
    // trace reads as one that was never sent.
    QCOMPARE(row.kind, matrix::RowKind::System);
    QCOMPARE(row.msgId, e.eventId);
    QVERIFY(row.text.contains(QStringLiteral("Alice")));
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
    // Reported rather than skipped: a message nothing can open and a message
    // that was never sent must not look the same. An event only reaches here
    // still encrypted when libQuotient had no key for it.
    QCOMPARE(row.kind, matrix::RowKind::Encrypted);
    QCOMPARE(row.msgId, e.eventId);
    // The row has to carry words of its own now that no room-wide notice
    // explains the gap.
    QVERIFY(!row.text.isEmpty());
}

void MatrixWiringTest::redactionOutranksAMissingKey()
{
    matrix::RawEvent e = textEvent();
    e.textLike = false;
    e.body.clear();
    e.encrypted = true;
    e.redacted = true;

    // A withdrawn message is withdrawn whether or not this device could have
    // read it, and "no key for this" would send the reader looking for one.
    QCOMPARE(matrix::rowFor(e).kind, matrix::RowKind::System);
}

void MatrixWiringTest::rowReportsUnsupported()
{
    matrix::RawEvent e = textEvent();
    e.textLike = false;
    e.body = QStringLiteral("holiday.png");

    const matrix::Row row = matrix::rowFor(e);
    // A msgtype with no renderer here is named rather than dropped, and it is
    // the room talking rather than the sender, so it is a system row.
    QCOMPARE(row.kind, matrix::RowKind::System);
    QVERIFY(row.text.contains(QStringLiteral("holiday.png")));
}

void MatrixWiringTest::rowCarriesAnAttachment()
{
    matrix::RawEvent e = textEvent();
    e.textLike = false;
    e.body = QStringLiteral("holiday.png");
    e.media = matrix::MediaKind::Image;
    e.mediaUrl = QStringLiteral("mxc://example.org/abc?user_id=@alice:example.org");
    e.mediaName = QStringLiteral("holiday.png");
    e.mediaMime = QStringLiteral("image/png");
    e.mediaSize = 4096;
    e.mediaWidth = 800;
    e.mediaHeight = 600;

    const matrix::Row row = matrix::rowFor(e);
    QCOMPARE(row.kind, matrix::RowKind::Attachment);
    QCOMPARE(row.media, matrix::MediaKind::Image);
    QCOMPARE(row.mediaUrl, e.mediaUrl);
    QCOMPARE(row.mediaWidth, 800);
    QCOMPARE(row.mediaHeight, 600);
    QCOMPARE(row.mediaSize, 4096);
    // The label is also the row's text, which is what the timeline draws
    // beside a file and what the conversation list previews.
    QCOMPARE(row.mediaName, QStringLiteral("holiday.png"));
    QCOMPARE(row.text, QStringLiteral("holiday.png"));
}

void MatrixWiringTest::rowNamesAnAttachmentThatHasNoName()
{
    matrix::RawEvent e = textEvent();
    e.textLike = false;
    e.body.clear();
    e.media = matrix::MediaKind::Audio;

    const matrix::Row row = matrix::rowFor(e);
    QCOMPARE(row.kind, matrix::RowKind::Attachment);
    // Never blank: a nameless attachment would be an empty row with a click
    // target nobody could see.
    QVERIFY(!row.text.isEmpty());
    QVERIFY(!row.mediaName.isEmpty());
}

void MatrixWiringTest::rowRendersStateEvents()
{
    matrix::RawEvent e = textEvent();
    e.textLike = false;
    e.body.clear();
    e.state = matrix::StateChange::Joined;

    const matrix::Row row = matrix::rowFor(e);
    QCOMPARE(row.kind, matrix::RowKind::System);
    QCOMPARE(row.msgId, e.eventId);
    QVERIFY(row.text.contains(QStringLiteral("Alice")));

    // A redacted state event still happened. Deleting the event does not undo
    // the join, so the line stays.
    e.redacted = true;
    QCOMPARE(matrix::rowFor(e).kind, matrix::RowKind::System);
}

void MatrixWiringTest::stateSentencesAreDistinct()
{
    // Two changes that read the same are two changes the timeline cannot tell
    // apart, which is the whole failure mode this enum exists to avoid.
    const QList<matrix::StateChange> changes = {
        matrix::StateChange::Joined,
        matrix::StateChange::Left,
        matrix::StateChange::Invited,
        matrix::StateChange::InviteWithdrawn,
        matrix::StateChange::InviteRejected,
        matrix::StateChange::Kicked,
        matrix::StateChange::Banned,
        matrix::StateChange::SelfBanned,
        matrix::StateChange::Unbanned,
        matrix::StateChange::SelfUnbanned,
        matrix::StateChange::KnockRequested,
        matrix::StateChange::DisplayNameSet,
        matrix::StateChange::DisplayNameChanged,
        matrix::StateChange::DisplayNameCleared,
        matrix::StateChange::MemberAvatarChanged,
        matrix::StateChange::RoomCreated,
        matrix::StateChange::RoomUpgraded,
        matrix::StateChange::RoomNameSet,
        matrix::StateChange::RoomNameCleared,
        matrix::StateChange::TopicSet,
        matrix::StateChange::TopicCleared,
        matrix::StateChange::AliasSet,
        matrix::StateChange::AliasCleared,
        matrix::StateChange::RoomAvatarChanged,
        matrix::StateChange::EncryptionEnabled,
        matrix::StateChange::PowerLevelsChanged,
        matrix::StateChange::Unknown,
    };

    QSet<QString> seen;
    for (matrix::StateChange change : changes) {
        const QString sentence = matrix::stateSentence(change, QStringLiteral("Alice"), QStringLiteral("Bob"));
        QVERIFY2(!sentence.isEmpty(), qPrintable(QString::number(int(change))));
        QVERIFY2(!seen.contains(sentence), qPrintable(sentence));
        seen.insert(sentence);
    }

    // None is the one that says nothing, because it is the absence of a change.
    QVERIFY(matrix::stateSentence(matrix::StateChange::None, QStringLiteral("Alice"), QString()).isEmpty());
}

void MatrixWiringTest::stateSentencesNameTheActor()
{
    QVERIFY(matrix::stateSentence(matrix::StateChange::Joined, QStringLiteral("Alice"), QString()).contains(QStringLiteral("Alice")));
    // An actor that could not be resolved still has to read as a sentence
    // rather than begin with a space.
    const QString anonymous = matrix::stateSentence(matrix::StateChange::Joined, QString(), QString());
    QVERIFY(!anonymous.isEmpty());
    QVERIFY(!anonymous.startsWith(QLatin1Char(' ')));
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

QTEST_GUILESS_MAIN(MatrixWiringTest)

#include "MatrixWiringTest.moc"
