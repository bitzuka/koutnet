// KOutNet — Reassembles chunked file transfers received over UDP
// Ported from gdf_network.py ( NT Server 1.8) → C++/Qt6
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
    void fileReceived(QJsonObject meta, QByteArray data);

private:
    struct PendingTransfer {
        QJsonObject meta;
        QMap<int, QByteArray> chunks; // idx -> raw chunk bytes
        int total = -1;
    };

    QHash<QString, PendingTransfer> m_pending; // tid -> transfer state
};

} // namespace koutnet
