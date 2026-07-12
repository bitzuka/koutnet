// KOutNet — Reassembles chunked file transfers received over UDP
// Ported from gdf_network.py ( NT Server 1.8) → C++/Qt6
#include "FileTransferHandler.h"

namespace koutnet {

FileTransferHandler::FileTransferHandler(QObject *parent) : QObject(parent) {}

void FileTransferHandler::onMeta(const QJsonObject &meta)
{
    const QString tid = meta.value("tid").toString();
    if (tid.isEmpty())
        return;
    PendingTransfer transfer;
    transfer.meta = meta;
    m_pending[tid] = transfer;
}

void FileTransferHandler::onChunkMessage(const QJsonObject &msg)
{
    const QString tid = msg.value("tid").toString();
    const int idx = msg.value("idx").toInt();
    const int total = msg.value("total").toInt(1);
    const QByteArray b64 = msg.value("data").toString().toLatin1();

    if (!m_pending.contains(tid))
        m_pending[tid] = PendingTransfer{};

    PendingTransfer &rec = m_pending[tid];
    rec.total = total;
    rec.chunks[idx] = QByteArray::fromBase64(b64);

    if (rec.chunks.size() == total) {
        QByteArray assembled;
        assembled.reserve(rec.chunks.size() * 60000); // matches sender chunk size
        for (int i = 0; i < total; ++i)
            assembled += rec.chunks.value(i);

        const QJsonObject meta = rec.meta;
        m_pending.remove(tid);
        emit fileReceived(meta, assembled);
    }
}

} // namespace koutnet
