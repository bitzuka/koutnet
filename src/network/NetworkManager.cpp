// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Network & Audio core
#include "NetworkManager.h"
#include "../core/security/CryptoManager.h"
#include "../core/security/SecretStore.h"
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
    // Deliberately not re-read from AppSettings here: main.cpp passes the
    // value once at start and on change.
    m_staticPeers = ips;
}

void NetworkManager::setProfile(const QString &handle, const QString &displayName, const QString &bio, const QString &revision)
{
    // Presence goes out on a short timer, so anything in here is paid for repeatedly;
    // a long bio gets cut rather than pushing the packet towards fragmentation.
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

QString NetworkManager::groupKeyWalletKey(const QString &gid)
{
    return QStringLiteral("group_key/") + gid;
}

QString NetworkManager::groupKeyFor(const QString &gid)
{
    const auto it = m_groupKeys.constFind(gid);
    if (it != m_groupKeys.constEnd())
        return it.value();

    QString key;
    if (SecretStore::read(groupKeyWalletKey(gid), &key) && !key.isEmpty()) {
        m_groupKeys.insert(gid, key);
        return key;
    }
    return QString();
}

void NetworkManager::setGroupKey(const QString &gid, const QString &key)
{
    if (key.isEmpty()) {
        m_groupKeys.remove(gid);
        SecretStore::remove(groupKeyWalletKey(gid));
        return;
    }
    m_groupKeys.insert(gid, key);
    SecretStore::write(groupKeyWalletKey(gid), key);
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
    if (!SecretStore::write(groupKeyWalletKey(gid), key))
        qCWarning(KOUTNET_LOG_NETWORK) << "group key for" << gid << "not persisted - KWallet unavailable, this session only";
    return key;
}

void NetworkManager::setConnectionMode(ConnectionMode mode)
{
    // Nothing to tear up or down here since the relay tunnel was cut: the mode
    // only tells QML which transport to route through.
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
    m_voiceTcpPort = tcpPort;

    m_udp = new QUdpSocket(this);
    bool bound = m_udp->bind(QHostAddress::AnyIPv4, udpPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    if (!bound) {
        bound = m_udp->bind(QHostAddress::AnyIPv4, udpPort + 1, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    }
    if (!bound) {
        // int(): KLocalizedString has no quint16 substitution overload.
        Q_EMIT errorOccurred(i18nc("@info:status %1 is a port number", "UDP bind failed on port %1.", int(udpPort)));
        return false;
    }
    connect(m_udp, &QUdpSocket::readyRead, this, &NetworkManager::onUdpReadyRead);

    m_udp->joinMulticastGroup(QHostAddress(QStringLiteral("224.0.0.251")));

    m_tcpServer = new QTcpServer(this);
    if (!m_tcpServer->listen(QHostAddress::AnyIPv4, tcpPort)) {
        if (!m_tcpServer->listen(QHostAddress::AnyIPv4, 0)) {
            Q_EMIT errorOccurred(i18nc("@info:status %1 is a port number", "TCP listen failed on port %1.", int(tcpPort)));
            return false;
        }
    }
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
    // Linux: /proc/net/arp, the only parser written so far. Windows and
    // macOS: TODO, the same via a process-based `arp -a` parse.
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
    const QByteArray data = protocol::encodeFrame(payload);
    const quint16 port = protocol::kUdpPortDefault;

    int sent = 0;
    for (const auto &ip : std::as_const(ips)) {
        if (ip != m_hostIp && !ip.startsWith(QLatin1String("169.254"))) {
            m_udp->writeDatagram(data, QHostAddress(ip), port);
            ++sent;
        }
    }
    if (sent > 0)
        qCDebug(KOUTNET_LOG_NETWORK) << "ARP scan: pinged" << sent << "neighbour(s)";

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
    const quint16 port = protocol::kUdpPortDefault;

    m_udp->writeDatagram(data, QHostAddress(QStringLiteral("224.0.0.251")), port);

    QSet<QString> sentBroadcasts;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto &iface : interfaces) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || (flags & QNetworkInterface::IsLoopBack))
            continue;
        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (const auto &entry : std::as_const(entries)) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            const QString bcast = entry.broadcast().toString();
            if (!bcast.isEmpty() && bcast != QLatin1String("0.0.0.0") && !sentBroadcasts.contains(bcast)) {
                m_udp->writeDatagram(data, entry.broadcast(), port);
                sentBroadcasts.insert(bcast);
            }
        }
    }

    // /24 sweep: the noisy part. Only while no peer is known, with an exponential
    // backoff plus +/-15% jitter so an empty network settles down instead of being
    // scanned every 30-120s forever. The moment a peer answers we stop sweeping
    // entirely (the beacon above keeps us visible) and reset the backoff, so the
    // next empty period starts loud again for a quick first find.
    const double now = nowEpoch();
    if (m_peers.isEmpty()) {
        const double jitter = 0.85 + 0.30 * QRandomGenerator::global()->generateDouble();
        const double due = (m_sweepIntervalMs * jitter) / 1000.0;
        if (now - m_lastScan > due) {
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
            m_sweepIntervalMs = qMin(m_sweepIntervalMs * 2.0, double(kSweepMaxMs));
        }
    } else {
        m_sweepIntervalMs = double(kSweepMinMs);
        m_lastScan = now;
    }

    // 5. Static peers: a unicast presence packet each cycle, but only to
    //    addresses that have not answered yet. A peer that answers keeps
    //    itself alive from then on; a silent one is not reminded every cycle.
    const QStringList targets = staticUnicastTargets(m_staticPeers, m_peers, m_hostIp);
    for (const QString &target : std::as_const(targets))
        m_udp->writeDatagram(data, QHostAddress(target), port);

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
        QString host = dg.senderAddress().toString();
        if (host.startsWith(QLatin1String("::ffff:")))
            host = host.mid(7);

        handleDatagram(host, dg.data());
    }
}

void NetworkManager::handleDatagram(const QString &host, const QByteArray &data)
{
    // Attacker-supplied bytes: decode the binary envelope defensively. Decode to
    // the raw CBOR map - that is the exact payload the signer canonicalised, so
    // byte-string fields (file_data "data") survive instead of being forced
    // through JSON and base64.
    QString type;
    QCborMap map;
    if (!protocol::decodeFrame(data, type, map)) {
        Q_EMIT errorOccurred(i18nc("@info:status %1 is a host address", "Unreadable packet from %1 - dropping it.", host));
        return;
    }

    // Layer 4 - HMAC verification, now on the CBOR map so the canonical bytes the
    // signer produced match what we verify against (a JSON round-trip would mangle
    // binary). Presence is exempt: it is broadcast and never carries a _sig.
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

    // Layer 4 HMAC verification and the Layer 5 replay guard now live in
    // handleDatagram(), where they run on the raw CBOR map so the canonical bytes
    // match the signer's. By the time we get here the packet is authenticated and
    // its from_id/from_ip are already resolved; pull the identity back for decrypt.
    const QString peerId = msg.value(QStringLiteral("from_id")).toString();

    if (type == protocol::kMsgPresence) {
        handlePresence(host, msg);
    } else if (type == protocol::kMsgChat || type == protocol::kMsgGroup || type == protocol::kMsgReaction || type == protocol::kMsgEdit
               || type == protocol::kMsgDelete || type == protocol::kMsgRead) {
        decryptMessageText(peerId, msg, [this](QJsonObject decrypted) {
            Q_EMIT message(decrypted);
        });
    } else if (type == protocol::kMsgPrivate) {
        // Against every address of ours, not just the primary one: comparing with
        // m_hostIp alone dropped this without a word on any multi-homed machine.
        if (myIps.contains(msg.value(QStringLiteral("to")).toString())) {
            decryptMessageText(peerId, msg, [this](QJsonObject decrypted) {
                Q_EMIT message(decrypted);
            });
        }
    } else if (type == protocol::kMsgCallReq) {
        if (!m_activeCalls.isEmpty()) {
            // already on a call, and the busy reply is cheaper for the caller to
            // hear than a ringing window that answers nothing
            sendCallBusy(host);
        } else {
            m_ringingCalls.insert(host);
            Q_EMIT callRequest(msg.value(QStringLiteral("username")).toString(QStringLiteral("?")), host);
        }
    } else if (type == protocol::kMsgCallAccept) {
        // The whole point of the state machine: a call_accept from a peer we
        // never called is a session-holder trying to open our microphone. The
        // accept is only real when it answers a call_req of ours.
        if (!m_pendingCalls.remove(host))
            return;
        Q_EMIT callAccepted(msg.value(QStringLiteral("username")).toString(QStringLiteral("?")), host);
    } else if (type == protocol::kMsgCallBusy || type == protocol::kMsgCallReject) {
        if (!m_pendingCalls.remove(host))
            return;
        Q_EMIT callRejected(host);
    } else if (type == protocol::kMsgCallEnd) {
        // call_end carries two meanings - "your ring was cancelled" to a caller
        // still waiting and "the call is over" to a peer we are talking to. A
        // call_end for a call in neither state is stale or forged and cancels
        // nothing.
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

    // A group message is sealed under its own key; anything without a gid -
    // private messages sent before the handshake, old group traffic - falls
    // back to the shared app-wide passphrase, and to cleartext when none
    // exists. A group whose key we do not hold lands on the same fallback:
    // the sender no longer uses it, so the text simply fails to open.
    QString passphrase = m_groupPassphrase;
    const QVariant gid = msg.value(QStringLiteral("gid"));
    if (!gid.isNull()) {
        const QString key = groupKeyFor(gid.toString());
        if (!key.isEmpty())
            passphrase = key;
    }

    // The sender "encrypted" flag used to decide this, so clearing it was enough to
    // have the text handed to the UI unchecked. What matters is whether we hold a key
    // for this channel; decrypt() refuses cleartext once we do.
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
    // Only ever a delivery candidate: a host at one address can advertise any other,
    // and trusting that is how it becomes a lie the interface repeats.
    const QString advertised = msg.value(QStringLiteral("ip")).toString();
    const QSet<QString> myIps = m_localIps.isEmpty() ? QSet<QString>{m_hostIp} : m_localIps;
    if (myIps.contains(advertised) || myIps.contains(host))
        return;
    if (host.isEmpty() || host == QLatin1String("0.0.0.0"))
        return; // nowhere to file it and nowhere to answer

    QString peerId;
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

    // One entry per identity, filed under the address first heard: two contacts for one
    // person is worse than a stale address, and deliveryAddresses() keeps the rest.
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
        // A flood of spoofed presences must not grow the peer table without bound:
        // the oldest entry makes room for the newcomer, like the replay buckets do.
        if (m_peers.size() >= kMaxPeers)
            evictOldestPeer();
        m_peers[key] = msg;
    } else {
        m_peers[key] = msg;
    }
    if (!peerId.isEmpty())
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
        QString ip = sock->peerAddress().toString();
        if (ip.startsWith(QLatin1String("::ffff:")))
            ip = ip.mid(7);

        // A reconnecting peer arrives while the last socket is still in the map, and
        // assigning over it stranded that socket, still reporting the call as ended.
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
    // Reassemble whole frames first. The old code called whatever had arrived a frame,
    // which is why encrypted voice never worked: the GCM tag was mid-slice.
    QByteArray buf = m_voiceRxBuffers.value(sock) + sock->readAll();
    QVector<QByteArray> frames;

    while (buf.size() >= protocol::kFrameHeaderBytes) {
        const quint32 len = readLengthPrefix(buf);
        if (len == 0 || len > protocol::kMaxVoiceFrameBytes) {
            // a peer-supplied length, so a silly one asks us to allocate a gigabyte
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
    // the call, and that erases this socket's buffer underneath us.
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

void NetworkManager::sendUdpToAll(QJsonObject payload, const QVector<QString> &targets)
{
    if (!m_udp)
        return;

    const QString msgType = payload.value(QStringLiteral("type")).toString();
    if (msgType != protocol::kMsgPresence) {
        payload[QStringLiteral("nonce")] = randomHex(8);
        payload[QStringLiteral("ts")] = nowEpoch();

        // Who this is from, so the far side finds the session even when the route picks
        // an unfamiliar interface. Before the signature, so it is only a claim.
        if (m_crypto)
            payload[QStringLiteral("from_id")] = m_crypto->ownIdentityId();

        // Layer 4 - HMAC-sign unicast packets once a session key exists with the
        // target; broadcasts have no single peer session to sign for. One signature for
        // the whole set, since these addresses are one peer holding one session key.
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
}

void NetworkManager::sendUdp(QCborMap payload, const QString &targetIp)
{
    sendUdpToAll(std::move(payload), targetIp.isEmpty() ? QVector<QString>{} : QVector<QString>{targetIp});
}

void NetworkManager::sendUdpToAll(QCborMap payload, const QVector<QString> &targets)
{
    if (!m_udp)
        return;

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
}

QVector<QString> NetworkManager::deliveryAddresses(const QString &toIp) const
{
    QVector<QString> targets;
    if (!toIp.isEmpty())
        targets.append(toIp);

    // Addresses a signed packet has actually arrived from come first: one of
    // those is worth more than anything the peer claims about itself.
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

    // Then the ones it advertised, which are guesses - and the reason for the cap,
    // since the list arrives at whatever length the sender felt like sending.
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

void NetworkManager::sendPrivate(const QString &text, const QString &toIp)
{
    QString outText = text;
    bool encrypted = false;

    if (m_crypto) {
        // A session is the better key when there is one. The group passphrase still
        // protects a message sent before the handshake with this peer completed.
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

    // One signed datagram copied to every address this peer answers on. It used to be
    // a sendUdp() per address, and since the signature was made for the address it was
    // aimed at, every copy but the first went out unsigned and was dropped.
    sendUdpToAll(payload, deliveryAddresses(toIp));
}

void NetworkManager::sendGroupMessage(const QString &gid, const QString &text, const QVector<QString> &members)
{
    // One key per group. The group's first message creates it; without a wallet
    // the generated key still protects this session, it just does not survive a
    // restart. The old shared passphrase is not used for a group that has a key
    // of its own, which is the point of this state existing.
    QString passphrase = groupKeyFor(gid);
    if (passphrase.isEmpty())
        passphrase = ensureGroupKey(gid);
    if (!m_crypto || passphrase.isEmpty()) {
        Q_EMIT errorOccurred(i18nc("@info:status", "Failed to create a group key - refusing to send in the clear."));
        return;
    }

    const QString cipherText = m_crypto->encrypt(text, passphrase, QString());
    if (cipherText == text) {
        // encrypt() hands back the plaintext when it fails, and sending that
        // would leak the message the user thinks is protected
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
    // that was actually sent - this is what the dispatch() gate looks at.
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
    // The remote's end of the ring no longer needs an answer, and a stale
    // accept must not be able to complete a call we have given up on.
    m_pendingCalls.remove(toIp);
    m_ringingCalls.remove(toIp);
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgCallEnd;
    sendUdp(payload, toIp);
}

void NetworkManager::sendReaction(const QString &chatId, double ts, const QString &emoji, bool added)
{
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgReaction;
    payload[QStringLiteral("from_ip")] = m_hostIp;
    payload[QStringLiteral("chat_id")] = chatId;
    payload[QStringLiteral("msg_ts")] = ts;
    payload[QStringLiteral("emoji")] = emoji;
    payload[QStringLiteral("added")] = added;
    sendUdp(payload, chatId);
}

void NetworkManager::sendMessageEdit(const QString &toIp, const QString &chatId, double ts, const QString &newText)
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
    // Every address the peer answers on, as sendPrivate() does: a lost receipt leaves
    // the sender message showing as unconfirmed forever.
    sendUdpToAll(payload, deliveryAddresses(toIp));
}

void NetworkManager::sendGroupInvite(const QString &gid, const QString &gname, const QString &toIp)
{
    QJsonObject payload;
    payload[QStringLiteral("type")] = protocol::kMsgGroupInv;
    payload[QStringLiteral("gid")] = gid;
    payload[QStringLiteral("gname")] = gname;
    // The group key rides along only under a session with the invitee: out in
    // the open it would hand the group to every sniffer on the LAN, and an
    // invitee without one joins blind, catching up when a key arrives some
    // safer way. sendGroupMessage() is happy to create a key on this side, so
    // this does not even have to be the group's first message.
    const QString key = groupKeyFor(gid);
    if (!key.isEmpty() && m_crypto && m_crypto->hasSession(toIp))
        payload[QStringLiteral("key")] = m_crypto->encrypt(key, QString(), toIp);
    sendUdp(payload, toIp);
}

void NetworkManager::sendFileInternal(const QString &toIp, const QString &filePath, const QByteArray &rawBytes, const QString &filename)
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
            return;
        }
        data = file.readAll();
        fname = QFileInfo(filePath).fileName();
        ext = QFileInfo(filePath).suffix().toLower();
    }

    // Sealed whole before chunking: one nonce and tag for the transfer,
    // and sizes count the ciphertext. No session for the peer means
    // plaintext and no flag, as before.
    bool encrypted = false;
    if (m_crypto) {
        const QByteArray sealed = m_crypto->encryptFileBytes(toIp, data);
        if (!sealed.isEmpty()) {
            data = sealed;
            encrypted = true;
        }
    }

    // The far side refuses anything past this cap, so sending it would only
    // burn the network: refuse up front instead of reading a multi-gigabyte
    // file into memory and chunking it into nothing.
    if (data.size() > FileTransferHandler::kMaxTransferBytes) {
        Q_EMIT errorOccurred(i18nc("@info:status %1 is a file path", "Refused to send %1: larger than the transfer limit.", filePath));
        return;
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
    meta[QStringLiteral("encrypted")] = encrypted;
    meta[QStringLiteral("from_ip")] = m_hostIp;
    meta[QStringLiteral("to")] = toIp.isEmpty() ? QStringLiteral("public") : toIp;
    meta[QStringLiteral("ts")] = nowEpoch();
    sendUdp(meta, toIp);

    // Raw byte string on the wire (no base64), so a chunk can approach the UDP
    // datagram ceiling; the envelope adds only ~12 bytes of header and fields.
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
}

void NetworkManager::sendChunksQueued(const QVector<QCborMap> &chunks, const QString &toIp, int idx, int batch)
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

    // No waitForConnected: it blocked the GUI thread for up to three seconds while the
    // callee ringtone was supposed to be playing.
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

// The LAN/VPN half of the ChatBackend interface: what the window can ask of
// any chat, answered for a chat id that is a datagram destination. The three
// room-shaped queries are always empty - a LAN chat is a peer, not a room -
// and the capability flags are the other way round: LAN has calls, typing,
// edits and no server echo, so Main.qml renders a local row before sending.
chatid::Transport NetworkManager::transport() const
{
    return chatid::Transport::Lan;
}

bool NetworkManager::canHandle(const QString &chatId) const
{
    // Everything without a foreign prefix is ours: a bare address is all a
    // datagram can be sent to, and the reserved chats ("__self__") are
    // guarded by the window before anything is asked of them.
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
    sendFileInternal(chatId, localFilePath, QByteArray(), QStringLiteral("file"));
    return true;
}

void NetworkManager::markRead(const QString &chatId)
{
    // The chat id of a LAN chat is the peer address, so the receipt goes back
    // to the same place the message came from.
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

bool NetworkManager::sendEdit(const QString &chatId, double ts, const QString &newText)
{
    // A LAN chat's identifier is its address, so both ends of the edit packet
    // are the same string the message was filed under.
    sendMessageEdit(chatId, chatId, ts, newText);
    return true;
}

bool NetworkManager::sendDelete(const QString &chatId, double ts)
{
    sendMessageDelete(chatId, chatId, ts);
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
