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

#include "Protocol.h"

namespace koutnet
{

class CryptoManager;
class AppSettings;

class NetworkManager : public QObject
{
    Q_OBJECT
    // The primary local address changes under us when a VPN adapter comes up,
    // so the status bar needs a notify rather than a one-shot read.
    Q_PROPERTY(QString hostIp READ hostIp NOTIFY hostIpChanged)

public:
    // LanOrVpn: broadcast, mDNS and ARP discovery over any local interface, no
    // server. KServer: a K-Server, wherever it is. Relay: discovery and NAT
    // traversal through a relay, which setRelayServer() has to supply for now.
    // Three K-Server entries used to sit here - self-hosted, somebody else's, and
    // the maintainer's VDS - with one protocol behind all three; the only thing that
    // differed was the address, which is a setting (kServerHost) and not a mode.
    // The values are persisted and read back as ints, so reordering them silently
    // moves everyone to a different mode. They changed once, when those three became
    // one; that migration is AppSettings::migrateConnectionModes().
    enum class ConnectionMode {
        LanOrVpn = 0,
        KServer = 1,
        Relay = 2,
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
    // How many addresses one message is copied to. The list comes from the peer, and
    // one advertising five thousand of them would have us send five thousand datagrams
    // per keystroke. Four covers a LAN address, a VPN address and a spare.
    static constexpr int kMaxDeliveryAddresses = 4;
    // Cap on the observed peer table: spoofed presences must not grow it without
    // bound, so the oldest entry gives way to a newcomer.
    static constexpr int kMaxPeers = 512;

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

    // Custom/self-hosted relay server. voicePort defaults to tunnelPort + 1
    // if not given. Persistence is the caller's job: main.cpp restores the
    // saved host and port from AppSettings before the connection starts.
    Q_INVOKABLE void setRelayServer(const QString &host, quint16 tunnelPort, quint16 voicePort = 0);

    // Split out of onUdpReadyRead() because everything below this line is
    // attacker-controlled, and a test needing two live UDP sockets never gets written.
    void handleDatagram(const QString &host, const QByteArray &data);

    void sendUdp(QJsonObject payload, const QString &targetIp = QString());
    Q_INVOKABLE void sendPrivate(const QString &text, const QString &toIp);
    Q_INVOKABLE void sendGroupMessage(const QString &gid, const QString &text, const QVector<QString> &members);
    Q_INVOKABLE void sendTyping(const QString &chatId, const QString &targetIp = QString());
    Q_INVOKABLE void sendCallRequest(const QString &toIp);
    Q_INVOKABLE void sendCallAccept(const QString &toIp);
    Q_INVOKABLE void sendCallReject(const QString &toIp);
    Q_INVOKABLE void sendCallEnd(const QString &toIp);
    Q_INVOKABLE void sendCallBusy(const QString &toIp);
    // Whether any call is live, in either direction. main.cpp keeps it in sync
    // with VoiceCallManager::activeCalls(); while true, incoming call requests
    // are answered with the busy reply instead of surfacing a second ringing
    // window. NetworkManager deliberately does not know VoiceCallManager.
    void setInCall(bool inCall);
    Q_INVOKABLE void sendReaction(const QString &toIp, const QString &chatId, double ts, const QString &emoji, bool added);
    Q_INVOKABLE void sendMessageEdit(const QString &toIp, const QString &chatId, double ts, const QString &newText);
    Q_INVOKABLE void sendMessageDelete(const QString &toIp, const QString &chatId, double ts);
    Q_INVOKABLE void sendReadReceipt(const QString &toIp, const QString &chatId);
    Q_INVOKABLE void sendGroupInvite(const QString &gid, const QString &gname, const QString &toIp);
    void sendFileInternal(const QString &toIp, const QString &filePath, const QByteArray &rawBytes = {}, const QString &filename = QStringLiteral("file"));
    // QML cannot supply the QByteArray/filename defaults cleanly.
    Q_INVOKABLE void sendFile(const QString &toIp, const QString &filePath);

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
    void fileChunk(QJsonObject chunk); // file_data packets -> FileTransferHandler
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
    void sendUdpToAll(QJsonObject payload, const QVector<QString> &targets);
    void dispatch(const QString &host, QJsonObject msg);
    void handlePresence(const QString &host, QJsonObject msg);
    void decryptMessageText(const QString &peerRef, QJsonObject &msg) const;
    // Files a socket under an address, deleting whatever it displaces.
    void replaceVoiceSocket(const QString &ip, QTcpSocket *sock);
    void onVoiceData(QTcpSocket *sock, const QString &ip);
    void onVoiceDisconnected(QTcpSocket *sock, const QString &ip);
    void startInternetTunnel();
    void onRelayData();
    void scheduleRelayReconnect();
    void sendChunksQueued(const QVector<QJsonObject> &chunks, const QString &toIp, int idx, int batch = 3);
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
    bool m_internetMode = false;
    bool m_inCall = false;

    quint16 m_voiceTcpPort = 0;

    QTimer m_broadcastTimer;
    QTimer m_ipRefreshTimer;

    // Relay / tunnel (Vds mode) - TODO: move to network/vds module
    QTcpSocket *m_relaySocket = nullptr;
    bool m_relayConnected = false;
    QByteArray m_relayBuffer; // partial frame from the tunnel, see onRelayData()
    ConnectionMode m_mode = ConnectionMode::LanOrVpn;
    QString m_groupPassphrase;
    QString m_profileHandle;
    QString m_profileDisplayName;
    QString m_profileBio;
    QString m_profileRevision;
    QString m_statusEmoji;
    int m_presence = 0;
    QString m_relayHostOverride; // set via setRelayServer()
    quint16 m_relayPortOverride = 0;
    quint16 m_relayVoicePortOverride = 0;
    int m_relayReconnectMs = protocol::kRelayReconnectBaseMs; // grows via backoff, see .cpp

    double m_lastScan = 0.0;
};

} // namespace koutnet
