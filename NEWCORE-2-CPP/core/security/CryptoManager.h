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

    explicit CryptoManager(QObject *parent = nullptr);
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

private:
    bool initKeypairs();
    bool loadStoredKeys();
    bool generateAndStoreKeys();

    static QByteArray gcmEncrypt(const QByteArray &key, const QByteArray &plaintext);
    static bool gcmDecrypt(const QByteArray &key, const QByteArray &data, QByteArray *outPlain);
    QByteArray deriveKey(const QString &passphrase, const QByteArray &salt) const;
    static QByteArray hkdfSha256(const QByteArray &secret, const QByteArray &info, int outLen);
    static QByteArray randomBytes(int n);

    bool m_valid = false;

    EVP_PKEY *m_identityPriv = nullptr; // Ed25519
    EVP_PKEY *m_dhPriv = nullptr;       // X25519
    QByteArray m_dhPubBytes;
    QByteArray m_identityPubBytes;
    QByteArray m_dhPubSig;

    QHash<QString, QByteArray> m_sessionKeys;             // peer ip -> 32-byte session key
    QHash<QString, QByteArray> m_peerIdPub;               // peer ip -> raw Ed25519 pubkey
    QHash<QString, QByteArray> m_warnedIdPub;             // peer ip -> key we last warned about
    QHash<QString, QHash<QString, double>> m_seenNonces;  // peer ip -> nonce -> ts
    QHash<QString, QVector<double>> m_rateCounters;       // peer ip -> recent timestamps

    mutable QHash<QString, QByteArray> m_passphraseKeyCache; // "passphrase|saltHex" -> key
};

} // namespace koutnet
