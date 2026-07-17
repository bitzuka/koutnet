// KOutNet — Network & Audio core
// Ported from gdf_network.py ( NT Server 1.8) → C++/Qt6
#include "NetworkManager.h"
#include "Protocol.h"
#include "../core/security/CryptoManager.h"
#include <QNetworkInterface>
#include <QNetworkDatagram>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QRandomGenerator>
#include <QFile>
#include <QFileInfo>

// TODO: wire this up once core/constructor lands:
//  - core/constructor    -> AppSettings (S() equivalent) — group passphrase,
//    static peer list, connection mode (LAN/internet), relay credentials.
//    See I_Do_It_Latet.! markers below and in Protocol.h.
// core/security (CryptoManager) is already wired in — see constructor.

namespace koutnet {

namespace {

QString localIpFallback()
{
    const auto addrs = QNetworkInterface::allAddresses();
    for (const auto &addr : addrs) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback())
            return addr.toString();
    }
    return QStringLiteral("127.0.0.1");
}

QSet<QString> allLocalIpsFallback()
{
    QSet<QString> result;
    const auto addrs = QNetworkInterface::allAddresses();
    for (const auto &addr : addrs) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol)
            result.insert(addr.toString());
    }
    return result;
}

QString randomHex(int bytes)
{
    QByteArray buf(bytes, 0);
    for (int i = 0; i < bytes; ++i)
        buf[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    return buf.toHex();
}

double nowEpoch()
{
    return QDateTime::currentMSecsSinceEpoch() / 1000.0;
}

// Canonical bytes used for HMAC sign/verify: compact JSON of the payload
// with "_sig" removed (or absent). Both sides must build this identically.
QByteArray signableBytes(QJsonObject obj)
{
    obj.remove("_sig");
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

} // namespace

NetworkManager::NetworkManager(CryptoManager *crypto, QObject *parent)
    : QObject(parent), m_crypto(crypto)
{
    m_hostIp = localIpFallback();

    connect(&m_broadcastTimer, &QTimer::timeout, this, &NetworkManager::onBroadcastTimer);
    connect(&m_ipRefreshTimer, &QTimer::timeout, this, &NetworkManager::refreshLocalIps);
    m_ipRefreshTimer.start(30'000); // refresh local IPs every 30s (VPN adapters etc.)
}

NetworkManager::~NetworkManager()
{
    stop();
}

void NetworkManager::setRelayServer(const QString &host, quint16 tunnelPort, quint16 voicePort)
{
    // TODO: persist across restarts once AppSettings lands.
    m_relayHostOverride = host;
    m_relayPortOverride = tunnelPort;
    m_relayVoicePortOverride = voicePort ? voicePort : quint16(tunnelPort + 1);
}

void NetworkManager::setConnectionMode(ConnectionMode mode)
{
    const bool wantVds = (mode == ConnectionMode::Vds);
    if (wantVds == m_internetMode)
        return;

    m_internetMode = wantVds;
    if (!m_running)
        return; // applied on next start()

    if (m_internetMode) {
        m_relayReconnectMs = protocol::kRelayReconnectBaseMs;
        startInternetTunnel();
    } else if (m_relaySocket) {
        m_relaySocket->disconnect(this); // don't trigger the reconnect-on-disconnect handler below
        m_relaySocket->close();
        m_relaySocket->deleteLater();
        m_relaySocket = nullptr;
        m_relayConnected = false;
    }
}

bool NetworkManager::vdsConfigured() const
{
    if (!m_relayHostOverride.isEmpty() && m_relayPortOverride != 0)
        return true;
    return !protocol::builtinRelays().isEmpty();
}

void NetworkManager::refreshLocalIps()
{
    m_localIps = allLocalIpsFallback();
    m_localIps.insert(m_hostIp);
    const QString newPrimary = localIpFallback();
    if (newPrimary != m_hostIp)
        m_hostIp = newPrimary;
}

bool NetworkManager::start()
{
    const quint16 udpPort = protocol::kUdpPortDefault;
    const quint16 tcpPort = protocol::kTcpPortDefault;
    m_voiceTcpPort = tcpPort;

    // ── UDP socket ──────────────────────────────────────────────────
    m_udp = new QUdpSocket(this);
    bool bound = m_udp->bind(QHostAddress::AnyIPv4, udpPort,
                             QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    if (!bound) {
        bound = m_udp->bind(QHostAddress::AnyIPv4, udpPort + 1,
                            QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    }
    if (!bound) {
        emit errorOccurred(QStringLiteral("UDP bind failed on port %1").arg(udpPort));
        return false;
    }
    connect(m_udp, &QUdpSocket::readyRead, this, &NetworkManager::onUdpReadyRead);

    // mDNS-like discovery multicast group (best-effort)
    m_udp->joinMulticastGroup(QHostAddress("224.0.0.251"));

    // ── TCP server (voice) ─────────────────────────────────────────
    m_tcpServer = new QTcpServer(this);
    if (!m_tcpServer->listen(QHostAddress::AnyIPv4, tcpPort)) {
        if (!m_tcpServer->listen(QHostAddress::AnyIPv4, 0)) {
            emit errorOccurred(QStringLiteral("TCP listen failed on port %1").arg(tcpPort));
            return false;
        }
    }
    connect(m_tcpServer, &QTcpServer::newConnection, this, &NetworkManager::onNewTcpConnection);

    m_running = true;
    // Connection mode is controlled explicitly via setConnectionMode()
    // (defaults to LanOrVpn, see header) instead of being force-reset here.
    // TODO: once AppSettings lands, read the persisted mode before start().
    m_localIps = allLocalIpsFallback();
    m_localIps.insert(m_hostIp);

    m_broadcastTimer.start(kActiveBroadcastMs); // fast discovery until peers are found
    if (m_internetMode)
        startInternetTunnel();

    onBroadcastTimer(); // first broadcast immediately

    QTimer::singleShot(3000, this, &NetworkManager::scanArpTable);
    return true;
}

void NetworkManager::stop()
{
    m_running = false;
    m_broadcastTimer.stop();

    for (auto *sock : std::as_const(m_voiceConnections))
        sock->disconnectFromHost();
    m_voiceConnections.clear();

    if (m_internetMode && m_relaySocket) {
        m_relaySocket->close();
        m_relaySocket = nullptr;
    }
    if (m_udp) {
        m_udp->close();
        m_udp->deleteLater();
        m_udp = nullptr;
    }
    if (m_tcpServer) {
        m_tcpServer->close();
        m_tcpServer->deleteLater();
        m_tcpServer = nullptr;
    }
}

void NetworkManager::scanArpTable()
{
    // Reads the OS ARP cache and pings every neighbour with a presence packet.
    // Linux: /proc/net/arp. Windows/macOS: `arp -a` (TODO — process-based parse).
    if (!m_running)
        return;

    QSet<QString> ips;
#ifdef Q_OS_LINUX
    QFile arpFile("/proc/net/arp");
    if (arpFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const auto lines = QString::fromUtf8(arpFile.readAll()).split('\n');
        for (int i = 1; i < lines.size(); ++i) {
            const auto parts = lines[i].split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 4 && parts[2] != QLatin1String("0x0"))
                ips.insert(parts[0]);
        }
    }
#endif
    if (ips.isEmpty()) {
        QTimer::singleShot(60'000, this, &NetworkManager::scanArpTable);
        return;
    }

    const QJsonObject payload = presencePayload();
    const QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const quint16 port = protocol::kUdpPortDefault;

    int sent = 0;
    for (const auto &ip : std::as_const(ips)) {
        if (ip != m_hostIp && !ip.startsWith(QLatin1String("169.254"))) {
            m_udp->writeDatagram(data, QHostAddress(ip), port);
            ++sent;
        }
    }
    Q_UNUSED(sent);

    QTimer::singleShot(60'000, this, &NetworkManager::scanArpTable);
}

QJsonObject NetworkManager::presencePayload() const
{
    QJsonObject payload;
    payload["type"] = protocol::kMsgPresence;
    payload["ip"] = m_hostIp;
    QJsonArray allIps;
    for (const auto &ip : m_localIps)
        allIps.append(ip);
    payload["all_ips"] = allIps;
    payload["os"] = QSysInfo::prettyProductName();
    payload["version"] = QStringLiteral("2.0");
    payload["protocol_version"] = protocol::kProtocolVersion;
    payload["ts"] = nowEpoch();
    payload["nonce"] = randomHex(8);

    // ECDH handshake bundle — lets any peer that sees this presence packet
    // derive a session key with us (Layer 1, see CryptoManager).
    if (m_crypto) {
        const QJsonObject hs = m_crypto->handshakePayload();
        for (auto it = hs.constBegin(); it != hs.constEnd(); ++it)
            payload[it.key()] = it.value();
    }

    // TODO: username, avatar, bio, status, premium — pull from AppSettings
    // once that module is ported.
    return payload;
}

void NetworkManager::onBroadcastTimer()
{
    if (m_internetMode) // internet mode never LAN-broadcasts
        return;
    if (!m_running || !m_udp)
        return;

    // Adaptive interval: broadcast aggressively while we have no peers yet,
    // then back off once discovery has succeeded. Also reduces how "loud"
    // the full /24 sweep below looks on corporate/public Wi-Fi.
    const int desiredInterval = m_peers.isEmpty() ? kActiveBroadcastMs : kIdleBroadcastMs;
    if (m_broadcastTimer.interval() != desiredInterval)
        m_broadcastTimer.setInterval(desiredInterval);

    const QJsonObject payload = presencePayload();
    const QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const quint16 port = protocol::kUdpPortDefault;

    // 1. Global LAN broadcast
    m_udp->writeDatagram(data, QHostAddress::Broadcast, port);

    // 2. Per-interface subnet broadcasts
    QSet<QString> sentBroadcasts;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto &iface : interfaces) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || (flags & QNetworkInterface::IsLoopBack))
            continue;
        for (const auto &entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            const QString bcast = entry.broadcast().toString();
            if (!bcast.isEmpty() && bcast != QLatin1String("0.0.0.0") && !sentBroadcasts.contains(bcast)) {
                m_udp->writeDatagram(data, entry.broadcast(), port);
                sentBroadcasts.insert(bcast);
            }
        }
    }

    // 3. mDNS multicast
    m_udp->writeDatagram(data, QHostAddress("224.0.0.251"), port);

    // 4. Unicast /24 subnet scan every 30s (fallback if broadcast blocked)
    const double now = nowEpoch();
    const double scanIntervalSec = m_peers.isEmpty() ? 30.0 : 120.0;
    if (now - m_lastScan > scanIntervalSec) {
        m_lastScan = now;
        const auto parts = m_hostIp.split('.');
        if (parts.size() == 4) {
            const QString prefix = parts[0] + '.' + parts[1] + '.' + parts[2] + '.';
            for (int last = 1; last < 255; ++last) {
                const QString target = prefix + QString::number(last);
                if (target != m_hostIp)
                    m_udp->writeDatagram(data, QHostAddress(target), port);
            }
        }
    }

    // 5. TODO: unicast to manually-added static peers (AppSettings::staticPeers())
    // 6. I_Do_It_Latet.! — relay server unicast (needs default/custom relay,
    //    see setRelayServer() and Protocol::kRelayHost).

    pruneStalePeers();
}

void NetworkManager::pruneStalePeers()
{
    const double now = nowEpoch();
    QVector<QString> stale;
    for (auto it = m_peers.constBegin(); it != m_peers.constEnd(); ++it) {
        const double lastSeen = it.value()["last_seen"].toDouble();
        if (now - lastSeen > 25)
            stale.append(it.key());
    }
    for (const auto &ip : stale) {
        m_peers.remove(ip);
        emit userOffline(ip);
    }
}

void NetworkManager::onUdpReadyRead()
{
    while (m_udp && m_udp->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_udp->receiveDatagram();
        QString host = dg.senderAddress().toString();
        if (host.startsWith(QLatin1String("::ffff:")))
            host = host.mid(7);

        const auto doc = QJsonDocument::fromJson(dg.data());
        if (!doc.isObject()) {
            emit errorOccurred(QStringLiteral("UDP parse error from %1").arg(host));
            continue;
        }
        dispatch(host, doc.object());
    }
}

void NetworkManager::dispatch(const QString &host, QJsonObject msg)
{
    // Layer 6 — rate limiting: max N packets/sec per source IP.
    if (m_crypto && !m_crypto->checkRate(host)) {
        return; // dropped — over rate limit
    }

    const QString msgFromIp = msg.value("from_ip").toString();
    const QSet<QString> myIps = m_localIps.isEmpty() ? QSet<QString>{m_hostIp} : m_localIps;
    const QString type = msg.value("type").toString();

    if (type != protocol::kMsgPresence && !type.isEmpty()) {
        if (!msgFromIp.isEmpty() && myIps.contains(msgFromIp))
            return; // own message echoed back
        if (myIps.contains(host))
            return; // own broadcast echoed back
    }

    // Layer 4 — HMAC verification (only meaningful once a session exists;
    // verifyPacket() itself returns true if there's no session yet, so
    // unauthenticated peers can still complete their first handshake).
    if (m_crypto && type != protocol::kMsgPresence && !type.isEmpty()) {
        const QString sig = msg.value("_sig").toString();
        if (!sig.isEmpty()) {
            const QByteArray payloadBytes = signableBytes(msg);
            if (!m_crypto->verifyPacket(host, payloadBytes, sig)) {
                emit errorOccurred(QStringLiteral("HMAC verification failed from %1 — dropping").arg(host));
                return;
            }
        }
    }

    if (type == protocol::kMsgPresence) {
        handlePresence(host, msg);
    } else if (type == protocol::kMsgChat || type == protocol::kMsgGroup
               || type == protocol::kMsgReaction || type == protocol::kMsgEdit
               || type == protocol::kMsgDelete || type == protocol::kMsgRead) {
        decryptMessageText(host, msg);
        emit message(msg);
    } else if (type == protocol::kMsgPrivate) {
        if (msg.value("to").toString() == m_hostIp) {
            decryptMessageText(host, msg);
            emit message(msg);
        }
    } else if (type == protocol::kMsgCallReq) {
        // TODO: check VoiceCallManager::active() and reply call_busy if in a call
        emit callRequest(msg.value("username").toString("?"), host);
    } else if (type == protocol::kMsgCallAccept) {
        emit callAccepted(msg.value("username").toString("?"), host);
    } else if (type == protocol::kMsgCallBusy || type == protocol::kMsgCallReject) {
        emit callRejected(host);
    } else if (type == protocol::kMsgCallEnd) {
        emit callEnded(host);
    } else if (type == protocol::kMsgFileMeta) {
        emit fileMeta(msg);
    } else if (type == protocol::kMsgFileData) {
        emit fileChunk(msg);
    } else if (type == protocol::kMsgGroupInv) {
        emit groupInvite(msg.value("gid").toString(), msg.value("gname").toString(), host);
    } else if (type == protocol::kMsgTyping) {
        emit typing(msg.value("username").toString(), msg.value("chat_id").toString("public"));
    }
}

void NetworkManager::decryptMessageText(const QString &fromIp, QJsonObject &msg) const
{
    if (!m_crypto || !msg.value("encrypted").toBool(false))
        return;

    const QString cipherText = msg.value("text").toString();
    // I_Do_It_Latet.! — group passphrase should come from AppSettings once
    // ported; until then only ECDH session decryption (peer-to-peer) works,
    // matching CryptoManager::decrypt()'s own plaintext-passthrough fallback.
    const QString plain = m_crypto->decrypt(cipherText, QString(), fromIp);
    msg["text"] = plain;
}

void NetworkManager::handlePresence(const QString &host, QJsonObject msg)
{
    QString ip = msg.value("ip").toString(host);
    const QSet<QString> myIps = m_localIps.isEmpty() ? QSet<QString>{m_hostIp} : m_localIps;
    if (myIps.contains(ip) || myIps.contains(host))
        return;

    if (host != ip && !host.isEmpty() && host != QLatin1String("0.0.0.0"))
        msg["source_ip"] = host;

    // Layer 5 — replay guard on presence packets (nonce + timestamp window).
    if (m_crypto) {
        const QString nonce = msg.value("nonce").toString();
        const double ts = msg.value("ts").toDouble();
        if (!nonce.isEmpty() && !m_crypto->checkReplay(ip, nonce, ts))
            return; // replayed presence packet

        // ECDH handshake — derives (or refreshes) the session key with this peer.
        if (msg.contains("dh_pub"))
            m_crypto->processHandshake(ip, msg);
    }

    const bool isNew = !m_peers.contains(ip);
    msg["last_seen"] = nowEpoch();
    // TODO: msg["conn_type"] = detectConnectionType(ip);
    if (m_crypto)
        msg["e2e"] = m_crypto->hasSession(ip);
    m_peers[ip] = msg;

    if (isNew)
        emit userOnline(msg);
}

void NetworkManager::onNewTcpConnection()
{
    while (m_tcpServer && m_tcpServer->hasPendingConnections()) {
        QTcpSocket *sock = m_tcpServer->nextPendingConnection();
        QString ip = sock->peerAddress().toString();
        if (ip.startsWith(QLatin1String("::ffff:")))
            ip = ip.mid(7);

        m_voiceConnections[ip] = sock;
        connect(sock, &QTcpSocket::readyRead, this, [this, sock, ip] { onVoiceData(sock, ip); });
        connect(sock, &QTcpSocket::disconnected, this, [this, ip] { onVoiceDisconnected(ip); });
        emit voiceConnected(ip);
    }
}

void NetworkManager::onVoiceData(QTcpSocket *sock, const QString &ip)
{
    while (sock->bytesAvailable() > 0) {
        const QByteArray data = sock->read(2048);
        if (!data.isEmpty()) {
            emit voiceData(data);          // legacy single-call path
            emit voiceDataFrom(ip, data);  // group-call mixer path — VoiceCallManager
                                            // decrypts (CryptoManager::decryptBytes)
                                            // before pushing into the jitter buffer.
        }
    }
}

void NetworkManager::onVoiceDisconnected(const QString &ip)
{
    m_voiceConnections.remove(ip);
    emit voiceDisconnected(ip);
    emit callEnded(ip);
}

// ── internet relay tunnel (TODO: move to network/vds once that module lands) ──
void NetworkManager::startInternetTunnel()
{
    QString host = m_relayHostOverride;
    quint16 port = m_relayPortOverride;

    if (host.isEmpty() || port == 0) {
        const auto &builtins = protocol::builtinRelays();
        if (!builtins.isEmpty()) {
            host = QString::fromLatin1(builtins.first().host);
            port = builtins.first().tunnelPort;
        }
    }

    if (host.isEmpty() || port == 0) {
        // No VDS configured yet (no built-in relay ships, no custom one set
        // via setRelayServer()). Don't spam-reconnect for something that
        // can't possibly succeed — just check back periodically in case the
        // user configures one, or an update ships a built-in relay.
        emit errorOccurred(QStringLiteral(
            "VDS mode is on but no relay server is configured — call setRelayServer(), "
            "or switch back to LAN/VPN mode."));
        QTimer::singleShot(protocol::kRelayReconnectMaxMs, this, [this] {
            if (m_internetMode && m_running)
                startInternetTunnel();
        });
        return;
    }

    m_relaySocket = new QTcpSocket(this);
    connect(m_relaySocket, &QTcpSocket::connected, this, [this] {
        m_relayReconnectMs = protocol::kRelayReconnectBaseMs; // reset backoff on success
    });
    connect(m_relaySocket, &QTcpSocket::disconnected, this, [this] {
        m_relayConnected = false;
        const int delay = m_relayReconnectMs;
        m_relayReconnectMs = qMin(m_relayReconnectMs * 2, protocol::kRelayReconnectMaxMs);
        QTimer::singleShot(delay, this, [this] {
            if (m_internetMode && m_running)
                startInternetTunnel();
        });
    });
    m_relaySocket->connectToHost(host, port);
    m_relayConnected = m_relaySocket->waitForConnected(3000);
    if (m_relayConnected) {
        m_relayReconnectMs = protocol::kRelayReconnectBaseMs;
        onBroadcastTimer();
    } else {
        emit errorOccurred(QStringLiteral("Tunnel connect failed"));
        const int delay = m_relayReconnectMs;
        m_relayReconnectMs = qMin(m_relayReconnectMs * 2, protocol::kRelayReconnectMaxMs);
        QTimer::singleShot(delay, this, [this] {
            if (m_internetMode && m_running)
                startInternetTunnel();
        });
    }
    // TODO: length-prefixed frame reader (4-byte BE length + JSON payload)
    // feeding into dispatch(), matching the Python _tunnel_recv_loop.
}

// ── outgoing ─────────────────────────────────────────────────────────
void NetworkManager::sendUdp(QJsonObject payload, const QString &targetIp)
{
    if (!m_udp)
        return;

    const QString msgType = payload.value("type").toString();
    if (msgType != protocol::kMsgPresence) {
        payload["nonce"] = randomHex(8);
        payload["ts"] = nowEpoch();

        // Layer 4 — HMAC-sign unicast packets once a session key exists with
        // the target (broadcasts have no single peer session to sign for).
        if (m_crypto && !targetIp.isEmpty() && m_crypto->hasSession(targetIp)) {
            const QByteArray payloadBytes = signableBytes(payload);
            payload["_sig"] = m_crypto->signPacket(targetIp, payloadBytes);
        }
    }

    const QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    if (m_internetMode) {
        if (m_relaySocket && m_relayConnected) {
            QByteArray header(4, 0);
            const quint32 len = static_cast<quint32>(data.size());
            header[0] = char((len >> 24) & 0xFF);
            header[1] = char((len >> 16) & 0xFF);
            header[2] = char((len >> 8) & 0xFF);
            header[3] = char(len & 0xFF);
            m_relaySocket->write(header + data);
        }
    } else if (!targetIp.isEmpty()) {
        m_udp->writeDatagram(data, QHostAddress(targetIp), protocol::kUdpPortDefault);
    } else {
        m_udp->writeDatagram(data, QHostAddress::Broadcast, protocol::kUdpPortDefault);
    }
}

void NetworkManager::sendChat(const QString &text)
{
    // Public/broadcast chat has no single peer to hold an ECDH session with,
    // so it can only be protected by a shared group passphrase.
    // I_Do_It_Latet.! — passphrase should come from AppSettings once ported.
    QJsonObject payload;
    payload["type"] = protocol::kMsgChat;
    payload["text"] = text;
    payload["from_ip"] = m_hostIp;
    payload["encrypted"] = false;
    sendUdp(payload);
}

void NetworkManager::sendPrivate(const QString &text, const QString &toIp)
{
    QString outText = text;
    bool encrypted = false;

    if (m_crypto) {
        // I_Do_It_Latet.! — passphrase fallback source (AppSettings) once ported;
        // for now this only actually encrypts once an ECDH session exists.
        const QString cipherText = m_crypto->encrypt(text, QString(), toIp);
        if (cipherText != text) {
            outText = cipherText;
            encrypted = true;
        }
    }

    QJsonObject payload;
    payload["type"] = protocol::kMsgPrivate;
    payload["text"] = outText;
    payload["to"] = toIp;
    payload["from_ip"] = m_hostIp;
    payload["encrypted"] = encrypted;
    sendUdp(payload, toIp);

    // Also send to alternate IPs the peer reported (VPN/LAN redundancy)
    const auto peerInfo = m_peers.value(toIp);
    const auto altIps = peerInfo.value("all_ips").toArray();
    for (const auto &v : altIps) {
        const QString altIp = v.toString();
        if (altIp != toIp && !m_localIps.contains(altIp))
            sendUdp(payload, altIp);
    }
}

void NetworkManager::sendGroupMessage(const QString &gid, const QString &text,
                                      const QVector<QString> &members)
{
    // I_Do_It_Latet.! — group E2E (per-group passphrase or per-member ECDH
    // fan-out) needs AppSettings for the passphrase; sent plaintext for now.
    QJsonObject payload;
    payload["type"] = protocol::kMsgGroup;
    payload["gid"] = gid;
    payload["text"] = text;
    payload["ts"] = nowEpoch();
    for (const auto &ip : members) {
        if (ip != m_hostIp)
            sendUdp(payload, ip);
    }
}

void NetworkManager::sendTyping(const QString &chatId, const QString &targetIp)
{
    QJsonObject payload;
    payload["type"] = protocol::kMsgTyping;
    payload["chat_id"] = chatId;
    sendUdp(payload, targetIp);
}

void NetworkManager::sendCallRequest(const QString &toIp)
{
    QJsonObject payload;
    payload["type"] = protocol::kMsgCallReq;
    sendUdp(payload, toIp);
}

void NetworkManager::sendCallAccept(const QString &toIp)
{
    QJsonObject payload;
    payload["type"] = protocol::kMsgCallAccept;
    sendUdp(payload, toIp);
    connectVoice(toIp);
}

void NetworkManager::sendCallReject(const QString &toIp)
{
    QJsonObject payload;
    payload["type"] = protocol::kMsgCallReject;
    sendUdp(payload, toIp);
}

void NetworkManager::sendCallEnd(const QString &toIp)
{
    QJsonObject payload;
    payload["type"] = protocol::kMsgCallEnd;
    sendUdp(payload, toIp);
}

void NetworkManager::sendReaction(const QString &toIp, const QString &chatId,
                                  double ts, const QString &emoji, bool added)
{
    QJsonObject payload;
    payload["type"] = protocol::kMsgReaction;
    payload["chat_id"] = chatId;
    payload["msg_ts"] = ts;
    payload["emoji"] = emoji;
    payload["added"] = added;
    sendUdp(payload, toIp);
}

void NetworkManager::sendMessageEdit(const QString &toIp, const QString &chatId,
                                     double ts, const QString &newText)
{
    QJsonObject payload;
    payload["type"] = protocol::kMsgEdit;
    payload["chat_id"] = chatId;
    payload["msg_ts"] = ts;
    payload["new_text"] = newText;
    sendUdp(payload, toIp);
}

void NetworkManager::sendMessageDelete(const QString &toIp, const QString &chatId, double ts)
{
    QJsonObject payload;
    payload["type"] = protocol::kMsgDelete;
    payload["chat_id"] = chatId;
    payload["msg_ts"] = ts;
    sendUdp(payload, toIp);
}

void NetworkManager::sendReadReceipt(const QString &toIp, const QString &chatId)
{
    QJsonObject payload;
    payload["type"] = protocol::kMsgRead;
    payload["chat_id"] = chatId;
    sendUdp(payload, toIp);
}

void NetworkManager::sendGroupInvite(const QString &gid, const QString &gname, const QString &toIp)
{
    QJsonObject payload;
    payload["type"] = protocol::kMsgGroupInv;
    payload["gid"] = gid;
    payload["gname"] = gname;
    sendUdp(payload, toIp);
}

void NetworkManager::sendFileInternal(const QString &toIp, const QString &filePath,
                              const QByteArray &rawBytes, const QString &filename)
{
    // TODO: encrypt file bytes via CryptoManager::encryptBytes before chunking,
    // same as voice — not wired yet, tracked separately from the E2E pass above.
    QByteArray data;
    QString fname;
    QString ext;

    if (!rawBytes.isEmpty()) {
        data = rawBytes;
        fname = filename;
        ext = QFileInfo(filename).suffix().toLower();
    } else {
        QFile file(filePath);
        if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
            emit errorOccurred(QStringLiteral("File not found: %1").arg(filePath));
            return;
        }
        data = file.readAll();
        fname = QFileInfo(filePath).fileName();
        ext = QFileInfo(filePath).suffix().toLower();
    }

    static const QSet<QString> kImageExts = {"png", "jpg", "jpeg", "gif", "bmp", "webp"};
    const bool isImage = kImageExts.contains(ext);
    const QString tid = randomHex(8);

    QJsonObject meta;
    meta["type"] = protocol::kMsgFileMeta;
    meta["tid"] = tid;
    meta["filename"] = fname;
    meta["size"] = data.size();
    meta["is_image"] = isImage;
    meta["from_ip"] = m_hostIp;
    meta["to"] = toIp.isEmpty() ? QStringLiteral("public") : toIp;
    meta["ts"] = nowEpoch();
    sendUdp(meta, toIp);

    constexpr int kChunkSize = 60000;
    const int total = data.size();
    const int totalChunks = (total + kChunkSize - 1) / kChunkSize;

    QVector<QJsonObject> chunks;
    int idx = 0;
    for (int offset = 0; offset < total; offset += kChunkSize, ++idx) {
        const QByteArray chunk = data.mid(offset, kChunkSize);
        QJsonObject c;
        c["type"] = protocol::kMsgFileData;
        c["tid"] = tid;
        c["idx"] = idx;
        c["total"] = totalChunks;
        c["data"] = QString::fromLatin1(chunk.toBase64());
        chunks.append(c);
    }
    sendChunksQueued(chunks, toIp, 0);
}

void NetworkManager::sendChunksQueued(const QVector<QJsonObject> &chunks,
                                      const QString &toIp, int idx, int batch)
{
    if (idx >= chunks.size())
        return;

    const int end = qMin(idx + batch, chunks.size());
    for (int i = idx; i < end; ++i)
        sendUdp(chunks[i], toIp);

    const int total = chunks.size();
    const int delay = total > 100 ? 8 : (total > 20 ? 5 : 2);
    QTimer::singleShot(delay, this, [this, chunks, toIp, end, batch] {
        sendChunksQueued(chunks, toIp, end, batch);
    });
}

// ── voice TCP ────────────────────────────────────────────────────────
bool NetworkManager::connectVoice(const QString &ip)
{
    if (m_voiceConnections.contains(ip))
        return true;

    auto *sock = new QTcpSocket(this);
    if (m_internetMode) {
        QString host = m_relayHostOverride;
        quint16 port = m_relayVoicePortOverride;
        if (host.isEmpty() || port == 0) {
            const auto &builtins = protocol::builtinRelays();
            if (!builtins.isEmpty()) {
                host = QString::fromLatin1(builtins.first().host);
                port = builtins.first().voicePort;
            }
        }
        if (host.isEmpty() || port == 0) {
            sock->deleteLater();
            emit errorOccurred(QStringLiteral("VDS voice relay not configured — cannot start call"));
            return false;
        }
        sock->connectToHost(host, port);
    } else {
        sock->connectToHost(QHostAddress(ip), m_voiceTcpPort);
    }

    if (sock->waitForConnected(3000)) {
        m_voiceConnections[ip] = sock;
        connect(sock, &QTcpSocket::readyRead, this, [this, sock, ip] { onVoiceData(sock, ip); });
        connect(sock, &QTcpSocket::disconnected, this, [this, ip] { onVoiceDisconnected(ip); });
        emit voiceConnected(ip);
        return true;
    }
    sock->close();
    sock->deleteLater();
    return false;
}

bool NetworkManager::sendVoice(const QString &ip, const QByteArray &data)
{
    auto *sock = m_voiceConnections.value(ip, nullptr);
    if (sock && sock->state() == QTcpSocket::ConnectedState) {
        sock->write(data);
        return true;
    }
    return false;
}

void NetworkManager::disconnectVoice(const QString &ip)
{
    if (auto *sock = m_voiceConnections.take(ip))
        sock->disconnectFromHost();
}

void NetworkManager::sendFile(const QString &toIp, const QString &filePath)
{
    sendFileInternal(toIp, filePath, QByteArray(), QStringLiteral("file"));
}

} // namespace koutnet
