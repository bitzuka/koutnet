// KOutNet — Reassembles chunked file transfers received over UDP
// Ported from gdf_network.py (NT Server 1.8) -> C++/Qt6
#pragma once

#include <QObject>
#include <QJsonObject>
#include <QByteArray>
#include <QMap>
#include <QHash>
#include <QTimer>

namespace koutnet {

class FileTransferHandler : public QObject {
    Q_OBJECT

public:
    // Sane default cap on a single incoming transfer — meta announcing a
    // larger size is rejected outright (no entry created, chunks dropped).
    // TODO: make this user-configurable via AppSettings once that lands.
    static constexpr qint64 kMaxTransferBytes = 200LL * 1024 * 1024; // 200 MB
    // Cap on concurrent in-flight transfers from all peers combined, so a
    // peer (or several) spamming file_meta without ever sending chunks can't
    // grow m_pending unboundedly.
    static constexpr int kMaxPendingTransfers = 50;
    // Incomplete transfers older than this are dropped by the prune timer.
    static constexpr qint64 kPendingTransferTtlMs = 10 * 60 * 1000; // 10 min

    explicit FileTransferHandler(QObject *parent = nullptr);

    // Called when a file_meta packet arrives (announces an incoming transfer).
    void onMeta(const QJsonObject &meta);

    // Called when a file_data (chunk) packet arrives.
    void onChunkMessage(const QJsonObject &msg);

signals:
    // Raw-bytes signal — kept for any consumer that wants the data directly
    // without touching disk.
    void fileReceived(QJsonObject meta, QByteArray data);

    // Fired once a completed transfer has been written to disk. QML listens
    // to this one — a local file:// path is far cheaper to hand across the
    // QML/C++ boundary than a raw byte blob.
    void fileSaved(QJsonObject meta, QString localPath);

    // Fired when an incoming transfer is refused or abandoned (oversized,
    // too many concurrent transfers, stale/incomplete, disk write failure).
    // UI can surface this; purely informational, no action required.
    void transferRejected(QString tid, QString reason);

private:
    struct PendingTransfer {
        QJsonObject meta;
        QMap<int, QByteArray> chunks; // idx -> raw chunk bytes
        int total = -1;
        qint64 receivedBytes = 0;
        qint64 startedAtMs = 0;
    };

    // Strips any directory components and rejects empty/"."/".." results so
    // a peer-supplied filename can never escape the destination folder.
    static QString sanitizeFilename(const QString &rawName);
    QString saveToDisk(const QJsonObject &meta, const QByteArray &data) const;
    void pruneStaleTransfers();

    QHash<QString, PendingTransfer> m_pending; // tid -> transfer state
    QTimer m_pruneTimer;
};

} // namespace koutnet
