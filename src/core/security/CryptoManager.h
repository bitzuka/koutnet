// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// Noise_XX handshake (X25519 + HKDF-SHA256), XChaCha20-Poly1305 encryption,
// counter-based replay, Argon2id for group passphrases, rate limiting.
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
    E2E, // Noise_XX session key established (XChaCha20-Poly1305)
};

class DeriveWorker; // defined in the .cpp, moved to m_deriveThread

class CryptoManager : public QObject
{
    Q_OBJECT

public:
    // 192-bit nonce. Counter uses the last 8 bytes (big-endian), the rest is
    // zero. Random nonces only in shared sessions (room calls).
    static constexpr int kNonceLen = 24;
    static constexpr int kTagLen = 16;
    static constexpr int kKeyLen = 32;
    // crypto_pwhash_SALTBYTES; static_asserted against libsodium in the .cpp.
    static constexpr int kSaltLen = 16;
    // Cap on cached Argon2id passphrase keys - cycling many group passphrases
    // over a long session grew this hash unboundedly.
    static constexpr int kMaxPassphraseCacheSize = 256;
    // Same again for the rate-limit windows, which are keyed on source address
    // and would otherwise be the way around the cap above.
    static constexpr int kMaxRatePeers = 1024;
    // Ceiling on the async decrypt queue, and on the ciphertext body a queued
    // message may carry. The derivation that heads the queue costs 64 MiB and
    // two passes, so a flood must cost memory slots, not memory itself - see
    // decryptAsync().
    static constexpr int kMaxPendingDecrypts = 8;
    static constexpr int kMaxAsyncPayloadBytes = 1024 * 1024;
    // Max addresses remembered per identity. Past LAN + VPN + second NIC
    // the oldest one drops.
    static constexpr int kMaxPeerAddresses = 8;

    explicit CryptoManager(QObject *parent = nullptr);
    // Identity kept under a suffix of its own, in the encrypted store and in the config
    // file alike. Only tests pass a scope; it is what lets two peers live in
    // one process without the second loading the first one's keypair.
    explicit CryptoManager(const QString &storageScope, QObject *parent = nullptr);
    ~CryptoManager() override;

    // True after generateAndStoreKeys() fell back to an in-memory identity
    // because the store was unreachable.  The caller (main.cpp, QML) should
    // show a warning on the first run that hits this.
    bool isStoreDegraded() const
    {
        return m_storeDegraded;
    }

    // False when keypair generation or loading failed at startup: nothing below
    // can establish a session without keys, so every call simply refuses.
    bool isValid() const
    {
        return m_valid;
    }

    // peerRef is either an identity id or an address (resolved to identity
    // via the address→id map). Addresses are routing hints, not auth.
    static QString identityIdFor(const QByteArray &idPubRaw);
    QString ownIdentityId() const;
    // Empty when this address has never carried a verified handshake. Callers
    // treat that as "no idea who this is", not as "not to be trusted".
    Q_INVOKABLE QString identityForAddress(const QString &address) const;
    // Newest first, capped at kMaxPeerAddresses. Only from actual datagrams,
    // not from what the peer advertises about itself.
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

    // Counter-based replay check: nonce must be > last seen from this peer.
    // Keyed on identity so a capture from another address doesn't help.
    bool checkReplay(const QString &peerRef, quint64 nonceCounter) const;
    // Deliberately still keyed on the source address: this one runs before
    // anything is known about who sent the packet, which is the point of it.
    bool checkRate(const QString &sourceAddress, int maxPerSec = 200) const;

    QString encrypt(const QString &plaintext, const QString &passphrase = QString(), const QString &peerRef = QString()) const;
    QString decrypt(const QString &ciphertext, const QString &passphrase = QString(), const QString &peerRef = QString()) const;

    // Async decrypt for the receive path. Argon2id derivation (64 MiB) runs
    // on a worker thread so an attacker's fresh-salt flood can't freeze the
    // GUI. delivered=false means the queue gate refused it (flood), don't render.
    void decryptAsync(const QString &ciphertext,
                      const QString &passphrase,
                      const QString &peerRef,
                      const std::function<void(const QString &plain, bool delivered)> &done);

    // Raw byte encryption (voice frames - no base64/JSON overhead)
    // Both refuse to work without a session: encryptBytes returns an empty
    // array and decryptBytes returns false, and the caller drops the frame.
    QByteArray encryptBytes(const QString &peerRef, const QByteArray &plaintext) const;
    bool decryptBytes(const QString &peerRef, const QByteArray &data, QByteArray *outPlain) const;

    // Caller-supplied session instead of handshake. Used for room calls where
    // media runs between addresses that never did a LAN handshake — the matrix
    // bridge routes the shared key through the room. Both directions get the
    // same key, nonces are random. Erased by dropSharedSession().
    bool installSharedSession(const QString &peerRef, const QByteArray &key32);
    void dropSharedSession(const QString &peerRef);

    // File transfer encryption. Same primitives, a separate AAD tag so file
    // bytes cannot be replayed as voice frames or the reverse; the whole
    // file is sealed before the sender chunks it. Empty result means no
    // session, and the caller falls back to plaintext.
    QByteArray encryptFileBytes(const QString &peerRef, const QByteArray &plaintext) const;
    bool decryptFileBytes(const QString &peerRef, const QByteArray &data, QByteArray *outPlain) const;

Q_SIGNALS:
    // A different identity showed up at an address we have a session for.
    // The handshake is refused, existing session stays. Could be an impostor
    // or the peer reinstalled and lost its keys — UI should show both
    // fingerprints.
    void peerIdentityChanged(const QString &address, const QString &oldFingerprint, const QString &newFingerprint);

    // The plaintext copy an older build left in the config file could not be
    // deleted and is still readable on disk; only the user can repair that.
    void plaintextKeysLeftInConfig(const QString &reason);

    // Store unreachable at startup. Throwaway identity for this session only,
    // won't survive restart. Warn the user.
    void storeDegraded(const QString &reason);

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
        QByteArray salt; // derive entries only, kSaltLen
        QByteArray payload; // derive entries only, ciphertext after the salt
        QString passphrase; // derive entries only, for the cache key
        QString result; // fast entries only, computed at enqueue time
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
    // HKDF-SHA256: extract-and-expand, used by the Noise_XX handshake to derive
    // the session key from the two X25519 shared secrets.
    static QByteArray hkdfExtract(const QByteArray &salt, const QByteArray &inputKeyMaterial);
    static QByteArray hkdfExpand(const QByteArray &prk, const QByteArray &info, int outputLen);
    QString resolveIdentity(const QString &peerRef) const;
    void noteObservedAddress(const QString &peerId, const QString &address);

    const QString m_storageScope;
    bool m_valid = false;
    bool m_storeDegraded = false;

    // Held as bytes rather than as library handles: libsodium has no key object,
    // and the store already stores exactly these. The identity secret is the
    // 64-byte expanded Ed25519 key; what reaches the store is its 32-byte seed.
    QByteArray m_identitySk;
    QByteArray m_dhSk; // X25519 secret key, 32 bytes
    QByteArray m_dhPubBytes;
    QByteArray m_identityPubBytes;
    QByteArray m_dhPubSig;

    // Single symmetric key + 8-byte big-endian nonce counter. Counter starts
    // at 0, increments with each encrypted message. Receiver rejects
    // nonce <= last seen.
    struct CipherState {
        QByteArray key; // 32-byte XChaCha20-Poly1305 key
        quint64 sendNonce = 0; // outgoing counter, incremented after each seal
        quint64 recvNonce = 0; // highest incoming counter accepted so far
        bool hasReceived = false; // true after the first counter is accepted
    };

    QHash<QString, CipherState> m_sessions; // identity id -> cipher state
    QHash<QString, QByteArray> m_peerIdPub; // identity id -> raw Ed25519 pubkey
    // Address→identity mapping, just a routing hint. Wrong entry = failed decrypt.
    QHash<QString, QString> m_addressToId; // address -> identity id
    QHash<QString, QStringList> m_idToAddresses; // identity id -> addresses, newest first
    QHash<QString, QByteArray> m_warnedIdPub; // address -> key we last warned about
    // Rate limiting: keyed on source address, not identity, so a flood from
    // spoofed sources grows this the way the old replay cache grew.
    mutable QHash<QString, QVector<double>> m_rateCounters;

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
