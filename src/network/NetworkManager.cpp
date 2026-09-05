// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Network & Audio core
#include "NetworkManager.h"
#include "../core/security/CryptoManager.h"
#include "../core/security/KeepSecret.h"
#include "DiscoverySweep.h"
#include "FileTransferHandler.h"
#include "Protocol.h"

#include <KLocalizedString>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QRandomGenerator>

#include "koutnet_network_debug.h"

#include <utility> // std::move, sendUdp() hands its payload on

namespace koutnet
{

namespace
{

// Normalise a peer address: strip the IPv4-mapped prefix that dual-stack
// sockets deliver (e.g. "::ffff:10.0.0.5" -> "10.0.0.5") and drop the
// zone-id suffix that link-local IPv6 addresses carry on some platforms
// (e.g. "fe80::1%eth0" -> "fe80::1").
QString normaliseAddress(const QHostAddress &addr)
{
    QString s = addr.toString();
    if (s.startsWith(QLatin1String("::ffff:")))
        s = s.mid(7);
    const int zone = s.indexOf(QLatin1Char('%'));
    if (zone >= 0)
        s.truncate(zone);
    return s;
}

bool isLoopbackAddress(const QString &addr)
{
    return addr == QLatin1String("127.0.0.1") || addr == QLatin1String("::1") || addr == QLatin1String("0.0.0.0") || addr == QLatin1String("::");
}

bool isLinkLocal(const QString &addr)
{
    return addr.startsWith(QLatin1String("169.254.")) || addr.startsWith(QLatin1String("fe80:"));
}

QString localIpFallback()
{
    // Prefer an IPv4 address for the primary host ip: the LAN protocol's /24
    // sweep and subnet broadcast are IPv4-only, and most LAN peers are IPv4.
    const auto addrs = QNetworkInterface::allAddresses();
    for (const auto &addr : addrs) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback())
            return normaliseAddress(addr);
    }
    // Fall back to any non-loopback address (IPv6 included).
    for (const auto &addr : addrs) {
        if (!addr.isLoopback())
            return normaliseAddress(addr);
    }
    return QStringLiteral("127.0.0.1");
}

QSet<QString> allLocalIpsFallback()
{
    QSet<QString> result;
    const auto addrs = QNetworkInterface::allAddresses();
    for (const auto &addr : addrs) {
        if (!addr.isLoopback())
            result.insert(normaliseAddress(addr));
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

// Canonical bytes used for HMAC sign/verify: CBOR of the payload with "_sig"
// removed (or absent). Both sides must build this identically; protocol::
// canonicalBytes() is the single source of truth so the wire and the signature
// never drift apart again.
QByteArray signableBytes(const QJsonObject &obj)
{
    return protocol::canonicalBytes(obj);
}

// Packet types accepted from a host we hold no session key for. Presence has to be
// here because it carries the handshake, so requiring a signature on it would mean
// no peer could ever get one. Keep this list at one entry.
bool allowedUnsigned(const QString &type)
{
    return type == protocol::kMsgPresence;
}

QByteArray lengthPrefix(quint32 len)
{
    QByteArray header(protocol::kFrameHeaderBytes, 0);
    header[0] = char((len >> 24) & 0xFF);
    header[1] = char((len >> 16) & 0xFF);
    header[2] = char((len >> 8) & 0xFF);
    header[3] = char(len & 0xFF);
    return header;
}

quint32 readLengthPrefix(const QByteArray &buf)
{
    return (quint32(quint8(buf.at(0))) << 24) | (quint32(quint8(buf.at(1))) << 16) | (quint32(quint8(buf.at(2))) << 8) | quint32(quint8(buf.at(3)));
}

} // namespace

NetworkManager::NetworkManager(CryptoManager *crypto, QObject *parent)
    : ChatBackend(parent)
    , m_crypto(crypto)
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

void NetworkManager::setActiveCalls(const QSet<QString> &ips)
{
    m_activeCalls = ips;
}

void NetworkManager::setStaticPeers(const QStringList &ips)
{
    // Deliberately not re-read from AppSettings: main.cpp passes the value once.
    m_staticPeers = ips;
}

void NetworkManager::setProfile(const QString &handle, const QString &displayName, const QString &bio, const QString &revision)
{
    // Presence goes out on a short timer, so anything in here is paid for repeatedly; a long bio gets cut.
    constexpr int kMaxBioChars = 280;

    const QString trimmedBio = bio.left(kMaxBioChars);
    if (m_profileHandle == handle && m_profileDisplayName == displayName && m_profileBio == trimmedBio && m_profileRevision == revision) {
        return;
    }

    m_profileHandle = handle;
    m_profileDisplayName = displayName;
    m_profileBio = trimmedBio;
    m_profileRevision = revision;

    if (m_running)
        onBroadcastTimer();
}

void NetworkManager::setStatus(int presence, const QString &statusEmoji)
{
    const QString trimmed = statusEmoji.left(8);
    if (m_presence == presence && m_statusEmoji == trimmed)
        return;

    m_presence = presence;
    m_statusEmoji = trimmed;

    if (m_running)
        onBroadcastTimer();
}

void NetworkManager::setGroupPassphrase(const QString &passphrase)
{
    m_groupPassphrase = passphrase;
}

QString NetworkManager::groupKeyStoreKey(const QString &gid)
{
    return QStringLiteral("group_key/") + gid;
}

QString NetworkManager::groupKeyFor(const QString &gid)
{
    const auto it = m_groupKeys.constFind(gid);
    if (it != m_groupKeys.constEnd())
        return it.value();

    QString key;
    if (KeepSecret::read(groupKeyStoreKey(gid), &key) && !key.isEmpty()) {
        m_groupKeys.insert(gid, key);
        return key;
    }
    return QString();
}

void NetworkManager::setGroupKey(const QString &gid, const QString &key)
{
    if (key.isEmpty()) {
        m_groupKeys.remove(gid);
        KeepSecret::remove(groupKeyStoreKey(gid));
        return;
    }
    m_groupKeys.insert(gid, key);
    KeepSecret::write(groupKeyStoreKey(gid), key);
}

void NetworkManager::removeGroupKey(const QString &gid)
{
    setGroupKey(gid, QString());
}

QString NetworkManager::ensureGroupKey(const QString &gid)
{
    if (const QString existing = groupKeyFor(gid); !existing.isEmpty())
        return existing;

    const QString key = randomHex(32);
    m_groupKeys.insert(gid, key);
    if (!KeepSecret::write(groupKeyStoreKey(gid), key))
        qCWarning(KOUTNET_LOG_NETWORK) << "group key for" << gid << "not persisted - secret store unavailable, this session only";
    return key;
}

void NetworkManager::setConnectionMode(ConnectionMode mode)
{
    m_mode = mode;
    if (!m_running)
        return; // applied on next start()
}

bool NetworkManager::modeAvailable(int mode) const
{
    switch (static_cast<ConnectionMode>(mode)) {
    case ConnectionMode::LanOrVpn:
        return true;
    case ConnectionMode::KServer:
        // KServer mode is Matrix. The Matrix transport lives in matrix/ and
        // registers with the chat transport registry on its own; this class
        // answers for LAN only.
        return true;
    }
    return false;
}

void NetworkManager::refreshLocalIps()
{
    m_localIps = allLocalIpsFallback();
    m_localIps.insert(m_hostIp);
    const QString newPrimary = localIpFallback();
    if (newPrimary != m_hostIp) {
        m_hostIp = newPrimary;
        Q_EMIT hostIpChanged();
    }
}

bool NetworkManager::start()
{
    const quint16 udpPort = protocol::kUdpPortDefault;
    const quint16 tcpPort = protocol::kTcpPortDefault;

    m_udp = new QUdpSocket(this);
    // Dual-stack: QHostAddress::Any binds to both IPv4 and IPv6 on systems
    // that support it, so a single socket receives traffic from either protocol.
    bool bound = m_udp->bind(QHostAddress::Any, udpPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    if (!bound) {
        bound = m_udp->bind(QHostAddress::Any, udpPort + 1, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    }
    if (!bound) {
        // int(): KLocalizedString has no quint16 substitution overload.
        Q_EMIT errorOccurred(i18nc("@info:status %1 is a port number", "UDP bind failed on port %1.", int(udpPort)));
        return false;
    }
    m_udpPort = m_udp->localPort();
    connect(m_udp, &QUdpSocket::readyRead, this, &NetworkManager::onUdpReadyRead);

    // mDNS multicast: IPv4 (224.0.0.251) and IPv6 (ff02::fb).
    m_udp->joinMulticastGroup(QHostAddress(QStringLiteral("224.0.0.251")));
    m_udp->joinMulticastGroup(QHostAddress(QStringLiteral("ff02::fb")));

    m_tcpServer = new QTcpServer(this);
    if (!m_tcpServer->listen(QHostAddress::Any, tcpPort)) {
        if (!m_tcpServer->listen(QHostAddress::Any, 0)) {
            Q_EMIT errorOccurred(i18nc("@info:status %1 is a port number", "TCP listen failed on port %1.", int(tcpPort)));
            return false;
        }
    }
    m_voiceTcpPort = m_tcpServer->serverPort();
    connect(m_tcpServer, &QTcpServer::newConnection, this, &NetworkManager::onNewTcpConnection);

    m_running = true;
    // The mode is applied by main.cpp from AppSettings before start() is called.
    m_localIps = allLocalIpsFallback();
    m_localIps.insert(m_hostIp);

    m_broadcastTimer.start(kActiveBroadcastMs); // fast discovery until peers are found

    onBroadcastTimer(); // first broadcast immediately

    QTimer::singleShot(3000, this, &NetworkManager::scanArpTable);
    return true;
}

void NetworkManager::stop()
{
    m_running = false;
    m_broadcastTimer.stop();

    // disconnect(this) before deleteLater(), the way setConnectionMode() does it: the
    // teardown handlers would fire into maps this function has just cleared, and
    // without the deleteLater() a stop()/start() cycle stranded every socket.
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

    if (m_udp) {
        m_udp->close();
        m_udp->deleteLater();
        m_udp = nullptr;
        m_udpPort = 0;
    }
    if (m_tcpServer) {
        m_tcpServer->close();
        m_tcpServer->deleteLater();
        m_tcpServer = nullptr;
        m_voiceTcpPort = 0;
    }
}

void NetworkManager::scanArpTable()
{
    // Reads the OS ARP/NDP cache and pings every neighbour with a presence
    // packet.  Linux: /proc/net/arp (IPv4) and /proc/net/neighbour (IPv6).
    // Windows and macOS: TODO, the same via a process-based `arp -a` parse.
    if (!m_running)
        return;

    QSet<QString> ips;
#ifdef Q_OS_LINUX
    // IPv4 ARP table.
    QFile arpFile(QStringLiteral("/proc/net/arp"));
    if (arpFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const auto lines = QString::fromUtf8(arpFile.readAll()).split(QLatin1Char('\n'));
        for (int i = 1; i < lines.size(); ++i) {
            const auto parts = lines[i].split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() >= 4 && parts[2] != QLatin1String("0x0"))
                ips.insert(parts[0]);
        }
    }
    // IPv6 neighbour table.
    QFile ndpFile(QStringLiteral("/proc/net/neighbour"));
    if (ndpFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const auto lines = QString::fromUtf8(ndpFile.readAll()).split(QLatin1Char('\n'));
        for (int i = 1; i < lines.size(); ++i) {
            const auto parts = lines[i].split(QLatin1Char(' '), Qt::SkipEmptyParts);
            // Column 0 is IP, column 3 is state (0x2 = REACHABLE, 0x4 = STALE, etc.)
            if (parts.size() >= 4 && parts[3] != QLatin1String("0x0"))
                ips.insert(parts[0]);
        }
    }
#endif
    if (ips.isEmpty()) {
        QTimer::singleShot(60'000, this, &NetworkManager::scanArpTable);
        return;
    }

    const QJsonObject payload = presencePayload();
    const QByteArray data = protocol::encodeFrame(payload);
    int sent = 0;
    for (const auto &ip : std::as_const(ips)) {
        if (ip == m_hostIp || isLinkLocal(ip) || isLoopbackAddress(ip))
            continue;
        m_udp->writeDatagram(data, QHostAddress(ip), protocol::kUdpPortDefault);
        if (m_udpPort != protocol::kUdpPortDefault)
            m_udp->writeDatagram(data, QHostAddress(ip), m_udpPort);
        ++sent;
    }
    if (sent > 0)
        qCDebug(KOUTNET_LOG_NETWORK) << "ARP/NDP scan: pinged" << sent << "neighbour(s)";

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

    if (m_crypto) {
        const QJsonObject hs = m_crypto->handshakePayload();
        for (auto it = hs.constBegin(); it != hs.constEnd(); ++it)
            payload[it.key()] = it.value();
    }

    payload[QStringLiteral("username")] = m_profileHandle;
    payload[QStringLiteral("display_name")] = m_profileDisplayName;
    payload[QStringLiteral("bio")] = m_profileBio;
    payload[QStringLiteral("profile_rev")] = m_profileRevision;
    payload[QStringLiteral("presence")] = m_presence;
    payload[QStringLiteral("status_emoji")] = m_statusEmoji;
    return payload;
}

QStringList NetworkManager::staticUnicastTargets(const QStringList &configured, const QMap<QString, QJsonObject> &knownPeers, const QString &hostIp)
{
    QStringList targets;
    for (const QString &target : configured) {
        if (target == hostIp || knownPeers.contains(target))
            continue;
        if (QHostAddress(target).isNull())
            continue; // settings field holds a non-address
        targets.append(target);
    }
    return targets;
}

void NetworkManager::onBroadcastTimer()
{
    if (!m_running)
        return;

    if (!m_udp)
        return;

    // Cheap, always-on beacon: multicast first, then the subnet broadcast. This
    // keeps us discoverable without ever touching the loud /24 sweep below.
    const int desiredInterval = m_peers.isEmpty() ? kActiveBroadcastMs : kIdleBroadcastMs;
    if (m_broadcastTimer.interval() != desiredInterval)
        m_broadcastTimer.setInterval(desiredInterval);

    const QJsonObject payload = presencePayload();
    const QByteArray data = protocol::encodeFrame(payload);
    // mDNS multicast: IPv4 and IPv6.
    m_udp->writeDatagram(data, QHostAddress(QStringLiteral("224.0.0.251")), m_udpPort);
    m_udp->writeDatagram(data, QHostAddress(QStringLiteral("ff02::fb")), m_udpPort);
    if (m_udpPort != protocol::kUdpPortDefault) {
        m_udp->writeDatagram(data, QHostAddress(QStringLiteral("224.0.0.251")), protocol::kUdpPortDefault);
        m_udp->writeDatagram(data, QHostAddress(QStringLiteral("ff02::fb")), protocol::kUdpPortDefault);
    }

    QSet<QString> sentBroadcasts;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto &iface : interfaces) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || (flags & QNetworkInterface::IsLoopBack))
            continue;
        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (const auto &entry : std::as_const(entries)) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                // IPv4: subnet broadcast (e.g. 192.168.1.255).
                const QString bcast = entry.broadcast().toString();
                if (!bcast.isEmpty() && bcast != QLatin1String("0.0.0.0") && !sentBroadcasts.contains(bcast)) {
                    m_udp->writeDatagram(data, entry.broadcast(), m_udpPort);
                    if (m_udpPort != protocol::kUdpPortDefault)
                        m_udp->writeDatagram(data, entry.broadcast(), protocol::kUdpPortDefault);
                    sentBroadcasts.insert(bcast);
                }
            } else if (entry.ip().protocol() == QAbstractSocket::IPv6Protocol) {
                // IPv6 has no subnet broadcast; use the all-nodes multicast
                // (ff02::1) scoped to this interface's link.
                const QHostAddress allNodes(QStringLiteral("ff02::1"));
                const QNetworkAddressEntry *e = &entry;
                const QString key = QStringLiteral("v6:%1").arg(iface.index());
                if (!sentBroadcasts.contains(key)) {
                    // Qt does not expose scope id on QNetworkAddressEntry in all
                    // versions; the socket's outgoing interface index suffices for
                    // link-local multicast on Linux.
                    m_udp->writeDatagram(data, allNodes, m_udpPort);
                    if (m_udpPort != protocol::kUdpPortDefault)
                        m_udp->writeDatagram(data, allNodes, protocol::kUdpPortDefault);
                    sentBroadcasts.insert(key);
                }
                Q_UNUSED(e)
            }
        }
    }

    // Subnet sweep: only while no peer is known, with exponential backoff
    // plus jitter so an empty network settles down. Uses the real prefix
    // length from the interface rather than assuming /24.  Subnets wider
    // than /20 (4094 hosts) are skipped — sweeping a /16 would send 65k
    // packets and that is not discovery, that is a scan.
    const double now = nowEpoch();
    const double jitter = 0.85 + 0.30 * QRandomGenerator::global()->generateDouble();
    if (koutnet::discovery::sweepTick(now, m_lastScan, m_sweepIntervalMs, m_peers.isEmpty(), kSweepMinMs, kSweepMaxMs, jitter)) {
        const QHostAddress hostAddr(m_hostIp);
        if (hostAddr.protocol() == QAbstractSocket::IPv4Protocol) {
            // find the interface entry that owns our primary IP so we get
            // the actual prefix length instead of guessing /24
            int prefix = 24;
            const auto ifaces = QNetworkInterface::allInterfaces();
            for (const auto &iface : ifaces) {
                for (const auto &entry : iface.addressEntries()) {
                    if (entry.ip() == hostAddr && entry.prefixLength() > 0) {
                        prefix = entry.prefixLength();
                        break;
                    }
                }
            }
            // skip anything wider than /20 — too many hosts to sweep
            if (prefix >= 20) {
                const quint32 ip = hostAddr.toIPv4Address();
                const quint32 mask = prefix < 32 ? ~((1u << (32 - prefix)) - 1u) : 0xFFFFFFFFu;
                const quint32 net = ip & mask;
                const quint32 bcast = net | ~mask;
                // first usable host = net+1, last = bcast-1
                for (quint32 addr = net + 1; addr < bcast; ++addr) {
                    if (addr == ip)
                        continue;
                    const QHostAddress target(addr);
                    m_udp->writeDatagram(data, target, m_udpPort);
                }
            }
        }
    }

    // Static peers: unicast each cycle to addresses that have not answered yet.
    // A silent one is not reminded every cycle.
    const QStringList targets = staticUnicastTargets(m_staticPeers, m_peers, m_hostIp);
    for (const QString &target : std::as_const(targets))
        m_udp->writeDatagram(data, QHostAddress(target), m_udpPort);

    pruneStalePeers();
}

void NetworkManager::pruneStalePeers()
{
    const double now = nowEpoch();
    QVector<QString> stale;
    for (auto it = m_peers.constBegin(); it != m_peers.constEnd(); ++it) {
        const double lastSeen = it.value().value(protocol::kFieldLastSeen).toDouble();
        if (now - lastSeen > 25)
            stale.append(it.key());
    }
    for (const auto &ip : stale) {
        const QString peerId = m_peers.value(ip).value(QStringLiteral("from_id")).toString();
        // Or the next presence packet from this peer would be filed under an address
        // no longer in the table, and it would never come back.
        if (!peerId.isEmpty() && m_peerKeyById.value(peerId) == ip)
            m_peerKeyById.remove(peerId);
        m_peers.remove(ip);
        Q_EMIT userOffline(ip);
    }
}

void NetworkManager::onUdpReadyRead()
{
    while (m_udp && m_udp->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_udp->receiveDatagram();
        const QString host = normaliseAddress(dg.senderAddress());

        handleDatagram(host, dg.data());
    }
}

void NetworkManager::handleDatagram(const QString &host, const QByteArray &data)
{
    // Attacker-supplied bytes: decode defensively. Raw CBOR so binary fields
    // survive instead of being forced through JSON and base64.
    QString type;
    QCborMap map;
    if (!protocol::decodeFrame(data, type, map)) {
        Q_EMIT errorOccurred(i18nc("@info:status %1 is a host address", "Unreadable packet from %1 - dropping it.", host));
        return;
    }

    // The header type is the one authentication branches on, so the payload's
    // "type" field must match it.
    const QString payloadType = map.value(QStringLiteral("type")).toString();
    if (payloadType != type) {
        Q_EMIT errorOccurred(i18nc("@info:status %1 is a host address", "Mismatched frame from %1 - dropping it.", host));
        return;
    }

    // HMAC verification on the CBOR map so canonical bytes match the signer's.
    // Presence is exempt: it is broadcast and never carries a _sig.
    if (m_crypto && !type.isEmpty() && !allowedUnsigned(type)) {
        if (m_crypto && !m_crypto->checkRate(host))
            return; // dropped - over rate limit

        const QString claimed = map.value(QStringLiteral("from_id")).toString();
        const QString peerId = m_crypto->hasSession(claimed) ? claimed : m_crypto->identityForAddress(host);
        if (!m_crypto->hasSession(peerId)) {
            Q_EMIT errorOccurred(i18nc("@info:status %1 is a message type, %2 a host address", "Unauthenticated %1 from %2 - dropping.", type, host));
            return;
        }
        const QString sig = map.value(QStringLiteral("_sig")).toString();
        if (sig.isEmpty() || !m_crypto->verifyPacket(peerId, protocol::canonicalBytes(map), sig)) {
            Q_EMIT errorOccurred(i18nc("@info:status %1 is a host address", "HMAC verification failed from %1 - dropping.", host));
            return;
        }

        // Layer 5 - replay guard, on the identity. The nonce and timestamp are inside
        // the signature, so a replay verifies as happily without this check.
        const QString nonce = map.value(QStringLiteral("nonce")).toString();
        if (!nonce.isEmpty() && !m_crypto->checkReplay(peerId, nonce, map.value(QStringLiteral("ts")).toDouble())) {
            return; // replayed or outside the timestamp window
        }

        // from_ip is where the sender believes it lives, not what the peer is filed
        // under here - the interface would route the message into an empty chat.
        const QString filedAs = m_peerKeyById.value(peerId);
        if (!filedAs.isEmpty())
            map.insert(QStringLiteral("from_ip"), filedAs);
        map.insert(QStringLiteral("from_id"), peerId);
    }

    // File chunks carry raw bytes and must not be forced through JSON: hand the
    // bytes straight to the reassembler. Anything else becomes a QJsonObject for
    // the existing handlers.
    if (type == protocol::kMsgFileData) {
        const QString tid = map.value(QStringLiteral("tid")).toString();
        const int idx = map.value(QStringLiteral("idx")).toInteger(-1);
        const int total = map.value(QStringLiteral("total")).toInteger(-1);
        const QCborValue dataValue = map.value(QStringLiteral("data"));
        const QByteArray chunk = dataValue.isByteArray() ? dataValue.toByteArray() : QByteArray::fromBase64(dataValue.toString().toLatin1());
        Q_EMIT fileChunkBytes(tid, idx, total, chunk);

        // Track received chunks per transfer. When all arrive, ACK the sender.
        if (!tid.isEmpty() && total > 0 && idx >= 0) {
            IncomingTransfer &inc = m_incomingTransfers[tid];
            if (inc.total == 0) {
                inc.total = total;
                inc.fromIp = host;
            }
            inc.received.insert(idx);
            if (inc.received.size() >= inc.total) {
                QCborMap ack;
                ack.insert(QStringLiteral("type"), protocol::kMsgFileAck);
                ack.insert(QStringLiteral("tid"), tid);
                ack.insert(QStringLiteral("from_ip"), m_hostIp);
                sendUdp(ack, inc.fromIp);
                m_incomingTransfers.remove(tid);
            }
        }
        return;
    }

    if (type == protocol::kMsgFileAck) {
        const QString tid = map.value(QStringLiteral("tid")).toString();
        auto it = m_outgoingTransfers.find(tid);
        if (it != m_outgoingTransfers.end()) {
            if (it->timer)
                it->timer->stop();
            m_outgoingTransfers.erase(it);
        }
        return;
    }

    dispatch(host, protocol::qjsonFromCbor(map));
}

void NetworkManager::dispatch(const QString &host, QJsonObject msg)
{
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

    // Layer 4 HMAC verification and Layer 5 replay guard now live in
    // handleDatagram() on the raw CBOR map; by the time we get here the packet
    // is authenticated and from_id/from_ip are resolved.
    const QString peerId = msg.value(QStringLiteral("from_id")).toString();

    if (type == protocol::kMsgPresence) {
        handlePresence(host, msg);
    } else if (type == protocol::kMsgChat || type == protocol::kMsgGroup || type == protocol::kMsgReaction || type == protocol::kMsgEdit
               || type == protocol::kMsgDelete || type == protocol::kMsgRead) {
        decryptMessageText(peerId, msg, [this](QJsonObject decrypted) {
            Q_EMIT message(decrypted);
        });
    } else if (type == protocol::kMsgPrivate) {
        // Against every address of ours, not just the primary one:
        // comparing with m_hostIp alone dropped this on multi-homed machines.
        if (myIps.contains(msg.value(QStringLiteral("to")).toString())) {
            decryptMessageText(peerId, msg, [this](QJsonObject decrypted) {
                Q_EMIT message(decrypted);
            });
        }
    } else if (type == protocol::kMsgCallReq) {
        if (!m_activeCalls.isEmpty()) {
            // already on a call; busy is cheaper for the caller than ringing forever
            sendCallBusy(host);
        } else {
            m_ringingCalls.insert(host);
            Q_EMIT callRequest(msg.value(QStringLiteral("username")).toString(QStringLiteral("?")), host);
        }
    } else if (type == protocol::kMsgCallAccept) {
        // The whole point of the state machine: a call_accept from a peer we
        // never called is a session-holder trying to open our microphone.
        if (!m_pendingCalls.remove(host))
            return;
        Q_EMIT callAccepted(msg.value(QStringLiteral("username")).toString(QStringLiteral("?")), host);
    } else if (type == protocol::kMsgCallBusy || type == protocol::kMsgCallReject) {
        if (!m_pendingCalls.remove(host))
            return;
        Q_EMIT callRejected(host);
    } else if (type == protocol::kMsgCallEnd) {
        // call_end carries two meanings: "your ring was cancelled" to a caller
        // and "the call is over" to a connected peer.
        if (!m_pendingCalls.remove(host) && !m_ringingCalls.remove(host) && !m_activeCalls.contains(host))
            return;
        Q_EMIT callEnded(host);
    } else if (type == protocol::kMsgFileMeta) {
        Q_EMIT fileMeta(msg);
    } else if (type == protocol::kMsgGroupInv) {
        // The invitation may carry the group key, sealed under our session
        // with the sender. Opening it is a session decrypt, synchronous and
        // cheap - no attacker salt involved - so it is safe to do in dispatch.
        const QString gid = msg.value(QStringLiteral("gid")).toString();
        const QString sealedKey = msg.value(QStringLiteral("key")).toString();
        if (!sealedKey.isEmpty() && m_crypto) {
            const QString key = m_crypto->decrypt(sealedKey, QString(), peerId);
            if (!key.isEmpty())
                setGroupKey(gid, key);
        }
        Q_EMIT groupInvite(msg.value(QStringLiteral("gid")).toString(), msg.value(QStringLiteral("gname")).toString(), host);
    } else if (type == protocol::kMsgTyping) {
        Q_EMIT typing(msg.value(QStringLiteral("username")).toString(), msg.value(QStringLiteral("chat_id")).toString(QStringLiteral("public")), host);
    }
}

void NetworkManager::decryptMessageText(const QString &peerRef, QJsonObject msg, const std::function<void(QJsonObject)> &done)
{
    if (!m_crypto || !msg.contains(QStringLiteral("text"))) {
        done(msg);
        return;
    }

    // A group message is sealed under its own key; anything without a gid falls
    // back to the shared passphrase, and to cleartext when none exists.
    QString passphrase = m_groupPassphrase;
    const QVariant gid = msg.value(QStringLiteral("gid"));
    if (!gid.isNull()) {
        const QString key = groupKeyFor(gid.toString());
        if (!key.isEmpty())
            passphrase = key;
    }

    // The sender "encrypted" flag used to decide this, so clearing it was enough to
    // have the text handed unchecked. What matters is whether we hold a key.
    if (!m_crypto->hasSession(peerRef) && passphrase.isEmpty()) {
        done(msg);
        return;
    }

    const QString cipherText = msg.value(QStringLiteral("text")).toString();
    // Async so an attacker's fresh-salt flood pays for the KDF on a worker
    // thread rather than in the GUI's event loop (see CryptoManager::decryptAsync).
    m_crypto->decryptAsync(cipherText, passphrase, peerRef, [msg, done](const QString &plain, bool delivered) mutable {
        if (!delivered)
            return; // the queue gate refused it: a flood is dropped, not rendered
        msg[QStringLiteral("text")] = plain;
        done(msg);
    });
}

void NetworkManager::handlePresence(const QString &host, QJsonObject msg)
{
    // Only ever a delivery candidate: a host at one address can advertise any other.
    const QString advertised = msg.value(QStringLiteral("ip")).toString();
    const QSet<QString> myIps = m_localIps.isEmpty() ? QSet<QString>{m_hostIp} : m_localIps;
    if (myIps.contains(advertised) || myIps.contains(host))
        return;
    if (host.isEmpty() || host == QLatin1String("0.0.0.0") || host == QLatin1String("::"))
        return; // nowhere to file it and nowhere to answer

    // Presence packets are unsigned (the handshake rides inside them) and
    // therefore the cheapest packet to forge.  A per-address cap of
    // kMaxPresencePerSec keeps normal operation unaffected while making a
    // spoofed flood pointless.
    {
        const double now = nowEpoch();
        QVector<double> &window = m_presenceRate[host];
        QVector<double> kept;
        kept.reserve(window.size());
        for (double t : std::as_const(window)) {
            if (now - t < 1.0)
                kept.append(t);
        }
        window = kept;
        if (window.size() >= kMaxPresencePerSec)
            return;
        window.append(now);
    }

    QString peerId;
    bool handshakeProcessed = false;
    if (m_crypto) {
        const QString nonce = msg.value(QStringLiteral("nonce")).toString();
        const double ts = msg.value(QStringLiteral("ts")).toDouble();

        if (msg.contains(QStringLiteral("dh_pub"))) {
            // The replay gate runs before the handshake, not after it: a captured
            // presence would otherwise re-derive the session every couple of
            // seconds until this check was reached. Keyed on the address because
            // the identity is not known yet.
            if (nonce.isEmpty() || !m_crypto->checkReplay(host, nonce, ts))
                return;
            // Whether the address already knew its owner when the gate above ran.
            // The handshake below is what teaches an unknown address its owner,
            // which is also why the gate above could not have used the identity.
            const QString ownerBefore = m_crypto->identityForAddress(host);
            // Someone new at an address a peer we still hold a session with is using.
            // The peer record must survive it - it holds the name and fingerprint the
            // warning shows, and a refused packet rewriting those aids the spoofer.
            if (m_crypto->processHandshakeFrom(host, msg, &peerId) == CryptoManager::HandshakeOutcome::AddressTaken)
                return;
            handshakeProcessed = true;
            // The identity bucket gets the nonce too, or a capture replayed from a
            // second address - where the address bucket above is empty - would slip
            // through and keep a dead peer looking alive. Only when the address was
            // unknown: resolved, it and the identity are the same bucket, and a
            // second check would refuse the packet the first one just admitted.
            if (ownerBefore.isEmpty() && !peerId.isEmpty() && !m_crypto->checkReplay(peerId, nonce, ts))
                return;
        } else if (!nonce.isEmpty() && !m_crypto->checkReplay(host, nonce, ts)) {
            return; // replayed presence packet
        }
    }

    // One entry per identity, filed under the address first heard: two contacts
    // for one person is worse than a stale address.
    const QString key = m_peerKeyById.value(peerId, host);
    msg[QStringLiteral("ip")] = key;
    if (!advertised.isEmpty() && advertised != key)
        msg[protocol::kFieldAdvertisedIp] = advertised;
    if (!peerId.isEmpty())
        msg[QStringLiteral("from_id")] = peerId;

    const bool isNew = !m_peers.contains(key);
    msg[protocol::kFieldLastSeen] = nowEpoch();
    if (m_crypto)
        msg[QStringLiteral("e2e")] = m_crypto->hasSession(peerId.isEmpty() ? host : peerId);
    if (isNew) {
        // Spoofed presences must not grow the peer table without bound:
        // the oldest entry makes room, like the replay buckets do.
        if (m_peers.size() >= kMaxPeers)
            evictOldestPeer();
        m_peers[key] = msg;
    } else {
        m_peers[key] = msg;
    }
    // Only update the identity→address mapping when a handshake was processed in
    // this presence (meaning a session was established or refreshed).  A bare
    // presence without crypto material must not teach the routing table a new
    // owner for an identity, because an attacker can forge the "from_id" field
    // in an unsigned packet.
    if (!peerId.isEmpty() && handshakeProcessed)
        m_peerKeyById[peerId] = key;

    if (isNew)
        Q_EMIT userOnline(msg);
    else
        Q_EMIT peerRefreshed(key, msg.value(protocol::kFieldLastSeen).toDouble());
}

void NetworkManager::evictOldestPeer()
{
    // One pass instead of a sort: under a flood this runs for every newcomer.
    auto oldest = m_peers.begin();
    for (auto it = m_peers.begin(); it != m_peers.end(); ++it) {
        if (it.value().value(protocol::kFieldLastSeen).toDouble() < oldest.value().value(protocol::kFieldLastSeen).toDouble())
            oldest = it;
    }
    const QString key = oldest.key();
    const QString peerId = oldest.value().value(QStringLiteral("from_id")).toString();
    m_peers.erase(oldest);
    if (!peerId.isEmpty() && m_peerKeyById.value(peerId) == key)
        m_peerKeyById.remove(peerId);
    Q_EMIT userOffline(key);
}

void NetworkManager::onNewTcpConnection()
{
    while (m_tcpServer && m_tcpServer->hasPendingConnections()) {
        QTcpSocket *sock = m_tcpServer->nextPendingConnection();
        const QString ip = normaliseAddress(sock->peerAddress());

        // A reconnecting peer arrives while the last socket is still in the map.
        replaceVoiceSocket(ip, sock);
        connect(sock, &QTcpSocket::readyRead, this, [this, sock, ip] {
            onVoiceData(sock, ip);
        });
        connect(sock, &QTcpSocket::disconnected, this, [this, sock, ip] {
            onVoiceDisconnected(sock, ip);
        });
        Q_EMIT voiceConnected(ip);
    }
}

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
    // Reassemble whole frames; TCP may split one frame across reads.
    QByteArray buf = m_voiceRxBuffers.value(sock) + sock->readAll();
    QVector<QByteArray> frames;

    while (buf.size() >= protocol::kFrameHeaderBytes) {
        const quint32 len = readLengthPrefix(buf);
        if (len == 0 || len > protocol::kMaxVoiceFrameBytes) {
            // peer-supplied length: a silly one asks us to allocate a gigabyte
            m_voiceRxBuffers.remove(sock);
            Q_EMIT errorOccurred(
                i18nc("@info:status %1 is a host address, %2 a byte count", "Voice frame from %1 declared %2 bytes - dropping the connection.", ip, len));
            // abort() on a connected socket emits disconnected(), where the teardown
            // lives - doing it here as well would tell the call layer twice.
            sock->abort();
            return;
        }
        if (quint32(buf.size() - protocol::kFrameHeaderBytes) < len)
            break; // the rest of this frame is still in flight
        frames.append(buf.mid(protocol::kFrameHeaderBytes, int(len)));
        buf.remove(0, protocol::kFrameHeaderBytes + int(len));
    }

    // Remainder goes back before anything is emitted: a listener may hang up
    // and erase this socket's buffer underneath us.
    m_voiceRxBuffers[sock] = buf;

    for (const auto &frame : std::as_const(frames)) {
        Q_EMIT voiceDataFrom(ip, frame);
    }
}

void NetworkManager::onVoiceDisconnected(QTcpSocket *sock, const QString &ip)
{
    m_voiceRxBuffers.remove(sock);
    m_voiceConnections.remove(ip);
    Q_EMIT voiceDisconnected(ip);
    Q_EMIT callEnded(ip);
}

void NetworkManager::sendUdp(QJsonObject payload, const QString &targetIp)
{
    sendUdpToAll(std::move(payload), targetIp.isEmpty() ? QVector<QString>{} : QVector<QString>{targetIp});
}

bool NetworkManager::sendUdpToAll(QJsonObject payload, const QVector<QString> &targets)
{
    if (!m_udp)
        return false;

    const QString msgType = payload.value(QStringLiteral("type")).toString();
    if (msgType != protocol::kMsgPresence) {
        payload[QStringLiteral("nonce")] = randomHex(8);
        payload[QStringLiteral("ts")] = nowEpoch();

        // Who this is from, so the far side finds the session. Before the signature,
        // so it is only a claim.
        if (m_crypto)
            payload[QStringLiteral("from_id")] = m_crypto->ownIdentityId();

        // HMAC-sign unicast packets once a session key exists; broadcasts have
        // no single peer session to sign for.
        QString peerId;
        for (const QString &target : targets) {
            peerId = m_crypto ? m_crypto->identityForAddress(target) : QString();
            if (!peerId.isEmpty())
                break;
        }
        if (!peerId.isEmpty())
            payload[QStringLiteral("_sig")] = m_crypto->signPacket(peerId, signableBytes(payload));
    }

    const QByteArray data = protocol::encodeFrame(payload);

    if (!targets.isEmpty()) {
        for (const QString &target : targets)
            m_udp->writeDatagram(data, QHostAddress(target), protocol::kUdpPortDefault);
    } else {
        m_udp->writeDatagram(data, QHostAddress::Broadcast, protocol::kUdpPortDefault);
    }
    return true;
}

void NetworkManager::sendUdp(QCborMap payload, const QString &targetIp)
{
    sendUdpToAll(std::move(payload), targetIp.isEmpty() ? QVector<QString>{} : QVector<QString>{targetIp});
}

bool NetworkManager::sendUdpToAll(QCborMap payload, const QVector<QString> &targets)
{
    if (!m_udp)
        return false;

    const QString msgType = payload.value(QStringLiteral("type")).toString();
    if (msgType != protocol::kMsgPresence) {
        payload.insert(QStringLiteral("nonce"), randomHex(8));
        payload.insert(QStringLiteral("ts"), nowEpoch());

        if (m_crypto)
            payload.insert(QStringLiteral("from_id"), m_crypto->ownIdentityId());

        QString peerId;
        for (const QString &target : targets) {
            peerId = m_crypto ? m_crypto->identityForAddress(target) : QString();
            if (!peerId.isEmpty())
                break;
        }
        if (!peerId.isEmpty())
            payload.insert(QStringLiteral("_sig"), m_crypto->signPacket(peerId, protocol::canonicalBytes(payload)));
    }

    const QByteArray data = protocol::encodeFrame(payload);

    if (!targets.isEmpty()) {
        for (const QString &target : targets)
            m_udp->writeDatagram(data, QHostAddress(target), protocol::kUdpPortDefault);
    } else {
        m_udp->writeDatagram(data, QHostAddress::Broadcast, protocol::kUdpPortDefault);
    }
    return true;
}

QVector<QString> NetworkManager::deliveryAddresses(const QString &toIp) const
{
    QVector<QString> targets;
    if (!toIp.isEmpty())
        targets.append(toIp);

    // Addresses a signed packet has actually arrived from come first.
    if (m_crypto) {
        const QString peerId = m_crypto->identityForAddress(toIp);
        const auto observed = m_crypto->addressesFor(peerId);
        for (const QString &addr : observed) {
            if (targets.size() >= kMaxDeliveryAddresses)
                return targets;
            if (!addr.isEmpty() && !targets.contains(addr) && !m_localIps.contains(addr))
                targets.append(addr);
        }
    }

    // Then the advertised ones, which are guesses - and the reason for the cap.
    const auto altIps = m_peers.value(toIp).value(QStringLiteral("all_ips")).toArray();
    for (const auto &v : altIps) {
        if (targets.size() >= kMaxDeliveryAddresses)
            break;
        const QString altIp = v.toString();
        if (!altIp.isEmpty() && !targets.contains(altIp) && !m_localIps.contains(altIp))
            targets.append(altIp);
    }
    return targets;
}

bool NetworkManager::sendPrivate(const QString &text, const QString &toIp)
{
    if (!m_crypto) {
        Q_EMIT errorOccurred(i18nc("@info:status", "Encryption unavailable - message not sent."));
        return false;
    }

    // A session is the better key when there is one.
    const QString passphrase = m_crypto->hasSession(toIp) ? QString() : m_groupPassphrase;
    const QString cipherText = m_crypto->encrypt(text, passphrase, toIp);
    if (cipherText.isEmpty() || cipherText == text) {
        Q_EMIT errorOccurred(i18nc("@info:status", "Failed to encrypt private message - not sent."));
        return false;
    }

    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgPrivate;
    payload[QStringLiteral("text")] = cipherText;
    payload[QStringLiteral("to")] = toIp;
    payload[QStringLiteral("from_ip")] = m_hostIp;
    payload[QStringLiteral("encrypted")] = true;

    // One signed datagram copied to every address this peer answers on.
    sendUdpToAll(payload, deliveryAddresses(toIp));
    return true;
}

void NetworkManager::sendGroupMessage(const QString &gid, const QString &text, const QVector<QString> &members)
{
    // One key per group. Created on first message; without the store the generated
    // key protects this session only.
    QString passphrase = groupKeyFor(gid);
    if (passphrase.isEmpty())
        passphrase = ensureGroupKey(gid);
    if (!m_crypto || passphrase.isEmpty()) {
        Q_EMIT errorOccurred(i18nc("@info:status", "Failed to create a group key - refusing to send in the clear."));
        return;
    }

    const QString cipherText = m_crypto->encrypt(text, passphrase, QString());
    if (cipherText == text) {
        // encrypt() returns plaintext on failure; sending that would leak the
        // message the user thinks is protected.
        Q_EMIT errorOccurred(i18nc("@info:status", "Failed to encrypt the group message - not sent."));
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
    // The accept/reject/busy/end for this ring only counts against a request
    // that was actually sent.
    m_pendingCalls.insert(toIp);
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgCallReq;
    sendUdp(payload, toIp);
}

void NetworkManager::sendCallAccept(const QString &toIp)
{
    m_ringingCalls.remove(toIp); // the ring is resolved either way
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgCallAccept;
    sendUdp(payload, toIp);
    connectVoice(toIp);
}

void NetworkManager::sendCallReject(const QString &toIp)
{
    m_ringingCalls.remove(toIp);
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgCallReject;
    sendUdp(payload, toIp);
}

void NetworkManager::sendCallBusy(const QString &toIp)
{
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgCallBusy;
    sendUdp(payload, toIp);
}

void NetworkManager::sendCallEnd(const QString &toIp)
{
    // The remote's end of the ring no longer needs an answer.
    m_pendingCalls.remove(toIp);
    m_ringingCalls.remove(toIp);
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgCallEnd;
    sendUdp(payload, toIp);
}

void NetworkManager::sendReaction(const QString &chatId, const QVariant &identifier, const QString &emoji, bool added)
{
    const double ts = identifier.toDouble();
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgReaction;
    payload[QStringLiteral("from_ip")] = m_hostIp;
    payload[QStringLiteral("chat_id")] = chatId;
    payload[QStringLiteral("msg_ts")] = ts;
    payload[QStringLiteral("emoji")] = emoji;
    payload[QStringLiteral("added")] = added;
    // Reactions carry no user-visible text but the emoji is content the user
    // expects to be private.  Encrypt under the session when there is one.
    if (m_crypto && m_crypto->hasSession(chatId)) {
        const QString plain = emoji;
        const QString cipher = m_crypto->encrypt(plain, QString(), chatId);
        if (!cipher.isEmpty() && cipher != plain) {
            payload[QStringLiteral("emoji")] = cipher;
            payload[QStringLiteral("encrypted")] = true;
        }
    }
    sendUdp(payload, chatId);
}

void NetworkManager::sendMessageEdit(const QString &toIp, const QString &chatId, const QVariant &identifier, const QString &newText)
{
    const double ts = identifier.toDouble();
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgEdit;
    payload[QStringLiteral("from_ip")] = m_hostIp;
    payload[QStringLiteral("chat_id")] = chatId;
    payload[QStringLiteral("msg_ts")] = ts;
    // Edits contain the corrected text and must be encrypted.
    if (m_crypto && m_crypto->hasSession(toIp)) {
        const QString cipher = m_crypto->encrypt(newText, QString(), toIp);
        if (!cipher.isEmpty() && cipher != newText) {
            payload[QStringLiteral("new_text")] = cipher;
            payload[QStringLiteral("encrypted")] = true;
        } else {
            Q_EMIT errorOccurred(i18nc("@info:status", "Failed to encrypt edited message - not sent."));
            return;
        }
    } else {
        payload[QStringLiteral("new_text")] = newText;
    }
    sendUdp(payload, toIp);
}

void NetworkManager::sendMessageDelete(const QString &toIp, const QString &chatId, const QVariant &identifier)
{
    const double ts = identifier.toDouble();
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
    // Every address the peer answers on: a lost receipt leaves the sender
    // showing as unconfirmed forever.
    sendUdpToAll(payload, deliveryAddresses(toIp));
}

void NetworkManager::sendGroupInvite(const QString &gid, const QString &gname, const QString &toIp)
{
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgGroupInv;
    payload[QStringLiteral("gid")] = gid;
    payload[QStringLiteral("gname")] = gname;
    // The group key rides along only under a session with the invitee.
    const QString key = groupKeyFor(gid);
    if (!key.isEmpty() && m_crypto && m_crypto->hasSession(toIp))
        payload[QStringLiteral("key")] = m_crypto->encrypt(key, QString(), toIp);
    sendUdp(payload, toIp);
}

bool NetworkManager::sendFileInternal(const QString &toIp, const QString &filePath, const QByteArray &rawBytes, const QString &filename)
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
            Q_EMIT errorOccurred(i18nc("@info:status %1 is a file path", "File not found: %1", filePath));
            return false;
        }
        data = file.readAll();
        fname = QFileInfo(filePath).fileName();
        ext = QFileInfo(filePath).suffix().toLower();
    }

    // Sealed whole before chunking: one nonce and tag for the transfer.
    if (!m_crypto) {
        Q_EMIT errorOccurred(i18nc("@info:status %1 is a file path", "Encryption unavailable for %1 - not sent.", filePath));
        return false;
    }
    const QByteArray sealed = m_crypto->encryptFileBytes(toIp, data);
    if (sealed.isEmpty()) {
        Q_EMIT errorOccurred(i18nc("@info:status %1 is a file path", "Failed to encrypt %1 - not sent.", filePath));
        return false;
    }
    data = sealed;

    // The far side refuses anything past this cap.
    if (data.size() > FileTransferHandler::kMaxTransferBytes) {
        Q_EMIT errorOccurred(i18nc("@info:status %1 is a file path", "Refused to send %1: larger than the transfer limit.", filePath));
        return false;
    }

    static const QSet<QString> kImageExts =
        {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("webp")};
    const bool isImage = kImageExts.contains(ext);
    const QString tid = randomHex(8);

    QJsonObject meta;
    meta[QStringLiteral("type")] = protocol::kMsgFileMeta;
    meta[QStringLiteral("tid")] = tid;
    meta[QStringLiteral("filename")] = fname;
    meta[QStringLiteral("size")] = data.size();
    meta[QStringLiteral("is_image")] = isImage;
    meta[QStringLiteral("encrypted")] = true;
    meta[QStringLiteral("from_ip")] = m_hostIp;
    meta[QStringLiteral("to")] = toIp.isEmpty() ? QStringLiteral("public") : toIp;
    meta[QStringLiteral("ts")] = nowEpoch();
    sendUdp(meta, toIp);

    // Raw byte string on the wire, so a chunk can approach the UDP ceiling.
    constexpr int kChunkSize = 60000;
    const int total = data.size();
    const int totalChunks = (total + kChunkSize - 1) / kChunkSize;

    QVector<QCborMap> chunks;
    int idx = 0;
    for (int offset = 0; offset < total; offset += kChunkSize, ++idx) {
        const QByteArray chunk = data.mid(offset, kChunkSize);
        QCborMap c;
        c.insert(QStringLiteral("type"), protocol::kMsgFileData);
        c.insert(QStringLiteral("tid"), tid);
        c.insert(QStringLiteral("idx"), idx);
        c.insert(QStringLiteral("total"), totalChunks);
        c.insert(QStringLiteral("data"), chunk); // byte string, not base64
        chunks.append(c);
    }
    sendChunksQueued(chunks, toIp, 0);
    return true;
}

void NetworkManager::sendChunksQueued(const QVector<QCborMap> &chunks, const QString &toIp, int idx, int batch)
{
    if (idx >= chunks.size()) {
        // All chunks sent. Wait for an ACK; retransmit if none arrives.
        if (!chunks.isEmpty()) {
            const QString tid = chunks.first().value(QStringLiteral("tid")).toString();
            if (!tid.isEmpty() && !m_outgoingTransfers.contains(tid)) {
                auto *timer = new QTimer(this);
                timer->setSingleShot(true);
                connect(timer, &QTimer::timeout, this, [this, tid]() {
                    auto it = m_outgoingTransfers.find(tid);
                    if (it == m_outgoingTransfers.end())
                        return;
                    // Retransmit: send all chunks again.
                    const auto &chunks = it->chunks;
                    for (const QCborMap &chunk : chunks)
                        sendUdp(chunk, it->toIp);
                    // Restart the timer for another attempt.
                    if (it->timer)
                        it->timer->start(kFileRetransmitMs);
                });
                m_outgoingTransfers.insert(tid, OutgoingTransfer(chunks, toIp, timer));
                timer->start(kFileRetransmitMs);
            }
        }
        return;
    }

    const int end = qMin(idx + batch, chunks.size());
    for (int i = idx; i < end; ++i)
        sendUdp(chunks[i], toIp);

    // Pace below the receiver's rate limit: one packet per 6 ms is ~166/s,
    // well under the 200/s cap.
    QTimer::singleShot(6 * batch, this, [this, chunks, toIp, end, batch] {
        sendChunksQueued(chunks, toIp, end, batch);
    });
}

bool NetworkManager::connectVoice(const QString &ip)
{
    if (m_voiceConnections.contains(ip))
        return true;
    if (m_pendingVoice.contains(ip))
        return true; // an attempt is already in flight for this peer

    auto *sock = new QTcpSocket(this);
    m_pendingVoice[ip] = sock;

    connect(sock, &QTcpSocket::connected, this, [this, sock, ip] {
        if (m_pendingVoice.value(ip) != sock)
            return; // superseded, or the call was already torn down
        m_pendingVoice.remove(ip);
        replaceVoiceSocket(ip, sock);
        connect(sock, &QTcpSocket::readyRead, this, [this, sock, ip] {
            onVoiceData(sock, ip);
        });
        connect(sock, &QTcpSocket::disconnected, this, [this, sock, ip] {
            onVoiceDisconnected(sock, ip);
        });
        Q_EMIT voiceConnected(ip);
    });
    connect(sock, &QTcpSocket::errorOccurred, this, [this, sock, ip](QAbstractSocket::SocketError) {
        if (m_pendingVoice.value(ip) != sock)
            return; // already connected, or replaced by a newer attempt
        m_pendingVoice.remove(ip);
        Q_EMIT errorOccurred(i18nc("@info:status %1 is a host address, %2 a socket error message", "Voice connect to %1 failed: %2", ip, sock->errorString()));
        Q_EMIT voiceDisconnected(ip);
        sock->deleteLater();
    });

    // No waitForConnected: it blocked the GUI while the ringtone played.
    sock->connectToHost(QHostAddress(ip), m_voiceTcpPort);

    return true;
}

bool NetworkManager::sendVoice(const QString &ip, const QByteArray &data)
{
    auto *sock = m_voiceConnections.value(ip, nullptr);
    if (!sock || sock->state() != QTcpSocket::ConnectedState)
        return false;
    // Same cap the receiver enforces.
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

// LAN chat id is a datagram destination, not a room.
chatid::Transport NetworkManager::transport() const
{
    return chatid::Transport::Lan;
}

bool NetworkManager::canHandle(const QString &chatId) const
{
    // Everything without a foreign prefix is ours.
    return chatid::transportOf(chatId) == chatid::Transport::Lan;
}

bool NetworkManager::serverOwnsTimeline(const QString &) const
{
    return false;
}

bool NetworkManager::hasRooms(const QString &) const
{
    return false;
}

bool NetworkManager::supportsCalls(const QString &) const
{
    return true;
}

bool NetworkManager::supportsTyping(const QString &) const
{
    return true;
}

bool NetworkManager::supportsEdits(const QString &) const
{
    return true;
}

bool NetworkManager::supportsReactions(const QString &) const
{
    return true;
}

bool NetworkManager::sendText(const QString &chatId, const QString &text)
{
    if (chatId.isEmpty())
        return false;
    sendPrivate(text, chatId);
    return true;
}

bool NetworkManager::sendFile(const QString &chatId, const QString &localFilePath)
{
    if (chatId.isEmpty())
        return false;
    return sendFileInternal(chatId, localFilePath, QByteArray(), QStringLiteral("file"));
}

void NetworkManager::markRead(const QString &chatId)
{
    // The chat id of a LAN chat is the peer address.
    sendReadReceipt(chatId, QStringLiteral("dm"));
}

void NetworkManager::sendTyping(const QString &chatId)
{
    NetworkManager::sendTyping(chatId, chatId);
}

bool NetworkManager::leaveChat(const QString &)
{
    return false; // there is nothing to leave: a LAN chat is an address
}

bool NetworkManager::sendEdit(const QString &chatId, const QVariant &identifier, const QString &newText)
{
    // A LAN chat's identifier is its address, so both ends are the same string.
    sendMessageEdit(chatId, chatId, identifier, newText);
    return true;
}

bool NetworkManager::sendDelete(const QString &chatId, const QVariant &identifier)
{
    sendMessageDelete(chatId, chatId, identifier);
    return true;
}

QVariantMap NetworkManager::roomInfo(const QString &) const
{
    return {};
}

QVariantList NetworkManager::roomMembers(const QString &) const
{
    return {};
}

QVariantMap NetworkManager::memberInfo(const QString &, const QString &) const
{
    return {};
}

} // namespace koutnet
