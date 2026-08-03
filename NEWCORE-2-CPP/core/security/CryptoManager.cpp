// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Security Engine v2 (C++/Qt6 port)
#include "CryptoManager.h"
#include "SecretStore.h"

#include <QSettings>
#include <QDateTime>
#include <QCryptographicHash>
#include <QTimer>
#include <QDebug>

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

// Wallet entry names. The QSettings paths they replaced are spelled out in
// migrateLegacyKeys().
QString identityWalletKey()
{
    return QStringLiteral("identity_priv_b64");
}

QString dhWalletKey()
{
    return QStringLiteral("dh_priv_b64");
}

// Where builds before the wallet kept the same two keys, in clear text.
QStringList legacyConfigKeys()
{
    return { QStringLiteral("security/identity_priv_b64"),
             QStringLiteral("security/dh_priv_b64") };
}

// Where a config-file key that is not the one in use ends up, so that deleting
// the plaintext never destroys the only copy of an identity.
QString supersededWalletKey(const QString &walletKey)
{
    return walletKey + QStringLiteral("_superseded");
}

QString bytesToFingerprint(const QByteArray &raw)
{
    const QByteArray h = QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex();
    QString out;
    for (int i = 0; i < 24; i += 4) {
        if (i) out += QLatin1Char(' ');
        out += QString::fromLatin1(h.mid(i, 4)).toUpper();
    }
    return out;
}

} // namespace

CryptoManager::CryptoManager(QObject *parent) : QObject(parent)
{
    m_valid = initKeypairs();
    if (!m_valid) {
        qCritical("CryptoManager: failed to initialize identity/DH keypairs - "
                  "encryption is unavailable for this session. Check isValid() "
                  "before relying on encrypt()/handshakePayload().");
    }
}

CryptoManager::~CryptoManager()
{
    if (m_identityPriv) EVP_PKEY_free(m_identityPriv);
    if (m_dhPriv) EVP_PKEY_free(m_dhPriv);
}

QByteArray CryptoManager::randomBytes(int n)
{
    QByteArray buf(n, 0);
    // an exhausted entropy pool would otherwise hand back a buffer of zeroes,
    // which is a perfectly usable nonce as far as the caller can tell
    if (RAND_bytes(reinterpret_cast<unsigned char *>(buf.data()), n) != 1)
        return {};
    return buf;
}

// Key lifecycle
// The private keys live in KWallet, never in QSettings. A session without a
// wallet still gets a keypair so the app works, it just forgets it on exit.
bool CryptoManager::initKeypairs()
{
    if (!loadStoredKeys()) {
        if (!generateAndStoreKeys())
            return false;
    }

    size_t len = 0;
    if (EVP_PKEY_get_raw_public_key(m_dhPriv, nullptr, &len) != 1 || len == 0)
        return false;
    m_dhPubBytes.resize(int(len));
    if (EVP_PKEY_get_raw_public_key(m_dhPriv, reinterpret_cast<unsigned char *>(m_dhPubBytes.data()), &len) != 1)
        return false;

    len = 0;
    if (EVP_PKEY_get_raw_public_key(m_identityPriv, nullptr, &len) != 1 || len == 0)
        return false;
    m_identityPubBytes.resize(int(len));
    if (EVP_PKEY_get_raw_public_key(m_identityPriv, reinterpret_cast<unsigned char *>(m_identityPubBytes.data()), &len) != 1)
        return false;

    // Sign our DH public key with our identity key (Ed25519 one-shot sign).
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx)
        return false;

    bool ok = EVP_DigestSignInit(mdctx, nullptr, nullptr, nullptr, m_identityPriv) == 1;

    size_t sigLen = 0;
    if (ok && EVP_DigestSign(mdctx, nullptr, &sigLen,
                             reinterpret_cast<const unsigned char *>(m_dhPubBytes.constData()),
                             m_dhPubBytes.size()) != 1) {
        ok = false;
    }

    if (ok) {
        m_dhPubSig.resize(int(sigLen));
        if (EVP_DigestSign(mdctx, reinterpret_cast<unsigned char *>(m_dhPubSig.data()), &sigLen,
                           reinterpret_cast<const unsigned char *>(m_dhPubBytes.constData()),
                           m_dhPubBytes.size()) != 1) {
            ok = false;
        } else {
            m_dhPubSig.resize(int(sigLen));
        }
    }

    EVP_MD_CTX_free(mdctx);
    return ok;
}

bool CryptoManager::migrateLegacyKeys(QString *outIdentityB64, QString *outDhB64)
{
    QString legacyId;
    QString legacyDh;
    {
        // Scoped so the instance that read the plaintext is gone before the one
        // that deletes it exists. Both would share a single in-memory copy of
        // the file anyway, but nothing here needs that to be true.
        // toString() and not toByteArray(): the old build stored these with a
        // QByteArray overload, so the file says @ByteArray(...) - QSettings
        // hands either type back as the same base64 text.
        QSettings settings;
        legacyId = settings.value(legacyConfigKeys().at(0)).toString();
        legacyDh = settings.value(legacyConfigKeys().at(1)).toString();
    }
    if (legacyId.isEmpty() || legacyDh.isEmpty())
        return false;

    *outIdentityB64 = legacyId;
    *outDhB64 = legacyDh;

    // Only drop the plaintext copy once the wallet has both halves, otherwise a
    // wallet that is merely unreachable today would cost the user their identity.
    if (!SecretStore::write(identityWalletKey(), legacyId)
        || !SecretStore::write(dhWalletKey(), legacyDh)) {
        qCritical("CryptoManager: your private keys are still stored in plain text in the "
                  "config file because they could not be moved into KWallet (%s). Start "
                  "kwalletd and restart KOutNet.",
                  qUtf8Printable(SecretStore::lastError()));
        reportPlaintextKeysLeft(SecretStore::lastError());
        return true;
    }

    qInfo("CryptoManager: copied the identity keys into KWallet");
    dropLegacyPlaintextKeys();
    return true;
}

// Deliberately not part of the migration branch. Deleting the plaintext used to
// be a side effect of the one run that filled the wallet, so a deletion that
// never reached the disk was never retried: from the next start on the wallet
// read succeeds, the migration is skipped, and the readable copy stays forever.
void CryptoManager::dropLegacyPlaintextKeys()
{
    if (!stashSupersededPlaintextKeys())
        return;

    QString detail;
    if (SecretStore::purgePlaintextConfigKeys(legacyConfigKeys(), &detail))
        return;

    qCritical("CryptoManager: KWallet holds your private keys, but the plaintext copy "
              "could NOT be deleted from the config file: %s. Anyone who can read that "
              "file can impersonate you - delete the identity_priv_b64 and dh_priv_b64 "
              "entries by hand.",
              qUtf8Printable(detail));
    reportPlaintextKeysLeft(detail);
}

// The config file can be carrying a different key pair than the wallet: a run
// where the wallet was unreachable generated a throwaway identity, or the user
// restored an old config file. That plaintext still has to go, but deleting the
// only copy of an identity the user might want back is not a fix, so it moves
// into the wallet first. False means do not delete anything yet.
bool CryptoManager::stashSupersededPlaintextKeys()
{
    QString legacyId;
    QString legacyDh;
    {
        QSettings settings;
        legacyId = settings.value(legacyConfigKeys().at(0)).toString();
        legacyDh = settings.value(legacyConfigKeys().at(1)).toString();
    }
    if (legacyId.isEmpty() && legacyDh.isEmpty())
        return true; // nothing in the file, so nothing to preserve or delete

    // A read that fails leaves the value empty, which compares as "not the pair
    // in the file" - the safe answer, since that path preserves before deleting.
    QString walletId;
    QString walletDh;
    SecretStore::read(identityWalletKey(), &walletId);
    SecretStore::read(dhWalletKey(), &walletDh);
    if (legacyId == walletId && legacyDh == walletDh)
        return true; // the wallet already holds exactly this pair

    if ((!legacyId.isEmpty()
         && !SecretStore::write(supersededWalletKey(identityWalletKey()), legacyId))
        || (!legacyDh.isEmpty()
            && !SecretStore::write(supersededWalletKey(dhWalletKey()), legacyDh))) {
        qCritical("CryptoManager: the config file holds an identity that KWallet does not, "
                  "and it could not be copied into the wallet (%s) - leaving the plaintext "
                  "alone rather than destroying the only copy of it.",
                  qUtf8Printable(SecretStore::lastError()));
        reportPlaintextKeysLeft(SecretStore::lastError());
        return false;
    }

    qWarning("CryptoManager: the config file held a different identity than the one in use; "
             "it was copied into KWallet as %s before the plaintext was deleted.",
             qUtf8Printable(supersededWalletKey(identityWalletKey())));
    return true;
}

void CryptoManager::reportPlaintextKeysLeft(const QString &reason)
{
    // Queued: this runs from the constructor, where nothing is connected yet.
    // QML sets its connections up while the engine loads, which is before the
    // event loop starts, so a zero timer still reaches the interface.
    QTimer::singleShot(0, this, [this, reason]() {
        Q_EMIT plaintextKeysLeftInConfig(reason);
    });
}

bool CryptoManager::loadStoredKeys()
{
    QString idB64;
    QString dhB64;
    if (!SecretStore::read(identityWalletKey(), &idB64)
        || !SecretStore::read(dhWalletKey(), &dhB64)
        || idB64.isEmpty() || dhB64.isEmpty()) {
        // nothing in the wallet: either a first run, or an older build that
        // kept the keys in QSettings
        if (!migrateLegacyKeys(&idB64, &dhB64))
            return false;
    } else {
        // The wallet is the only copy we use from here on, so anything left in
        // the config file is pure liability. Checked on every start because an
        // earlier run may have filled the wallet and then failed to rewrite
        // the file.
        dropLegacyPlaintextKeys();
    }

    const QByteArray idRaw = QByteArray::fromBase64(idB64.toLatin1());
    const QByteArray dhRaw = QByteArray::fromBase64(dhB64.toLatin1());

    // Ed25519/X25519 private keys are always exactly 32 raw bytes - guard
    // against a truncated/corrupted stored entry producing a garbage key.
    if (idRaw.size() != 32 || dhRaw.size() != 32)
        return false;

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

bool CryptoManager::generateAndStoreKeys()
{
    EVP_PKEY_CTX *idCtx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    if (!idCtx)
        return false;
    if (EVP_PKEY_keygen_init(idCtx) != 1 || EVP_PKEY_keygen(idCtx, &m_identityPriv) != 1) {
        EVP_PKEY_CTX_free(idCtx);
        return false;
    }
    EVP_PKEY_CTX_free(idCtx);

    EVP_PKEY_CTX *dhCtx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    if (!dhCtx) {
        EVP_PKEY_free(m_identityPriv);
        m_identityPriv = nullptr;
        return false;
    }
    if (EVP_PKEY_keygen_init(dhCtx) != 1 || EVP_PKEY_keygen(dhCtx, &m_dhPriv) != 1) {
        EVP_PKEY_CTX_free(dhCtx);
        EVP_PKEY_free(m_identityPriv);
        m_identityPriv = nullptr;
        return false;
    }
    EVP_PKEY_CTX_free(dhCtx);

    size_t len = 0;
    if (EVP_PKEY_get_raw_private_key(m_identityPriv, nullptr, &len) != 1 || len == 0)
        return false;
    QByteArray idRaw(int(len), 0);
    if (EVP_PKEY_get_raw_private_key(m_identityPriv, reinterpret_cast<unsigned char *>(idRaw.data()), &len) != 1)
        return false;

    len = 0;
    if (EVP_PKEY_get_raw_private_key(m_dhPriv, nullptr, &len) != 1 || len == 0)
        return false;
    QByteArray dhRaw(int(len), 0);
    if (EVP_PKEY_get_raw_private_key(m_dhPriv, reinterpret_cast<unsigned char *>(dhRaw.data()), &len) != 1)
        return false;

    // A wallet we cannot reach is not a reason to refuse to run, but it is a
    // reason to say so: the keypair above works for this session and then goes
    // away, so peers will see a new fingerprint next time.
    if (!SecretStore::write(identityWalletKey(), QString::fromLatin1(idRaw.toBase64()))
        || !SecretStore::write(dhWalletKey(), QString::fromLatin1(dhRaw.toBase64()))) {
        qCritical("CryptoManager: could not store the identity keys in KWallet (%s). "
                  "Running with a throwaway identity for this session - it is NOT "
                  "written to disk in plain text.",
                  qUtf8Printable(SecretStore::lastError()));
        return true;
    }

    // Reached when the legacy pair was unusable (only one half present, or
    // corrupt), so this identity replaces it. The unusable half is still
    // readable key material and buys the user nothing now.
    dropLegacyPlaintextKeys();
    return true;
}

// Handshake
QJsonObject CryptoManager::handshakePayload() const
{
    QJsonObject payload;
    payload[QStringLiteral("dh_pub")] = QString::fromLatin1(m_dhPubBytes.toBase64());
    payload[QStringLiteral("id_pub")] = QString::fromLatin1(m_identityPubBytes.toBase64());
    payload[QStringLiteral("dh_pub_sig")] = QString::fromLatin1(m_dhPubSig.toBase64());
    return payload;
}

bool CryptoManager::processHandshake(const QString &peerIp, const QJsonObject &data)
{
    const QByteArray peerDhBytes = QByteArray::fromBase64(data.value(QStringLiteral("dh_pub")).toString().toLatin1());
    const QByteArray peerIdBytes = QByteArray::fromBase64(data.value(QStringLiteral("id_pub")).toString().toLatin1());
    const QByteArray peerDhSig = QByteArray::fromBase64(data.value(QStringLiteral("dh_pub_sig")).toString().toLatin1());
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

    // Trust on first use. Presence is unauthenticated by design, since it is
    // the packet that carries the handshake, so without this any host claiming
    // a known peer's IP could hand us its own identity key and take over the
    // session with no prompt. The first key seen for an IP is the one that
    // stays; a different one later is either a MITM or a reinstall, and only
    // the user can tell those apart.
    const auto pinned = m_peerIdPub.constFind(peerIp);
    if (pinned != m_peerIdPub.constEnd() && *pinned != peerIdBytes) {
        // presence repeats every couple of seconds, so warn once per offending
        // key instead of on every packet
        if (m_warnedIdPub.value(peerIp) != peerIdBytes) {
            m_warnedIdPub[peerIp] = peerIdBytes;
            Q_EMIT peerIdentityChanged(peerIp, bytesToFingerprint(*pinned),
                                       bytesToFingerprint(peerIdBytes));
        }
        EVP_PKEY_free(peerIdPub);
        return false; // the session we already had stays live and usable
    }

    EVP_PKEY *peerDhPub = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr,
        reinterpret_cast<const unsigned char *>(peerDhBytes.constData()), peerDhBytes.size());
    if (!peerDhPub) {
        EVP_PKEY_free(peerIdPub);
        return false;
    }

    EVP_PKEY_CTX *dctx = EVP_PKEY_CTX_new(m_dhPriv, nullptr);
    if (!dctx) {
        EVP_PKEY_free(peerDhPub);
        EVP_PKEY_free(peerIdPub);
        return false;
    }

    // a failed derive leaves sharedSecret full of zeroes, and the session key
    // built from it would then be the same on every machine
    size_t secretLen = 0;
    QByteArray sharedSecret;
    bool derived = EVP_PKEY_derive_init(dctx) == 1
                   && EVP_PKEY_derive_set_peer(dctx, peerDhPub) == 1
                   && EVP_PKEY_derive(dctx, nullptr, &secretLen) == 1
                   && secretLen > 0;
    if (derived) {
        sharedSecret.resize(int(secretLen));
        derived = EVP_PKEY_derive(dctx, reinterpret_cast<unsigned char *>(sharedSecret.data()), &secretLen) == 1;
        if (derived)
            sharedSecret.resize(int(secretLen));
    }
    EVP_PKEY_CTX_free(dctx);
    EVP_PKEY_free(peerDhPub);

    if (!derived) {
        EVP_PKEY_free(peerIdPub);
        return false;
    }

    const QByteArray sessionKey = hkdfSha256(sharedSecret, QByteArrayLiteral("-v2-session"), kKeyLen);
    if (sessionKey.size() != kKeyLen) {
        EVP_PKEY_free(peerIdPub);
        return false;
    }

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

// HKDF-SHA256
QByteArray CryptoManager::hkdfSha256(const QByteArray &secret, const QByteArray &info, int outLen)
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!ctx)
        return {};

    // RFC 5869 default salt (no salt provided) = HashLen zero bytes.
    const QByteArray zeroSalt(32, 0);

    QByteArray out(outLen, 0);
    size_t len = size_t(outLen);
    // an unchecked failure here returns all zeroes, which downstream code
    // cannot tell apart from a real key
    const bool ok = EVP_PKEY_derive_init(ctx) == 1
                    && EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) == 1
                    && EVP_PKEY_CTX_set1_hkdf_salt(ctx, reinterpret_cast<const unsigned char *>(zeroSalt.constData()), zeroSalt.size()) == 1
                    && EVP_PKEY_CTX_set1_hkdf_key(ctx, reinterpret_cast<const unsigned char *>(secret.constData()), secret.size()) == 1
                    && EVP_PKEY_CTX_add1_hkdf_info(ctx, reinterpret_cast<const unsigned char *>(info.constData()), info.size()) == 1
                    && EVP_PKEY_derive(ctx, reinterpret_cast<unsigned char *>(out.data()), &len) == 1;
    EVP_PKEY_CTX_free(ctx);

    if (!ok || len != size_t(outLen))
        return {};
    return out;
}

// AES-256-GCM
QByteArray CryptoManager::gcmEncrypt(const QByteArray &key, const QByteArray &plaintext)
{
    const QByteArray nonce = randomBytes(kNonceLen);
    if (nonce.size() != kNonceLen)
        return {}; // no nonce, no encryption - reusing a fixed one breaks GCM outright

    QByteArray ciphertext(plaintext.size(), 0);
    QByteArray tag(kTagLen, 0);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return {};

    int outLen = 0;
    int finalLen = 0;
    const bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
                    && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceLen, nullptr) == 1
                    && EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                           reinterpret_cast<const unsigned char *>(key.constData()),
                           reinterpret_cast<const unsigned char *>(nonce.constData())) == 1
                    && EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char *>(ciphertext.data()), &outLen,
                           reinterpret_cast<const unsigned char *>(plaintext.constData()), plaintext.size()) == 1
                    && EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(ciphertext.data()) + outLen, &finalLen) == 1
                    && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagLen, tag.data()) == 1;
    EVP_CIPHER_CTX_free(ctx);

    if (!ok)
        return {};

    return nonce + ciphertext.left(outLen + finalLen) + tag; // matches legacy wire layout
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
    if (!ctx)
        return false;

    int outLen = 0;
    int finalLen = 0;
    // a skipped setup step means the tag is never actually checked, so setup
    // failures have to be as fatal as a tag mismatch
    const bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
                    && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceLen, nullptr) == 1
                    && EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                           reinterpret_cast<const unsigned char *>(key.constData()),
                           reinterpret_cast<const unsigned char *>(nonce.constData())) == 1
                    && EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(plaintext.data()), &outLen,
                           reinterpret_cast<const unsigned char *>(ciphertext.constData()), ciphertext.size()) == 1
                    && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagLen,
                           const_cast<char *>(tag.constData())) == 1
                    && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(plaintext.data()) + outLen, &finalLen) == 1;
    EVP_CIPHER_CTX_free(ctx);

    if (!ok)
        return false; // tag mismatch - tampered or wrong key

    *outPlain = plaintext.left(outLen + finalLen);
    return true;
}

// PBKDF2 passphrase keys (cached)
QByteArray CryptoManager::deriveKey(const QString &passphrase, const QByteArray &salt) const
{
    const QString cacheKey = passphrase + QLatin1Char('|') + QString::fromLatin1(salt.toHex());
    if (m_passphraseKeyCache.contains(cacheKey))
        return m_passphraseKeyCache.value(cacheKey);

    QByteArray key(kKeyLen, 0);
    const QByteArray pass = passphrase.toUtf8();
    // an all-zero key would be shared by every peer whose derivation failed
    if (PKCS5_PBKDF2_HMAC(pass.constData(), pass.size(),
            reinterpret_cast<const unsigned char *>(salt.constData()), salt.size(),
            kKdfIters, EVP_sha256(), kKeyLen, reinterpret_cast<unsigned char *>(key.data())) != 1) {
        return {};
    }

    // Cheap unbounded-growth guard: each PBKDF2 derivation is expensive
    // (480k iterations), which is why we cache it - but a long session
    // cycling through many distinct passphrases must not grow this forever.
    if (m_passphraseKeyCache.size() >= kMaxPassphraseCacheSize)
        m_passphraseKeyCache.clear();

    m_passphraseKeyCache[cacheKey] = key;
    return key;
}

// Packet HMAC
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
    // no key means nothing was verified, so the answer is no. deciding which
    // pre-session packets may pass anyway is the caller's job, not ours.
    if (!m_sessionKeys.contains(peerIp))
        return false;

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

// Replay protection
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

// Rate limiting
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

// Public encrypt/decrypt
// Wire format (base64 after the "KNC1:" tag):
//   type[1] + payload
//   0x01 = AES-GCM with ECDH session key  (payload = nonce+ciphertext+tag)
//   0x02 = AES-GCM with PBKDF2 passphrase key (payload = salt[32]+nonce+ciphertext+tag)
QString CryptoManager::encrypt(const QString &plaintext, const QString &passphrase,
                               const QString &peerIp) const
{
    const QByteArray data = plaintext.toUtf8();

    // an empty return says "could not seal this", never "here it is in the
    // clear", so a broken salt or cipher cannot leak the message
    QByteArray wire;
    if (!peerIp.isEmpty() && m_sessionKeys.contains(peerIp)) {
        const QByteArray sealed = gcmEncrypt(m_sessionKeys.value(peerIp), data);
        if (sealed.isEmpty())
            return QString();
        wire.append(char(0x01));
        wire.append(sealed);
    } else if (!passphrase.isEmpty()) {
        const QByteArray salt = randomBytes(kSaltLen);
        if (salt.size() != kSaltLen)
            return QString();
        const QByteArray key = deriveKey(passphrase, salt);
        if (key.size() != kKeyLen)
            return QString();
        const QByteArray sealed = gcmEncrypt(key, data);
        if (sealed.isEmpty())
            return QString();
        wire.append(char(0x02));
        wire.append(salt);
        wire.append(sealed);
    } else {
        return plaintext; // nothing to encrypt with
    }

    return QStringLiteral("KNC1:") + QString::fromLatin1(wire.toBase64());
}

QString CryptoManager::decrypt(const QString &ciphertext, const QString &passphrase,
                               const QString &peerIp) const
{
    // whether this text was supposed to arrive sealed is decided by the keys
    // we hold, not by anything the sender put in the packet. otherwise
    // stripping the tag is all it takes to downgrade a session to cleartext.
    const bool expectSealed = (!peerIp.isEmpty() && m_sessionKeys.contains(peerIp))
                              || !passphrase.isEmpty();

    if (!ciphertext.startsWith(QStringLiteral("KNC1:"))) {
        if (expectSealed)
            return QStringLiteral("[decrypt error: cleartext on a keyed channel]");
        return ciphertext; // no key on this channel anyway - pass through
    }

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
            if (key.size() == kKeyLen)
                ok = gcmDecrypt(key, payload.mid(kSaltLen), &plain);
        }
    }

    if (!ok)
        return QStringLiteral("[decrypt error: invalid key or tampered packet]");

    return QString::fromUtf8(plain);
}

// Raw byte encryption (voice)
QByteArray CryptoManager::encryptBytes(const QString &peerIp, const QByteArray &plaintext) const
{
    // empty means "not encrypted", and the caller has to drop the frame. voice
    // in the clear is not a graceful degradation, it is the bug.
    if (!m_sessionKeys.contains(peerIp))
        return {};

    return gcmEncrypt(m_sessionKeys.value(peerIp), plaintext); // nonce+ciphertext+tag
}

bool CryptoManager::decryptBytes(const QString &peerIp, const QByteArray &data, QByteArray *outPlain) const
{
    if (!m_sessionKeys.contains(peerIp))
        return false; // unkeyed frames are not audio we are willing to play

    return gcmDecrypt(m_sessionKeys.value(peerIp), data, outPlain);
}

} // namespace koutnet
