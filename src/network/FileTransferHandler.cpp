// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Reassembles chunked file transfers received over UDP
#include "FileTransferHandler.h"

#include <KLocalizedString>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtNumeric> // qIsFinite, on a value that came straight off the wire

namespace koutnet
{

FileTransferHandler::FileTransferHandler(QObject *parent)
    : QObject(parent)
{
    connect(&m_pruneTimer, &QTimer::timeout, this, &FileTransferHandler::pruneStaleTransfers);
    m_pruneTimer.start(60'000); // sweep once a minute
}

QString FileTransferHandler::sanitizeFilename(const QString &rawName)
{
    const QString fallback = QStringLiteral("file_%1").arg(QDateTime::currentMSecsSinceEpoch());

    // Cut at the last separator of either kind: QFileInfo::fileName() only knows the
    // forward slash on Unix, so a backslash path survives it whole.
    QString name = rawName;
    const qsizetype cut = qMax(name.lastIndexOf(QLatin1Char('/')), name.lastIndexOf(QLatin1Char('\\')));
    if (cut >= 0)
        name = name.mid(cut + 1);

    // NULs and control characters go first: QFile hands the path to the C API, which
    // stops at the first NUL, so the path checked is not the path opened.
    QString clean;
    clean.reserve(name.size());
    for (const QChar c : name) {
        if (c.unicode() >= 0x20 && c.unicode() != 0x7F)
            clean.append(c);
    }
    name = clean;

    if (name.isEmpty() || name.count(QLatin1Char('.')) == name.size())
        return fallback;

    while (name.toUtf8().size() > kMaxFilenameBytes && !name.isEmpty())
        name.chop(1);
    if (name.isEmpty() || name.count(QLatin1Char('.')) == name.size())
        return fallback;

    return name;
}

qint64 FileTransferHandler::pendingBufferedBytes() const
{
    qint64 total = 0;
    for (auto it = m_pending.constBegin(); it != m_pending.constEnd(); ++it)
        total += it.value().receivedBytes;
    return total;
}

void FileTransferHandler::onMeta(const QJsonObject &meta)
{
    const QString tid = meta.value(QStringLiteral("tid")).toString();
    if (tid.isEmpty())
        return;

    // Checked as a double first: narrowing 1e300 or a NaN from the peer JSON to
    // qint64 is undefined behaviour, not a big number that fails the test below.
    const QJsonValue sizeValue = meta.value(QStringLiteral("size"));
    const double announced = sizeValue.toDouble(-1.0);
    if (!sizeValue.isDouble() || !qIsFinite(announced) || announced < 0.0 || announced > double(kMaxTransferBytes)) {
        Q_EMIT transferRejected(tid, i18nc("@info:status file transfer refused", "The announced size is not a usable length."));
        return; // no entry created - onChunkMessage will drop its chunks
    }

    if (!m_pending.contains(tid) && m_pending.size() >= kMaxPendingTransfers) {
        Q_EMIT transferRejected(tid, i18nc("@info:status file transfer refused", "Too many concurrent incoming transfers."));
        return;
    }

    PendingTransfer &t = m_pending[tid];
    t.meta = meta;
    t.total = -1; // filled in once the first chunk arrives with its "total" field
    t.chunks.clear();
    t.receivedBytes = 0;
    t.startedAtMs = QDateTime::currentMSecsSinceEpoch();
}

void FileTransferHandler::onChunkMessage(const QJsonObject &msg)
{
    const QString tid = msg.value(QStringLiteral("tid")).toString();
    if (tid.isEmpty() || !m_pending.contains(tid))
        return; // chunk for a transfer we never saw (or rejected) meta for - drop it

    PendingTransfer &t = m_pending[tid];

    const int idx = msg.value(QStringLiteral("idx")).toInt(-1);
    const int total = msg.value(QStringLiteral("total")).toInt(-1);
    if (idx < 0 || total <= 0 || idx >= total)
        return;

    // Reject a transfer that grows past the cap whatever the attacker-controlled
    // meta claimed: meta.size=small with far more or larger chunks than announced.
    const qint64 maxChunks = (kMaxTransferBytes / 1024) + 1024; // generous upper bound
    if (total > maxChunks) {
        m_pending.remove(tid);
        Q_EMIT transferRejected(tid, i18nc("@info:status file transfer refused", "The chunk count exceeds the limit."));
        return;
    }

    t.total = total;
    const QByteArray chunk = QByteArray::fromBase64(msg.value(QStringLiteral("data")).toString().toLatin1());

    const auto held = t.chunks.constFind(idx);
    if (held != t.chunks.constEnd() && *held != chunk) {
        // a peer resending an index with different bytes is either broken or
        // walking the whole range twice to hide the second pass from the cap
        m_pending.remove(tid);
        Q_EMIT transferRejected(tid, i18nc("@info:status file transfer refused", "A chunk was resent with different contents."));
        return;
    }

    // Count the delta against what is already held for this index: insert()
    // replaces, so one byte now and full size later used to cost nothing.
    t.receivedBytes += chunk.size() - (held != t.chunks.constEnd() ? held->size() : 0);
    if (t.receivedBytes > kMaxTransferBytes) {
        m_pending.remove(tid);
        Q_EMIT transferRejected(tid, i18nc("@info:status file transfer refused", "The transfer exceeded the size limit."));
        return;
    }
    t.chunks.insert(idx, chunk);

    if (t.chunks.size() < t.total)
        return; // still waiting on more chunks

    QByteArray full;
    full.reserve(int(qMin<qint64>(t.receivedBytes, kMaxTransferBytes)));
    for (int i = 0; i < t.total; ++i) {
        if (!t.chunks.contains(i)) {
            // missing a chunk despite count matching (duplicate?) - bail out safely
            return;
        }
        full.append(t.chunks.value(i));
    }

    Q_EMIT fileReceived(t.meta, full);

    const QString localPath = saveToDisk(t.meta, full);
    if (!localPath.isEmpty())
        Q_EMIT fileSaved(t.meta, localPath);
    else
        Q_EMIT transferRejected(tid, i18nc("@info:status file transfer refused", "Failed to write the file to disk."));

    m_pending.remove(tid);
}

void FileTransferHandler::pruneStaleTransfers()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVector<QString> stale;
    for (auto it = m_pending.constBegin(); it != m_pending.constEnd(); ++it) {
        if (now - it.value().startedAtMs > kPendingTransferTtlMs)
            stale.append(it.key());
    }
    for (const auto &tid : stale) {
        m_pending.remove(tid);
        Q_EMIT transferRejected(tid, i18nc("@info:status file transfer refused", "The transfer timed out while incomplete."));
    }
}

QString FileTransferHandler::saveToDisk(const QJsonObject &meta, const QByteArray &data) const
{
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString dirPath = baseDir.isEmpty() ? QString(QDir::homePath() + QStringLiteral("/KOutNet/received")) : QString(baseDir + QStringLiteral("/KOutNet"));

    QDir dir;
    if (!dir.mkpath(dirPath))
        return QString();

    // Never trust a peer-supplied filename in a disk path - see sanitizeFilename().
    const QString filename = sanitizeFilename(meta.value(QStringLiteral("filename")).toString());

    QString candidate = dirPath + QLatin1Char('/') + filename;
    if (QFileInfo::exists(candidate)) {
        const QFileInfo fi(filename);
        const QString base = fi.completeBaseName();
        const QString ext = fi.suffix();
        int n = 1;
        do {
            candidate = dirPath + QLatin1Char('/') + base + QStringLiteral("(%1)").arg(n) + (ext.isEmpty() ? QString() : QString(QLatin1Char('.') + ext));
            ++n;
        } while (QFileInfo::exists(candidate));
    }

    // absoluteFilePath() only cleans "." and ".." out of the string, so a symlink
    // along the way still escaped. Compare the parent: the candidate does not exist.
    const QString canonicalDir = QFileInfo(dirPath).canonicalFilePath();
    const QString candidateDir = QFileInfo(candidate).absolutePath();
    if (canonicalDir.isEmpty() || QFileInfo(candidateDir).canonicalFilePath() != canonicalDir)
        return QString();

    // NewOnly, not WriteOnly: something could have created the path since the
    // exists() loop ran, and failing beats overwriting whatever appeared.
    QFile out(candidate);
    if (!out.open(QIODevice::NewOnly))
        return QString();
    const qint64 written = out.write(data);
    const bool ok = written == data.size() && out.flush();
    out.close();
    if (!ok) {
        // a half-written file is worse than none, and the name is free again
        out.remove();
        return QString();
    }

    return candidate;
}

} // namespace koutnet
