// KOutNet — Reassembles chunked file transfers received over UDP
// Ported from gdf_network.py (NT Server 1.8) -> C++/Qt6
#include "FileTransferHandler.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDateTime>

namespace koutnet {

FileTransferHandler::FileTransferHandler(QObject *parent) : QObject(parent)
{
    connect(&m_pruneTimer, &QTimer::timeout, this, &FileTransferHandler::pruneStaleTransfers);
    m_pruneTimer.start(60'000); // sweep once a minute
}

QString FileTransferHandler::sanitizeFilename(const QString &rawName)
{
    // QFileInfo::fileName() strips any leading directory components (both
    // "/" and, on Qt, "\" are treated as separators), which is what stops a
    // peer sending "../../etc/passwd" or an absolute path from escaping the
    // destination folder below.
    QString name = QFileInfo(rawName).fileName();

    // Guard the remaining edge cases QFileInfo::fileName() doesn't fully
    // collapse (empty, ".", "..", or a name that somehow round-trips to
    // nothing useful).
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        return QStringLiteral("file_%1").arg(QDateTime::currentMSecsSinceEpoch());

    return name;
}

void FileTransferHandler::onMeta(const QJsonObject &meta)
{
    const QString tid = meta.value("tid").toString();
    if (tid.isEmpty())
        return;

    const qint64 announcedSize = meta.value("size").toDouble(0.0);
    if (announcedSize < 0 || announcedSize > kMaxTransferBytes) {
        emit transferRejected(tid, QStringLiteral("announced size exceeds limit"));
        return; // no entry created — onChunkMessage will drop its chunks
    }

    if (!m_pending.contains(tid) && m_pending.size() >= kMaxPendingTransfers) {
        emit transferRejected(tid, QStringLiteral("too many concurrent incoming transfers"));
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
    const QString tid = msg.value("tid").toString();
    if (tid.isEmpty() || !m_pending.contains(tid))
        return; // chunk for a transfer we never saw (or rejected) meta for — drop it

    PendingTransfer &t = m_pending[tid];

    const int idx = msg.value("idx").toInt(-1);
    const int total = msg.value("total").toInt(-1);
    if (idx < 0 || total <= 0 || idx >= total)
        return;

    // Reject a transfer that grows past the cap regardless of what the
    // (attacker-controlled) meta claimed — protects against a peer sending
    // meta.size=small but far more/larger chunks than announced.
    const qint64 maxChunks = (kMaxTransferBytes / 1024) + 1024; // generous upper bound
    if (total > maxChunks) {
        m_pending.remove(tid);
        emit transferRejected(tid, QStringLiteral("chunk count exceeds limit"));
        return;
    }

    t.total = total;
    const QByteArray chunk = QByteArray::fromBase64(msg.value("data").toString().toLatin1());

    if (!t.chunks.contains(idx)) {
        t.receivedBytes += chunk.size();
        if (t.receivedBytes > kMaxTransferBytes) {
            m_pending.remove(tid);
            emit transferRejected(tid, QStringLiteral("transfer exceeded size limit"));
            return;
        }
    }
    t.chunks.insert(idx, chunk);

    if (t.chunks.size() < t.total)
        return; // still waiting on more chunks

    // All chunks in — reassemble in order.
    QByteArray full;
    full.reserve(int(qMin<qint64>(t.receivedBytes, kMaxTransferBytes)));
    for (int i = 0; i < t.total; ++i) {
        if (!t.chunks.contains(i)) {
            // missing a chunk despite count matching (duplicate?) — bail out safely
            return;
        }
        full.append(t.chunks.value(i));
    }

    emit fileReceived(t.meta, full);

    const QString localPath = saveToDisk(t.meta, full);
    if (!localPath.isEmpty())
        emit fileSaved(t.meta, localPath);
    else
        emit transferRejected(tid, QStringLiteral("failed to write file to disk"));

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
        emit transferRejected(tid, QStringLiteral("transfer timed out (incomplete)"));
    }
}

QString FileTransferHandler::saveToDisk(const QJsonObject &meta, const QByteArray &data) const
{
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString dirPath = baseDir.isEmpty()
        ? QDir::homePath() + "/KOutNet/received"
        : baseDir + "/KOutNet";

    QDir dir;
    if (!dir.mkpath(dirPath))
        return QString();

    // Sanitized — never trust a peer-supplied filename directly in a disk
    // path (path traversal). See sanitizeFilename().
    const QString filename = sanitizeFilename(meta.value("filename").toString());

    // Avoid clobbering an existing file with the same name.
    QString candidate = dirPath + "/" + filename;
    if (QFileInfo::exists(candidate)) {
        const QFileInfo fi(filename);
        const QString base = fi.completeBaseName();
        const QString ext = fi.suffix();
        int n = 1;
        do {
            candidate = dirPath + "/" + base + QStringLiteral("(%1)").arg(n)
                       + (ext.isEmpty() ? QString() : "." + ext);
            ++n;
        } while (QFileInfo::exists(candidate));
    }

    // Belt-and-suspenders: confirm the resolved absolute path is still
    // inside dirPath before writing, in case some future edge case slips
    // past sanitizeFilename().
    const QFileInfo candidateInfo(candidate);
    const QString canonicalDir = QFileInfo(dirPath).absoluteFilePath();
    if (!candidateInfo.absoluteFilePath().startsWith(canonicalDir + "/"))
        return QString();

    QFile out(candidate);
    if (!out.open(QIODevice::WriteOnly))
        return QString();
    out.write(data);
    out.close();

    return candidate;
}

} // namespace koutnet
