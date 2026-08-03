// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Reassembles chunked file transfers received over UDP
#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QTimer>

namespace koutnet
{

class FileTransferHandler : public QObject
{
    Q_OBJECT

public:
    // Sane default cap on a single incoming transfer - meta announcing a
    // larger size is rejected outright (no entry created, chunks dropped).
    // TODO: make this user-configurable via AppSettings once that lands.
    static constexpr qint64 kMaxTransferBytes = 200LL * 1024 * 1024; // 200 MB
    // Cap on concurrent in-flight transfers from all peers combined, so a
    // peer (or several) spamming file_meta without ever sending chunks can't
    // grow m_pending unboundedly.
    static constexpr int kMaxPendingTransfers = 50;
    // Incomplete transfers older than this are dropped by the prune timer.
    static constexpr qint64 kPendingTransferTtlMs = 10 * 60 * 1000; // 10 min
    // Filename length cap in UTF-8 bytes, which is the unit the filesystem
    // counts in. Comfortably under the 255 that ext4 and friends allow, with
    // room left for the "(2)" a name collision appends.
    static constexpr int kMaxFilenameBytes = 200;

    explicit FileTransferHandler(QObject *parent = nullptr);

    // Called when a file_meta packet arrives (announces an incoming transfer).
    void onMeta(const QJsonObject &meta);

    // Called when a file_data (chunk) packet arrives.
    void onChunkMessage(const QJsonObject &msg);

    // Turns a peer-supplied name into a bare filename that cannot leave the
    // destination folder: strips directory components, control characters and
    // NULs, replaces a name that is nothing but dots, and caps the length.
    // Public because it is the entire security boundary of this class and
    // deserves checking on its own, not only through a transfer that happens
    // to make it as far as the disk.
    static QString sanitizeFilename(const QString &rawName);

    // What the peers are currently making us hold. A refused transfer has to
    // bring both back down, which is invisible from outside otherwise.
    int pendingTransferCount() const
    {
        return m_pending.size();
    }
    qint64 pendingBufferedBytes() const;

Q_SIGNALS:
    // Raw-bytes signal - kept for any consumer that wants the data directly
    // without touching disk.
    void fileReceived(QJsonObject meta, QByteArray data);

    // Fired once a completed transfer has been written to disk. QML listens
    // to this one - a local file:// path is far cheaper to hand across the
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

    QString saveToDisk(const QJsonObject &meta, const QByteArray &data) const;
    void pruneStaleTransfers();

    QHash<QString, PendingTransfer> m_pending; // tid -> transfer state
    QTimer m_pruneTimer;
};

} // namespace koutnet
