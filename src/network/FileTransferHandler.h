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

#include <functional>

namespace koutnet
{

class FileTransferHandler : public QObject
{
    Q_OBJECT

public:
    // Built-in ceiling on one incoming transfer; larger meta is refused outright,
    // with no entry created so its chunks are dropped too. The default instance
    // uses it, and main.cpp replaces it from the maxTransferMb AppSettings entry.
    static constexpr qint64 kMaxTransferBytes = 200LL * 1024 * 1024; // 200 MB
    // Cap on concurrent in-flight transfers, so peers spamming file_meta without
    // ever sending chunks cannot grow m_pending unboundedly.
    static constexpr int kMaxPendingTransfers = 50;
    // The per-transfer cap is not enough: many incomplete transfers could otherwise
    // consume one cap each before their TTL expires.
    static constexpr qint64 kMaxPendingBytes = 256LL * 1024 * 1024;
    static constexpr qint64 kPendingTransferTtlMs = 10 * 60 * 1000; // 10 min
    // Filename length cap in UTF-8 bytes, the unit the filesystem counts in, with
    // room left for the "(2)" a name collision appends.
    static constexpr int kMaxFilenameBytes = 200;

    explicit FileTransferHandler(QObject *parent = nullptr);

    // What a single announced size may be, in bytes. Starts at kMaxTransferBytes;
    // main.cpp lowers or raises it from AppSettings.
    void setMaxTransferBytes(qint64 bytes);

    // Completes a transfer whose meta carries "encrypted": true. Empty
    // result rejects the transfer. main.cpp wires it to
    // CryptoManager::decryptFileBytes; the handler keeps no crypto
    // dependency of its own so the test links no sodium.
    using FileDecryptor = std::function<QByteArray(const QString &peerIp, const QByteArray &cipher)>;
    void setFileDecryptor(FileDecryptor decryptor);

    void onMeta(const QJsonObject &meta);

    void onChunkMessage(const QString &tid, int idx, int total, const QByteArray &chunk);

    // Turns a peer-supplied name into a bare filename that cannot leave the
    // destination folder. Public because it is the entire security boundary of this
    // class and deserves checking on its own.
    static QString sanitizeFilename(const QString &rawName);

    // What the peers are currently making us hold. A refused transfer has to bring
    // both back down.
    int pendingTransferCount() const
    {
        return m_pending.size();
    }
    qint64 pendingBufferedBytes() const;

Q_SIGNALS:
    void fileReceived(QJsonObject meta, QByteArray data);

    // Fired once a completed transfer is on disk. QML listens to this one - a
    // file:// path is far cheaper across the QML/C++ boundary than a byte blob.
    void fileSaved(QJsonObject meta, QString localPath);

    void transferRejected(QString tid, QString reason);

private:
    struct PendingTransfer {
        QJsonObject meta;
        QMap<int, QByteArray> chunks; // idx -> raw chunk bytes
        int total = -1;
        qint64 announcedBytes = -1;
        qint64 receivedBytes = 0;
        qint64 startedAtMs = 0;
    };

    QString saveToDisk(const QJsonObject &meta, const QByteArray &data) const;
    void pruneStaleTransfers();

    QHash<QString, PendingTransfer> m_pending; // tid -> transfer state
    QTimer m_pruneTimer;
    qint64 m_maxTransferBytes = kMaxTransferBytes;
    FileDecryptor m_decryptor;
};

} // namespace koutnet
