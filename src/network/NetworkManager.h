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
// TODO: AppSettings (S() equivalent) - core/constructor not yet ported.
class AppSettings;

class NetworkManager : public QObject
{
    Q_OBJECT
    // The primary local address changes under us when a VPN adapter comes up,
    // so the status bar needs a notify rather than a one-shot read.
    Q_PROPERTY(QString hostIp READ hostIp NOTIFY hostIpChanged)

public:
    // LanOrVpn: broadcast, mDNS and ARP discovery over any local interface
    // including VPN adapters, no server. Vds: discovery and NAT traversal
    // through a relay, which setRelayServer() has to supply until an
    // official one ships.
    // Numbered explicitly: the values are persisted in QSettings and read
    // back as ints by the settings page, so reordering them would silently
    // move everyone to a different mode.
    enum class ConnectionMode {
        LanOrVpn = 0,
        KServerSelfHosted = 1,
        KServerClient = 2,
        Relay = 3,
        MaintainerVds = 4,
    };
    Q_ENUM(ConnectionMode)
    // CryptoManager is owned by the application (one instance shared across
    // NetworkManager / VoiceCallManager / UI) and injected here, never
    // created internally - identity keys and session state must stay
    // single-sourced. See core/security/CryptoManager.
    explicit NetworkManager(CryptoManager *crypto, QObject *parent = nullptr);
    ~NetworkManager() override;

    // Discovery broadcast cadence: aggressive while no peers are known yet,
    // then backs off once the mesh is established - cuts background network
    // noise (battery/CPU on laptops too) once discovery has already worked.
    static constexpr int kActiveBroadcastMs = 2000;
    static constexpr int kIdleBroadcastMs = 8000;
    // How many addresses one message is copied to. A session belongs to the
    // peer's identity now, so the same signed datagram is valid from any of its
    // interfaces and a spare copy is cheap redundancy - but the list of
    // addresses comes from the peer, and a peer advertising five thousand of
    // them would have us send five thousand datagrams per keystroke. Four
    // covers a LAN address, a VPN address and a spare.
    static constexpr int kMaxDeliveryAddresses = 4;

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
    // Every address one message to this peer is sent to, newest observed first
    // and never more than kMaxDeliveryAddresses of them. Public because the cap
    // is the whole point of it and a test has to be able to read it back.
    QVector<QString> deliveryAddresses(const QString &toIp) const;

    // Safe to call before start(), where it applies on the next one, or
    // while running, where it raises or drops the relay tunnel on the spot.
    Q_INVOKABLE void setConnectionMode(ConnectionMode mode);
    Q_INVOKABLE ConnectionMode connectionMode() const
    {
        return m_mode;
    }
    // True once a relay is actually usable - either a custom one was set via
    // setRelayServer(), or the built-in list (network/Protocol.h) is
    // non-empty. False means Vds mode can be selected but won't connect to
    // anything yet - surface this in the UI before letting the user pick it.
    Q_INVOKABLE bool vdsConfigured() const;

    // Whether a mode has anything behind it. The settings page asks rather
    // than hardcoding the answer, so landing a K-Server means changing this
    // function and nothing in QML.
    Q_INVOKABLE bool modeAvailable(int mode) const;

    // Profile fields advertised in presence. main() feeds these from
    // AppSettings so the network layer stays unaware of that module.
    // revision is a short digest of everything in the profile,
    // including the images that are too big to broadcast, so a peer
    // can tell it needs to re-fetch without being sent the files.
    Q_INVOKABLE void setProfile(const QString &handle, const QString &displayName, const QString &bio, const QString &revision);

    // Shared secret for the public chat. Broadcast has no single peer to
    // hold an ECDH session with, so a passphrase everyone knows is the only
    // thing that can protect it. Empty means the chat goes out in the clear.
    void setGroupPassphrase(const QString &passphrase);

    // Custom/self-hosted relay server. voicePort defaults to tunnelPort + 1
    // if not given. TODO: persist across restarts once AppSettings lands.
    Q_INVOKABLE void setRelayServer(const QString &host, quint16 tunnelPort, quint16 voicePort = 0);

    // incoming messages
    // One received datagram, from parsing to whatever handler it belongs to.
    // Split out of onUdpReadyRead() so the packet path can be driven without a
    // bound socket: everything below this line is attacker-controlled, and a
    // test that has to stand up two live UDP sockets to reach it is a test that
    // does not get written.
    void handleDatagram(const QString &host, const QByteArray &data);

    // outgoing messages
    void sendUdp(QJsonObject payload, const QString &targetIp = QString());
    Q_INVOKABLE void sendPrivate(const QString &text, const QString &toIp);
    Q_INVOKABLE void sendGroupMessage(const QString &gid, const QString &text, const QVector<QString> &members);
    Q_INVOKABLE void sendTyping(const QString &chatId, const QString &targetIp = QString());
    Q_INVOKABLE void sendCallRequest(const QString &toIp);
    Q_INVOKABLE void sendCallAccept(const QString &toIp);
    Q_INVOKABLE void sendCallReject(const QString &toIp);
    Q_INVOKABLE void sendCallEnd(const QString &toIp);
    Q_INVOKABLE void sendReaction(const QString &toIp, const QString &chatId, double ts, const QString &emoji, bool added);
    Q_INVOKABLE void sendMessageEdit(const QString &toIp, const QString &chatId, double ts, const QString &newText);
    Q_INVOKABLE void sendMessageDelete(const QString &toIp, const QString &chatId, double ts);
    Q_INVOKABLE void sendReadReceipt(const QString &toIp, const QString &chatId);
    Q_INVOKABLE void sendGroupInvite(const QString &gid, const QString &gname, const QString &toIp);
    void sendFileInternal(const QString &toIp, const QString &filePath, const QByteArray &rawBytes = {}, const QString &filename = QStringLiteral("file"));
    // QML-facing overload - QML can't supply the QByteArray/filename default
    // args cleanly, so this is the entry point for "attach file" in the UI.
    Q_INVOKABLE void sendFile(const QString &toIp, const QString &filePath);

    // voice TCP
    // Starts the connect and returns straight away: true means an attempt is
    // in flight (or a socket already exists), false only that there is nothing
    // to connect to. The outcome arrives as voiceConnected() or, on failure,
    // voiceDisconnected() with an errorOccurred() before it.
    bool connectVoice(const QString &ip);
    bool sendVoice(const QString &ip, const QByteArray &data);
    void disconnectVoice(const QString &ip);

Q_SIGNALS:
    void hostIpChanged();
    void userOnline(QJsonObject peerInfo);
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
    void typing(QString username, QString chatId);
    void voiceConnected(QString ip);
    void voiceDisconnected(QString ip);

private Q_SLOTS:
    void onUdpReadyRead();
    void onNewTcpConnection();
    void onBroadcastTimer();
    void refreshLocalIps();
    void scanArpTable();

private:
    // setup helpers
    QJsonObject presencePayload() const;
    // Stamps the packet, signs it once for the peer identity behind these
    // addresses, and writes the same bytes to each of them. One signature for
    // the whole set is the point: it is what a session keyed on identity buys.
    // An empty list means broadcast.
    void sendUdpToAll(QJsonObject payload, const QVector<QString> &targets);
    void dispatch(const QString &host, QJsonObject msg);
    void handlePresence(const QString &host, QJsonObject msg);
    void decryptMessageText(const QString &peerRef, QJsonObject &msg) const;
    // Files a socket under an address, deleting whatever it displaces. Both the
    // inbound and the outbound path can find one already there.
    void replaceVoiceSocket(const QString &ip, QTcpSocket *sock);
    void onVoiceData(QTcpSocket *sock, const QString &ip);
    void onVoiceDisconnected(QTcpSocket *sock, const QString &ip);
    void startInternetTunnel();
    void onRelayData();
    // Fires startInternetTunnel() again after the current backoff and doubles
    // it, capped at kRelayReconnectMaxMs.
    void scheduleRelayReconnect();
    void sendChunksQueued(const QVector<QJsonObject> &chunks, const QString &toIp, int idx, int batch = 3);
    void pruneStalePeers();

    CryptoManager *m_crypto = nullptr;

    QUdpSocket *m_udp = nullptr;
    QTcpServer *m_tcpServer = nullptr;
    QMap<QString, QTcpSocket *> m_voiceConnections; // ip -> voice socket
    QMap<QString, QTcpSocket *> m_pendingVoice; // ip -> socket still connecting
    // Partial frames per voice socket. Keyed on the socket rather than the IP
    // because the inbound and outbound socket for one peer are different
    // streams and must not share a buffer.
    QHash<QTcpSocket *, QByteArray> m_voiceRxBuffers;
    // The address a peer is filed under is the first one we heard it on, not
    // the one it advertises - a multi-homed host sends from whichever interface
    // the route picked. m_peerKeyById keeps one entry per identity, so a peer
    // broadcasting on three interfaces is one contact and not three.
    QMap<QString, QJsonObject> m_peers; // observed address -> peer info
    QHash<QString, QString> m_peerKeyById; // identity id -> its key in m_peers

    QString m_hostIp;
    QSet<QString> m_localIps;
    bool m_running = false;
    bool m_internetMode = false;

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
    QString m_relayHostOverride; // set via setRelayServer()
    quint16 m_relayPortOverride = 0;
    quint16 m_relayVoicePortOverride = 0;
    int m_relayReconnectMs = protocol::kRelayReconnectBaseMs; // grows via backoff, see .cpp

    double m_lastScan = 0.0;
};

} // namespace koutnet
