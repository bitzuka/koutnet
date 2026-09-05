// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Network & Audio core
#pragma once

#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QVector>

#include <functional>

#include "Protocol.h"
#include "core/backend/ChatBackend.h"

namespace koutnet
{

class CryptoManager;
class AppSettings;

class NetworkManager : public ChatBackend
{
    Q_OBJECT
    // The primary local address changes under us when a VPN adapter comes up,
    // so the status bar needs a notify rather than a one-shot read.
    Q_PROPERTY(QString hostIp READ hostIp NOTIFY hostIpChanged)

public:
    // The LAN/VPN transport, registered with ChatBackendRegistry in main.cpp.
    // Everything the interface can ask of a chat comes through the registry;
    // the datagram calls below are the transport's own vocabulary (calls,
    // receipts, edits), used by Main.qml only where the backend it routed to
    // is this one - see the capability flags on ChatBackend.
    // LanOrVpn: broadcast, mDNS and ARP discovery over any local interface, no
    // server. KServer: a K-Server, wherever it is. A relay mode used to sit
    // here too; it is gone, there is no relay host to ship with the app.
    // Three K-Server entries used to sit here - self-hosted, somebody else's, and
    // the maintainer's VDS - with one protocol behind all three; the only thing that
    // differed was the address, which is a setting (kServerHost) and not a mode.
    // The values are persisted and read back as ints, so reordering them silently
    // moves everyone to a different mode. They changed when those three became
    // one, and when the relay mode was dropped; that migration is
    // AppSettings::migrateConnectionModes().
    enum class ConnectionMode {
        LanOrVpn = 0,
        KServer = 1,
    };
    Q_ENUM(ConnectionMode)
    // CryptoManager is owned by the application and injected here, never created
    // internally - identity keys and session state must stay single-sourced.
    // See core/security/CryptoManager.
    explicit NetworkManager(CryptoManager *crypto, QObject *parent = nullptr);
    ~NetworkManager() override;

    // Aggressive while no peers are known, then backs off: discovery has already
    // worked, and the noise costs battery and CPU on laptops.
    static constexpr int kActiveBroadcastMs = 2000;
    static constexpr int kIdleBroadcastMs = 8000;
    // /24 sweep backoff: starts loud for a quick first find, then doubles with
    // jitter up to the cap so an empty network is not hammered forever.
    static constexpr int kSweepMinMs = 2000;
    static constexpr int kSweepMaxMs = 120000;
    // How many addresses one message is copied to. The list comes from the peer, and
    // one advertising five thousand of them would have us send five thousand datagrams
    // per keystroke. Four covers a LAN address, a VPN address and a spare.
    static constexpr int kMaxDeliveryAddresses = 4;
    // Cap on the observed peer table: spoofed presences must not grow it without
    // bound, so the oldest entry gives way to a newcomer.
    static constexpr int kMaxPeers = 512;
    // Presence packets are unsigned (the handshake rides inside them) and
    // therefore cheaper to forge than any other type.  A per-address cap of
    // five per second is generous for normal operation while making a spoofed
    // flood expensive.
    static constexpr int kMaxPresencePerSec = 5;

    bool start();
    void stop();

    Q_INVOKABLE bool isRunning() const
    {
        return m_running;
    }
    Q_INVOKABLE QString hostIp() const
    {
        return m_hostIp;
    }
    quint16 udpPort() const
    {
        return m_udpPort;
    }
    quint16 tcpPort() const
    {
        return m_voiceTcpPort;
    }
    const QMap<QString, QJsonObject> &peers() const
    {
        return m_peers;
    }
    // Newest observed first, never more than kMaxDeliveryAddresses. Public because the
    // cap is the point of it and a test has to be able to read it back.
    QVector<QString> deliveryAddresses(const QString &toIp) const;

    // Safe before start(), or while running, where it raises or drops the tunnel.
    Q_INVOKABLE void setConnectionMode(ConnectionMode mode);
    Q_INVOKABLE ConnectionMode connectionMode() const
    {
        return m_mode;
    }
    // Whether a mode has anything behind it. The settings page asks rather than
    // hardcoding it, so landing a K-Server changes this function and nothing in QML.
    Q_INVOKABLE bool modeAvailable(int mode) const;
    // revision is a short digest of everything in the profile, images included, so a
    // peer can tell it needs to re-fetch without being sent the files.
    Q_INVOKABLE void setProfile(const QString &handle, const QString &displayName, const QString &bio, const QString &revision);

    // What the user says they are, and the one emoji they say it with. Kept
    // out of the profile digest: going busy has not changed a picture, and bumping
    // the revision would ask every peer to refetch an identical profile.
    Q_INVOKABLE void setStatus(int presence, const QString &statusEmoji);

    // Broadcast has no single peer to hold an ECDH session with, so a passphrase
    // everyone knows is the only thing that can protect it. Empty means cleartext.
    void setGroupPassphrase(const QString &passphrase);

    // One key per group, so opening one group's traffic says nothing about the
    // next. The key is generated on the group's first message and kept in
    // KeepSecret under the gid; a group that predates this state still works, on
    // the shared passphrase, and a member without the group key falls back the
    // same way. Pushing an explicit key here is for tests and future UI; the
    // LAN path hands keys through invites, sealed under the session with the
    // invitee when one exists.
    Q_INVOKABLE void setGroupKey(const QString &gid, const QString &key);
    Q_INVOKABLE void removeGroupKey(const QString &gid);

    // Addresses to unicast presence to every cycle until they answer.
    // main.cpp feeds it from AppSettings; the settings page applies edits
    // the same way it applies the K-Server fields.
    Q_INVOKABLE void setStaticPeers(const QStringList &ips);

    // Configured addresses minus our own and ones already heard from.
    // Static so the decision can be tested without sockets.
    static QStringList staticUnicastTargets(const QStringList &configured, const QMap<QString, QJsonObject> &knownPeers, const QString &hostIp);

    // Split out of onUdpReadyRead() because everything below this line is
    // attacker-controlled, and a test needing two live UDP sockets never gets written.
    void handleDatagram(const QString &host, const QByteArray &data);

    void sendUdp(QJsonObject payload, const QString &targetIp = QString());
    void sendUdp(QCborMap payload, const QString &targetIp = QString());
    Q_INVOKABLE bool sendPrivate(const QString &text, const QString &toIp);
    Q_INVOKABLE void sendGroupMessage(const QString &gid, const QString &text, const QVector<QString> &members);
    Q_INVOKABLE void sendTyping(const QString &chatId, const QString &targetIp);
    Q_INVOKABLE void sendCallRequest(const QString &toIp);
    Q_INVOKABLE void sendCallAccept(const QString &toIp);
    Q_INVOKABLE void sendCallReject(const QString &toIp);
    Q_INVOKABLE void sendCallEnd(const QString &toIp);
    Q_INVOKABLE void sendCallBusy(const QString &toIp);
    // The peers currently in a live call with us, in either direction. main.cpp
    // keeps it in sync with VoiceCallManager::activeCalls(), and it is what the
    // call signalling gates on: a call_accept only lands when we actually asked
    // that peer, a call_end only cancels a call or a ring that exists, and a
    // call_req arriving while one is live gets the busy reply instead of a
    // second ringing window. NetworkManager deliberately does not know
    // VoiceCallManager.
    void setActiveCalls(const QSet<QString> &ips);
    Q_INVOKABLE void sendMessageEdit(const QString &toIp, const QString &chatId, const QVariant &identifier, const QString &newText);
    Q_INVOKABLE void sendMessageDelete(const QString &toIp, const QString &chatId, const QVariant &identifier);
    Q_INVOKABLE void sendReadReceipt(const QString &toIp, const QString &chatId);
    Q_INVOKABLE void sendGroupInvite(const QString &gid, const QString &gname, const QString &toIp);
    bool sendFileInternal(const QString &toIp, const QString &filePath, const QByteArray &rawBytes = {}, const QString &filename = QStringLiteral("file"));

    // ChatBackend interface. Registered with ChatBackendRegistry in main.cpp;
    // the window routes every chat action through chatTransport, and the flags
    // below are what let it offer the right furniture for a LAN chat.
    chatid::Transport transport() const override;
    bool canHandle(const QString &chatId) const override;
    bool serverOwnsTimeline(const QString &chatId) const override;
    bool hasRooms(const QString &chatId) const override;
    bool supportsCalls(const QString &chatId) const override;
    bool supportsTyping(const QString &chatId) const override;
    bool supportsEdits(const QString &chatId) const override;
    bool supportsReactions(const QString &chatId) const override;
    bool sendText(const QString &chatId, const QString &text) override;
    bool sendFile(const QString &chatId, const QString &localFilePath) override;
    void markRead(const QString &chatId) override;
    void sendReaction(const QString &chatId, const QVariant &identifier, const QString &emoji, bool added) override;
    void sendTyping(const QString &chatId) override;
    // ChatBackend's ts-keyed edit and unsend, which the window routes through
    // the registry; both resolve to the protocol packets below.
    bool sendEdit(const QString &chatId, const QVariant &identifier, const QString &newText) override;
    bool sendDelete(const QString &chatId, const QVariant &identifier) override;
    bool leaveChat(const QString &chatId) override;
    QVariantMap roomInfo(const QString &chatId) const override;
    QVariantList roomMembers(const QString &chatId) const override;
    QVariantMap memberInfo(const QString &chatId, const QString &userId) const override;

    // Returns straight away: true means an attempt is in flight, false that there is
    // nothing to connect to. The outcome arrives as voiceConnected/voiceDisconnected.
    bool connectVoice(const QString &ip);
    bool sendVoice(const QString &ip, const QByteArray &data);
    void disconnectVoice(const QString &ip);

Q_SIGNALS:
    void hostIpChanged();
    void userOnline(QJsonObject peerInfo);
    // userOnline() fires once, on the first packet, so without this the interface idea
    // of when a peer was last around would freeze at its first appearance.
    void peerRefreshed(QString ip, double lastSeen);
    void userOffline(QString ip);
    void message(QJsonObject msg);
    void callRequest(QString username, QString ip);
    void callAccepted(QString username, QString ip);
    void callRejected(QString ip);
    void callEnded(QString ip);
    void voiceDataFrom(QString ip, QByteArray raw);
    void fileMeta(QJsonObject meta);
    void fileChunkBytes(QString tid, int idx, int total, QByteArray chunk); // file_data -> FileTransferHandler
    void groupInvite(QString groupId, QString name, QString fromIp);
    void errorOccurred(QString message);
    // fromIp is the address the packet arrived on, which is the key a conversation is
    // filed under - a username is a string a peer chooses, and two can match.
    void typing(QString username, QString chatId, QString fromIp);
    void voiceConnected(QString ip);
    void voiceDisconnected(QString ip);

private Q_SLOTS:
    void onUdpReadyRead();
    void onNewTcpConnection();
    void onBroadcastTimer();
    void refreshLocalIps();
    void scanArpTable();

private:
    QJsonObject presencePayload() const;
    // Signs once for the peer identity behind these addresses and writes the same bytes
    // to each: one signature for the whole set. An empty list means broadcast.
    bool sendUdpToAll(QJsonObject payload, const QVector<QString> &targets);
    bool sendUdpToAll(QCborMap payload, const QVector<QString> &targets);
    quint16 udpPortFor(const QString &ip) const;
    quint16 tcpPortFor(const QString &ip) const;
    void dispatch(const QString &host, QJsonObject msg);
    void handlePresence(const QString &host, QJsonObject msg);
    void decryptMessageText(const QString &peerRef, QJsonObject msg, const std::function<void(QJsonObject)> &done);
    // Files a socket under an address, deleting whatever it displaces.
    void replaceVoiceSocket(const QString &ip, QTcpSocket *sock);
    void onVoiceData(QTcpSocket *sock, const QString &ip);
    void onVoiceDisconnected(QTcpSocket *sock, const QString &ip);
    void sendChunksQueued(const QVector<QCborMap> &chunks, const QString &toIp, int idx, int batch = 3);
    void pruneStalePeers();
    void evictOldestPeer();

    CryptoManager *m_crypto = nullptr;

    QUdpSocket *m_udp = nullptr;
    QTcpServer *m_tcpServer = nullptr;
    QMap<QString, QTcpSocket *> m_voiceConnections; // ip -> voice socket
    QMap<QString, QTcpSocket *> m_pendingVoice; // ip -> socket still connecting
    // Keyed on the socket rather than the IP: the inbound and outbound socket for one
    // peer are different streams and must not share a buffer.
    QHash<QTcpSocket *, QByteArray> m_voiceRxBuffers;
    // A peer is filed under the first address it was heard on, not the one it
    // advertises; m_peerKeyById keeps one entry per identity, not per interface.
    QMap<QString, QJsonObject> m_peers; // observed address -> peer info
    QHash<QString, QString> m_peerKeyById; // identity id -> its key in m_peers

    QString m_hostIp;
    QSet<QString> m_localIps;
    bool m_running = false;

    // Call signalling state. m_pendingCalls is "we sent call_req and have not
    // heard back", m_ringingCalls is "their call_req is on screen waiting for
    // the user", m_activeCalls is the mirror of VoiceCallManager::activeCalls().
    // Nothing may cross a boundary without first sitting in the bucket its
    // sender belongs in - that is what keeps a forged accept from opening the
    // microphone (see the dispatch() comment on kMsgCallAccept).
    QSet<QString> m_pendingCalls;
    QSet<QString> m_ringingCalls;
    QSet<QString> m_activeCalls;

    quint16 m_udpPort = 0;
    quint16 m_voiceTcpPort = 0;

    QTimer m_broadcastTimer;
    QTimer m_ipRefreshTimer;

    // Outgoing file transfers: tid -> {chunks, target, timer}. If no ACK arrives
    // within kFileRetransmitMs, all chunks are sent again. Simple and sufficient
    // for LAN where loss is rare but not impossible.
    struct OutgoingTransfer {
        QVector<QCborMap> chunks;
        QString toIp;
        QTimer *timer = nullptr;
        OutgoingTransfer() = default;
        OutgoingTransfer(QVector<QCborMap> c, QString ip, QTimer *t)
            : chunks(std::move(c))
            , toIp(std::move(ip))
            , timer(t)
        {
        }
    };
    QHash<QString, OutgoingTransfer> m_outgoingTransfers;
    static constexpr int kFileRetransmitMs = 5000;

    // Incoming file transfers: tid -> {received indices, total, sender}.
    // When all chunks arrive, a file_ack is sent back so the sender can clean up.
    struct IncomingTransfer {
        QSet<int> received;
        int total = 0;
        QString fromIp;
    };
    QHash<QString, IncomingTransfer> m_incomingTransfers;

    // No relay / tunnel members here any more; the mode is gone for good.
    ConnectionMode m_mode = ConnectionMode::LanOrVpn;
    QString m_groupPassphrase;
    // gid -> its own key. The mirror of what the store holds, kept because a
    // store read on every decrypted group message would date every group chat.
    static QString groupKeyStoreKey(const QString &gid);
    QString groupKeyFor(const QString &gid);
    QString ensureGroupKey(const QString &gid);
    QHash<QString, QString> m_groupKeys; // in-memory cache of group keys
    QString m_profileHandle;
    QString m_profileDisplayName;
    QString m_profileBio;
    QString m_profileRevision;
    QString m_statusEmoji;
    int m_presence = 0;

    QStringList m_staticPeers; // set via setStaticPeers()
    double m_lastScan = 0.0;
    double m_sweepIntervalMs = double(kSweepMinMs); // current /24 sweep gap, grows with backoff
    // Per-address presence rate limiter: address -> timestamps of recent arrivals.
    QHash<QString, QVector<double>> m_presenceRate;
};

} // namespace koutnet
