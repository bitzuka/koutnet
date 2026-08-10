// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet security engine, built on libsodium: crypto_kx key agreement with an
// Ed25519-signed identity, XChaCha20-Poly1305 on messages and voice frames,
// Argon2id for group passphrases, crypto_auth on control packets, a replay
// window over nonce and timestamp, per-address rate limiting.
//
// A peer is an Ed25519 identity key, never an address: a host has as many
// addresses as interfaces, so a message from the session established over the
// LAN arrives from the VPN tunnel the moment one comes up. Addresses are an
// index into that state and nothing more.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVector>
#include <functional>
#include <memory>

namespace koutnet
{

enum class SecurityLevel {
    Plain, // no encryption at all
    Psk, // pre-shared passphrase (Argon2id + XChaCha20-Poly1305)
    E2E, // crypto_kx session keys established (XChaCha20-Poly1305)
};

class DeriveWorker; // defined in the .cpp, moved to m_deriveThread

class CryptoManager : public QObject
{
    Q_OBJECT

public:
    // XChaCha20's 192-bit nonce. Wide enough that a nonce drawn at random is
    // safe for the life of a key, which is what lets every frame carry its own
    // rather than a counter that has to survive restarts and reordering.
    static constexpr int kNonceLen = 24;
    static constexpr int kTagLen = 16;
    static constexpr int kKeyLen = 32;
    // Timestamp riding next to a voice frame's nonce, in big-endian ms, so the
    // receiver's replay window can judge the frame like any other packet.
    static constexpr int kVoiceTsLen = 8;
    // crypto_pwhash_SALTBYTES; static_asserted against libsodium in the .cpp.
    static constexpr int kSaltLen = 16;
    static constexpr double kReplayWindowSec = 30.0;
    static constexpr double kNonceCacheTtlSec = 60.0;
    // Cap on cached Argon2id passphrase keys - cycling many group passphrases
    // over a long session grew this hash unboundedly.
    static constexpr int kMaxPassphraseCacheSize = 256;
    // Same for the replay cache: a peer sending fresh nonces faster than the
    // TTL sweep grew one bucket without limit, and a flood from spoofed source
    // addresses grew the number of buckets. Oldest goes first, so a real peer's
    // recent nonces survive.
    static constexpr int kMaxNoncesPerPeer = 4096;
    static constexpr int kMaxNoncePeers = 256;
    // Same again for the rate-limit windows, which are keyed on source address
    // and would otherwise be the way around the cap above.
    static constexpr int kMaxRatePeers = 1024;
    // Ceiling on the async decrypt queue, and on the ciphertext body a queued
    // message may carry. The derivation that heads the queue costs 64 MiB and
    // two passes, so a flood must cost memory slots, not memory itself - see
    // decryptAsync().
    static constexpr int kMaxPendingDecrypts = 8;
    static constexpr int kMaxAsyncPayloadBytes = 1024 * 1024;
    // How many addresses one identity is remembered at. Past a LAN address, a
    // VPN address and a second NIC it is a peer walking the index, so the
    // oldest entry goes.
    static constexpr int kMaxPeerAddresses = 8;

    explicit CryptoManager(QObject *parent = nullptr);
    // Identity kept under a suffix of its own, in the wallet and in the config
    // file alike. Only tests pass a scope; it is what lets two peers live in
    // one process without the second loading the first one's keypair.
    explicit CryptoManager(const QString &storageScope, QObject *parent = nullptr);
    ~CryptoManager() override;

    // False when keypair generation or loading failed at startup: nothing below
    // can establish a session without keys, so every call simply refuses.
    bool isValid() const
    {
        return m_valid;
    }

    // Every peerRef below is either an identity id from identityIdFor() - what
    // all the state here is keyed on - or an address, which resolves through the
    // index to whoever last used it: it names a peer, it never authenticates
    // one.
    static QString identityIdFor(const QByteArray &idPubRaw);
    QString ownIdentityId() const;
    // Empty when this address has never carried a verified handshake. Callers
    // treat that as "no idea who this is", not as "not to be trusted".
    Q_INVOKABLE QString identityForAddress(const QString &address) const;
    // Newest first, capped at kMaxPeerAddresses. Only addresses datagrams have
    // actually arrived from; what a peer advertises about itself is not
    // evidence.
    Q_INVOKABLE QStringList addressesFor(const QString &peerId) const;

    // Refused: malformed, or the Ed25519 signature over dh_pub did not check
    // out, so nothing was learned. AddressTaken: the identity is sound but
    // another identity we hold a session with owns that address, see
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
    Q_INVOKABLE bool hasSession(const QString &peerRef) const;

    QString fingerprint() const;
    Q_INVOKABLE QString peerFingerprint(const QString &peerRef) const;
    SecurityLevel securityLevel(const QString &peerRef, bool encryptionEnabled, bool hasPassphrase) const;

    QString signPacket(const QString &peerRef, const QByteArray &payload) const;
    bool verifyPacket(const QString &peerRef, const QByteArray &payload, const QString &sigB64) const;

    // The replay bucket belongs to the identity, so a captured packet is no
    // fresher for being resent from somewhere else. An unresolvable peerRef gets
    // a bucket of its own under that name - what presence from a stranger gets.
    bool checkReplay(const QString &peerRef, const QString &nonceHex, double ts) const;
    // Deliberately still keyed on the source address: this one runs before
    // anything is known about who sent the packet, which is the point of it.
    bool checkRate(const QString &sourceAddress, int maxPerSec = 200) const;

    QString encrypt(const QString &plaintext, const QString &passphrase = QString(), const QString &peerRef = QString()) const;
    QString decrypt(const QString &ciphertext, const QString &passphrase = QString(), const QString &peerRef = QString()) const;

    // Asynchronous variant of decrypt() for the receive path. The Argon2id
    // derivation a passphrase-sealed message with a fresh salt costs is paid by
    // the receiver - 64 MiB and two passes per unseen salt - so a peer who has
    // a session (TOFU hands one to anyone) could otherwise freeze the GUI with
    // a handful of packets per second. The derivation runs on a private worker
    // thread, the text arrives through done() in arrival order (a fast message
    // never overtakes a slow one in front of it), and the pending queue is
    // capped, so a flood drops messages instead of memory. delivered=false
    // means the queue gate refused the message: it must not be rendered at
    // all, unlike a decrypt error, which is one message and is worth showing.
    void decryptAsync(const QString &ciphertext, const QString &passphrase, const QString &peerRef, const std::function<void(const QString &plain, bool delivered)> &done);

    // Raw byte encryption (voice frames - no base64/JSON overhead)
    // Both refuse to work without a session: encryptBytes returns an empty
    // array and decryptBytes returns false, and the caller drops the frame.
    QByteArray encryptBytes(const QString &peerRef, const QByteArray &plaintext) const;
    bool decryptBytes(const QString &peerRef, const QByteArray &data, QByteArray *outPlain) const;

Q_SIGNALS:
    // Someone new turned up at an address a peer we still hold a session with
    // is using. The pin is on the identity key, so this is not a broken pin, but
    // the handshake is refused and the existing session left alone. Either an
    // impostor or a peer that reinstalled and lost its keys; the UI should show
    // both fingerprints and let the user decide.
    void peerIdentityChanged(const QString &address, const QString &oldFingerprint, const QString &newFingerprint);

    // The plaintext copy an older build left in the config file could not be
    // deleted and is still readable on disk; only the user can repair that.
    void plaintextKeysLeftInConfig(const QString &reason);

private:
    bool initKeypairs();
    bool loadStoredKeys();
    bool generateAndStoreKeys();
    bool migrateLegacyKeys(QString *outIdentityB64, QString *outDhB64);
    void dropLegacyPlaintextKeys();
    bool stashSupersededPlaintextKeys();
    void reportPlaintextKeysLeft(const QString &reason);

    // The decryptAsync() queue. A fast entry (session key, cache hit, error,
    // cleartext passthrough) carries its resolved text and only exists to keep
    // its place in line behind a slow one; a derive entry is waiting for its
    // Argon2id derivation on the worker thread.
    struct PendingDecrypt {
        bool needsDerivation = false;
        QByteArray salt;    // derive entries only, kSaltLen
        QByteArray payload; // derive entries only, ciphertext after the salt
        QString passphrase; // derive entries only, for the cache key
        QString result;     // fast entries only, computed at enqueue time
        std::function<void(const QString &plain, bool delivered)> done;
    };

    void startDerivation();
    void onDerived(const QByteArray &salt, std::shared_ptr<QByteArray> key, bool ok);
    static QByteArray cacheKeyFor(const QString &passphrase, const QByteArray &salt);

    // aad is the one-byte domain tag of the slot this ciphertext belongs in, so
    // a sealed voice frame cannot be spliced in where a message body is read.
    static QByteArray aeadSeal(const QByteArray &key, const QByteArray &plaintext, char aad);
    static bool aeadOpen(const QByteArray &key, const QByteArray &data, char aad, QByteArray *outPlain);
    QByteArray deriveKey(const QString &passphrase, const QByteArray &salt) const;
    static QByteArray randomBytes(int n);
    void evictOldestNoncePeers() const;
    QString resolveIdentity(const QString &peerRef) const;
    void noteObservedAddress(const QString &peerId, const QString &address);

    const QString m_storageScope;
    bool m_valid = false;

    // Held as bytes rather than as library handles: libsodium has no key object,
    // and the wallet already stores exactly these. The identity secret is the
    // 64-byte expanded Ed25519 key; what reaches the wallet is its 32-byte seed.
    QByteArray m_identitySk;
    QByteArray m_dhSk; // crypto_kx secret key, 32 bytes
    QByteArray m_dhPubBytes;
    QByteArray m_identityPubBytes;
    QByteArray m_dhPubSig;

    // The timestamp answers "is this old enough to forget", the sequence number
    // answers "which of these arrived first" - a flood fits thousands of nonces
    // into one millisecond, so the clock cannot answer the second question.
    struct SeenNonce {
        double ts = 0.0;
        quint64 seq = 0;
    };

    // crypto_kx hands each side a receiving key and a sending key rather than one
    // shared key, so the two directions never share a keystream and a packet
    // cannot be reflected back at its sender as though the peer had written it.
    // Our tx is the peer's rx: which of the two roles we take is settled in
    // processHandshakeFrom() by ordering the public keys.
    struct SessionKeys {
        QByteArray rx; // opens and verifies what the peer sent
        QByteArray tx; // seals and signs what we send
    };

    QHash<QString, SessionKeys> m_sessionKeys; // identity id -> the pair above
    QHash<QString, QByteArray> m_peerIdPub; // identity id -> raw Ed25519 pubkey
    // The only address-keyed trust-adjacent state left. It is a hint for
    // resolving an incoming datagram, so a wrong entry costs a failed HMAC
    // check.
    QHash<QString, QString> m_addressToId; // address -> identity id
    QHash<QString, QStringList> m_idToAddresses; // identity id -> addresses, newest first
    QHash<QString, QByteArray> m_warnedIdPub; // address -> key we last warned about
    // Mutable like the passphrase cache above: the replay and rate gates are
    // stateful even though their call sites are const.
    mutable QHash<QString, QHash<QString, SeenNonce>> m_seenNonces; // identity id -> nonce -> when
    mutable QHash<QString, quint64> m_nonceBucketTouched; // identity id -> its newest seq
    mutable quint64 m_nonceSeq = 0; // arrivals, ever
    mutable QHash<QString, QVector<double>> m_rateCounters; // source address -> recent timestamps

    // sha256(salt + passphrase) -> key. Keyed on a digest rather than on the
    // passphrase, which used to keep the plaintext alive here for the life of
    // the process. See deriveKey().
    mutable QHash<QByteArray, QByteArray> m_passphraseKeyCache;

    // The queue above, one derivation in flight at a time. The thread is created
    // on the first derivation that needs one, and the destructor stops it
    // before any key material is wiped.
    QList<PendingDecrypt> m_pendingDecrypts;
    bool m_derivationInFlight = false;
    QThread *m_deriveThread = nullptr;
    DeriveWorker *m_deriveWorker = nullptr;
};

} // namespace koutnet
