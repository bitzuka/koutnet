// KOutNet — Network & Audio core
// Ported from gdf_network.py ( NT Server 1.8) → C++/Qt6
#include "NetworkManager.h"
#include "Protocol.h"

#include <QNetworkInterface>
#include <QNetworkDatagram>
#include <QJsonDocument>
#include <QDateTime>
#include <QRandomGenerator>
#include <QFile>
#include <QFileInfo>

// TODO: wire these up once the corresponding modules are ported:
//  - core/security      -> Crypto (session keys, HMAC, replay guard)
//  - core/constructor    -> AppSettings (S() equivalent)
//  - network/utils       -> getLocalIp / getAllLocalIps / detectConnectionType
// For now the network layer compiles standalone with sane fallbacks so the
// module skeleton is usable while those pieces are being ported in parallel.

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

} // namespace

NetworkManager::NetworkManager(QObject *parent) : QObject(parent)
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
    m_internetMode = false; // TODO: read from AppSettings::connectionMode() == "internet"
    m_localIps = allLocalIpsFallback();
    m_localIps.insert(m_hostIp);

    m_broadcastTimer.start(2000); // fast discovery — every 2s
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
    // TODO: username, avatar, bio, status, premium, ECDH handshake keys —
    // pull from AppSettings + CryptoManager once those modules are ported.
    return payload;
}

void NetworkManager::onBroadcastTimer()
{
    if (m_internetMode) // internet mode never LAN-broadcasts
        return;
    if (!m_running || !m_udp)
        return;

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
    if (now - m_lastScan > 30) {
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
    // 6. TODO: relay server unicast (AppSettings::relayServer())

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
    // TODO: rate limiting (CryptoManager::checkRate), HMAC verification
    // (CryptoManager::verifyPacket) — depends on core/security module.

    const QString msgFromIp = msg.value("from_ip").toString();
    const QSet<QString> myIps = m_localIps.isEmpty() ? QSet<QString>{m_hostIp} : m_localIps;
    const QString type = msg.value("type").toString();

    if (type != protocol::kMsgPresence && !type.isEmpty()) {
        if (!msgFromIp.isEmpty() && myIps.contains(msgFromIp))
            return; // own message echoed back
        if (myIps.contains(host))
            return; // own broadcast echoed back
    }

    if (type == protocol::kMsgPresence) {
        handlePresence(host, msg);
    } else if (type == protocol::kMsgChat || type == protocol::kMsgGroup
               || type == protocol::kMsgReaction || type == protocol::kMsgEdit
               || type == protocol::kMsgDelete || type == protocol::kMsgRead) {
        emit message(msg);
    } else if (type == protocol::kMsgPrivate) {
        if (msg.value("to").toString() == m_hostIp)
            emit message(msg);
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
    } else if (type == protocol::kMsgGroupInv) {
        emit groupInvite(msg.value("gid").toString(), msg.value("gname").toString(), host);
    } else if (type == protocol::kMsgTyping) {
        emit typing(msg.value("username").toString(), msg.value("chat_id").toString("public"));
    }
}

void NetworkManager::handlePresence(const QString &host, QJsonObject msg)
{
    QString ip = msg.value("ip").toString(host);
    const QSet<QString> myIps = m_localIps.isEmpty() ? QSet<QString>{m_hostIp} : m_localIps;
    if (myIps.contains(ip) || myIps.contains(host))
        return;

    if (host != ip && !host.isEmpty() && host != QLatin1String("0.0.0.0"))
        msg["source_ip"] = host;

    // TODO: replay guard (CryptoManager::checkReplay) using nonce/ts fields
    // TODO: ECDH handshake processing (CryptoManager::processHandshake)

    const bool isNew = !m_peers.contains(ip);
    msg["last_seen"] = nowEpoch();
    // TODO: msg["conn_type"] = detectConnectionType(ip);
    // TODO: msg["e2e"] = CryptoManager::hasSession(ip);
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
            emit voiceDataFrom(ip, data);  // group-call mixer path
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
    m_relaySocket = new QTcpSocket(this);
    connect(m_relaySocket, &QTcpSocket::connected, this, [] {
        // tunnel established
    });
    connect(m_relaySocket, &QTcpSocket::disconnected, this, [this] {
        m_relayConnected = false;
        QTimer::singleShot(3000, this, &NetworkManager::startInternetTunnel);
    });
    m_relaySocket->connectToHost(protocol::kRelayHost, protocol::kRelayTunnelPort);
    m_relayConnected = m_relaySocket->waitForConnected(3000);
    if (m_relayConnected)
        onBroadcastTimer();
    else
        emit errorOccurred(QStringLiteral("Tunnel connect failed"));
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
        // TODO: HMAC sign via CryptoManager::signPacket if session exists
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
    // TODO: encrypt via CryptoManager if enabled — see AppSettings::encryptionEnabled()
    QJsonObject payload;
    payload["type"] = protocol::kMsgChat;
    payload["text"] = text;
    payload["from_ip"] = m_hostIp;
    payload["encrypted"] = false;
    sendUdp(payload);
}

void NetworkManager::sendPrivate(const QString &text, const QString &toIp)
{
    QJsonObject payload;
    payload["type"] = protocol::kMsgPrivate;
    payload["text"] = text;
    payload["to"] = toIp;
    payload["from_ip"] = m_hostIp;
    payload["encrypted"] = false; // TODO: E2E via CryptoManager
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

void NetworkManager::sendFile(const QString &toIp, const QString &filePath,
                              const QByteArray &rawBytes, const QString &filename)
{
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
        sock->connectToHost(QHostAddress(protocol::kRelayVoiceHost), protocol::kRelayVoicePort);
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

} // namespace koutnet
