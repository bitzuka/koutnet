// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include <KLocalizedString>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonValue>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <limits>

#include "../network/FileTransferHandler.h"

using koutnet::FileTransferHandler;

namespace
{

QString downloadDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + QStringLiteral("/KOutNet");
}

QJsonObject metaFor(const QString &tid, qint64 size, const QString &filename)
{
    QJsonObject o;
    o[QStringLiteral("tid")] = tid;
    o[QStringLiteral("size")] = double(size);
    o[QStringLiteral("filename")] = filename;
    return o;
}

QJsonObject chunkFor(const QString &tid, int idx, int total, const QByteArray &bytes)
{
    QJsonObject o;
    o[QStringLiteral("tid")] = tid;
    o[QStringLiteral("idx")] = idx;
    o[QStringLiteral("total")] = total;
    o[QStringLiteral("data")] = QString::fromLatin1(bytes.toBase64());
    return o;
}

} // namespace

class FileTransferHandlerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // A name is only safe if joining it to the destination cannot point elsewhere.
    void sanitizedNamesStayInTheFolder_data()
    {
        QTest::addColumn<QString>("raw");

        QTest::newRow("plain") << QStringLiteral("holiday.jpg");
        QTest::newRow("relative traversal") << QStringLiteral("../../etc/passwd");
        QTest::newRow("deep traversal") << QStringLiteral("../../../../../../../../etc/shadow");
        QTest::newRow("absolute") << QStringLiteral("/etc/passwd");
        QTest::newRow("absolute home") << QStringLiteral("/home/user/.bashrc");
        QTest::newRow("windows traversal") << QStringLiteral("..\\..\\Windows\\System32\\drivers\\etc\\hosts");
        QTest::newRow("windows drive") << QStringLiteral("C:\\Windows\\notepad.exe");
        QTest::newRow("mixed separators") << QStringLiteral("../..\\../evil.sh");
        QTest::newRow("trailing separator") << QStringLiteral("subdir/");
        QTest::newRow("dot") << QStringLiteral(".");
        QTest::newRow("dotdot") << QStringLiteral("..");
        QTest::newRow("only dots") << QStringLiteral("....");
        QTest::newRow("empty") << QString();
        QTest::newRow("space only") << QStringLiteral("   ");
        QTest::newRow("newline") << QStringLiteral("report\ninjected.sh");
        QTest::newRow("carriage return") << QStringLiteral("report\r\n.sh");
        QTest::newRow("tab") << QStringLiteral("re\tport.txt");
        // The one that matters most: every check in this class reasons about a
        // QString, and QFile hands the path to an API that stops at the NUL.
        QTest::newRow("embedded nul") << (QStringLiteral("safe.txt") + QChar(u'\0') + QStringLiteral("/../../etc/passwd"));
        QTest::newRow("nul then dotdot") << (QStringLiteral("..") + QChar(u'\0'));
        QTest::newRow("nul first") << (QChar(u'\0') + QStringLiteral("hidden"));
        QTest::newRow("very long") << QString(5000, QLatin1Char('a'));
        QTest::newRow("very long traversal") << (QStringLiteral("../") + QString(5000, QLatin1Char('b')));
        QTest::newRow("long multibyte") << QString(2000, QChar(0x00E9));
    }

    void sanitizedNamesStayInTheFolder()
    {
        QFETCH(QString, raw);
        const QString name = FileTransferHandler::sanitizeFilename(raw);

        QVERIFY2(!name.isEmpty(), "an empty name would make the write target the folder itself");
        QVERIFY2(!name.contains(QLatin1Char('/')), qPrintable(name));
        QVERIFY2(!name.contains(QLatin1Char('\\')), qPrintable(name));
        QVERIFY2(name != QLatin1String("."), qPrintable(name));
        QVERIFY2(name != QLatin1String(".."), qPrintable(name));
        QVERIFY2(name.count(QLatin1Char('.')) != name.size(), qPrintable(name));

        for (const QChar c : name) {
            QVERIFY2(c.unicode() >= 0x20 && c.unicode() != 0x7F,
                     "a control character survived, so the path that was checked "
                     "is not necessarily the path that gets opened");
        }

        QVERIFY2(name.toUtf8().size() <= FileTransferHandler::kMaxFilenameBytes, qPrintable(QStringLiteral("%1 bytes of filename").arg(name.toUtf8().size())));

        const QString base = QDir::cleanPath(downloadDir());
        const QString joined = QDir::cleanPath(base + QLatin1Char('/') + name);
        QVERIFY2(joined.startsWith(base + QLatin1Char('/')), qPrintable(QStringLiteral("%1 escapes %2").arg(joined, base)));
        QCOMPARE(QFileInfo(joined).absolutePath(), base);
    }

    // Again through the real path: saveToDisk() compares canonical paths, and only a
    // write proves it.
    void aHostileNameIsWrittenInsideTheFolder_data()
    {
        QTest::addColumn<QString>("raw");
        QTest::newRow("traversal") << QStringLiteral("../../../../etc/passwd");
        QTest::newRow("absolute") << QStringLiteral("/etc/passwd");
        QTest::newRow("dotdot") << QStringLiteral("..");
        QTest::newRow("long") << QString(4000, QLatin1Char('z'));
    }

    void aHostileNameIsWrittenInsideTheFolder()
    {
        QFETCH(QString, raw);
        FileTransferHandler handler;
        QSignalSpy saved(&handler, &FileTransferHandler::fileSaved);
        QSignalSpy rejected(&handler, &FileTransferHandler::transferRejected);

        const QByteArray payload = QByteArrayLiteral("contents");
        const QString tid = QStringLiteral("t-name");
        handler.onMeta(metaFor(tid, payload.size(), raw));
        handler.onChunkMessage(chunkFor(tid, 0, 1, payload));

        QCOMPARE(rejected.count(), 0);
        QCOMPARE(saved.count(), 1);
        const QString path = saved.at(0).at(1).toString();
        QVERIFY(!path.isEmpty());

        const QString base = QFileInfo(downloadDir()).canonicalFilePath();
        QVERIFY(!base.isEmpty());
        QCOMPARE(QFileInfo(path).absolutePath(), base);
        QVERIFY2(QFileInfo::exists(path), qPrintable(path));

        QCOMPARE(handler.pendingTransferCount(), 0);
        QCOMPARE(handler.pendingBufferedBytes(), 0);
    }

    void announcedSizeMustBeALength_data()
    {
        QTest::addColumn<QJsonValue>("size");
        QTest::addColumn<bool>("accepted");

        QTest::newRow("zero") << QJsonValue(0) << true;
        QTest::newRow("small") << QJsonValue(1024) << true;
        QTest::newRow("at the cap") << QJsonValue(double(FileTransferHandler::kMaxTransferBytes)) << true;

        QTest::newRow("negative") << QJsonValue(-1) << false;
        QTest::newRow("very negative") << QJsonValue(-1e18) << false;
        QTest::newRow("over the cap") << QJsonValue(double(FileTransferHandler::kMaxTransferBytes) + 1.0) << false;
        // Narrowing either of these to qint64 is undefined behaviour, not a large
        // number.
        QTest::newRow("absurd") << QJsonValue(1e300) << false;
        QTest::newRow("infinity") << QJsonValue(std::numeric_limits<double>::infinity()) << false;
        QTest::newRow("nan") << QJsonValue(std::numeric_limits<double>::quiet_NaN()) << false;
        QTest::newRow("numeric string") << QJsonValue(QStringLiteral("1024")) << false;
        QTest::newRow("text") << QJsonValue(QStringLiteral("lots")) << false;
        QTest::newRow("bool") << QJsonValue(true) << false;
        QTest::newRow("null") << QJsonValue(QJsonValue::Null) << false;
        QTest::newRow("missing") << QJsonValue(QJsonValue::Undefined) << false;
    }

    void announcedSizeMustBeALength()
    {
        QFETCH(QJsonValue, size);
        QFETCH(bool, accepted);

        FileTransferHandler handler;
        QSignalSpy rejected(&handler, &FileTransferHandler::transferRejected);

        QJsonObject meta;
        meta[QStringLiteral("tid")] = QStringLiteral("t-size");
        meta[QStringLiteral("filename")] = QStringLiteral("thing.bin");
        if (!size.isUndefined())
            meta[QStringLiteral("size")] = size;
        handler.onMeta(meta);

        QCOMPARE(handler.pendingTransferCount(), accepted ? 1 : 0);
        QCOMPARE(rejected.count(), accepted ? 0 : 1);
        if (!accepted)
            QCOMPARE(handler.pendingBufferedBytes(), 0);
    }

    void anIdenticalResendIsAccepted()
    {
        FileTransferHandler handler;
        QSignalSpy rejected(&handler, &FileTransferHandler::transferRejected);
        QSignalSpy saved(&handler, &FileTransferHandler::fileSaved);

        const QString tid = QStringLiteral("t-resend");
        const QByteArray first = QByteArrayLiteral("aaaa");
        const QByteArray second = QByteArrayLiteral("bbbb");
        handler.onMeta(metaFor(tid, 8, QStringLiteral("resend.bin")));

        handler.onChunkMessage(chunkFor(tid, 0, 2, first));
        QCOMPARE(handler.pendingBufferedBytes(), 4);
        handler.onChunkMessage(chunkFor(tid, 0, 2, first));
        QCOMPARE(rejected.count(), 0);
        QVERIFY2(handler.pendingBufferedBytes() == 4, "a duplicate of a chunk we already hold was counted twice");

        handler.onChunkMessage(chunkFor(tid, 1, 2, second));
        QCOMPARE(rejected.count(), 0);
        QCOMPARE(saved.count(), 1);
        QCOMPARE(handler.pendingTransferCount(), 0);
    }

    void aConflictingResendKillsTheTransfer()
    {
        FileTransferHandler handler;
        QSignalSpy rejected(&handler, &FileTransferHandler::transferRejected);

        const QString tid = QStringLiteral("t-conflict");
        handler.onMeta(metaFor(tid, 8, QStringLiteral("conflict.bin")));
        handler.onChunkMessage(chunkFor(tid, 0, 2, QByteArrayLiteral("aaaa")));
        handler.onChunkMessage(chunkFor(tid, 0, 2, QByteArrayLiteral("zzzz")));

        QCOMPARE(rejected.count(), 1);
        QCOMPARE(handler.pendingTransferCount(), 0);
        QCOMPARE(handler.pendingBufferedBytes(), 0);
    }

    // The accounting bug this replaced: insert() replaces, so every index at one
    // byte and then again at full size cost nothing the first pass and the cap never
    // saw the second.
    void aSecondPassAtFullSizeIsRefused()
    {
        FileTransferHandler handler;
        QSignalSpy rejected(&handler, &FileTransferHandler::transferRejected);

        const QString tid = QStringLiteral("t-twopass");
        const int total = 16;
        handler.onMeta(metaFor(tid, 16, QStringLiteral("twopass.bin")));

        // One short on purpose: the last chunk would flush to disk and empty the
        // buffers.
        for (int i = 0; i < total - 1; ++i)
            handler.onChunkMessage(chunkFor(tid, i, total, QByteArrayLiteral("x")));
        QCOMPARE(rejected.count(), 0);
        QCOMPARE(handler.pendingBufferedBytes(), total - 1);

        handler.onChunkMessage(chunkFor(tid, 0, total, QByteArray(4096, 'x')));
        QVERIFY2(rejected.count() == 1 || handler.pendingBufferedBytes() > total - 1, "a resent index grew the transfer without being charged for it");
        QCOMPARE(handler.pendingTransferCount(), 0);
        QCOMPARE(handler.pendingBufferedBytes(), 0);
    }

    // Deliberately expensive: proving the cap holds means handing over more than the
    // cap.
    void theByteCapHolds()
    {
        FileTransferHandler handler;
        QSignalSpy rejected(&handler, &FileTransferHandler::transferRejected);

        const QString tid = QStringLiteral("t-cap");
        const int chunkBytes = 4 * 1024 * 1024;
        const QByteArray chunk(chunkBytes, 'q');
        const int total = int(FileTransferHandler::kMaxTransferBytes / chunkBytes) + 2;

        handler.onMeta(metaFor(tid, 1024, QStringLiteral("big.bin")));

        int sent = 0;
        for (int i = 0; i < total && rejected.isEmpty(); ++i) {
            handler.onChunkMessage(chunkFor(tid, i, total, chunk));
            ++sent;
        }

        QCOMPARE(rejected.count(), 1);
        QVERIFY2(qint64(sent) * chunkBytes <= FileTransferHandler::kMaxTransferBytes + chunkBytes, "more than the cap was accepted before the refusal");
        QCOMPARE(handler.pendingTransferCount(), 0);
        QCOMPARE(handler.pendingBufferedBytes(), 0);
    }

    void nonsenseChunkHeadersAreDropped_data()
    {
        QTest::addColumn<int>("idx");
        QTest::addColumn<int>("total");
        QTest::newRow("negative index") << -1 << 4;
        QTest::newRow("index past total") << 4 << 4;
        QTest::newRow("index far past total") << 1'000'000 << 4;
        QTest::newRow("zero total") << 0 << 0;
        QTest::newRow("negative total") << 0 << -1;
        QTest::newRow("absurd total") << 0 << 2'000'000'000;
    }

    void nonsenseChunkHeadersAreDropped()
    {
        QFETCH(int, idx);
        QFETCH(int, total);

        FileTransferHandler handler;
        QSignalSpy saved(&handler, &FileTransferHandler::fileSaved);

        const QString tid = QStringLiteral("t-headers");
        handler.onMeta(metaFor(tid, 64, QStringLiteral("headers.bin")));
        handler.onChunkMessage(chunkFor(tid, idx, total, QByteArrayLiteral("data")));

        QCOMPARE(saved.count(), 0);
        QCOMPARE(handler.pendingBufferedBytes(), 0);
    }

    void chunksWithoutAMetaAreDropped()
    {
        FileTransferHandler handler;
        handler.onChunkMessage(chunkFor(QStringLiteral("never-announced"), 0, 1, QByteArrayLiteral("data")));
        QCOMPARE(handler.pendingTransferCount(), 0);

        QJsonObject oversized;
        oversized[QStringLiteral("tid")] = QStringLiteral("t-refused");
        oversized[QStringLiteral("size")] = 1e300;
        handler.onMeta(oversized);
        handler.onChunkMessage(chunkFor(QStringLiteral("t-refused"), 0, 1, QByteArrayLiteral("data")));
        QCOMPARE(handler.pendingTransferCount(), 0);
        QCOMPARE(handler.pendingBufferedBytes(), 0);

        handler.onMeta(metaFor(QString(), 16, QStringLiteral("x.bin")));
        QCOMPARE(handler.pendingTransferCount(), 0);
    }

    void tooManyConcurrentTransfersAreRefused()
    {
        FileTransferHandler handler;
        QSignalSpy rejected(&handler, &FileTransferHandler::transferRejected);

        for (int i = 0; i < FileTransferHandler::kMaxPendingTransfers + 20; ++i) {
            handler.onMeta(metaFor(QStringLiteral("t-%1").arg(i), 1024, QStringLiteral("f%1.bin").arg(i)));
        }
        QCOMPARE(handler.pendingTransferCount(), FileTransferHandler::kMaxPendingTransfers);
        QCOMPARE(rejected.count(), 20);
    }
};

// The download directory is the one path QStandardPaths test mode does not move: on
// Unix it comes from the XDG user dirs, so this would write into real ~/Downloads.
int main(int argc, char *argv[])
{
    QTemporaryDir home;
    if (!home.isValid()) {
        qCritical("could not create a scratch home directory");
        return 1;
    }
    qputenv("HOME", home.path().toLocal8Bit());
    // No user-dirs.dirs in there, so DownloadLocation falls back to $HOME/Downloads.
    qputenv("XDG_CONFIG_HOME", QByteArray(home.path().toLocal8Bit() + "/.config"));

    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    // Without a domain ki18n warns on every string, burying the test output.
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("koutnet"));
    QCoreApplication::setOrganizationName(QStringLiteral("koutnet-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("file-transfer-handler"));

    FileTransferHandlerTest tc;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tc, argc, argv);
}
#include "FileTransferHandlerTest.moc"
