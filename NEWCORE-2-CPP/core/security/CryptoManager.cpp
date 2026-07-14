// KOutNet — Security Engine v2 (C++/Qt6 port)
// Ported from gdf_core.py (CryptoEngine, NT Server 1.8)
#include "CryptoManager.h"

#include <QSettings>
#include <QDateTime>
#include <QCryptographicHash>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/crypto.h>

namespace koutnet {

namespace {

double nowEpoch()
{
    return QDateTime::currentMSecsSinceEpoch() / 1000.0;
}

QString bytesToFingerprint(const QByteArray &raw)
{
    const QByteArray h = QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex();
    QString out;
    for (int i = 0; i < 24; i += 4) {
        if (i) out += ' ';
        out += QString::fromLatin1(h.mid(i, 4)).toUpper();
    }
    return out;
}

} // namespace

CryptoManager::CryptoManager(QObject *parent) : QObject(parent)
{
    initKeypairs();
}

CryptoManager::~CryptoManager()
{
    if (m_identityPriv) EVP_PKEY_free(m_identityPriv);
    if (m_dhPriv) EVP_PKEY_free(m_dhPriv);
}

QByteArray CryptoManager::randomBytes(int n)
{
    QByteArray buf(n, 0);
    RAND_bytes(reinterpret_cast<unsigned char *>(buf.data()), n);
    return buf;
}

// ── Key lifecycle ──────────────────────────────────────────────────────
// TODO: route through AppSettings once core/constructor lands — QSettings
// is used directly for now so identity persists across restarts.
void CryptoManager::initKeypairs()
{
    if (!loadStoredKeys())
        generateAndStoreKeys();

    size_t len = 0;
    EVP_PKEY_get_raw_public_key(m_dhPriv, nullptr, &len);
    m_dhPubBytes.resize(int(len));
    EVP_PKEY_get_raw_public_key(m_dhPriv, reinterpret_cast<unsigned char *>(m_dhPubBytes.data()), &len);

    len = 0;
    EVP_PKEY_get_raw_public_key(m_identityPriv, nullptr, &len);
    m_identityPubBytes.resize(int(len));
    EVP_PKEY_get_raw_public_key(m_identityPriv, reinterpret_cast<unsigned char *>(m_identityPubBytes.data()), &len);

    // Sign our DH public key with our identity key (Ed25519 one-shot sign).
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    EVP_DigestSignInit(mdctx, nullptr, nullptr, nullptr, m_identityPriv);
    size_t sigLen = 0;
    EVP_DigestSign(mdctx, nullptr, &sigLen,
                   reinterpret_cast<const unsigned char *>(m_dhPubBytes.constData()), m_dhPubBytes.size());
    m_dhPubSig.resize(int(sigLen));
    EVP_DigestSign(mdctx, reinterpret_cast<unsigned char *>(m_dhPubSig.data()), &sigLen,
                   reinterpret_cast<const unsigned char *>(m_dhPubBytes.constData()), m_dhPubBytes.size());
    m_dhPubSig.resize(int(sigLen));
    EVP_MD_CTX_free(mdctx);
}

bool CryptoManager::loadStoredKeys()
{
    QSettings settings;
    const QByteArray idB64 = settings.value("security/identity_priv_b64").toByteArray();
    const QByteArray dhB64 = settings.value("security/dh_priv_b64").toByteArray();
    if (idB64.isEmpty() || dhB64.isEmpty())
        return false;

    const QByteArray idRaw = QByteArray::fromBase64(idB64);
    const QByteArray dhRaw = QByteArray::fromBase64(dhB64);

    m_identityPriv = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
        reinterpret_cast<const unsigned char *>(idRaw.constData()), idRaw.size());
    m_dhPriv = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr,
        reinterpret_cast<const unsigned char *>(dhRaw.constData()), dhRaw.size());

    if (!m_identityPriv || !m_dhPriv) {
        if (m_identityPriv) { EVP_PKEY_free(m_identityPriv); m_identityPriv = nullptr; }
        if (m_dhPriv) { EVP_PKEY_free(m_dhPriv); m_dhPriv = nullptr; }
        return false;
    }
    return true;
}

void CryptoManager::generateAndStoreKeys()
{
    EVP_PKEY_CTX *idCtx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    EVP_PKEY_keygen_init(idCtx);
    EVP_PKEY_keygen(idCtx, &m_identityPriv);
    EVP_PKEY_CTX_free(idCtx);

    EVP_PKEY_CTX *dhCtx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    EVP_PKEY_keygen_init(dhCtx);
    EVP_PKEY_keygen(dhCtx, &m_dhPriv);
    EVP_PKEY_CTX_free(dhCtx);

    size_t len = 0;
    EVP_PKEY_get_raw_private_key(m_identityPriv, nullptr, &len);
    QByteArray idRaw(int(len), 0);
    EVP_PKEY_get_raw_private_key(m_identityPriv, reinterpret_cast<unsigned char *>(idRaw.data()), &len);

    len = 0;
    EVP_PKEY_get_raw_private_key(m_dhPriv, nullptr, &len);
    QByteArray dhRaw(int(len), 0);
    EVP_PKEY_get_raw_private_key(m_dhPriv, reinterpret_cast<unsigned char *>(dhRaw.data()), &len);

    QSettings settings;
    settings.setValue("security/identity_priv_b64", idRaw.toBase64());
    settings.setValue("security/dh_priv_b64", dhRaw.toBase64());
    settings.sync();
}

// ── Handshake ────────────────────────────────────────────────────────
QJsonObject CryptoManager::handshakePayload() const
{
    QJsonObject payload;
    payload["dh_pub"] = QString::fromLatin1(m_dhPubBytes.toBase64());
    payload["id_pub"] = QString::fromLatin1(m_identityPubBytes.toBase64());
    payload["dh_pub_sig"] = QString::fromLatin1(m_dhPubSig.toBase64());
    return payload;
}

bool CryptoManager::processHandshake(const QString &peerIp, const QJsonObject &data)
{
    const QByteArray peerDhBytes = QByteArray::fromBase64(data.value("dh_pub").toString().toLatin1());
    const QByteArray peerIdBytes = QByteArray::fromBase64(data.value("id_pub").toString().toLatin1());
    const QByteArray peerDhSig = QByteArray::fromBase64(data.value("dh_pub_sig").toString().toLatin1());
    if (peerDhBytes.isEmpty() || peerIdBytes.isEmpty() || peerDhSig.isEmpty())
        return false;

    EVP_PKEY *peerIdPub = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
        reinterpret_cast<const unsigned char *>(peerIdBytes.constData()), peerIdBytes.size());
    if (!peerIdPub)
        return false;

    // Verify: peer's identity key signed their DH key.
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    EVP_DigestVerifyInit(mdctx, nullptr, nullptr, nullptr, peerIdPub);
    const int verifyRc = EVP_DigestVerify(mdctx,
        reinterpret_cast<const unsigned char *>(peerDhSig.constData()), peerDhSig.size(),
        reinterpret_cast<const unsigned char *>(peerDhBytes.constData()), peerDhBytes.size());
    EVP_MD_CTX_free(mdctx);
    if (verifyRc != 1) {
        EVP_PKEY_free(peerIdPub);
        return false;
    }

    EVP_PKEY *peerDhPub = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr,
        reinterpret_cast<const unsigned char *>(peerDhBytes.constData()), peerDhBytes.size());
    if (!peerDhPub) {
        EVP_PKEY_free(peerIdPub);
        return false;
    }

    EVP_PKEY_CTX *dctx = EVP_PKEY_CTX_new(m_dhPriv, nullptr);
    EVP_PKEY_derive_init(dctx);
    EVP_PKEY_derive_set_peer(dctx, peerDhPub);
    size_t secretLen = 0;
    EVP_PKEY_derive(dctx, nullptr, &secretLen);
    QByteArray sharedSecret(int(secretLen), 0);
    EVP_PKEY_derive(dctx, reinterpret_cast<unsigned char *>(sharedSecret.data()), &secretLen);
    EVP_PKEY_CTX_free(dctx);
    EVP_PKEY_free(peerDhPub);

    const QByteArray sessionKey = hkdfSha256(sharedSecret, QByteArrayLiteral("-v2-session"), kKeyLen);

    m_sessionKeys[peerIp] = sessionKey;
    m_peerIdPub[peerIp] = peerIdBytes;
    EVP_PKEY_free(peerIdPub);
    return true;
}

bool CryptoManager::hasSession(const QString &peerIp) const
{
    return m_sessionKeys.contains(peerIp);
}

QString CryptoManager::fingerprint() const
{
    return bytesToFingerprint(m_identityPubBytes);
}

QString CryptoManager::peerFingerprint(const QString &peerIp) const
{
    if (!m_peerIdPub.contains(peerIp))
        return QStringLiteral("?");
    return bytesToFingerprint(m_peerIdPub.value(peerIp));
}

SecurityLevel CryptoManager::securityLevel(const QString &peerIp, bool encryptionEnabled,
                                           bool hasPassphrase) const
{
    if (!peerIp.isEmpty() && m_sessionKeys.contains(peerIp))
        return SecurityLevel::E2E;
    if (encryptionEnabled && hasPassphrase)
        return SecurityLevel::Psk;
    return SecurityLevel::Plain;
}

// ── HKDF-SHA256 ───────────────────────────────────────────────────────
QByteArray CryptoManager::hkdfSha256(const QByteArray &secret, const QByteArray &info, int outLen)
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    EVP_PKEY_derive_init(ctx);
    EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256());
    // RFC 5869 default salt (no salt provided) = HashLen zero bytes.
    const QByteArray zeroSalt(32, 0);
    EVP_PKEY_CTX_set1_hkdf_salt(ctx, reinterpret_cast<const unsigned char *>(zeroSalt.constData()), zeroSalt.size());
    EVP_PKEY_CTX_set1_hkdf_key(ctx, reinterpret_cast<const unsigned char *>(secret.constData()), secret.size());
    EVP_PKEY_CTX_add1_hkdf_info(ctx, reinterpret_cast<const unsigned char *>(info.constData()), info.size());

    QByteArray out(outLen, 0);
    size_t len = size_t(outLen);
    EVP_PKEY_derive(ctx, reinterpret_cast<unsigned char *>(out.data()), &len);
    EVP_PKEY_CTX_free(ctx);
    return out;
}

// ── AES-256-GCM ──────────────────────────────────────────────────────
QByteArray CryptoManager::gcmEncrypt(const QByteArray &key, const QByteArray &plaintext)
{
    const QByteArray nonce = randomBytes(kNonceLen);
    QByteArray ciphertext(plaintext.size(), 0);
    QByteArray tag(kTagLen, 0);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceLen, nullptr);
    EVP_EncryptInit_ex(ctx, nullptr, nullptr,
        reinterpret_cast<const unsigned char *>(key.constData()),
        reinterpret_cast<const unsigned char *>(nonce.constData()));

    int outLen = 0;
    EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char *>(ciphertext.data()), &outLen,
        reinterpret_cast<const unsigned char *>(plaintext.constData()), plaintext.size());
    int finalLen = 0;
    EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(ciphertext.data()) + outLen, &finalLen);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagLen, tag.data());
    EVP_CIPHER_CTX_free(ctx);

    return nonce + ciphertext + tag; // nonce + ciphertext + tag, matches legacy wire layout
}

bool CryptoManager::gcmDecrypt(const QByteArray &key, const QByteArray &data, QByteArray *outPlain)
{
    if (data.size() < kNonceLen + kTagLen)
        return false;

    const QByteArray nonce = data.left(kNonceLen);
    const QByteArray tag = data.right(kTagLen);
    const QByteArray ciphertext = data.mid(kNonceLen, data.size() - kNonceLen - kTagLen);

    QByteArray plaintext(ciphertext.size(), 0);
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceLen, nullptr);
    EVP_DecryptInit_ex(ctx, nullptr, nullptr,
        reinterpret_cast<const unsigned char *>(key.constData()),
        reinterpret_cast<const unsigned char *>(nonce.constData()));

    int outLen = 0;
    EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(plaintext.data()), &outLen,
        reinterpret_cast<const unsigned char *>(ciphertext.constData()), ciphertext.size());
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagLen,
        const_cast<char *>(tag.constData()));

    int finalLen = 0;
    const int rc = EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(plaintext.data()) + outLen, &finalLen);
    EVP_CIPHER_CTX_free(ctx);

    if (rc != 1)
        return false; // tag mismatch — tampered or wrong key

    *outPlain = plaintext.left(outLen + finalLen);
    return true;
}

// ── PBKDF2 passphrase keys (cached) ───────────────────────────────────
QByteArray CryptoManager::deriveKey(const QString &passphrase, const QByteArray &salt) const
{
    const QString cacheKey = passphrase + '|' + QString::fromLatin1(salt.toHex());
    if (m_passphraseKeyCache.contains(cacheKey))
        return m_passphraseKeyCache.value(cacheKey);

    QByteArray key(kKeyLen, 0);
    const QByteArray pass = passphrase.toUtf8();
    PKCS5_PBKDF2_HMAC(pass.constData(), pass.size(),
        reinterpret_cast<const unsigned char *>(salt.constData()), salt.size(),
        kKdfIters, EVP_sha256(), kKeyLen, reinterpret_cast<unsigned char *>(key.data()));

    m_passphraseKeyCache[cacheKey] = key;
    return key;
}

// ── Packet HMAC ──────────────────────────────────────────────────────
QString CryptoManager::signPacket(const QString &peerIp, const QByteArray &payload) const
{
    if (!m_sessionKeys.contains(peerIp))
        return QString();

    const QByteArray key = m_sessionKeys.value(peerIp);
    unsigned char digest[32];
    unsigned int digestLen = 0;
    HMAC(EVP_sha256(), key.constData(), key.size(),
        reinterpret_cast<const unsigned char *>(payload.constData()), payload.size(),
        digest, &digestLen);

    return QString::fromLatin1(QByteArray(reinterpret_cast<char *>(digest), int(digestLen)).toBase64());
}

bool CryptoManager::verifyPacket(const QString &peerIp, const QByteArray &payload,
                                 const QString &sigB64) const
{
    if (!m_sessionKeys.contains(peerIp))
        return true; // no session yet — accept unsigned packet, matches legacy behaviour

    const QByteArray key = m_sessionKeys.value(peerIp);
    unsigned char expected[32];
    unsigned int expectedLen = 0;
    HMAC(EVP_sha256(), key.constData(), key.size(),
        reinterpret_cast<const unsigned char *>(payload.constData()), payload.size(),
        expected, &expectedLen);

    const QByteArray given = QByteArray::fromBase64(sigB64.toLatin1());
    if (given.size() != int(expectedLen))
        return false;

    return CRYPTO_memcmp(expected, given.constData(), expectedLen) == 0;
}

// ── Replay protection ──────────────────────────────────────────────────
bool CryptoManager::checkReplay(const QString &peerIp, const QString &nonceHex, double ts)
{
    const double now = nowEpoch();
    if (std::abs(now - ts) > kReplayWindowSec)
        return false;

    QHash<QString, double> &bucket = m_seenNonces[peerIp];
    if (bucket.contains(nonceHex))
        return false;

    bucket[nonceHex] = now;

    for (auto it = bucket.begin(); it != bucket.end();) {
        if (now - it.value() > kNonceCacheTtlSec)
            it = bucket.erase(it);
        else
            ++it;
    }
    return true;
}

// ── Rate limiting ────────────────────────────────────────────────────
bool CryptoManager::checkRate(const QString &peerIp, int maxPerSec)
{
    const double now = nowEpoch();
    QVector<double> &window = m_rateCounters[peerIp];

    QVector<double> kept;
    kept.reserve(window.size());
    for (double t : std::as_const(window)) {
        if (now - t < 1.0)
            kept.append(t);
    }
    window = kept;

    if (window.size() >= maxPerSec)
        return false;

    window.append(now);
    return true;
}

// ── Public encrypt/decrypt ────────────────────────────────────────────
// Wire format (base64 after the "KNC1:" tag):
//   type[1] + payload
//   0x01 = AES-GCM with ECDH session key  (payload = nonce+ciphertext+tag)
//   0x02 = AES-GCM with PBKDF2 passphrase key (payload = salt[32]+nonce+ciphertext+tag)
QString CryptoManager::encrypt(const QString &plaintext, const QString &passphrase,
                               const QString &peerIp) const
{
    const QByteArray data = plaintext.toUtf8();

    QByteArray wire;
    if (!peerIp.isEmpty() && m_sessionKeys.contains(peerIp)) {
        wire.append(char(0x01));
        wire.append(gcmEncrypt(m_sessionKeys.value(peerIp), data));
    } else if (!passphrase.isEmpty()) {
        const QByteArray salt = randomBytes(kSaltLen);
        const QByteArray key = deriveKey(passphrase, salt);
        wire.append(char(0x02));
        wire.append(salt);
        wire.append(gcmEncrypt(key, data));
    } else {
        return plaintext; // nothing to encrypt with
    }

    return QStringLiteral("KNC1:") + QString::fromLatin1(wire.toBase64());
}

QString CryptoManager::decrypt(const QString &ciphertext, const QString &passphrase,
                               const QString &peerIp) const
{
    if (!ciphertext.startsWith(QStringLiteral("KNC1:")))
        return ciphertext; // not encrypted by us — pass through

    const QByteArray wire = QByteArray::fromBase64(ciphertext.mid(5).toLatin1());
    if (wire.isEmpty())
        return QStringLiteral("[decrypt error: empty packet]");

    const quint8 type = static_cast<quint8>(wire.at(0));
    const QByteArray payload = wire.mid(1);

    QByteArray plain;
    bool ok = false;

    if (type == 0x01) {
        if (!peerIp.isEmpty() && m_sessionKeys.contains(peerIp))
            ok = gcmDecrypt(m_sessionKeys.value(peerIp), payload, &plain);
    } else if (type == 0x02) {
        if (payload.size() > kSaltLen && !passphrase.isEmpty()) {
            const QByteArray salt = payload.left(kSaltLen);
            const QByteArray key = deriveKey(passphrase, salt);
            ok = gcmDecrypt(key, payload.mid(kSaltLen), &plain);
        }
    }

    if (!ok)
        return QStringLiteral("[decrypt error: invalid key or tampered packet]");

    return QString::fromUtf8(plain);
}


// ── Raw byte encryption (voice) ────────────────────────────────────────
QByteArray CryptoManager::encryptBytes(const QString &peerIp, const QByteArray &plaintext) const
{
    if (!m_sessionKeys.contains(peerIp))
        return plaintext; // no session yet — send unencrypted, same fallback as text path

    return gcmEncrypt(m_sessionKeys.value(peerIp), plaintext); // nonce+ciphertext+tag
}

bool CryptoManager::decryptBytes(const QString &peerIp, const QByteArray &data, QByteArray *outPlain) const
{
    if (!m_sessionKeys.contains(peerIp)) {
        *outPlain = data; // no session — treat as plaintext passthrough
        return true;
    }
    return gcmDecrypt(m_sessionKeys.value(peerIp), data, outPlain);
}

// ── Raw byte encryption (voice) ────────────────────────────────────────
QByteArray CryptoManager::encryptBytes(const QString &peerIp, const QByteArray &plaintext) const
{
    if (!m_sessionKeys.contains(peerIp))
        return plaintext; // no session yet — send unencrypted, same fallback as text path

    return gcmEncrypt(m_sessionKeys.value(peerIp), plaintext); // nonce+ciphertext+tag
}

bool CryptoManager::decryptBytes(const QString &peerIp, const QByteArray &data, QByteArray *outPlain) const
{
    if (!m_sessionKeys.contains(peerIp)) {
        *outPlain = data; // no session — treat as plaintext passthrough
        return true;
    }
    return gcmDecrypt(m_sessionKeys.value(peerIp), data, outPlain);
}


// ── Raw byte encryption (voice) ────────────────────────────────────────
QByteArray CryptoManager::encryptBytes(const QString &peerIp, const QByteArray &plaintext) const
{
    if (!m_sessionKeys.contains(peerIp))
        return plaintext; // no session yet — send unencrypted, same fallback as text path

    return gcmEncrypt(m_sessionKeys.value(peerIp), plaintext); // nonce+ciphertext+tag
}

bool CryptoManager::decryptBytes(const QString &peerIp, const QByteArray &data, QByteArray *outPlain) const
{
    if (!m_sessionKeys.contains(peerIp)) {
        *outPlain = data; // no session — treat as plaintext passthrough
        return true;
    }
    return gcmDecrypt(m_sessionKeys.value(peerIp), data, outPlain);
}

} // namespace koutnet
