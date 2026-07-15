// KOutNet — Reassembles chunked file transfers received over UDP
// Ported from gdf_network.py (NT Server 1.8) -> C++/Qt6
#pragma once

#include <QObject>
#include <QJsonObject>
#include <QByteArray>
#include <QMap>
#include <QHash>

namespace koutnet {

class FileTransferHandler : public QObject {
    Q_OBJECT

public:
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

private:
    struct PendingTransfer {
        QJsonObject meta;
        QMap<int, QByteArray> chunks; // idx -> raw chunk bytes
        int total = -1;
    };

    QString saveToDisk(const QJsonObject &meta, const QByteArray &data) const;

    QHash<QString, PendingTransfer> m_pending; // tid -> transfer state
};

} // namespace koutnet
