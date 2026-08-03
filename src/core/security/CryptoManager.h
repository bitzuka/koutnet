// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet security engine.
//
// X25519 ECDH exchange with an Ed25519-signed identity, AES-256-GCM on
// messages, a PBKDF2-SHA256 passphrase overlay for groups, HMAC-SHA256 on
// control packets, a replay window over nonce and timestamp, and per-IP
// rate limiting.
#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QJsonObject>
#include <QHash>
#include <QVector>

typedef struct evp_pkey_st EVP_PKEY;

namespace koutnet {

enum class SecurityLevel {
    Plain, // no encryption at all
    Psk,   // pre-shared passphrase (PBKDF2 + AES-GCM)
    E2E,   // ECDH session key established (AES-GCM)
};

class CryptoManager : public QObject {
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
    // too and would otherwise be the way around the cap above.
    static constexpr int kMaxRatePeers = 1024;

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
    bool isValid() const { return m_valid; }

    // Handshake
    QJsonObject handshakePayload() const;
    bool processHandshake(const QString &peerIp, const QJsonObject &data);
    bool hasSession(const QString &peerIp) const;

    QString fingerprint() const;
    QString peerFingerprint(const QString &peerIp) const;
    SecurityLevel securityLevel(const QString &peerIp, bool encryptionEnabled,
                                bool hasPassphrase) const;

    // Packet HMAC
    QString signPacket(const QString &peerIp, const QByteArray &payload) const;
    bool verifyPacket(const QString &peerIp, const QByteArray &payload,
                      const QString &sigB64) const;

    // Replay / rate limiting
    bool checkReplay(const QString &peerIp, const QString &nonceHex, double ts);
    bool checkRate(const QString &peerIp, int maxPerSec = 200);

    // Message encryption (text, base64-wrapped wire format)
    QString encrypt(const QString &plaintext, const QString &passphrase = QString(),
                    const QString &peerIp = QString()) const;
    QString decrypt(const QString &ciphertext, const QString &passphrase = QString(),
                    const QString &peerIp = QString()) const;

    // Raw byte encryption (voice frames - no base64/JSON overhead)
    // Both refuse to work without a session: encryptBytes returns an empty
    // array and decryptBytes returns false, and the caller drops the frame.
    QByteArray encryptBytes(const QString &peerIp, const QByteArray &plaintext) const;
    bool decryptBytes(const QString &peerIp, const QByteArray &data, QByteArray *outPlain) const;

Q_SIGNALS:
    // A handshake presented an identity key that does not match the one
    // already pinned for this IP. The handshake was refused and the existing
    // session left alone, so this is a warning, not a state change: either
    // someone is impersonating the peer, or the peer reinstalled and lost its
    // keys. The UI should show both fingerprints and let the user decide
    // (clearing the pin is not implemented yet).
    void peerIdentityChanged(const QString &peerIp, const QString &oldFingerprint,
                             const QString &newFingerprint);

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

    const QString m_storageScope;
    bool m_valid = false;

    EVP_PKEY *m_identityPriv = nullptr; // Ed25519
    EVP_PKEY *m_dhPriv = nullptr;       // X25519
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

    QHash<QString, QByteArray> m_sessionKeys;             // peer ip -> 32-byte session key
    QHash<QString, QByteArray> m_peerIdPub;               // peer ip -> raw Ed25519 pubkey
    QHash<QString, QByteArray> m_warnedIdPub;             // peer ip -> key we last warned about
    QHash<QString, QHash<QString, SeenNonce>> m_seenNonces; // peer ip -> nonce -> when
    QHash<QString, quint64> m_nonceBucketTouched;         // peer ip -> its newest seq
    quint64 m_nonceSeq = 0;                               // arrivals, ever
    QHash<QString, QVector<double>> m_rateCounters;       // peer ip -> recent timestamps

    // sha256(salt + passphrase) -> key. Keyed on a digest rather than on the
    // passphrase itself, which used to keep the plaintext alive here for as
    // long as the process ran. See deriveKey().
    mutable QHash<QByteArray, QByteArray> m_passphraseKeyCache;
};

} // namespace koutnet
