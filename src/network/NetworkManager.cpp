// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Network & Audio core
#include "NetworkManager.h"
#include "Protocol.h"
#include "../core/security/CryptoManager.h"

#include <KLocalizedString>
#include <QNetworkInterface>
#include <QNetworkDatagram>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QRandomGenerator>
#include <QFile>
#include <QFileInfo>

// TODO: AppSettings exists now, so the I_Do_It_Latet.! markers below can be
// wired to it: group passphrase, static peer list, connection mode and
// relay credentials. CryptoManager is already injected via the constructor.

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

// These end up as replay-guard nonces and transfer ids, so they have to be
// unguessable: global() is a seeded Mersenne twister, system() is the OS CSPRNG.
QString randomHex(int bytes)
{
    QByteArray buf(bytes, 0);
    QRandomGenerator::system()->generate(buf.begin(), buf.end());
    return QString::fromLatin1(buf.toHex());
}

double nowEpoch()
{
    return QDateTime::currentMSecsSinceEpoch() / 1000.0;
}

// Canonical bytes used for HMAC sign/verify: compact JSON of the payload
// with "_sig" removed (or absent). Both sides must build this identically.
QByteArray signableBytes(QJsonObject obj)
{
    obj.remove(QStringLiteral("_sig"));
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

// Packet types accepted from a host we hold no session key for. Presence has
// to be here because it is the packet that carries the handshake, so requiring
// a signature on it would mean no peer could ever get one. Nothing else has an
// excuse, so keep this list at one entry unless the protocol grows another
// genuinely pre-session message.
bool allowedUnsigned(const QString &type)
{
    return type == protocol::kMsgPresence;
}

// The 4-byte big-endian length that goes in front of every framed message.
// Written out by hand rather than through QDataStream so the wire format stays
// obvious to anyone reimplementing the protocol.
QByteArray lengthPrefix(quint32 len)
{
    QByteArray header(protocol::kFrameHeaderBytes, 0);
    header[0] = char((len >> 24) & 0xFF);
    header[1] = char((len >> 16) & 0xFF);
    header[2] = char((len >> 8) & 0xFF);
    header[3] = char(len & 0xFF);
    return header;
}

// Callers check that at least kFrameHeaderBytes are buffered first.
quint32 readLengthPrefix(const QByteArray &buf)
{
    return (quint32(quint8(buf.at(0))) << 24) | (quint32(quint8(buf.at(1))) << 16)
        | (quint32(quint8(buf.at(2))) << 8) | quint32(quint8(buf.at(3)));
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

void NetworkManager::setProfile(const QString &handle, const QString &displayName,
                                const QString &bio, const QString &revision)
{
    // Presence goes out on a short timer, so anything in here is paid
    // for repeatedly. A long bio gets cut rather than pushing the
    // packet towards fragmentation.
    constexpr int kMaxBioChars = 280;

    const QString trimmedBio = bio.left(kMaxBioChars);
    if (m_profileHandle == handle && m_profileDisplayName == displayName
        && m_profileBio == trimmedBio && m_profileRevision == revision) {
        return;
    }

    m_profileHandle = handle;
    m_profileDisplayName = displayName;
    m_profileBio = trimmedBio;
    m_profileRevision = revision;

    // Announce straight away instead of waiting out the timer, so an
    // edit shows up on other screens while the user still has the
    // profile page open.
    if (m_running)
        onBroadcastTimer();
}

void NetworkManager::setGroupPassphrase(const QString &passphrase)
{
    m_groupPassphrase = passphrase;
}

void NetworkManager::setConnectionMode(ConnectionMode mode)
{
    // Only the relay-backed modes raise the tunnel. K-Server will have its
    // own transport and does not exist yet.
    const bool wantRelay = (mode == ConnectionMode::Relay
                            || mode == ConnectionMode::MaintainerVds);
    // Relay and maintainer VDS ride the same tunnel, so moving between those
    // two changes the mode without disturbing the socket.
    const bool relayChanged = (wantRelay != m_internetMode);

    m_mode = mode;
    m_internetMode = wantRelay;

    if (!relayChanged || !m_running)
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

bool NetworkManager::modeAvailable(int mode) const
{
    switch (static_cast<ConnectionMode>(mode)) {
    case ConnectionMode::LanOrVpn:
        return true;
    case ConnectionMode::Relay:
        // Usable the moment someone fills in a relay address.
        return true;
    case ConnectionMode::KServerSelfHosted:
    case ConnectionMode::KServerClient:
        // No K-Server exists in any form yet.
        return false;
    case ConnectionMode::MaintainerVds:
        // Waiting on a deployed relay; the built-in list is still empty.
        return !protocol::builtinRelays().isEmpty();
    }
    return false;
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
    if (newPrimary != m_hostIp) {
        m_hostIp = newPrimary;
        // the address is on screen and in every packet we send, so a silent
        // swap leaves the UI showing the old one until the next restart
        Q_EMIT hostIpChanged();
    }
}

bool NetworkManager::start()
{
    const quint16 udpPort = protocol::kUdpPortDefault;
    const quint16 tcpPort = protocol::kTcpPortDefault;
    m_voiceTcpPort = tcpPort;

    // UDP socket
    m_udp = new QUdpSocket(this);
    bool bound = m_udp->bind(QHostAddress::AnyIPv4, udpPort,
                             QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    if (!bound) {
        bound = m_udp->bind(QHostAddress::AnyIPv4, udpPort + 1,
                            QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    }
    if (!bound) {
        // int(): KLocalizedString has no quint16 substitution overload, so
        // the port has to pick one explicitly.
        Q_EMIT errorOccurred(i18nc("@info:status %1 is a port number",
                                   "UDP bind failed on port %1.", int(udpPort)));
        return false;
    }
    connect(m_udp, &QUdpSocket::readyRead, this, &NetworkManager::onUdpReadyRead);

    // mDNS-like discovery multicast group (best-effort)
    m_udp->joinMulticastGroup(QHostAddress(QStringLiteral("224.0.0.251")));

    // TCP server (voice)
    m_tcpServer = new QTcpServer(this);
    if (!m_tcpServer->listen(QHostAddress::AnyIPv4, tcpPort)) {
        if (!m_tcpServer->listen(QHostAddress::AnyIPv4, 0)) {
            Q_EMIT errorOccurred(i18nc("@info:status %1 is a port number",
                                       "TCP listen failed on port %1.", int(tcpPort)));
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

    // disconnect(this) before deleteLater() on all three, the way
    // setConnectionMode() does it: the teardown handlers would otherwise fire
    // into maps this function has just cleared. Without the deleteLater() every
    // socket stayed alive as a child of this object, so a stop()/start() cycle -
    // switching connection mode, for instance - stranded the whole set each time.
    for (auto *sock : std::as_const(m_voiceConnections)) {
        sock->disconnect(this);
        sock->close();
        sock->deleteLater();
    }
    m_voiceConnections.clear();
    m_voiceRxBuffers.clear();

    for (auto *sock : std::as_const(m_pendingVoice)) {
        sock->disconnect(this);
        sock->abort();
        sock->deleteLater();
    }
    m_pendingVoice.clear();

    // Not conditional on m_internetMode any more: the socket exists or it does
    // not, and a mode changed since it was opened is no reason to leave it
    // behind with its handlers attached.
    if (m_relaySocket) {
        m_relaySocket->disconnect(this);
        m_relaySocket->close();
        m_relaySocket->deleteLater();
        m_relaySocket = nullptr;
        m_relayConnected = false;
    }
    m_relayBuffer.clear();
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
    // Linux: /proc/net/arp. Windows/macOS: `arp -a` (TODO - process-based parse).
    if (!m_running)
        return;

    QSet<QString> ips;
#ifdef Q_OS_LINUX
    QFile arpFile(QStringLiteral("/proc/net/arp"));
    if (arpFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const auto lines = QString::fromUtf8(arpFile.readAll()).split(QLatin1Char('\n'));
        for (int i = 1; i < lines.size(); ++i) {
            const auto parts = lines[i].split(QLatin1Char(' '), Qt::SkipEmptyParts);
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
    payload[QStringLiteral("type")] = protocol::kMsgPresence;
    payload[QStringLiteral("ip")] = m_hostIp;
    QJsonArray allIps;
    for (const auto &ip : m_localIps)
        allIps.append(ip);
    payload[QStringLiteral("all_ips")] = allIps;
    payload[QStringLiteral("os")] = QSysInfo::prettyProductName();
    payload[QStringLiteral("version")] = QStringLiteral("2.0");
    payload[QStringLiteral("protocol_version")] = protocol::kProtocolVersion;
    payload[QStringLiteral("ts")] = nowEpoch();
    payload[QStringLiteral("nonce")] = randomHex(8);

    // ECDH handshake bundle - lets any peer that sees this presence packet
    // derive a session key with us (Layer 1, see CryptoManager).
    if (m_crypto) {
        const QJsonObject hs = m_crypto->handshakePayload();
        for (auto it = hs.constBegin(); it != hs.constEnd(); ++it)
            payload[it.key()] = it.value();
    }

    payload[QStringLiteral("username")] = m_profileHandle;
    payload[QStringLiteral("display_name")] = m_profileDisplayName;
    payload[QStringLiteral("bio")] = m_profileBio;
    payload[QStringLiteral("profile_rev")] = m_profileRevision;
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
    m_udp->writeDatagram(data, QHostAddress(QStringLiteral("224.0.0.251")), port);

    // 4. Unicast /24 subnet scan every 30s (fallback if broadcast blocked)
    const double now = nowEpoch();
    const double scanIntervalSec = m_peers.isEmpty() ? 30.0 : 120.0;
    if (now - m_lastScan > scanIntervalSec) {
        m_lastScan = now;
        const auto parts = m_hostIp.split(QLatin1Char('.'));
        if (parts.size() == 4) {
            const QString prefix = parts[0] + QLatin1Char('.') + parts[1] + QLatin1Char('.') + parts[2] + QLatin1Char('.');
            for (int last = 1; last < 255; ++last) {
                const QString target = prefix + QString::number(last);
                if (target != m_hostIp)
                    m_udp->writeDatagram(data, QHostAddress(target), port);
            }
        }
    }

    // 5. TODO: unicast to manually-added static peers (AppSettings::staticPeers())
    // 6. I_Do_It_Latet.! - relay server unicast (needs default/custom relay,
    //    see setRelayServer() and Protocol::kRelayHost).

    pruneStalePeers();
}

void NetworkManager::pruneStalePeers()
{
    const double now = nowEpoch();
    QVector<QString> stale;
    for (auto it = m_peers.constBegin(); it != m_peers.constEnd(); ++it) {
        const double lastSeen = it.value()[QStringLiteral("last_seen")].toDouble();
        if (now - lastSeen > 25)
            stale.append(it.key());
    }
    for (const auto &ip : stale) {
        m_peers.remove(ip);
        Q_EMIT userOffline(ip);
    }
}

void NetworkManager::onUdpReadyRead()
{
    while (m_udp && m_udp->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_udp->receiveDatagram();
        QString host = dg.senderAddress().toString();
        if (host.startsWith(QLatin1String("::ffff:")))
            host = host.mid(7);

        handleDatagram(host, dg.data());
    }
}

void NetworkManager::handleDatagram(const QString &host, const QByteArray &data)
{
    // Attacker-supplied bytes, so say what was wrong with them rather than
    // treating "not an object" and "not even JSON" as the same thing.
    QJsonParseError parseErr;
    const auto doc = QJsonDocument::fromJson(data, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        Q_EMIT errorOccurred(i18nc("@info:status %1 is a host address, %2 the parser message",
                                   "UDP parse error from %1: %2", host,
                                   parseErr.errorString()));
        return;
    }
    dispatch(host, doc.object());
}

void NetworkManager::dispatch(const QString &host, QJsonObject msg)
{
    // Layer 6 - rate limiting: max N packets/sec per source IP.
    if (m_crypto && !m_crypto->checkRate(host)) {
        return; // dropped - over rate limit
    }

    const QString msgFromIp = msg.value(QStringLiteral("from_ip")).toString();
    const QSet<QString> myIps = m_localIps.isEmpty() ? QSet<QString>{m_hostIp} : m_localIps;
    const QString type = msg.value(QStringLiteral("type")).toString();

    if (type != protocol::kMsgPresence && !type.isEmpty()) {
        if (!msgFromIp.isEmpty() && myIps.contains(msgFromIp))
            return; // own message echoed back
        if (myIps.contains(host))
            return; // own broadcast echoed back
    }

    // Layer 4 - HMAC verification. Everything outside the allowlist needs a
    // session and a valid _sig; an absent one used to be waved through, which
    // let anyone spoof a peer we had already authenticated by simply omitting
    // the field.
    //
    // The allowlist is checked first and not only before a session exists.
    // Presence is a broadcast, so sendUdp() has no single peer to sign it for
    // and never puts a _sig on one - which meant that as soon as the handshake
    // in the first presence packet succeeded, every later presence packet from
    // that peer failed this check, the peer went stale after 25 seconds, and
    // the session key that caused it kept it away for good. What stands behind
    // a presence packet is the identity pin in CryptoManager::processHandshake,
    // not an HMAC.
    if (m_crypto && !type.isEmpty() && !allowedUnsigned(type)) {
        if (!m_crypto->hasSession(host)) {
            Q_EMIT errorOccurred(i18nc("@info:status %1 is a message type, %2 a host address",
                                       "Unauthenticated %1 from %2 - dropping.", type, host));
            return;
        }
        const QString sig = msg.value(QStringLiteral("_sig")).toString();
        if (sig.isEmpty() || !m_crypto->verifyPacket(host, signableBytes(msg), sig)) {
            Q_EMIT errorOccurred(i18nc("@info:status %1 is a host address",
                                       "HMAC verification failed from %1 - dropping.", host));
            return;
        }

        // Layer 5 - replay guard. Only presence used to get one, so a captured
        // packet of any other type could be resent forever: the nonce and the
        // timestamp are inside the signature, so replaying the whole thing
        // verifies as happily as the original did.
        const QString nonce = msg.value(QStringLiteral("nonce")).toString();
        if (!nonce.isEmpty()
            && !m_crypto->checkReplay(host, nonce, msg.value(QStringLiteral("ts")).toDouble())) {
            return; // replayed or outside the timestamp window
        }
    }

    if (type == protocol::kMsgPresence) {
        handlePresence(host, msg);
    } else if (type == protocol::kMsgChat || type == protocol::kMsgGroup
               || type == protocol::kMsgReaction || type == protocol::kMsgEdit
               || type == protocol::kMsgDelete || type == protocol::kMsgRead) {
        decryptMessageText(host, msg);
        Q_EMIT message(msg);
    } else if (type == protocol::kMsgPrivate) {
        if (msg.value(QStringLiteral("to")).toString() == m_hostIp) {
            decryptMessageText(host, msg);
            Q_EMIT message(msg);
        }
    } else if (type == protocol::kMsgCallReq) {
        // TODO: check VoiceCallManager::active() and reply call_busy if in a call
        Q_EMIT callRequest(msg.value(QStringLiteral("username")).toString(QStringLiteral("?")), host);
    } else if (type == protocol::kMsgCallAccept) {
        Q_EMIT callAccepted(msg.value(QStringLiteral("username")).toString(QStringLiteral("?")), host);
    } else if (type == protocol::kMsgCallBusy || type == protocol::kMsgCallReject) {
        Q_EMIT callRejected(host);
    } else if (type == protocol::kMsgCallEnd) {
        Q_EMIT callEnded(host);
    } else if (type == protocol::kMsgFileMeta) {
        Q_EMIT fileMeta(msg);
    } else if (type == protocol::kMsgFileData) {
        Q_EMIT fileChunk(msg);
    } else if (type == protocol::kMsgGroupInv) {
        Q_EMIT groupInvite(msg.value(QStringLiteral("gid")).toString(), msg.value(QStringLiteral("gname")).toString(), host);
    } else if (type == protocol::kMsgTyping) {
        Q_EMIT typing(msg.value(QStringLiteral("username")).toString(), msg.value(QStringLiteral("chat_id")).toString(QStringLiteral("public")));
    }
}

void NetworkManager::decryptMessageText(const QString &fromIp, QJsonObject &msg) const
{
    // Reactions, receipts and deletes share this dispatch path but carry no
    // body, and inventing a "text" field for them would confuse the UI.
    if (!m_crypto || !msg.contains(QStringLiteral("text")))
        return;

    // The sender's "encrypted" flag used to decide this, which meant clearing
    // it was enough to have the text handed to the UI unchecked. What matters
    // is whether we hold a key for this channel; decrypt() refuses cleartext
    // once we do.
    if (!m_crypto->hasSession(fromIp) && m_groupPassphrase.isEmpty())
        return;

    const QString cipherText = msg.value(QStringLiteral("text")).toString();
    // decrypt() picks the path from the tag byte, so handing it both the
    // passphrase and the peer covers session and group traffic alike.
    const QString plain = m_crypto->decrypt(cipherText, m_groupPassphrase, fromIp);
    msg[QStringLiteral("text")] = plain;
}

void NetworkManager::handlePresence(const QString &host, QJsonObject msg)
{
    QString ip = msg.value(QStringLiteral("ip")).toString(host);
    const QSet<QString> myIps = m_localIps.isEmpty() ? QSet<QString>{m_hostIp} : m_localIps;
    if (myIps.contains(ip) || myIps.contains(host))
        return;

    if (host != ip && !host.isEmpty() && host != QLatin1String("0.0.0.0"))
        msg[QStringLiteral("source_ip")] = host;

    // Layer 5 - replay guard on presence packets (nonce + timestamp window).
    if (m_crypto) {
        const QString nonce = msg.value(QStringLiteral("nonce")).toString();
        const double ts = msg.value(QStringLiteral("ts")).toDouble();
        if (!nonce.isEmpty() && !m_crypto->checkReplay(ip, nonce, ts))
            return; // replayed presence packet

        // ECDH handshake - derives (or refreshes) the session key with this peer.
        // A refusal for an address we already hold a session for is the
        // impostor case in CryptoManager::processHandshake: someone claiming a
        // known peer's address with an identity key of their own. The session
        // and the pin survive that, but the peer record has to survive it too -
        // it is where the interface reads the name and the fingerprint it shows
        // beside the warning, and letting the refused packet rewrite those
        // hands the spoofer the only part of the takeover the user can see.
        if (msg.contains(QStringLiteral("dh_pub")) && !m_crypto->processHandshake(ip, msg)
            && m_crypto->hasSession(ip)) {
            return;
        }
    }

    const bool isNew = !m_peers.contains(ip);
    msg[QStringLiteral("last_seen")] = nowEpoch();
    // TODO: msg["conn_type"] = detectConnectionType(ip);
    if (m_crypto)
        msg[QStringLiteral("e2e")] = m_crypto->hasSession(ip);
    m_peers[ip] = msg;

    if (isNew)
        Q_EMIT userOnline(msg);
}

void NetworkManager::onNewTcpConnection()
{
    while (m_tcpServer && m_tcpServer->hasPendingConnections()) {
        QTcpSocket *sock = m_tcpServer->nextPendingConnection();
        QString ip = sock->peerAddress().toString();
        if (ip.startsWith(QLatin1String("::ffff:")))
            ip = ip.mid(7);

        // A peer that reconnects arrives here again while the socket from the
        // last call is still in the map. Assigning over it left that one alive
        // with its handlers attached - one stranded QTcpSocket per reconnect,
        // and a disconnect on the old one still reporting the call as ended.
        replaceVoiceSocket(ip, sock);
        connect(sock, &QTcpSocket::readyRead, this, [this, sock, ip] { onVoiceData(sock, ip); });
        connect(sock, &QTcpSocket::disconnected, this, [this, sock, ip] { onVoiceDisconnected(sock, ip); });
        Q_EMIT voiceConnected(ip);
    }
}

// Puts a socket in the voice map, seeing off whatever was there for that address.
void NetworkManager::replaceVoiceSocket(const QString &ip, QTcpSocket *sock)
{
    if (auto *previous = m_voiceConnections.value(ip, nullptr); previous && previous != sock) {
        previous->disconnect(this); // its disconnected() would tear down the new call
        m_voiceRxBuffers.remove(previous);
        previous->abort();
        previous->deleteLater();
    }
    m_voiceConnections[ip] = sock;
}

void NetworkManager::onVoiceData(QTcpSocket *sock, const QString &ip)
{
    // Reassemble whole frames before handing anything on. The old code read
    // whatever had arrived and called it a frame, which is why encrypted voice
    // never worked: the GCM tag was almost never at the end of the slice.
    QByteArray buf = m_voiceRxBuffers.value(sock) + sock->readAll();
    QVector<QByteArray> frames;

    while (buf.size() >= protocol::kFrameHeaderBytes) {
        const quint32 len = readLengthPrefix(buf);
        if (len == 0 || len > protocol::kMaxVoiceFrameBytes) {
            // the length is peer-supplied, so a silly one is either a broken
            // sender or an attempt to make us allocate a gigabyte
            m_voiceRxBuffers.remove(sock);
            Q_EMIT errorOccurred(i18nc("@info:status %1 is a host address, %2 a byte count",
                                       "Voice frame from %1 declared %2 bytes - dropping the connection.",
                                       ip, len));
            // abort() on a connected socket emits disconnected(), which is
            // where the teardown lives - doing it here as well would tell the
            // call layer twice.
            sock->abort();
            return;
        }
        if (quint32(buf.size() - protocol::kFrameHeaderBytes) < len)
            break; // the rest of this frame is still in flight
        frames.append(buf.mid(protocol::kFrameHeaderBytes, int(len)));
        buf.remove(0, protocol::kFrameHeaderBytes + int(len));
    }

    // Remainder goes back before anything is emitted: a listener may hang up
    // the call, and that erases this socket's buffer underneath us.
    m_voiceRxBuffers[sock] = buf;

    for (const auto &frame : std::as_const(frames)) {
        Q_EMIT voiceData(frame);          // legacy single-call path
        Q_EMIT voiceDataFrom(ip, frame);  // group-call mixer path - VoiceCallManager
                                          // decrypts (CryptoManager::decryptBytes)
                                          // before pushing into the jitter buffer.
    }
}

void NetworkManager::onVoiceDisconnected(QTcpSocket *sock, const QString &ip)
{
    m_voiceRxBuffers.remove(sock);
    m_voiceConnections.remove(ip);
    Q_EMIT voiceDisconnected(ip);
    Q_EMIT callEnded(ip);
}

// internet relay tunnel (TODO: move to network/vds once that module lands)
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
        // can't possibly succeed - just check back periodically in case the
        // user configures one, or an update ships a built-in relay.
        Q_EMIT errorOccurred(i18nc("@info:status",
                                   "VDS mode is on but no relay server is configured. "
                                   "Set one in Settings, or switch back to LAN/VPN mode."));
        QTimer::singleShot(protocol::kRelayReconnectMaxMs, this, [this] {
            if (m_internetMode && m_running)
                startInternetTunnel();
        });
        return;
    }

    // One socket at a time. Every retry used to hand m_relaySocket a fresh
    // QTcpSocket and leave the old one alive with its handlers attached.
    if (m_relaySocket) {
        m_relaySocket->disconnect(this);
        m_relaySocket->abort();
        m_relaySocket->deleteLater();
    }
    m_relayConnected = false;

    auto *sock = new QTcpSocket(this);
    m_relaySocket = sock;

    connect(sock, &QTcpSocket::connected, this, [this, sock] {
        if (m_relaySocket != sock)
            return; // a newer attempt already replaced this socket
        m_relayConnected = true;
        m_relayBuffer.clear(); // a fresh stream never continues the old one
        m_relayReconnectMs = protocol::kRelayReconnectBaseMs; // reset backoff on success
        onBroadcastTimer();
    });
    connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
        if (m_relaySocket != sock)
            return;
        onRelayData();
    });
    connect(sock, &QTcpSocket::disconnected, this, [this, sock] {
        if (m_relaySocket != sock)
            return;
        m_relayConnected = false;
        m_relayBuffer.clear();
        scheduleRelayReconnect();
    });
    // A connect that never lands (refused, unreachable, bad name) emits this
    // and never disconnected(), so it is the only place that hears about it.
    connect(sock, &QTcpSocket::errorOccurred, this, [this, sock](QAbstractSocket::SocketError) {
        if (m_relaySocket != sock || m_relayConnected)
            return; // an established link that drops is disconnected()'s job
        Q_EMIT errorOccurred(i18nc("@info:status %1 is a socket error message",
                                   "Tunnel connect failed: %1", sock->errorString()));
        scheduleRelayReconnect();
    });

    // No waitForConnected: this runs on the GUI thread, so it froze the whole
    // window for three seconds on every attempt, retries included.
    sock->connectToHost(host, port);
}

// The relay counterpart of onVoiceData: same 4-byte big-endian framing, and the
// payload is one compact JSON object per frame, which is what sendUdp() writes
// on this socket.
void NetworkManager::onRelayData()
{
    m_relayBuffer += m_relaySocket->readAll();

    QVector<QByteArray> frames;
    while (m_relayBuffer.size() >= protocol::kFrameHeaderBytes) {
        const quint32 len = readLengthPrefix(m_relayBuffer);
        if (len == 0 || len > protocol::kMaxRelayFrameBytes) {
            // the relay is no more trusted than a peer, and a bogus length here
            // would otherwise mean a multi-gigabyte allocation
            Q_EMIT errorOccurred(i18nc("@info:status %1 is a byte count",
                                       "Relay frame declared %1 bytes - dropping the tunnel.", len));
            m_relayBuffer.clear();
            // the disconnected handler clears the flag and schedules the
            // reconnect, so abort() is all that is needed here
            m_relaySocket->abort();
            return;
        }
        if (quint32(m_relayBuffer.size() - protocol::kFrameHeaderBytes) < len)
            break; // wait for the rest
        frames.append(m_relayBuffer.mid(protocol::kFrameHeaderBytes, int(len)));
        m_relayBuffer.remove(0, protocol::kFrameHeaderBytes + int(len));
    }

    for (const auto &frame : std::as_const(frames)) {
        QJsonParseError parseErr;
        const auto doc = QJsonDocument::fromJson(frame, &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            Q_EMIT errorOccurred(i18nc("@info:status %1 is the parser message",
                                       "Relay parse error: %1", parseErr.errorString()));
            continue;
        }
        const QJsonObject msg = doc.object();
        // The TCP peer is the relay, not the sender, so the source address has
        // to come out of the payload - it is what the replay guard, the HMAC
        // check and the peer table are all keyed on.
        QString host = msg.value(QStringLiteral("from_ip")).toString();
        if (host.isEmpty())
            host = msg.value(QStringLiteral("ip")).toString();
        if (host.isEmpty()) {
            Q_EMIT errorOccurred(i18nc("@info:status",
                                       "Relay frame with no sender address - dropping."));
            continue;
        }
        dispatch(host, msg);
    }
}

void NetworkManager::scheduleRelayReconnect()
{
    const int delay = m_relayReconnectMs;
    m_relayReconnectMs = qMin(m_relayReconnectMs * 2, protocol::kRelayReconnectMaxMs);
    QTimer::singleShot(delay, this, [this] {
        if (m_internetMode && m_running)
            startInternetTunnel();
    });
}

// outgoing
void NetworkManager::sendUdp(QJsonObject payload, const QString &targetIp)
{
    if (!m_udp)
        return;

    const QString msgType = payload.value(QStringLiteral("type")).toString();
    if (msgType != protocol::kMsgPresence) {
        payload[QStringLiteral("nonce")] = randomHex(8);
        payload[QStringLiteral("ts")] = nowEpoch();

        // Layer 4 - HMAC-sign unicast packets once a session key exists with
        // the target (broadcasts have no single peer session to sign for).
        if (m_crypto && !targetIp.isEmpty() && m_crypto->hasSession(targetIp)) {
            const QByteArray payloadBytes = signableBytes(payload);
            payload[QStringLiteral("_sig")] = m_crypto->signPacket(targetIp, payloadBytes);
        }
    }

    const QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    if (m_internetMode) {
        if (m_relaySocket && m_relayConnected)
            m_relaySocket->write(lengthPrefix(quint32(data.size())) + data);
    } else if (!targetIp.isEmpty()) {
        m_udp->writeDatagram(data, QHostAddress(targetIp), protocol::kUdpPortDefault);
    } else {
        m_udp->writeDatagram(data, QHostAddress::Broadcast, protocol::kUdpPortDefault);
    }
}

void NetworkManager::sendChat(const QString &text)
{
    // Nothing calls this yet. It is the broadcast path the group and public
    // chats will be built on, kept because a shared passphrase is the right
    // key for a room where no two members hold an ECDH session. The key will
    // come from GroupManager per room rather than from one app-wide setting.
    QString outText = text;
    bool encrypted = false;
    if (m_crypto && !m_groupPassphrase.isEmpty()) {
        const QString cipherText = m_crypto->encrypt(text, m_groupPassphrase, QString());
        if (cipherText != text) {
            outText = cipherText;
            encrypted = true;
        }
    }

    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgChat;
    payload[QStringLiteral("text")] = outText;
    payload[QStringLiteral("from_ip")] = m_hostIp;
    payload[QStringLiteral("encrypted")] = encrypted;
    sendUdp(payload);
}

void NetworkManager::sendPrivate(const QString &text, const QString &toIp)
{
    QString outText = text;
    bool encrypted = false;

    if (m_crypto) {
        // A session is the better key when there is one. Falling back to the
        // group passphrase means a message still gets protection before the
        // handshake with this peer has completed.
        const QString passphrase = m_crypto->hasSession(toIp) ? QString() : m_groupPassphrase;
        const QString cipherText = m_crypto->encrypt(text, passphrase, toIp);
        if (cipherText != text) {
            outText = cipherText;
            encrypted = true;
        }
    }

    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgPrivate;
    payload[QStringLiteral("text")] = outText;
    payload[QStringLiteral("to")] = toIp;
    payload[QStringLiteral("from_ip")] = m_hostIp;
    payload[QStringLiteral("encrypted")] = encrypted;
    sendUdp(payload, toIp);

    // Also send to alternate IPs the peer reported (VPN/LAN redundancy)
    const auto peerInfo = m_peers.value(toIp);
    const auto altIps = peerInfo.value(QStringLiteral("all_ips")).toArray();
    for (const auto &v : altIps) {
        const QString altIp = v.toString();
        if (altIp != toIp && !m_localIps.contains(altIp))
            sendUdp(payload, altIp);
    }
}

void NetworkManager::sendGroupMessage(const QString &gid, const QString &text,
                                      const QVector<QString> &members)
{
    // TODO: per-group passphrase, or per-member ECDH fan-out, once GroupManager
    // can store one. For now every group shares the app-wide passphrase, which
    // is also what the receiving side decrypts with.
    if (!m_crypto || m_groupPassphrase.isEmpty()) {
        Q_EMIT errorOccurred(i18nc("@info:status",
                                   "Set a group passphrase before sending to a group - "
                                   "refusing to send in the clear."));
        return;
    }

    const QString cipherText = m_crypto->encrypt(text, m_groupPassphrase, QString());
    if (cipherText == text) {
        // encrypt() hands back the plaintext when it fails, and sending that
        // would leak the message the user thinks is protected
        Q_EMIT errorOccurred(i18nc("@info:status",
                                   "Failed to encrypt the group message - not sent."));
        return;
    }

    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgGroup;
    payload[QStringLiteral("gid")] = gid;
    payload[QStringLiteral("text")] = cipherText;
    payload[QStringLiteral("encrypted")] = true;
    payload[QStringLiteral("ts")] = nowEpoch();
    for (const auto &ip : members) {
        if (ip != m_hostIp)
            sendUdp(payload, ip);
    }
}

void NetworkManager::sendTyping(const QString &chatId, const QString &targetIp)
{
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgTyping;
    payload[QStringLiteral("chat_id")] = chatId;
    sendUdp(payload, targetIp);
}

void NetworkManager::sendCallRequest(const QString &toIp)
{
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgCallReq;
    sendUdp(payload, toIp);
}

void NetworkManager::sendCallAccept(const QString &toIp)
{
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgCallAccept;
    sendUdp(payload, toIp);
    connectVoice(toIp);
}

void NetworkManager::sendCallReject(const QString &toIp)
{
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgCallReject;
    sendUdp(payload, toIp);
}

void NetworkManager::sendCallEnd(const QString &toIp)
{
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgCallEnd;
    sendUdp(payload, toIp);
}

void NetworkManager::sendReaction(const QString &toIp, const QString &chatId,
                                  double ts, const QString &emoji, bool added)
{
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgReaction;
    payload[QStringLiteral("from_ip")] = m_hostIp;
    payload[QStringLiteral("chat_id")] = chatId;
    payload[QStringLiteral("msg_ts")] = ts;
    payload[QStringLiteral("emoji")] = emoji;
    payload[QStringLiteral("added")] = added;
    sendUdp(payload, toIp);
}

void NetworkManager::sendMessageEdit(const QString &toIp, const QString &chatId,
                                     double ts, const QString &newText)
{
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgEdit;
    payload[QStringLiteral("from_ip")] = m_hostIp;
    payload[QStringLiteral("chat_id")] = chatId;
    payload[QStringLiteral("msg_ts")] = ts;
    payload[QStringLiteral("new_text")] = newText;
    sendUdp(payload, toIp);
}

void NetworkManager::sendMessageDelete(const QString &toIp, const QString &chatId, double ts)
{
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgDelete;
    payload[QStringLiteral("from_ip")] = m_hostIp;
    payload[QStringLiteral("chat_id")] = chatId;
    payload[QStringLiteral("msg_ts")] = ts;
    sendUdp(payload, toIp);
}

void NetworkManager::sendReadReceipt(const QString &toIp, const QString &chatId)
{
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgRead;
    payload[QStringLiteral("from_ip")] = m_hostIp;
    payload[QStringLiteral("chat_id")] = chatId;
    sendUdp(payload, toIp);
}

void NetworkManager::sendGroupInvite(const QString &gid, const QString &gname, const QString &toIp)
{
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgGroupInv;
    payload[QStringLiteral("gid")] = gid;
    payload[QStringLiteral("gname")] = gname;
    sendUdp(payload, toIp);
}

void NetworkManager::sendFileInternal(const QString &toIp, const QString &filePath,
                              const QByteArray &rawBytes, const QString &filename)
{
    // TODO: encrypt file bytes via CryptoManager::encryptBytes before chunking,
    // same as voice - not wired yet, tracked separately from the E2E pass above.
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
            Q_EMIT errorOccurred(i18nc("@info:status %1 is a file path",
                                       "File not found: %1", filePath));
            return;
        }
        data = file.readAll();
        fname = QFileInfo(filePath).fileName();
        ext = QFileInfo(filePath).suffix().toLower();
    }

    static const QSet<QString> kImageExts = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("webp")};
    const bool isImage = kImageExts.contains(ext);
    const QString tid = randomHex(8);

    QJsonObject meta;
    meta[QStringLiteral("type")] = protocol::kMsgFileMeta;
    meta[QStringLiteral("tid")] = tid;
    meta[QStringLiteral("filename")] = fname;
    meta[QStringLiteral("size")] = data.size();
    meta[QStringLiteral("is_image")] = isImage;
    meta[QStringLiteral("from_ip")] = m_hostIp;
    meta[QStringLiteral("to")] = toIp.isEmpty() ? QStringLiteral("public") : toIp;
    meta[QStringLiteral("ts")] = nowEpoch();
    sendUdp(meta, toIp);

    constexpr int kChunkSize = 60000;
    const int total = data.size();
    const int totalChunks = (total + kChunkSize - 1) / kChunkSize;

    QVector<QJsonObject> chunks;
    int idx = 0;
    for (int offset = 0; offset < total; offset += kChunkSize, ++idx) {
        const QByteArray chunk = data.mid(offset, kChunkSize);
        QJsonObject c;
        c[QStringLiteral("type")] = protocol::kMsgFileData;
        c[QStringLiteral("tid")] = tid;
        c[QStringLiteral("idx")] = idx;
        c[QStringLiteral("total")] = totalChunks;
        c[QStringLiteral("data")] = QString::fromLatin1(chunk.toBase64());
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

// voice TCP
bool NetworkManager::connectVoice(const QString &ip)
{
    if (m_voiceConnections.contains(ip))
        return true;
    if (m_pendingVoice.contains(ip))
        return true; // an attempt is already in flight for this peer

    // Resolved up front so a missing relay is still reported synchronously,
    // which is the one failure the caller can know about without waiting.
    QString relayHost;
    quint16 relayPort = 0;
    if (m_internetMode) {
        relayHost = m_relayHostOverride;
        relayPort = m_relayVoicePortOverride;
        if (relayHost.isEmpty() || relayPort == 0) {
            const auto &builtins = protocol::builtinRelays();
            if (!builtins.isEmpty()) {
                relayHost = QString::fromLatin1(builtins.first().host);
                relayPort = builtins.first().voicePort;
            }
        }
        if (relayHost.isEmpty() || relayPort == 0) {
            Q_EMIT errorOccurred(i18nc("@info:status",
                                       "VDS voice relay is not configured - cannot start the call."));
            return false;
        }
    }

    auto *sock = new QTcpSocket(this);
    m_pendingVoice[ip] = sock;

    connect(sock, &QTcpSocket::connected, this, [this, sock, ip] {
        if (m_pendingVoice.value(ip) != sock)
            return; // superseded, or the call was already torn down
        m_pendingVoice.remove(ip);
        replaceVoiceSocket(ip, sock);
        connect(sock, &QTcpSocket::readyRead, this, [this, sock, ip] { onVoiceData(sock, ip); });
        connect(sock, &QTcpSocket::disconnected, this, [this, sock, ip] { onVoiceDisconnected(sock, ip); });
        Q_EMIT voiceConnected(ip);
    });
    connect(sock, &QTcpSocket::errorOccurred, this, [this, sock, ip](QAbstractSocket::SocketError) {
        if (m_pendingVoice.value(ip) != sock)
            return; // already connected, or replaced by a newer attempt
        m_pendingVoice.remove(ip);
        Q_EMIT errorOccurred(i18nc("@info:status %1 is a host address, %2 a socket error message",
                                   "Voice connect to %1 failed: %2", ip, sock->errorString()));
        // whoever started the call listens for this to unwind its own state
        Q_EMIT voiceDisconnected(ip);
        sock->deleteLater();
    });

    // No waitForConnected: it blocked the GUI thread for up to three seconds
    // while the callee's ringtone was supposed to be playing. The result now
    // arrives as voiceConnected() or voiceDisconnected().
    if (m_internetMode)
        sock->connectToHost(relayHost, relayPort);
    else
        sock->connectToHost(QHostAddress(ip), m_voiceTcpPort);

    return true;
}

bool NetworkManager::sendVoice(const QString &ip, const QByteArray &data)
{
    auto *sock = m_voiceConnections.value(ip, nullptr);
    if (!sock || sock->state() != QTcpSocket::ConnectedState)
        return false;
    // Same cap the receiver enforces, so we never send a frame a correct peer
    // would have to hang up on.
    if (data.isEmpty() || quint32(data.size()) > protocol::kMaxVoiceFrameBytes)
        return false;

    sock->write(lengthPrefix(quint32(data.size())) + data);
    return true;
}

void NetworkManager::disconnectVoice(const QString &ip)
{
    // a call hung up while the socket is still connecting has to stop too
    if (auto *pending = m_pendingVoice.take(ip)) {
        pending->abort();
        pending->deleteLater();
    }
    if (auto *sock = m_voiceConnections.take(ip)) {
        m_voiceRxBuffers.remove(sock);
        sock->disconnectFromHost();
    }
}

void NetworkManager::sendFile(const QString &toIp, const QString &filePath)
{
    sendFileInternal(toIp, filePath, QByteArray(), QStringLiteral("file"));
}

} // namespace koutnet
