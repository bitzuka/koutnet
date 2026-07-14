// KOutNet — Security Engine v2 (C++/Qt6 port)
// Ported from gdf_core.py (CryptoEngine, NT Server 1.8)
//
// Layer 1 — X25519 ECDH key exchange, Ed25519-signed identity
// Layer 2 — AES-256-GCM message encryption
// Layer 3 — PBKDF2-SHA256 passphrase overlay (groups)
// Layer 4 — Packet HMAC-SHA256 (session-key signed control packets)
// Layer 5 — Replay protection (nonce + timestamp window)
// Layer 6 — Rate limiting (packets/sec per source IP)
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

    explicit CryptoManager(QObject *parent = nullptr);
    ~CryptoManager() override;

    // ── Handshake ────────────────────────────────────────────────────
    QJsonObject handshakePayload() const;
    bool processHandshake(const QString &peerIp, const QJsonObject &data);
    bool hasSession(const QString &peerIp) const;

    QString fingerprint() const;
    QString peerFingerprint(const QString &peerIp) const;
    SecurityLevel securityLevel(const QString &peerIp, bool encryptionEnabled,
                                bool hasPassphrase) const;

    // ── Packet HMAC ──────────────────────────────────────────────────
    QString signPacket(const QString &peerIp, const QByteArray &payload) const;
    bool verifyPacket(const QString &peerIp, const QByteArray &payload,
                      const QString &sigB64) const;

    // ── Replay / rate limiting ─────────────────────────────────────────
    bool checkReplay(const QString &peerIp, const QString &nonceHex, double ts);
    bool checkRate(const QString &peerIp, int maxPerSec = 200);

    // ── Message encryption (text, base64-wrapped wire format) ──────────
    QString encrypt(const QString &plaintext, const QString &passphrase = QString(),
                    const QString &peerIp = QString()) const;
    QString decrypt(const QString &ciphertext, const QString &passphrase = QString(),
                    const QString &peerIp = QString()) const;

    // ── Raw byte encryption (voice frames — no base64/JSON overhead) ───
    // Falls back to passthrough (plaintext) if no session exists yet, same
    // as the text path falling back to unencrypted when nothing is set up.
    QByteArray encryptBytes(const QString &peerIp, const QByteArray &plaintext) const;
    bool decryptBytes(const QString &peerIp, const QByteArray &data, QByteArray *outPlain) const;

private:
    void initKeypairs();
    bool loadStoredKeys();
    void generateAndStoreKeys();

    static QByteArray gcmEncrypt(const QByteArray &key, const QByteArray &plaintext);
    static bool gcmDecrypt(const QByteArray &key, const QByteArray &data, QByteArray *outPlain);
    QByteArray deriveKey(const QString &passphrase, const QByteArray &salt) const;
    static QByteArray hkdfSha256(const QByteArray &secret, const QByteArray &info, int outLen);
    static QByteArray randomBytes(int n);

    EVP_PKEY *m_identityPriv = nullptr; // Ed25519
    EVP_PKEY *m_dhPriv = nullptr;       // X25519
    QByteArray m_dhPubBytes;
    QByteArray m_identityPubBytes;
    QByteArray m_dhPubSig;

    QHash<QString, QByteArray> m_sessionKeys;             // peer ip -> 32-byte session key
    QHash<QString, QByteArray> m_peerIdPub;               // peer ip -> raw Ed25519 pubkey
    QHash<QString, QHash<QString, double>> m_seenNonces;  // peer ip -> nonce -> ts
    QHash<QString, QVector<double>> m_rateCounters;       // peer ip -> recent timestamps

    mutable QHash<QString, QByteArray> m_passphraseKeyCache; // "passphrase|saltHex" -> key
};

} // namespace koutnet
