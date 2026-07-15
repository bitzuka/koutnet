// KOutNet — Reassembles chunked file transfers received over UDP
// Ported from gdf_network.py (NT Server 1.8) -> C++/Qt6
#include "FileTransferHandler.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDateTime>

namespace koutnet {

FileTransferHandler::FileTransferHandler(QObject *parent) : QObject(parent) {}

void FileTransferHandler::onMeta(const QJsonObject &meta)
{
    const QString tid = meta.value("tid").toString();
    if (tid.isEmpty())
        return;

    PendingTransfer &t = m_pending[tid];
    t.meta = meta;
    t.total = -1; // filled in once the first chunk arrives with its "total" field
    t.chunks.clear();
}

void FileTransferHandler::onChunkMessage(const QJsonObject &msg)
{
    const QString tid = msg.value("tid").toString();
    if (tid.isEmpty() || !m_pending.contains(tid))
        return; // chunk for a transfer we never saw meta for — drop it

    PendingTransfer &t = m_pending[tid];

    const int idx = msg.value("idx").toInt(-1);
    const int total = msg.value("total").toInt(-1);
    if (idx < 0 || total <= 0)
        return;

    t.total = total;
    const QByteArray chunk = QByteArray::fromBase64(msg.value("data").toString().toLatin1());
    t.chunks.insert(idx, chunk);

    if (t.chunks.size() < t.total)
        return; // still waiting on more chunks

    // All chunks in — reassemble in order.
    QByteArray full;
    full.reserve(t.meta.value("size").toInt(0));
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

    m_pending.remove(tid);
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

    QString filename = meta.value("filename").toString();
    if (filename.isEmpty())
        filename = QStringLiteral("file_%1").arg(QDateTime::currentMSecsSinceEpoch());

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

    QFile out(candidate);
    if (!out.open(QIODevice::WriteOnly))
        return QString();
    out.write(data);
    out.close();

    return candidate;
}

} // namespace koutnet
