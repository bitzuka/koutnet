// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Security Engine v2 (C++/Qt6 port)
#include "CryptoManager.h"
#include "SecretStore.h"
#include "koutnet_crypto_debug.h"

#include <KLocalizedString>

#include <QCryptographicHash>
#include <QDateTime>
#include <QSettings>
#include <QTimer>

#include <algorithm> // nth_element, for the replay cache eviction
#include <cmath>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

namespace koutnet
{

namespace
{

double nowEpoch()
{
    return QDateTime::currentMSecsSinceEpoch() / 1000.0;
}

// Overwrite a buffer of key material before it is freed. QByteArray and QString
// only release the block, so a private key, a shared secret or a passphrase
// stays legible in the heap until something else happens to reuse the page -
// and a core dump, a swap file or a hibernation image is where it turns up.
//
// data() detaches first, which means this only wipes the copy in hand: it is
// correct for a buffer nothing else holds a reference to, which is what every
// call site below is. Where a buffer is deliberately handed on to live longer
// (a session key going into m_sessionKeys) it is moved rather than copied, and
// wiped from there instead.
void cleanse(QByteArray &buf)
{
    if (!buf.isEmpty())
        OPENSSL_cleanse(buf.data(), size_t(buf.size()));
}

void cleanse(QString &str)
{
    if (!str.isEmpty())
        OPENSSL_cleanse(str.data(), size_t(str.size()) * sizeof(QChar));
}

// Same thing at every exit path of a function, which is the part that gets
// forgotten - loadStoredKeys() alone returns from five places.
template<typename T>
class Wiper
{
public:
    explicit Wiper(T &buf)
        : m_buf(buf)
    {
    }
    ~Wiper()
    {
        cleanse(m_buf);
    }
    Wiper(const Wiper &) = delete;
    Wiper &operator=(const Wiper &) = delete;

private:
    T &m_buf;
};

// Wallet entry names. The QSettings paths they replaced are spelled out in
// migrateLegacyKeys(). An empty scope is the application's own identity, so
// the names it has always used stay exactly as they were.
QString identityWalletKey(const QString &scope)
{
    return scope.isEmpty() ? QStringLiteral("identity_priv_b64") : QStringLiteral("identity_priv_b64_") + scope;
}

QString dhWalletKey(const QString &scope)
{
    return scope.isEmpty() ? QStringLiteral("dh_priv_b64") : QStringLiteral("dh_priv_b64_") + scope;
}

// Where builds before the wallet kept the same two keys, in clear text.
QStringList legacyConfigKeys(const QString &scope)
{
    if (scope.isEmpty()) {
        return {QStringLiteral("security/identity_priv_b64"), QStringLiteral("security/dh_priv_b64")};
    }
    return {QStringLiteral("security/%1_identity_priv_b64").arg(scope), QStringLiteral("security/%1_dh_priv_b64").arg(scope)};
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
        if (i)
            out += QLatin1Char(' ');
        out += QString::fromLatin1(h.mid(i, 4)).toUpper();
    }
    return out;
}

} // namespace

CryptoManager::CryptoManager(QObject *parent)
    : CryptoManager(QString(), parent)
{
}

CryptoManager::CryptoManager(const QString &storageScope, QObject *parent)
    : QObject(parent)
    , m_storageScope(storageScope)
{
    m_valid = initKeypairs();
    if (!m_valid) {
        qCCritical(KOUTNET_LOG_CRYPTO,
                   "failed to initialize identity/DH keypairs - encryption is unavailable "
                   "for this session. Check isValid() before relying on "
                   "encrypt()/handshakePayload().");
    }
}

CryptoManager::~CryptoManager()
{
    if (m_identityPriv)
        EVP_PKEY_free(m_identityPriv);
    if (m_dhPriv)
        EVP_PKEY_free(m_dhPriv);

    // The session keys and the cached passphrase keys live as long as this
    // object does, so this is the exit path for both of them.
    for (auto it = m_sessionKeys.begin(); it != m_sessionKeys.end(); ++it)
        cleanse(*it);
    for (auto it = m_passphraseKeyCache.begin(); it != m_passphraseKeyCache.end(); ++it)
        cleanse(*it);
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
    if (ok && EVP_DigestSign(mdctx, nullptr, &sigLen, reinterpret_cast<const unsigned char *>(m_dhPubBytes.constData()), m_dhPubBytes.size()) != 1) {
        ok = false;
    }

    if (ok) {
        m_dhPubSig.resize(int(sigLen));
        if (EVP_DigestSign(mdctx,
                           reinterpret_cast<unsigned char *>(m_dhPubSig.data()),
                           &sigLen,
                           reinterpret_cast<const unsigned char *>(m_dhPubBytes.constData()),
                           m_dhPubBytes.size())
            != 1) {
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
    // Both hold a private key in base64. The caller gets a copy of each and
    // wipes those in turn.
    Wiper wipeId(legacyId);
    Wiper wipeDh(legacyDh);
    {
        // Scoped so the instance that read the plaintext is gone before the one
        // that deletes it exists. Both would share a single in-memory copy of
        // the file anyway, but nothing here needs that to be true.
        // toString() and not toByteArray(): the old build stored these with a
        // QByteArray overload, so the file says @ByteArray(...) - QSettings
        // hands either type back as the same base64 text.
        QSettings settings;
        legacyId = settings.value(legacyConfigKeys(m_storageScope).at(0)).toString();
        legacyDh = settings.value(legacyConfigKeys(m_storageScope).at(1)).toString();
    }
    if (legacyId.isEmpty() || legacyDh.isEmpty())
        return false;

    *outIdentityB64 = legacyId;
    *outDhB64 = legacyDh;

    // Only drop the plaintext copy once the wallet has both halves, otherwise a
    // wallet that is merely unreachable today would cost the user their identity.
    if (!SecretStore::write(identityWalletKey(m_storageScope), legacyId) || !SecretStore::write(dhWalletKey(m_storageScope), legacyDh)) {
        qCCritical(KOUTNET_LOG_CRYPTO,
                   "your private keys are still stored in plain text in the config file "
                   "because they could not be moved into KWallet (%s). Start kwalletd and "
                   "restart KOutNet.",
                   qUtf8Printable(SecretStore::lastError()));
        reportPlaintextKeysLeft(SecretStore::lastError());
        return true;
    }

    qCInfo(KOUTNET_LOG_CRYPTO, "copied the identity keys into KWallet");
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
    if (SecretStore::purgePlaintextConfigKeys(legacyConfigKeys(m_storageScope), &detail))
        return;

    qCCritical(KOUTNET_LOG_CRYPTO,
               "KWallet holds your private keys, but the plaintext copy could NOT be "
               "deleted from the config file: %s. Anyone who can read that file can "
               "impersonate you - delete the identity_priv_b64 and dh_priv_b64 entries "
               "by hand.",
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
    QString walletId;
    QString walletDh;
    // Four private keys in base64 by the end of this function.
    Wiper wipeId(legacyId);
    Wiper wipeDh(legacyDh);
    Wiper wipeWalletId(walletId);
    Wiper wipeWalletDh(walletDh);
    {
        QSettings settings;
        legacyId = settings.value(legacyConfigKeys(m_storageScope).at(0)).toString();
        legacyDh = settings.value(legacyConfigKeys(m_storageScope).at(1)).toString();
    }
    if (legacyId.isEmpty() && legacyDh.isEmpty())
        return true; // nothing in the file, so nothing to preserve or delete

    // A read that fails leaves the value empty, which compares as "not the pair
    // in the file" - the safe answer, since that path preserves before deleting.
    SecretStore::read(identityWalletKey(m_storageScope), &walletId);
    SecretStore::read(dhWalletKey(m_storageScope), &walletDh);
    if (legacyId == walletId && legacyDh == walletDh)
        return true; // the wallet already holds exactly this pair

    if ((!legacyId.isEmpty() && !SecretStore::write(supersededWalletKey(identityWalletKey(m_storageScope)), legacyId))
        || (!legacyDh.isEmpty() && !SecretStore::write(supersededWalletKey(dhWalletKey(m_storageScope)), legacyDh))) {
        qCCritical(KOUTNET_LOG_CRYPTO,
                   "the config file holds an identity that KWallet does not, and it could "
                   "not be copied into the wallet (%s) - leaving the plaintext alone rather "
                   "than destroying the only copy of it.",
                   qUtf8Printable(SecretStore::lastError()));
        reportPlaintextKeysLeft(SecretStore::lastError());
        return false;
    }

    qCWarning(KOUTNET_LOG_CRYPTO,
              "the config file held a different identity than the one in use; it was copied "
              "into KWallet as %s before the plaintext was deleted.",
              qUtf8Printable(supersededWalletKey(identityWalletKey(m_storageScope))));
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
    // Base64 of the two private keys, and below them the raw bytes themselves.
    // Five returns out of this function, hence the guards rather than a call
    // per exit.
    Wiper wipeIdB64(idB64);
    Wiper wipeDhB64(dhB64);
    if (!SecretStore::read(identityWalletKey(m_storageScope), &idB64) || !SecretStore::read(dhWalletKey(m_storageScope), &dhB64) || idB64.isEmpty()
        || dhB64.isEmpty()) {
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

    QByteArray idRaw = QByteArray::fromBase64(idB64.toLatin1());
    QByteArray dhRaw = QByteArray::fromBase64(dhB64.toLatin1());
    Wiper wipeIdRaw(idRaw);
    Wiper wipeDhRaw(dhRaw);

    // Ed25519/X25519 private keys are always exactly 32 raw bytes - guard
    // against a truncated/corrupted stored entry producing a garbage key.
    if (idRaw.size() != 32 || dhRaw.size() != 32)
        return false;

    m_identityPriv = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char *>(idRaw.constData()), idRaw.size());
    m_dhPriv = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, reinterpret_cast<const unsigned char *>(dhRaw.constData()), dhRaw.size());

    if (!m_identityPriv || !m_dhPriv) {
        if (m_identityPriv) {
            EVP_PKEY_free(m_identityPriv);
            m_identityPriv = nullptr;
        }
        if (m_dhPriv) {
            EVP_PKEY_free(m_dhPriv);
            m_dhPriv = nullptr;
        }
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

    // The raw private keys, and then the base64 of each. Both get wiped whichever
    // way this function ends; the copies KWallet takes are its problem.
    size_t len = 0;
    QByteArray idRaw;
    QByteArray dhRaw;
    QString idB64;
    QString dhB64;
    Wiper wipeIdRaw(idRaw);
    Wiper wipeDhRaw(dhRaw);
    Wiper wipeIdB64(idB64);
    Wiper wipeDhB64(dhB64);

    if (EVP_PKEY_get_raw_private_key(m_identityPriv, nullptr, &len) != 1 || len == 0)
        return false;
    idRaw.resize(int(len));
    if (EVP_PKEY_get_raw_private_key(m_identityPriv, reinterpret_cast<unsigned char *>(idRaw.data()), &len) != 1)
        return false;

    len = 0;
    if (EVP_PKEY_get_raw_private_key(m_dhPriv, nullptr, &len) != 1 || len == 0)
        return false;
    dhRaw.resize(int(len));
    if (EVP_PKEY_get_raw_private_key(m_dhPriv, reinterpret_cast<unsigned char *>(dhRaw.data()), &len) != 1)
        return false;

    idB64 = QString::fromLatin1(idRaw.toBase64());
    dhB64 = QString::fromLatin1(dhRaw.toBase64());

    // A wallet we cannot reach is not a reason to refuse to run, but it is a
    // reason to say so: the keypair above works for this session and then goes
    // away, so peers will see a new fingerprint next time.
    if (!SecretStore::write(identityWalletKey(m_storageScope), idB64) || !SecretStore::write(dhWalletKey(m_storageScope), dhB64)) {
        qCCritical(KOUTNET_LOG_CRYPTO,
                   "could not store the identity keys in KWallet (%s). Running with a "
                   "throwaway identity for this session - it is NOT written to disk in "
                   "plain text.",
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

// Peer identity
// The handle every map in here is keyed on. A digest and not the key itself, so
// it is short enough to travel in a packet and to read in a log line, and it
// cannot be confused with an address by anything that handles both.
QString CryptoManager::identityIdFor(const QByteArray &idPubRaw)
{
    if (idPubRaw.isEmpty())
        return QString();
    return QString::fromLatin1(QCryptographicHash::hash(idPubRaw, QCryptographicHash::Sha256).toHex());
}

QString CryptoManager::ownIdentityId() const
{
    return identityIdFor(m_identityPubBytes);
}

QString CryptoManager::identityForAddress(const QString &address) const
{
    return m_addressToId.value(address);
}

QStringList CryptoManager::addressesFor(const QString &peerId) const
{
    return m_idToAddresses.value(resolveIdentity(peerId));
}

QString CryptoManager::resolveIdentity(const QString &peerRef) const
{
    if (peerRef.isEmpty())
        return QString();
    // An identity id names itself. Checked against the pin rather than against
    // the session, so a peer whose handshake is still in flight still resolves.
    if (m_peerIdPub.contains(peerRef))
        return peerRef;
    return m_addressToId.value(peerRef);
}

void CryptoManager::noteObservedAddress(const QString &peerId, const QString &address)
{
    if (peerId.isEmpty() || address.isEmpty())
        return;

    // An address belongs to one identity at a time. Whoever proved a handshake
    // from it most recently is the one it points at, and the peer that used to
    // be there keeps its session either way - only the shortcut moves.
    const QString previous = m_addressToId.value(address);
    if (previous != peerId && !previous.isEmpty()) {
        QStringList &theirs = m_idToAddresses[previous];
        theirs.removeAll(address);
        if (theirs.isEmpty())
            m_idToAddresses.remove(previous);
    }
    m_addressToId[address] = peerId;

    QStringList &addresses = m_idToAddresses[peerId];
    addresses.removeAll(address);
    addresses.prepend(address);
    while (addresses.size() > kMaxPeerAddresses) {
        const QString dropped = addresses.takeLast();
        // Only if it still points here: a later handshake may have moved it.
        if (m_addressToId.value(dropped) == peerId)
            m_addressToId.remove(dropped);
    }
}

bool CryptoManager::processHandshake(const QString &observedAddress, const QJsonObject &data)
{
    return processHandshakeFrom(observedAddress, data) == HandshakeOutcome::Established;
}

CryptoManager::HandshakeOutcome CryptoManager::processHandshakeFrom(const QString &observedAddress, const QJsonObject &data, QString *outPeerId)
{
    const QByteArray peerDhBytes = QByteArray::fromBase64(data.value(QStringLiteral("dh_pub")).toString().toLatin1());
    const QByteArray peerIdBytes = QByteArray::fromBase64(data.value(QStringLiteral("id_pub")).toString().toLatin1());
    const QByteArray peerDhSig = QByteArray::fromBase64(data.value(QStringLiteral("dh_pub_sig")).toString().toLatin1());
    if (peerDhBytes.isEmpty() || peerIdBytes.isEmpty() || peerDhSig.isEmpty())
        return HandshakeOutcome::Refused;

    EVP_PKEY *peerIdPub =
        EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char *>(peerIdBytes.constData()), peerIdBytes.size());
    if (!peerIdPub)
        return HandshakeOutcome::Refused;

    // Verify: peer's identity key signed their DH key.
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        // out of memory, and passing the null on to EVP_DigestVerifyInit would
        // be a crash rather than a refused handshake
        EVP_PKEY_free(peerIdPub);
        return HandshakeOutcome::Refused;
    }
    const int verifyRc = EVP_DigestVerifyInit(mdctx, nullptr, nullptr, nullptr, peerIdPub) != 1
        ? 0
        : EVP_DigestVerify(mdctx,
                           reinterpret_cast<const unsigned char *>(peerDhSig.constData()),
                           peerDhSig.size(),
                           reinterpret_cast<const unsigned char *>(peerDhBytes.constData()),
                           peerDhBytes.size());
    EVP_MD_CTX_free(mdctx);
    if (verifyRc != 1) {
        EVP_PKEY_free(peerIdPub);
        return HandshakeOutcome::Refused;
    }

    // Past this line the payload has proved itself: whoever wrote it holds the
    // private half of id_pub, whatever address it came from. This is the only
    // thing in the packet worth keying anything on.
    const QString peerId = identityIdFor(peerIdBytes);
    if (outPeerId)
        *outPeerId = peerId;

    // Trust on first use, on the identity. The id is a digest of the key, so a
    // pin that disagrees with the key that hashed to it means we hashed
    // something else - unreachable, and cheaper to refuse than to reason about.
    const auto pinned = m_peerIdPub.constFind(peerId);
    if (pinned != m_peerIdPub.constEnd() && *pinned != peerIdBytes) {
        EVP_PKEY_free(peerIdPub);
        return HandshakeOutcome::Refused;
    }

    // Someone else is already at this address and still has a live session. The
    // identity above is not in doubt, so this is not a trust decision - but the
    // address is how the interface files a peer, and handing a stranger the
    // slot of a peer the user is talking to is the part of a takeover the user
    // would see. Refuse the shortcut and say so; the newcomer can have its own
    // session as soon as it turns up somewhere that is not taken.
    const QString sitting = m_addressToId.value(observedAddress);
    if (!sitting.isEmpty() && sitting != peerId && m_sessionKeys.contains(sitting)) {
        // presence repeats every couple of seconds, so warn once per offending
        // key instead of on every packet
        if (m_warnedIdPub.value(observedAddress) != peerIdBytes) {
            m_warnedIdPub[observedAddress] = peerIdBytes;
            Q_EMIT peerIdentityChanged(observedAddress, bytesToFingerprint(m_peerIdPub.value(sitting)), bytesToFingerprint(peerIdBytes));
        }
        EVP_PKEY_free(peerIdPub);
        return HandshakeOutcome::AddressTaken; // the session we already had stays live and usable
    }

    EVP_PKEY *peerDhPub =
        EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, reinterpret_cast<const unsigned char *>(peerDhBytes.constData()), peerDhBytes.size());
    if (!peerDhPub) {
        EVP_PKEY_free(peerIdPub);
        return HandshakeOutcome::Refused;
    }

    EVP_PKEY_CTX *dctx = EVP_PKEY_CTX_new(m_dhPriv, nullptr);
    if (!dctx) {
        EVP_PKEY_free(peerDhPub);
        EVP_PKEY_free(peerIdPub);
        return HandshakeOutcome::Refused;
    }

    // a failed derive leaves sharedSecret full of zeroes, and the session key
    // built from it would then be the same on every machine
    size_t secretLen = 0;
    QByteArray sharedSecret;
    // The one buffer here that is worth reading out of a core dump: every
    // session key with this peer, for as long as neither side regenerates its
    // keypair, comes out of these bytes.
    Wiper wipeSecret(sharedSecret);
    bool derived =
        EVP_PKEY_derive_init(dctx) == 1 && EVP_PKEY_derive_set_peer(dctx, peerDhPub) == 1 && EVP_PKEY_derive(dctx, nullptr, &secretLen) == 1 && secretLen > 0;
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
        return HandshakeOutcome::Refused;
    }

    QByteArray sessionKey = hkdfSha256(sharedSecret, QByteArrayLiteral("-v2-session"), kKeyLen);
    if (sessionKey.size() != kKeyLen) {
        cleanse(sessionKey);
        EVP_PKEY_free(peerIdPub);
        return HandshakeOutcome::Refused;
    }

    // The key this replaces goes first: a repeat handshake with the same peer
    // would otherwise leave the previous one in the heap unwiped. Moved rather
    // than copied, so the hash ends up owning the only copy - the destructor is
    // where that one gets wiped.
    const auto existing = m_sessionKeys.find(peerId);
    if (existing != m_sessionKeys.end())
        cleanse(*existing);

    m_sessionKeys[peerId] = std::move(sessionKey);
    m_peerIdPub[peerId] = peerIdBytes;
    noteObservedAddress(peerId, observedAddress);
    EVP_PKEY_free(peerIdPub);
    return HandshakeOutcome::Established;
}

bool CryptoManager::hasSession(const QString &peerRef) const
{
    return m_sessionKeys.contains(resolveIdentity(peerRef));
}

QString CryptoManager::fingerprint() const
{
    return bytesToFingerprint(m_identityPubBytes);
}

QString CryptoManager::peerFingerprint(const QString &peerRef) const
{
    const QString peerId = resolveIdentity(peerRef);
    if (!m_peerIdPub.contains(peerId))
        return QStringLiteral("?");
    return bytesToFingerprint(m_peerIdPub.value(peerId));
}

SecurityLevel CryptoManager::securityLevel(const QString &peerRef, bool encryptionEnabled, bool hasPassphrase) const
{
    if (m_sessionKeys.contains(resolveIdentity(peerRef)))
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
    const bool ok = EVP_PKEY_derive_init(ctx) == 1 && EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) == 1
        && EVP_PKEY_CTX_set1_hkdf_salt(ctx, reinterpret_cast<const unsigned char *>(zeroSalt.constData()), zeroSalt.size()) == 1
        && EVP_PKEY_CTX_set1_hkdf_key(ctx, reinterpret_cast<const unsigned char *>(secret.constData()), secret.size()) == 1
        && EVP_PKEY_CTX_add1_hkdf_info(ctx, reinterpret_cast<const unsigned char *>(info.constData()), info.size()) == 1
        && EVP_PKEY_derive(ctx, reinterpret_cast<unsigned char *>(out.data()), &len) == 1;
    EVP_PKEY_CTX_free(ctx);

    if (!ok || len != size_t(outLen)) {
        // a partial derive leaves part of a real key in there
        cleanse(out);
        return {};
    }
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
        && EVP_EncryptInit_ex(ctx,
                              nullptr,
                              nullptr,
                              reinterpret_cast<const unsigned char *>(key.constData()),
                              reinterpret_cast<const unsigned char *>(nonce.constData()))
            == 1
        && EVP_EncryptUpdate(ctx,
                             reinterpret_cast<unsigned char *>(ciphertext.data()),
                             &outLen,
                             reinterpret_cast<const unsigned char *>(plaintext.constData()),
                             plaintext.size())
            == 1
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
        && EVP_DecryptInit_ex(ctx,
                              nullptr,
                              nullptr,
                              reinterpret_cast<const unsigned char *>(key.constData()),
                              reinterpret_cast<const unsigned char *>(nonce.constData()))
            == 1
        && EVP_DecryptUpdate(ctx,
                             reinterpret_cast<unsigned char *>(plaintext.data()),
                             &outLen,
                             reinterpret_cast<const unsigned char *>(ciphertext.constData()),
                             ciphertext.size())
            == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagLen, const_cast<char *>(tag.constData())) == 1
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
    QByteArray pass = passphrase.toUtf8();
    Wiper wipePass(pass);

    // A hash of the pair, not the pair itself. The cache key used to be the
    // passphrase with the salt appended, which kept the user's group passphrase
    // in plain text in this hash for the lifetime of the process - and unlike the
    // derived key it is the secret the user typed and probably reuses elsewhere.
    // The salt is fixed length, so salt-then-passphrase is unambiguous.
    QCryptographicHash tag(QCryptographicHash::Sha256);
    tag.addData(salt);
    tag.addData(pass);
    const QByteArray cacheKey = tag.result();

    const auto cached = m_passphraseKeyCache.constFind(cacheKey);
    if (cached != m_passphraseKeyCache.constEnd())
        return *cached;

    QByteArray key(kKeyLen, 0);
    // an all-zero key would be shared by every peer whose derivation failed
    if (PKCS5_PBKDF2_HMAC(pass.constData(),
                          pass.size(),
                          reinterpret_cast<const unsigned char *>(salt.constData()),
                          salt.size(),
                          kKdfIters,
                          EVP_sha256(),
                          kKeyLen,
                          reinterpret_cast<unsigned char *>(key.data()))
        != 1) {
        cleanse(key);
        return {};
    }

    // Cheap unbounded-growth guard: each PBKDF2 derivation is expensive
    // (480k iterations), which is why we cache it - but a long session
    // cycling through many distinct passphrases must not grow this forever.
    if (m_passphraseKeyCache.size() >= kMaxPassphraseCacheSize) {
        for (auto it = m_passphraseKeyCache.begin(); it != m_passphraseKeyCache.end(); ++it)
            cleanse(*it);
        m_passphraseKeyCache.clear();
    }

    // The caller gets a copy that shares this block, which is why the wipe for
    // it lives in the destructor rather than here.
    m_passphraseKeyCache[cacheKey] = key;
    return key;
}

// Packet HMAC
QString CryptoManager::signPacket(const QString &peerRef, const QByteArray &payload) const
{
    // constFind rather than value(): a copy of the session key would be one more
    // buffer holding it, and one more thing to remember to wipe.
    const auto key = m_sessionKeys.constFind(resolveIdentity(peerRef));
    if (key == m_sessionKeys.constEnd())
        return QString();

    unsigned char digest[32];
    unsigned int digestLen = 0;
    HMAC(EVP_sha256(), key->constData(), key->size(), reinterpret_cast<const unsigned char *>(payload.constData()), payload.size(), digest, &digestLen);

    const QString out = QString::fromLatin1(QByteArray(reinterpret_cast<char *>(digest), int(digestLen)).toBase64());
    OPENSSL_cleanse(digest, sizeof(digest));
    return out;
}

bool CryptoManager::verifyPacket(const QString &peerRef, const QByteArray &payload, const QString &sigB64) const
{
    // no key means nothing was verified, so the answer is no. deciding which
    // pre-session packets may pass anyway is the caller's job, not ours.
    const auto key = m_sessionKeys.constFind(resolveIdentity(peerRef));
    if (key == m_sessionKeys.constEnd())
        return false;

    unsigned char expected[32];
    unsigned int expectedLen = 0;
    HMAC(EVP_sha256(), key->constData(), key->size(), reinterpret_cast<const unsigned char *>(payload.constData()), payload.size(), expected, &expectedLen);

    const QByteArray given = QByteArray::fromBase64(sigB64.toLatin1());
    const bool ok = given.size() == int(expectedLen) && CRYPTO_memcmp(expected, given.constData(), expectedLen) == 0;
    OPENSSL_cleanse(expected, sizeof(expected));
    return ok;
}

// Replay protection
bool CryptoManager::checkReplay(const QString &peerRef, const QString &nonceHex, double ts)
{
    const double now = nowEpoch();
    if (std::abs(now - ts) > kReplayWindowSec)
        return false;

    // The identity when there is one, so moving to another interface does not
    // hand the sender a fresh window to replay into. A stranger falls back to
    // whatever it was called, which is the address it came from.
    const QString peerId = resolveIdentity(peerRef);
    const QString bucketKey = peerId.isEmpty() ? peerRef : peerId;

    QHash<QString, SeenNonce> &bucket = m_seenNonces[bucketKey];
    if (bucket.contains(nonceHex))
        return false;

    const quint64 seq = ++m_nonceSeq;
    bucket[nonceHex] = SeenNonce{now, seq};
    m_nonceBucketTouched[bucketKey] = seq;

    for (auto it = bucket.begin(); it != bucket.end();) {
        if (now - it.value().ts > kNonceCacheTtlSec)
            it = bucket.erase(it);
        else
            ++it;
    }

    // A peer sending fresh nonces faster than the TTL retires them outruns the
    // sweep above, so the newest half is kept and the older half goes. Ordered
    // by arrival and not by the clock, because a flood puts thousands of these
    // in the same millisecond and every one of them would look equally old.
    // Forgetting an old nonce means a packet from that far back could be
    // replayed once, which is the lesser problem: the alternative is a bucket
    // that grows for as long as the flood lasts.
    if (bucket.size() > kMaxNoncesPerPeer) {
        QVector<quint64> seqs;
        seqs.reserve(bucket.size());
        for (auto it = bucket.cbegin(); it != bucket.cend(); ++it)
            seqs.append(it.value().seq);
        const qsizetype half = seqs.size() / 2;
        std::nth_element(seqs.begin(), seqs.begin() + half, seqs.end());
        const quint64 cutoff = seqs.at(half);
        for (auto it = bucket.begin(); it != bucket.end();) {
            if (it.value().seq < cutoff) // unique, so exactly the older half goes
                it = bucket.erase(it);
            else
                ++it;
        }
    }

    if (m_seenNonces.size() > kMaxNoncePeers)
        evictOldestNoncePeers();
    return true;
}

void CryptoManager::evictOldestNoncePeers()
{
    // One linear scan per eviction over at most kMaxNoncePeers entries. Under a
    // flood from spoofed source addresses this runs for every packet, so it does
    // not sort - it takes the least recently used bucket and drops it.
    while (m_seenNonces.size() > kMaxNoncePeers && !m_nonceBucketTouched.isEmpty()) {
        auto oldest = m_nonceBucketTouched.cbegin();
        for (auto it = m_nonceBucketTouched.cbegin(); it != m_nonceBucketTouched.cend(); ++it) {
            if (it.value() < oldest.value())
                oldest = it;
        }
        const QString key = oldest.key();
        m_nonceBucketTouched.remove(key);
        m_seenNonces.remove(key);
    }

    // A bucket with no recorded arrival cannot be ordered against the others, so
    // if any are left over the cap they go together. Unreachable unless the two
    // hashes fall out of step.
    if (m_seenNonces.size() > kMaxNoncePeers) {
        m_seenNonces.clear();
        m_nonceBucketTouched.clear();
    }
}

// Rate limiting
bool CryptoManager::checkRate(const QString &sourceAddress, int maxPerSec)
{
    const double now = nowEpoch();

    // Keyed on the source address, so a flood from spoofed ones grows this the
    // same way it grew the replay cache. A window nobody has added to for a
    // second is empty and worth nothing, so those are what go.
    if (m_rateCounters.size() > kMaxRatePeers) {
        for (auto it = m_rateCounters.begin(); it != m_rateCounters.end();) {
            if (it.value().isEmpty() || now - it.value().constLast() >= 1.0)
                it = m_rateCounters.erase(it);
            else
                ++it;
        }
    }

    QVector<double> &window = m_rateCounters[sourceAddress];

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
QString CryptoManager::encrypt(const QString &plaintext, const QString &passphrase, const QString &peerRef) const
{
    const QByteArray data = plaintext.toUtf8();

    // an empty return says "could not seal this", never "here it is in the
    // clear", so a broken salt or cipher cannot leak the message
    QByteArray wire;
    const auto sessionKey = m_sessionKeys.constFind(resolveIdentity(peerRef));
    if (sessionKey != m_sessionKeys.constEnd()) {
        const QByteArray sealed = gcmEncrypt(*sessionKey, data);
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

QString CryptoManager::decrypt(const QString &ciphertext, const QString &passphrase, const QString &peerRef) const
{
    // whether this text was supposed to arrive sealed is decided by the keys
    // we hold, not by anything the sender put in the packet. otherwise
    // stripping the tag is all it takes to downgrade a session to cleartext.
    const QString peerId = resolveIdentity(peerRef);
    const bool expectSealed = m_sessionKeys.contains(peerId) || !passphrase.isEmpty();

    if (!ciphertext.startsWith(QStringLiteral("KNC1:"))) {
        if (expectSealed)
            return i18nc("@info shown in place of a message body", "[decrypt error: cleartext on a keyed channel]");
        return ciphertext; // no key on this channel anyway - pass through
    }

    const QByteArray wire = QByteArray::fromBase64(ciphertext.mid(5).toLatin1());
    if (wire.isEmpty())
        return i18nc("@info shown in place of a message body", "[decrypt error: empty packet]");

    const quint8 type = static_cast<quint8>(wire.at(0));
    const QByteArray payload = wire.mid(1);

    QByteArray plain;
    bool ok = false;

    if (type == 0x01) {
        const auto sessionKey = m_sessionKeys.constFind(peerId);
        if (sessionKey != m_sessionKeys.constEnd())
            ok = gcmDecrypt(*sessionKey, payload, &plain);
    } else if (type == 0x02) {
        if (payload.size() > kSaltLen && !passphrase.isEmpty()) {
            const QByteArray salt = payload.left(kSaltLen);
            const QByteArray key = deriveKey(passphrase, salt);
            if (key.size() == kKeyLen)
                ok = gcmDecrypt(key, payload.mid(kSaltLen), &plain);
        }
    }

    if (!ok)
        return i18nc("@info shown in place of a message body", "[decrypt error: invalid key or tampered packet]");

    return QString::fromUtf8(plain);
}

// Raw byte encryption (voice)
QByteArray CryptoManager::encryptBytes(const QString &peerRef, const QByteArray &plaintext) const
{
    // empty means "not encrypted", and the caller has to drop the frame. voice
    // in the clear is not a graceful degradation, it is the bug.
    const auto key = m_sessionKeys.constFind(resolveIdentity(peerRef));
    if (key == m_sessionKeys.constEnd())
        return {};

    return gcmEncrypt(*key, plaintext); // nonce+ciphertext+tag
}

bool CryptoManager::decryptBytes(const QString &peerRef, const QByteArray &data, QByteArray *outPlain) const
{
    const auto key = m_sessionKeys.constFind(resolveIdentity(peerRef));
    if (key == m_sessionKeys.constEnd())
        return false; // unkeyed frames are not audio we are willing to play

    return gcmDecrypt(*key, data, outPlain);
}

} // namespace koutnet
