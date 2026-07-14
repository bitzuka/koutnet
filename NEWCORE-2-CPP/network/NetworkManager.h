// KOutNet — Network & Audio core
// Ported from gdf_network.py ( NT Server 1.8) → C++/Qt6
#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QHostAddress>
#include <QJsonObject>
#include <QSet>
#include <QMap>
#include <QVector>

namespace koutnet {

class CryptoManager;
// TODO: AppSettings (S() equivalent) — core/constructor not yet ported.
class AppSettings;

class NetworkManager : public QObject {
    Q_OBJECT

public:
    // CryptoManager is owned by the application (one instance shared across
    // NetworkManager / VoiceCallManager / UI) and injected here, never
    // created internally — identity keys and session state must stay
    // single-sourced. See core/security/CryptoManager.
    explicit NetworkManager(CryptoManager *crypto, QObject *parent = nullptr);
    ~NetworkManager() override;

    bool start();
    void stop();

    bool isRunning() const { return m_running; }
    QString hostIp() const { return m_hostIp; }
    const QMap<QString, QJsonObject> &peers() const { return m_peers; }

    // I_Do_It_Latet.! — self-hosted / custom relay server support, and
    // selection of a default public relay when the user hasn't configured
    // one. LAN discovery (broadcast/mDNS/ARP) needs none of this and already
    // works without a server — see onBroadcastTimer / scanArpTable.
    void setRelayServer(const QString &host, quint16 port);

    // ── outgoing messages ───────────────────────────────────────────
    void sendUdp(QJsonObject payload, const QString &targetIp = QString());
    void sendChat(const QString &text);
    void sendPrivate(const QString &text, const QString &toIp);
    void sendGroupMessage(const QString &gid, const QString &text,
                          const QVector<QString> &members);
    void sendTyping(const QString &chatId, const QString &targetIp = QString());
    void sendCallRequest(const QString &toIp);
    void sendCallAccept(const QString &toIp);
    void sendCallReject(const QString &toIp);
    void sendCallEnd(const QString &toIp);
    void sendReaction(const QString &toIp, const QString &chatId,
                      double ts, const QString &emoji, bool added);
    void sendMessageEdit(const QString &toIp, const QString &chatId,
                         double ts, const QString &newText);
    void sendMessageDelete(const QString &toIp, const QString &chatId, double ts);
    void sendReadReceipt(const QString &toIp, const QString &chatId);
    void sendGroupInvite(const QString &gid, const QString &gname, const QString &toIp);
    void sendFile(const QString &toIp, const QString &filePath,
                  const QByteArray &rawBytes = {}, const QString &filename = "file");

    // ── voice TCP ────────────────────────────────────────────────────
    bool connectVoice(const QString &ip);
    bool sendVoice(const QString &ip, const QByteArray &data);
    void disconnectVoice(const QString &ip);

signals:
    void userOnline(QJsonObject peerInfo);
    void userOffline(QString ip);
    void message(QJsonObject msg);
    void callRequest(QString username, QString ip);
    void callAccepted(QString username, QString ip);
    void callRejected(QString ip);
    void callEnded(QString ip);
    void voiceData(QByteArray raw);              // legacy single-call
    void voiceDataFrom(QString ip, QByteArray raw);
    void fileMeta(QJsonObject meta);
    void groupInvite(QString groupId, QString name, QString fromIp);
    void errorOccurred(QString message);
    void typing(QString username, QString chatId);
    void voiceConnected(QString ip);
    void voiceDisconnected(QString ip);

private slots:
    void onUdpReadyRead();
    void onNewTcpConnection();
    void onBroadcastTimer();
    void refreshLocalIps();
    void scanArpTable();

private:
    // ── setup helpers ───────────────────────────────────────────────
    QJsonObject presencePayload() const;
    void dispatch(const QString &host, QJsonObject msg);
    void handlePresence(const QString &host, QJsonObject msg);
    void decryptMessageText(const QString &fromIp, QJsonObject &msg) const;
    void onVoiceData(QTcpSocket *sock, const QString &ip);
    void onVoiceDisconnected(const QString &ip);
    void startInternetTunnel();
    void sendChunksQueued(const QVector<QJsonObject> &chunks,
                          const QString &toIp, int idx, int batch = 3);
    void pruneStalePeers();

    CryptoManager *m_crypto = nullptr;

    QUdpSocket *m_udp = nullptr;
    QTcpServer *m_tcpServer = nullptr;
    QMap<QString, QTcpSocket *> m_voiceConnections;   // ip -> voice socket
    QMap<QString, QJsonObject>  m_peers;              // ip -> peer info

    QString m_hostIp;
    QSet<QString> m_localIps;
    bool m_running = false;
    bool m_internetMode = false;

    quint16 m_voiceTcpPort = 0;

    QTimer m_broadcastTimer;
    QTimer m_ipRefreshTimer;

    // Relay / tunnel (internet mode) — TODO: move to network/vds module
    QTcpSocket *m_relaySocket = nullptr;
    bool m_relayConnected = false;
    QByteArray m_relayBuffer;
    QString m_relayHostOverride;   // I_Do_It_Latet.! — set via setRelayServer()
    quint16 m_relayPortOverride = 0;

    double m_lastScan = 0.0;
};

} // namespace koutnet
