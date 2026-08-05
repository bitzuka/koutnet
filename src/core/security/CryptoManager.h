// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet security engine.
//
// X25519 ECDH exchange with an Ed25519-signed identity, AES-256-GCM on
// messages, a PBKDF2-SHA256 passphrase overlay for groups, HMAC-SHA256 on
// control packets, a replay window over nonce and timestamp, and per-address
// rate limiting.
//
// A peer is an Ed25519 identity key, never an address. Sessions, the trust-on-
// first-use pin and the replay state all hang off that key, because a host has
// as many addresses as it has interfaces - bring a VPN up and a message signed
// with the session we established over the LAN arrives from the tunnel instead.
// Addresses are kept as an index into that state and nothing more.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

typedef struct evp_pkey_st EVP_PKEY;

namespace koutnet
{

enum class SecurityLevel {
    Plain, // no encryption at all
    Psk, // pre-shared passphrase (PBKDF2 + AES-GCM)
    E2E, // ECDH session key established (AES-GCM)
};

class CryptoManager : public QObject
{
    Q_OBJECT

public:
    static constexpr int kNonceLen = 12;
    static constexpr int kTagLen = 16;
    static constexpr int kKeyLen = 32;
    static constexpr int kKdfIters = 480000;
    static constexpr int kSaltLen = 32;
    static constexpr double kReplayWindowSec = 30.0;
    static constexpr double kNonceCacheTtlSec = 60.0;
    // Cap on cached PBKDF2 passphrase keys - without this, cycling through
    // many different group passphrases over a long session grows this hash
    // unboundedly.
    static constexpr int kMaxPassphraseCacheSize = 256;
    // Caps on the replay cache, in the same spirit. A peer sending a fresh
    // nonce faster than the TTL sweep retires them grew one bucket without
    // limit, and a flood from spoofed source addresses grew the number of
    // buckets without limit - neither needs anything but a UDP socket. Oldest
    // goes first, so a real peer's recent nonces are the ones that survive.
    // Worst case is roughly kMaxNoncePeers * kMaxNoncesPerPeer entries.
    static constexpr int kMaxNoncesPerPeer = 4096;
    static constexpr int kMaxNoncePeers = 256;
    // Same again for the rate-limit windows, which are keyed on source address
    // and would otherwise be the way around the cap above.
    static constexpr int kMaxRatePeers = 1024;
    // How many addresses one identity is remembered at. A multi-homed host has
    // a LAN address, a VPN address and on a bad day a second NIC; past that it
    // is a peer walking the index rather than a peer with interfaces, so the
    // oldest entry goes.
    static constexpr int kMaxPeerAddresses = 8;

    explicit CryptoManager(QObject *parent = nullptr);
    // Same thing with its identity kept under a suffix of its own, in the
    // wallet and in the config file alike. The application never passes a
    // scope; it is what lets a test hold two peers in one process without
    // the second one loading the first one's keypair.
    explicit CryptoManager(const QString &storageScope, QObject *parent = nullptr);
    ~CryptoManager() override;

    // False when keypair generation or loading failed at startup. Check it
    // before offering any secure feature: nothing below can establish a
    // session without keys, so every call will simply refuse.
    bool isValid() const
    {
        return m_valid;
    }

    // Peer identity
    // Every peerRef below is either an identity id - what identityIdFor()
    // returns, and what all the state here is keyed on - or an address, which
    // is looked up in the address index and may simply not be there. An
    // address resolves to whatever identity was last seen using it and nothing
    // more: it names a peer, it never authenticates one.
    static QString identityIdFor(const QByteArray &idPubRaw);
    QString ownIdentityId() const;
    // Empty when this address has never carried a verified handshake. Callers
    // treat that as "no idea who this is", not as "not to be trusted".
    QString identityForAddress(const QString &address) const;
    // Newest first, capped at kMaxPeerAddresses. Only addresses datagrams have
    // actually arrived from are in here; what a peer advertises about itself is
    // not evidence of anything and stays out.
    QStringList addressesFor(const QString &peerId) const;

    // Handshake
    // Refused: malformed, or the Ed25519 signature over dh_pub did not check
    // out, so nothing was learned. AddressTaken: the identity is sound but
    // another identity we still hold a session with is using that address, see
    // peerIdentityChanged. Established: session derived or refreshed.
    enum class HandshakeOutcome {
        Refused,
        AddressTaken,
        Established,
    };
    QJsonObject handshakePayload() const;
    // outPeerId is filled in whenever the payload proved its own identity, which
    // includes the AddressTaken case - the caller needs to know who showed up.
    HandshakeOutcome processHandshakeFrom(const QString &observedAddress, const QJsonObject &data, QString *outPeerId = nullptr);
    bool processHandshake(const QString &observedAddress, const QJsonObject &data);
    bool hasSession(const QString &peerRef) const;

    QString fingerprint() const;
    QString peerFingerprint(const QString &peerRef) const;
    SecurityLevel securityLevel(const QString &peerRef, bool encryptionEnabled, bool hasPassphrase) const;

    // Packet HMAC
    QString signPacket(const QString &peerRef, const QByteArray &payload) const;
    bool verifyPacket(const QString &peerRef, const QByteArray &payload, const QString &sigB64) const;

    // Replay / rate limiting
    // The replay bucket belongs to the identity, so a captured packet is no
    // fresher for being resent from somewhere else. An unresolvable peerRef
    // gets a bucket of its own under that name, which is what unauthenticated
    // presence from a stranger gets.
    bool checkReplay(const QString &peerRef, const QString &nonceHex, double ts);
    // Deliberately still keyed on the source address: this one runs before
    // anything is known about who sent the packet, which is the point of it.
    bool checkRate(const QString &sourceAddress, int maxPerSec = 200);

    // Message encryption (text, base64-wrapped wire format)
    QString encrypt(const QString &plaintext, const QString &passphrase = QString(), const QString &peerRef = QString()) const;
    QString decrypt(const QString &ciphertext, const QString &passphrase = QString(), const QString &peerRef = QString()) const;

    // Raw byte encryption (voice frames - no base64/JSON overhead)
    // Both refuse to work without a session: encryptBytes returns an empty
    // array and decryptBytes returns false, and the caller drops the frame.
    QByteArray encryptBytes(const QString &peerRef, const QByteArray &plaintext) const;
    bool decryptBytes(const QString &peerRef, const QByteArray &data, QByteArray *outPlain) const;

Q_SIGNALS:
    // Someone new turned up at an address a peer we still hold a session with
    // is using. Not a broken pin any more - the pin is on the identity key, and
    // that one cannot be argued with - but the peer's slot in the contact list
    // is the visible half of a takeover, so the handshake is refused and the
    // existing session left alone. Either an impostor, or the peer reinstalled
    // and lost its keys. The UI should show both fingerprints and let the user
    // decide (clearing the pin is not implemented yet).
    void peerIdentityChanged(const QString &address, const QString &oldFingerprint, const QString &newFingerprint);

    // KWallet has the private keys, but the plaintext copy an older build left
    // in the config file could not be deleted, so it is still readable on disk.
    // The log is not enough for that: only the user can repair the file.
    void plaintextKeysLeftInConfig(const QString &reason);

private:
    bool initKeypairs();
    bool loadStoredKeys();
    bool generateAndStoreKeys();
    // Lifts keys written by an older build out of plaintext QSettings and into
    // the wallet. Returns the base64 it found either way, so an identity is
    // never thrown away just because the wallet is unreachable.
    bool migrateLegacyKeys(QString *outIdentityB64, QString *outDhB64);
    // Deletes the plaintext copy and confirms it is gone. Safe to call on every
    // start; it is a no-op once the config file is clean.
    void dropLegacyPlaintextKeys();
    // Moves a config-file key that is not the one in use into the wallet, so
    // the deletion above cannot be the end of an identity. False means the
    // plaintext has to stay for now.
    bool stashSupersededPlaintextKeys();
    void reportPlaintextKeysLeft(const QString &reason);

    static QByteArray gcmEncrypt(const QByteArray &key, const QByteArray &plaintext);
    static bool gcmDecrypt(const QByteArray &key, const QByteArray &data, QByteArray *outPlain);
    QByteArray deriveKey(const QString &passphrase, const QByteArray &salt) const;
    static QByteArray hkdfSha256(const QByteArray &secret, const QByteArray &info, int outLen);
    static QByteArray randomBytes(int n);
    // Drops the least recently used peer buckets until the replay cache is back
    // inside kMaxNoncePeers. Called only when it is over.
    void evictOldestNoncePeers();
    // An identity id passes through; anything else is looked up in the address
    // index. Empty means nobody here knows this peer.
    QString resolveIdentity(const QString &peerRef) const;
    // Files an address under an identity. Called from the handshake only, so
    // what lands here is an address a signed payload actually arrived from.
    void noteObservedAddress(const QString &peerId, const QString &address);

    const QString m_storageScope;
    bool m_valid = false;

    EVP_PKEY *m_identityPriv = nullptr; // Ed25519
    EVP_PKEY *m_dhPriv = nullptr; // X25519
    QByteArray m_dhPubBytes;
    QByteArray m_identityPubBytes;
    QByteArray m_dhPubSig;

    // One remembered nonce. The timestamp answers "is this old enough to
    // forget", the sequence number answers "which of these arrived first" -
    // and a flood fits thousands of nonces into one millisecond, so the clock
    // cannot answer the second question.
    struct SeenNonce {
        double ts = 0.0;
        quint64 seq = 0;
    };

    QHash<QString, QByteArray> m_sessionKeys; // identity id -> 32-byte session key
    QHash<QString, QByteArray> m_peerIdPub; // identity id -> raw Ed25519 pubkey
    // The index, and the only address-keyed trust-adjacent state left. It is a
    // hint for resolving an incoming datagram, so a wrong entry costs a failed
    // HMAC check and nothing else.
    QHash<QString, QString> m_addressToId; // address -> identity id
    QHash<QString, QStringList> m_idToAddresses; // identity id -> addresses, newest first
    QHash<QString, QByteArray> m_warnedIdPub; // address -> key we last warned about
    QHash<QString, QHash<QString, SeenNonce>> m_seenNonces; // identity id -> nonce -> when
    QHash<QString, quint64> m_nonceBucketTouched; // identity id -> its newest seq
    quint64 m_nonceSeq = 0; // arrivals, ever
    QHash<QString, QVector<double>> m_rateCounters; // source address -> recent timestamps

    // sha256(salt + passphrase) -> key. Keyed on a digest rather than on the
    // passphrase itself, which used to keep the plaintext alive here for as
    // long as the process ran. See deriveKey().
    mutable QHash<QByteArray, QByteArray> m_passphraseKeyCache;
};

} // namespace koutnet
