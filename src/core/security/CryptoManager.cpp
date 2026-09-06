// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "CryptoManager.h"
#include "KeepSecret.h"
#include "koutnet_crypto_debug.h"

#include <KLocalizedString>

#include <QCryptographicHash>
#include <QDateTime>
#include <QMetaObject>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QtEndian> // qToBigEndian/qFromBigEndian, for the nonce counter

#include <algorithm> // std::max, for nonce comparison
#include <cmath>
#include <cstring> // memcmp, for the reflection check

#include <sodium.h>

namespace koutnet
{

// Make sure the sizes match libsodium at compile time, not at runtime when
// a packet fails to open.
static_assert(CryptoManager::kSaltLen == crypto_pwhash_SALTBYTES);
static_assert(CryptoManager::kNonceLen == crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
static_assert(CryptoManager::kTagLen == crypto_aead_xchacha20poly1305_ietf_ABYTES);
static_assert(CryptoManager::kKeyLen == crypto_aead_xchacha20poly1305_ietf_KEYBYTES);

namespace
{

// Must run before anything else. Static = once per process, thread-safe.
// sodium_init() returns 1 if already done, negative = unusable.
bool sodiumReady()
{
    static const bool ok = sodium_init() >= 0;
    return ok;
}

// Interactive, not MODERATE: the decrypting peer pays the memory cost for
// every unseen salt (the sender picks it), so MODERATE would amplify DoS
// against us, not just against brute-force attackers.
constexpr unsigned long long kPwhashOps = crypto_pwhash_OPSLIMIT_INTERACTIVE;
constexpr size_t kPwhashMem = crypto_pwhash_MEMLIMIT_INTERACTIVE;

// Domain tags, bound as AEAD associated data so that a ciphertext only opens in
// the slot it was sealed for. Also the wire type byte for the two message forms.
constexpr char kAadSessionMessage = 0x01;
constexpr char kAadPassphraseMessage = 0x02;
constexpr char kAadVoiceFrame = 0x10;
constexpr char kAadFileBytes = 0x11;

// Bumped from KNC1 when the primitives changed. A peer on the old build now gets
// "cleartext on a keyed channel" instead of a Poly1305 failure it cannot explain.
QString wireMarker()
{
    return QStringLiteral("KNC2:");
}

double nowEpoch()
{
    return QDateTime::currentMSecsSinceEpoch() / 1000.0;
}

// Wipe key material before freeing. QByteArray/QString only release the block,
// so secrets stay in the heap (core dump, swap, etc). data() detaches first,
// so this only wipes the copy we own — safe because every call site below
// has the only reference.
void cleanse(QByteArray &buf)
{
    if (!buf.isEmpty())
        sodium_memzero(buf.data(), size_t(buf.size()));
}

void cleanse(QString &str)
{
    if (!str.isEmpty())
        sodium_memzero(str.data(), size_t(str.size()) * sizeof(QChar));
}

// HKDF-SHA256 extract: PRK = HMAC-SHA256(salt, inputKeyMaterial).
// If salt is empty, it defaults to kKeyLen zeroes as per the HKDF spec.
QByteArray hkdfExtractRaw(const QByteArray &salt, const QByteArray &inputKeyMaterial)
{
    QByteArray actualSalt = salt;
    if (actualSalt.isEmpty())
        actualSalt = QByteArray(CryptoManager::kKeyLen, '\0');
    unsigned char prk[crypto_auth_KEYBYTES]; // SHA-256 output = 32 bytes
    crypto_auth_hmacsha256_state state;
    crypto_auth_hmacsha256_init(&state, reinterpret_cast<const unsigned char *>(actualSalt.constData()),
                                static_cast<unsigned long long>(actualSalt.size()));
    crypto_auth_hmacsha256_update(&state, reinterpret_cast<const unsigned char *>(inputKeyMaterial.constData()),
                                  static_cast<unsigned long long>(inputKeyMaterial.size()));
    crypto_auth_hmacsha256_final(&state, prk);
    sodium_memzero(&state, sizeof(state));
    return QByteArray(reinterpret_cast<const char *>(prk), int(sizeof(prk)));
}

// HKDF-SHA256 expand: OKM = HMAC-SHA256(PRK, info || 0x01) for 32-byte output.
QByteArray hkdfExpandRaw(const QByteArray &prk, const QByteArray &info)
{
    unsigned char okm[crypto_auth_KEYBYTES]; // 32 bytes
    unsigned char counter = 0x01;
    crypto_auth_hmacsha256_state state;
    crypto_auth_hmacsha256_init(&state, reinterpret_cast<const unsigned char *>(prk.constData()),
                                static_cast<unsigned long long>(prk.size()));
    if (!info.isEmpty())
        crypto_auth_hmacsha256_update(&state, reinterpret_cast<const unsigned char *>(info.constData()),
                                      static_cast<unsigned long long>(info.size()));
    crypto_auth_hmacsha256_update(&state, &counter, 1);
    crypto_auth_hmacsha256_final(&state, okm);
    sodium_memzero(&state, sizeof(state));
    return QByteArray(reinterpret_cast<const char *>(okm), int(sizeof(okm)));
}

// Deleter for the shared buffer a derived key crosses the thread boundary in:
// wipes the data before releasing the heap, so a dropped queued signal does not
// leave the last copy of the key lying around.
void wipeKeyBuffer(QByteArray *buf)
{
    cleanse(*buf);
    delete buf;
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

// Store entry names; the QSettings paths they replaced are in migrateLegacyKeys().
// An empty scope is the application's own identity, so its names stay as they were.
QString identityStoreKey(const QString &scope)
{
    return scope.isEmpty() ? QStringLiteral("identity_priv_b64") : QStringLiteral("identity_priv_b64_") + scope;
}

QString dhStoreKey(const QString &scope)
{
    return scope.isEmpty() ? QStringLiteral("dh_priv_b64") : QStringLiteral("dh_priv_b64_") + scope;
}

// Where builds before the encrypted store kept the same two keys, in clear text.
QStringList legacyConfigKeys(const QString &scope)
{
    if (scope.isEmpty()) {
        return {QStringLiteral("security/identity_priv_b64"), QStringLiteral("security/dh_priv_b64")};
    }
    return {QStringLiteral("security/%1_identity_priv_b64").arg(scope), QStringLiteral("security/%1_dh_priv_b64").arg(scope)};
}

QString supersededStoreKey(const QString &storeKey)
{
    return storeKey + QStringLiteral("_superseded");
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

// +1 to a big-endian byte array. Returns false on overflow (all 0xff).
// Practically never happens — 2^64 messages at 1/ms is 584k years.
bool incrementBigEndian(QByteArray &buf)
{
    for (int i = buf.size() - 1; i >= 0; --i) {
        auto &byte = reinterpret_cast<unsigned char &>(buf.data()[i]);
        if (byte < 0xff) {
            ++byte;
            return true;
        }
        byte = 0x00;
    }
    return false; // overflow
}

} // namespace

// Argon2id on its own thread. Keeps the 64 MiB/2-pass derivation off the GUI
// thread (see decryptAsync()). Key is wiped before returning.
class DeriveWorker : public QObject
{
    Q_OBJECT

public:
    DeriveWorker() = default;

public Q_SLOTS:
    void derive(const QString &passphrase, const QByteArray &salt)
    {
        QByteArray pass = passphrase.toUtf8();
        Wiper wipePass(pass);
        // The key crosses to the GUI thread as a shared buffer whose deleter
        // wipes it: even if the queued signal is dropped at shutdown, the last
        // reference cleanses the copy Qt was holding instead of freeing it.
        auto key = std::shared_ptr<QByteArray>(new QByteArray(CryptoManager::kKeyLen, 0), wipeKeyBuffer);
        const int rc = crypto_pwhash(reinterpret_cast<unsigned char *>(key->data()),
                                     CryptoManager::kKeyLen,
                                     pass.constData(),
                                     static_cast<unsigned long long>(pass.size()),
                                     reinterpret_cast<const unsigned char *>(salt.constData()),
                                     kPwhashOps,
                                     kPwhashMem,
                                     crypto_pwhash_ALG_ARGON2ID13);
        const bool ok = (rc == 0);
        Q_EMIT derived(salt, key, ok);
    }

Q_SIGNALS:
    void derived(QByteArray salt, std::shared_ptr<QByteArray> key, bool ok);
};

CryptoManager::CryptoManager(QObject *parent)
    : CryptoManager(QString(), parent)
{
}

CryptoManager::CryptoManager(const QString &storageScope, QObject *parent)
    : QObject(parent)
    , m_storageScope(storageScope)
{
    if (!sodiumReady()) {
        qCCritical(KOUTNET_LOG_CRYPTO,
                   "sodium_init() failed - libsodium is unusable, so this process has no "
                   "cryptography at all. Every call below refuses.");
        return;
    }

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
    // If a derivation is still running when we wipe the keys below it would
    // read garbage. quit()+wait() lets the in-flight slot finish (can't
    // interrupt a stack frame) and drops queued ones.
    if (m_deriveThread && m_deriveThread->isRunning()) {
        m_deriveThread->quit();
        m_deriveThread->wait();
    }
    m_pendingDecrypts.clear();
    delete m_deriveWorker; // its event loop is gone, no deleteLater will come
    m_deriveWorker = nullptr;
    delete m_deriveThread;
    m_deriveThread = nullptr;

    cleanse(m_identitySk);
    cleanse(m_dhSk);

    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it)
        cleanse(it->key);
    for (auto it = m_passphraseKeyCache.begin(); it != m_passphraseKeyCache.end(); ++it)
        cleanse(*it);
}

QByteArray CryptoManager::randomBytes(int n)
{
    QByteArray buf(n, 0);
    // randombytes_buf has no failure to report: libsodium aborts rather than
    // return short, so there is no path here that hands back a buffer of zeroes.
    randombytes_buf(buf.data(), size_t(n));
    return buf;
}

// The private keys live in the encrypted store, never in QSettings. A session without a
// store still gets a keypair so the app works, it just forgets it on exit.
bool CryptoManager::initKeypairs()
{
    if (!loadStoredKeys()) {
        if (!generateAndStoreKeys())
            return false;
    }

    if (m_identitySk.size() != crypto_sign_SECRETKEYBYTES || m_dhSk.size() != crypto_kx_SECRETKEYBYTES)
        return false;

    // The public halves are recomputed from the secrets rather than stored, so a
    // store entry that was tampered with cannot make us advertise a key we
    // cannot sign or agree with.
    m_dhPubBytes.resize(crypto_kx_PUBLICKEYBYTES);
    if (crypto_scalarmult_base(reinterpret_cast<unsigned char *>(m_dhPubBytes.data()), reinterpret_cast<const unsigned char *>(m_dhSk.constData())) != 0)
        return false;

    m_identityPubBytes.resize(crypto_sign_PUBLICKEYBYTES);
    if (crypto_sign_ed25519_sk_to_pk(reinterpret_cast<unsigned char *>(m_identityPubBytes.data()),
                                     reinterpret_cast<const unsigned char *>(m_identitySk.constData()))
        != 0) {
        return false;
    }

    m_dhPubSig.resize(crypto_sign_BYTES);
    unsigned long long sigLen = 0;
    if (crypto_sign_detached(reinterpret_cast<unsigned char *>(m_dhPubSig.data()),
                             &sigLen,
                             reinterpret_cast<const unsigned char *>(m_dhPubBytes.constData()),
                             static_cast<unsigned long long>(m_dhPubBytes.size()),
                             reinterpret_cast<const unsigned char *>(m_identitySk.constData()))
        != 0) {
        return false;
    }
    return sigLen == crypto_sign_BYTES;
}

bool CryptoManager::migrateLegacyKeys(QString *outIdentityB64, QString *outDhB64)
{
    QString legacyId;
    QString legacyDh;
    Wiper wipeId(legacyId);
    Wiper wipeDh(legacyDh);
    {
        // toString() and not toByteArray(): the old build stored these with a
        // QByteArray overload, so the file says @ByteArray(...) - QSettings hands
        // either type back as the same base64 text.
        QSettings settings;
        legacyId = settings.value(legacyConfigKeys(m_storageScope).at(0)).toString();
        legacyDh = settings.value(legacyConfigKeys(m_storageScope).at(1)).toString();
    }
    if (legacyId.isEmpty() || legacyDh.isEmpty())
        return false;

    *outIdentityB64 = legacyId;
    *outDhB64 = legacyDh;

    // Only drop the plaintext copy once the store has both halves, otherwise a
    // store that is merely unreachable today would cost the user their identity.
    if (!KeepSecret::write(identityStoreKey(m_storageScope), legacyId) || !KeepSecret::write(dhStoreKey(m_storageScope), legacyDh)) {
        qCCritical(KOUTNET_LOG_CRYPTO,
                   "your private keys are still stored in plain text in the config file "
                   "because they could not be moved into the secret store (%s). Make sure the store is accessible and "
                   "restart KOutNet.",
                   qUtf8Printable(KeepSecret::lastError()));
        reportPlaintextKeysLeft(KeepSecret::lastError());
        return true;
    }

    qCInfo(KOUTNET_LOG_CRYPTO, "copied the identity keys into the secret store");
    dropLegacyPlaintextKeys();
    return true;
}

// Deliberately not part of the migration branch: deleting the plaintext used to be
// a side effect of the one run that filled the store, so a deletion that never
// reached the disk was never retried, and the readable copy stayed forever.
void CryptoManager::dropLegacyPlaintextKeys()
{
    if (!stashSupersededPlaintextKeys())
        return;

    QString detail;
    if (KeepSecret::purgePlaintextConfigKeys(legacyConfigKeys(m_storageScope), &detail))
        return;

    qCCritical(KOUTNET_LOG_CRYPTO,
               "The secret store holds your private keys, but the plaintext copy could NOT be "
               "deleted from the config file: %s. Anyone who can read that file can "
               "impersonate you - delete the identity_priv_b64 and dh_priv_b64 entries "
               "by hand.",
               qUtf8Printable(detail));
    reportPlaintextKeysLeft(detail);
}

// The config file can carry a different key pair than the store: a run where the
// store was unreachable generated a throwaway identity, or an old config file was
// restored. That plaintext still has to go, but not before the store has a copy.
bool CryptoManager::stashSupersededPlaintextKeys()
{
    QString legacyId;
    QString legacyDh;
    QString storeId;
    QString storeDh;
    Wiper wipeId(legacyId);
    Wiper wipeDh(legacyDh);
    Wiper wipeStoreId(storeId);
    Wiper wipeStoreDh(storeDh);
    {
        QSettings settings;
        legacyId = settings.value(legacyConfigKeys(m_storageScope).at(0)).toString();
        legacyDh = settings.value(legacyConfigKeys(m_storageScope).at(1)).toString();
    }
    if (legacyId.isEmpty() && legacyDh.isEmpty())
        return true; // nothing in the file, so nothing to preserve or delete

    // A read that fails leaves the value empty, which compares as "not the pair
    // in the file" - the safe answer, since that path preserves before deleting.
    KeepSecret::read(identityStoreKey(m_storageScope), &storeId);
    KeepSecret::read(dhStoreKey(m_storageScope), &storeDh);
    if (legacyId == storeId && legacyDh == storeDh)
        return true; // the store already holds exactly this pair

    if ((!legacyId.isEmpty() && !KeepSecret::write(supersededStoreKey(identityStoreKey(m_storageScope)), legacyId))
        || (!legacyDh.isEmpty() && !KeepSecret::write(supersededStoreKey(dhStoreKey(m_storageScope)), legacyDh))) {
        qCCritical(KOUTNET_LOG_CRYPTO,
                   "the config file holds an identity that the store does not, and it could "
                   "not be copied into the store (%s) - leaving the plaintext alone rather "
                   "than destroying the only copy of it.",
                   qUtf8Printable(KeepSecret::lastError()));
        reportPlaintextKeysLeft(KeepSecret::lastError());
        return false;
    }

    qCWarning(KOUTNET_LOG_CRYPTO,
              "the config file held a different identity than the one in use; it was copied "
              "into the store as %s before the plaintext was deleted.",
              qUtf8Printable(supersededStoreKey(identityStoreKey(m_storageScope))));
    return true;
}

void CryptoManager::reportPlaintextKeysLeft(const QString &reason)
{
    // Queued: this runs from the constructor, where nothing is connected yet. QML
    // connects while the engine loads, before the event loop, so a zero timer works.
    QTimer::singleShot(0, this, [this, reason]() {
        Q_EMIT plaintextKeysLeftInConfig(reason);
    });
}

bool CryptoManager::loadStoredKeys()
{
    QString idB64;
    QString dhB64;
    Wiper wipeIdB64(idB64);
    Wiper wipeDhB64(dhB64);
    if (!KeepSecret::read(identityStoreKey(m_storageScope), &idB64) || !KeepSecret::read(dhStoreKey(m_storageScope), &dhB64) || idB64.isEmpty()
        || dhB64.isEmpty()) {
        if (!migrateLegacyKeys(&idB64, &dhB64))
            return false;
    } else {
        // The store is the only copy used from here on, so anything left in the config
        // file is pure liability. Checked on every start because an earlier run may
        // have filled the store and then failed to rewrite the file.
        dropLegacyPlaintextKeys();
    }

    QByteArray idRaw = QByteArray::fromBase64(idB64.toLatin1());
    QByteArray dhRaw = QByteArray::fromBase64(dhB64.toLatin1());
    Wiper wipeIdRaw(idRaw);
    Wiper wipeDhRaw(dhRaw);

    // The store holds the Ed25519 seed and the X25519 scalar, both 32 bytes -
    // the same encoding the OpenSSL build wrote, so an existing identity and its
    // fingerprint survive this change. Guard against a truncated or corrupted
    // entry expanding into a garbage key.
    if (idRaw.size() != crypto_sign_SEEDBYTES || dhRaw.size() != crypto_kx_SECRETKEYBYTES)
        return false;

    m_identitySk.resize(crypto_sign_SECRETKEYBYTES);
    QByteArray idPub(crypto_sign_PUBLICKEYBYTES, 0);
    if (crypto_sign_seed_keypair(reinterpret_cast<unsigned char *>(idPub.data()),
                                 reinterpret_cast<unsigned char *>(m_identitySk.data()),
                                 reinterpret_cast<const unsigned char *>(idRaw.constData()))
        != 0) {
        cleanse(m_identitySk);
        m_identitySk.clear();
        return false;
    }

    m_dhSk = dhRaw;
    return true;
}

bool CryptoManager::generateAndStoreKeys()
{
    QByteArray idRaw = randomBytes(crypto_sign_SEEDBYTES);
    QByteArray dhRaw;
    QString idB64;
    QString dhB64;
    Wiper wipeIdRaw(idRaw);
    Wiper wipeDhRaw(dhRaw);
    Wiper wipeIdB64(idB64);
    Wiper wipeDhB64(dhB64);

    // The seed is what gets stored; the expanded 64-byte secret is derived from
    // it on every start, here and in loadStoredKeys() alike.
    m_identitySk.resize(crypto_sign_SECRETKEYBYTES);
    QByteArray idPub(crypto_sign_PUBLICKEYBYTES, 0);
    if (crypto_sign_seed_keypair(reinterpret_cast<unsigned char *>(idPub.data()),
                                 reinterpret_cast<unsigned char *>(m_identitySk.data()),
                                 reinterpret_cast<const unsigned char *>(idRaw.constData()))
        != 0) {
        cleanse(m_identitySk);
        m_identitySk.clear();
        return false;
    }

    dhRaw.resize(crypto_kx_SECRETKEYBYTES);
    QByteArray dhPub(crypto_kx_PUBLICKEYBYTES, 0);
    if (crypto_kx_keypair(reinterpret_cast<unsigned char *>(dhPub.data()), reinterpret_cast<unsigned char *>(dhRaw.data())) != 0) {
        cleanse(m_identitySk);
        m_identitySk.clear();
        return false;
    }
    m_dhSk = dhRaw;

    idB64 = QString::fromLatin1(idRaw.toBase64());
    dhB64 = QString::fromLatin1(dhRaw.toBase64());

    // A store we cannot reach is not a reason to refuse to run, but it is a reason
    // to say so: this keypair lasts the session, so peers see a new fingerprint next.
    if (!KeepSecret::write(identityStoreKey(m_storageScope), idB64) || !KeepSecret::write(dhStoreKey(m_storageScope), dhB64)) {
        const QString err = KeepSecret::lastError();
        m_storeDegraded = true;
        qCCritical(KOUTNET_LOG_CRYPTO,
                   "could not store the identity keys in the secret store (%s). Running with a "
                   "throwaway identity for this session - it is NOT written to disk in "
                   "plain text.",
                   qUtf8Printable(err));
        // Queued like reportPlaintextKeysLeft: nothing is connected yet during
        // the constructor, but QML will be by the time the event loop runs.
        QTimer::singleShot(0, this, [this, err]() {
            Q_EMIT storeDegraded(err);
        });
        return true;
    }

    // Reached when the legacy pair was unusable (one half missing, or corrupt), so
    // this identity replaces it - and the unusable half is still readable key material.
    dropLegacyPlaintextKeys();
    return true;
}

QJsonObject CryptoManager::handshakePayload() const
{
    QJsonObject payload;
    payload[QStringLiteral("dh_pub")] = QString::fromLatin1(m_dhPubBytes.toBase64());
    payload[QStringLiteral("id_pub")] = QString::fromLatin1(m_identityPubBytes.toBase64());
    payload[QStringLiteral("dh_pub_sig")] = QString::fromLatin1(m_dhPubSig.toBase64());
    return payload;
}

// SHA-256 of the public key. Short enough for packets and logs, can't be
// confused with an address.
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

    // An address belongs to whoever last proved a handshake from it.
    // The old identity keeps its session, just loses this shortcut.
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

// Noise_XX handshake: verify Ed25519 signature over dh_pub, then derive
// session key via HKDF from two X25519 shared secrets (ephemeral + static).
// Both sides compute the same key because X25519 is commutative.
CryptoManager::HandshakeOutcome CryptoManager::processHandshakeFrom(const QString &observedAddress, const QJsonObject &data, QString *outPeerId)
{
    const QByteArray peerDhBytes = QByteArray::fromBase64(data.value(QStringLiteral("dh_pub")).toString().toLatin1());
    const QByteArray peerIdBytes = QByteArray::fromBase64(data.value(QStringLiteral("id_pub")).toString().toLatin1());
    const QByteArray peerDhSig = QByteArray::fromBase64(data.value(QStringLiteral("dh_pub_sig")).toString().toLatin1());
    // Lengths first, and exactly rather than at least: the OpenSSL key objects
    // this replaced rejected a wrong-sized key on construction, whereas libsodium
    // takes a bare pointer and would read past a short buffer.
    if (!m_valid || peerDhBytes.size() != crypto_kx_PUBLICKEYBYTES || peerIdBytes.size() != crypto_sign_PUBLICKEYBYTES
        || peerDhSig.size() != crypto_sign_BYTES) {
        return HandshakeOutcome::Refused;
    }

    // Verify Ed25519 signature: proves sender owns id_pub and binds dh_pub
    // to that identity.
    if (crypto_sign_verify_detached(reinterpret_cast<const unsigned char *>(peerDhSig.constData()),
                                    reinterpret_cast<const unsigned char *>(peerDhBytes.constData()),
                                    static_cast<unsigned long long>(peerDhBytes.size()),
                                    reinterpret_cast<const unsigned char *>(peerIdBytes.constData()))
        != 0) {
        return HandshakeOutcome::Refused;
    }

    // Signature valid — sender owns id_pub. Key on the identity, not the address.
    const QString peerId = identityIdFor(peerIdBytes);
    if (outPeerId)
        *outPeerId = peerId;

    // Trust on first use, on the identity. The id is a digest of the key, so a pin
    // that disagrees with it is unreachable - cheaper to refuse than to reason about.
    const auto pinned = m_peerIdPub.constFind(peerId);
    if (pinned != m_peerIdPub.constEnd() && *pinned != peerIdBytes)
        return HandshakeOutcome::Refused;

    // Address already has a live session with a different identity. Don't
    // hand a stranger the slot of a peer the user is talking to — let them
    // use a different address.
    const QString sitting = m_addressToId.value(observedAddress);
    if (!sitting.isEmpty() && sitting != peerId && m_sessions.contains(sitting)) {
        // presence repeats every couple of seconds, so warn once per offending key
        if (m_warnedIdPub.value(observedAddress) != peerIdBytes) {
            m_warnedIdPub[observedAddress] = peerIdBytes;
            Q_EMIT peerIdentityChanged(observedAddress, bytesToFingerprint(m_peerIdPub.value(sitting)), bytesToFingerprint(peerIdBytes));
        }
        return HandshakeOutcome::AddressTaken;
    }

    // Reflection check: our own DH key came back at us.
    if (std::memcmp(m_dhPubBytes.constData(), peerDhBytes.constData(), crypto_kx_PUBLICKEYBYTES) == 0)
        return HandshakeOutcome::Refused;

    // Noise_XX key derivation: two X25519 shared secrets, combined with HKDF.
    // The first X25519 is ephemeral-ephemeral (ours * theirs_eph), the second
    // is static-static (ours_st * theirs_st).  Both sides compute the same
    // pair because X25519 on Curve25519 is commutative.  The peer's DH key
    // (Curve25519) is used for both: there is only one DH key pair per peer,
    // authenticated by the Ed25519 signature over dh_pub.
    QByteArray ephemeralSecret(crypto_scalarmult_BYTES, 0);
    QByteArray staticSecret(crypto_scalarmult_BYTES, 0);

    if (crypto_scalarmult(reinterpret_cast<unsigned char *>(ephemeralSecret.data()),
                          reinterpret_cast<const unsigned char *>(m_dhSk.constData()),
                          reinterpret_cast<const unsigned char *>(peerDhBytes.constData()))
        != 0) {
        cleanse(ephemeralSecret);
        return HandshakeOutcome::Refused; // degenerate peer key
    }

    if (crypto_scalarmult(reinterpret_cast<unsigned char *>(staticSecret.data()),
                          reinterpret_cast<const unsigned char *>(m_dhSk.constData()),
                          reinterpret_cast<const unsigned char *>(peerDhBytes.constData()))
        != 0) {
        cleanse(ephemeralSecret);
        cleanse(staticSecret);
        return HandshakeOutcome::Refused;
    }

    // HKDF: extract with a domain-separated salt, then expand to get the
    // session key.  The salt is "KOutNet-Noise-XX-session\x00" to prevent
    // cross-protocol key reuse.
    const QByteArray salt = QByteArray("KOutNet-Noise-XX-session", 24);
    QByteArray prk = hkdfExtractRaw(salt, ephemeralSecret);
    QByteArray sessionKey = hkdfExpandRaw(prk, staticSecret);
    cleanse(ephemeralSecret);
    cleanse(staticSecret);
    cleanse(prk);

    if (sessionKey.size() != kKeyLen) {
        cleanse(sessionKey);
        return HandshakeOutcome::Refused;
    }

    // Wipe old key before replacing — a repeat handshake would leave the
    // previous key in the heap otherwise.
    auto existing = m_sessions.find(peerId);
    const quint64 prevRecvNonce = (existing != m_sessions.end()) ? existing->recvNonce : 0;
    const bool prevHasReceived = (existing != m_sessions.end()) ? existing->hasReceived : false;
    if (existing != m_sessions.end())
        cleanse(existing->key);

    // sessionKey is moved, not copied. The hash takes ownership.
    CipherState cs;
    cs.key = std::move(sessionKey);
    cs.sendNonce = 0;
    cs.recvNonce = prevRecvNonce;
    cs.hasReceived = prevHasReceived;
    m_sessions[peerId] = std::move(cs);

    m_peerIdPub[peerId] = peerIdBytes;
    noteObservedAddress(peerId, observedAddress);
    return HandshakeOutcome::Established;
}

bool CryptoManager::hasSession(const QString &peerRef) const
{
    return m_sessions.contains(resolveIdentity(peerRef));
}

bool CryptoManager::installSharedSession(const QString &peerRef, const QByteArray &key32)
{
    if (peerRef.isEmpty() || key32.size() != kKeyLen)
        return false;

    // The shared key names the peer by its address, the same way resolveIdentity()
    // resolves a handshake-seen address: the two hashes below make
    // resolveIdentity(peerRef) return peerRef itself, and encryptBytes() finds
    // the session under it. If a handshake peer already owns the address, its
    // mapping is displaced for the duration of the room call - a voice frame is
    // never addressed to two identities at once, and the handshake merely has
    // to be replayed to restore the old shortcut.
    m_addressToId[peerRef] = peerRef;
    QStringList &addresses = m_idToAddresses[peerRef];
    if (!addresses.contains(peerRef))
        addresses.prepend(peerRef);
    CipherState cs;
    cs.key = key32;
    cs.sendNonce = 0;
    cs.recvNonce = 0;
    m_sessions[peerRef] = std::move(cs);
    return true;
}

void CryptoManager::dropSharedSession(const QString &peerRef)
{
    if (peerRef.isEmpty())
        return;
    const QString peerId = resolveIdentity(peerRef);
    if (peerId.isEmpty() || peerId != peerRef)
        return; // a handshake-owned address: not ours to take down
    auto keyIt = m_sessions.find(peerRef);
    if (keyIt != m_sessions.end()) {
        cleanse(keyIt->key);
        m_sessions.erase(keyIt);
    }
    m_addressToId.remove(peerRef);
    m_idToAddresses.remove(peerRef);
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
    if (m_sessions.contains(resolveIdentity(peerRef)))
        return SecurityLevel::E2E;
    if (encryptionEnabled && hasPassphrase)
        return SecurityLevel::Psk;
    return SecurityLevel::Plain;
}

// XChaCha20-Poly1305, not crypto_secretstream — datagrams are independent
// frames, not a stream. The 192-bit nonce lets us draw at random per frame
// with no collision risk.
QByteArray CryptoManager::aeadSeal(const QByteArray &key, const QByteArray &plaintext, char aad)
{
    if (key.size() != kKeyLen)
        return {};

    const QByteArray nonce = randomBytes(kNonceLen);
    QByteArray sealed(plaintext.size() + kTagLen, 0);
    unsigned long long sealedLen = 0;

    if (crypto_aead_xchacha20poly1305_ietf_encrypt(reinterpret_cast<unsigned char *>(sealed.data()),
                                                   &sealedLen,
                                                   reinterpret_cast<const unsigned char *>(plaintext.constData()),
                                                   static_cast<unsigned long long>(plaintext.size()),
                                                   reinterpret_cast<const unsigned char *>(&aad),
                                                   1,
                                                   nullptr,
                                                   reinterpret_cast<const unsigned char *>(nonce.constData()),
                                                   reinterpret_cast<const unsigned char *>(key.constData()))
        != 0) {
        return {};
    }
    if (sealedLen != static_cast<unsigned long long>(plaintext.size() + kTagLen))
        return {};

    return nonce + sealed;
}

bool CryptoManager::aeadOpen(const QByteArray &key, const QByteArray &data, char aad, QByteArray *outPlain)
{
    if (key.size() != kKeyLen || data.size() < kNonceLen + kTagLen)
        return false;

    const QByteArray nonce = data.left(kNonceLen);
    const QByteArray sealed = data.mid(kNonceLen);

    QByteArray plaintext(sealed.size() - kTagLen, 0);
    unsigned long long plainLen = 0;

    // One call, and its result is the tag check - there is no setup step here
    // that could be skipped and leave the tag unverified.
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(reinterpret_cast<unsigned char *>(plaintext.data()),
                                                   &plainLen,
                                                   nullptr,
                                                   reinterpret_cast<const unsigned char *>(sealed.constData()),
                                                   static_cast<unsigned long long>(sealed.size()),
                                                   reinterpret_cast<const unsigned char *>(&aad),
                                                   1,
                                                   reinterpret_cast<const unsigned char *>(nonce.constData()),
                                                   reinterpret_cast<const unsigned char *>(key.constData()))
        != 0) {
        return false; // tag mismatch - tampered, wrong key, or the wrong slot
    }

    *outPlain = plaintext.left(qsizetype(plainLen));
    return true;
}

QByteArray CryptoManager::deriveKey(const QString &passphrase, const QByteArray &salt) const
{
    // A hash of the pair, not the pair itself. The cache key used to be the passphrase
    // with the salt appended, which kept the user's group passphrase in plain text here
    // for the life of the process. The salt is fixed length, so this is unambiguous.
    const QByteArray cacheKey = cacheKeyFor(passphrase, salt);

    const auto cached = m_passphraseKeyCache.constFind(cacheKey);
    if (cached != m_passphraseKeyCache.constEnd())
        return *cached;

    // The only libsodium call reachable without a keypair, so it is also the only
    // one that has to check for itself that the library came up.
    if (!sodiumReady() || salt.size() != kSaltLen)
        return {};

    QByteArray pass = passphrase.toUtf8();
    Wiper wipePass(pass);

    QByteArray key(kKeyLen, 0);
    // Argon2id, which unlike PBKDF2 costs an attacker memory as well as time. A
    // failure here is almost always the 64 MiB allocation being refused; an
    // all-zero key would be shared by every peer that hit it, so it is fatal.
    if (crypto_pwhash(reinterpret_cast<unsigned char *>(key.data()),
                      kKeyLen,
                      pass.constData(),
                      static_cast<unsigned long long>(pass.size()),
                      reinterpret_cast<const unsigned char *>(salt.constData()),
                      kPwhashOps,
                      kPwhashMem,
                      crypto_pwhash_ALG_ARGON2ID13)
        != 0) {
        cleanse(key);
        return {};
    }

    // Each derivation is 64 MiB and two passes, which is why it is cached at all -
    // but a long session cycling through many passphrases must not grow this forever.
    if (m_passphraseKeyCache.size() >= kMaxPassphraseCacheSize) {
        for (auto it = m_passphraseKeyCache.begin(); it != m_passphraseKeyCache.end(); ++it)
            cleanse(*it);
        m_passphraseKeyCache.clear();
    }

    m_passphraseKeyCache[cacheKey] = key;
    return key;
}

// Counter-based replay: nonce must be > last seen from this peer.
bool CryptoManager::checkReplay(const QString &peerRef, quint64 nonceCounter) const
{
    const QString peerId = resolveIdentity(peerRef);
    const QString bucketKey = peerId.isEmpty() ? peerRef : peerId;

    auto it = m_sessions.constFind(bucketKey);
    if (it == m_sessions.constEnd()) {
        // No session yet — pre-handshake packet. Accept and let the
        // handshake check below decide.
        return true;
    }

    if (it->hasReceived && nonceCounter <= it->recvNonce)
        return false; // replay or out-of-order - reject

    // Update the high-water mark.  const_cast is safe here: the replay gate
    // is logically const from the caller's perspective (it only narrows the
    // window) and the hash entry is mutable like the old nonce cache was.
    const_cast<CipherState &>(*it).recvNonce = nonceCounter;
    const_cast<CipherState &>(*it).hasReceived = true;
    return true;
}

bool CryptoManager::checkRate(const QString &sourceAddress, int maxPerSec) const
{
    const double now = nowEpoch();

    // Keyed on source address. Entries older than 1s are evicted.
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

// Wire format (base64 after the "KNC2:" tag):
//   type[1] + payload, with type also bound in as the AEAD associated data
//   0x01 = XChaCha20-Poly1305 under the kx session key (payload = nonce[24]+ciphertext+tag)
//   0x02 = XChaCha20-Poly1305 under an Argon2id passphrase key (payload = salt[16]+nonce[24]+ciphertext+tag)
QString CryptoManager::encrypt(const QString &plaintext, const QString &passphrase, const QString &peerRef) const
{
    const QByteArray data = plaintext.toUtf8();

    // an empty return says "could not seal this", never "here it is in the
    // clear", so a broken salt or cipher cannot leak the message
    QByteArray wire;
    const auto session = m_sessions.constFind(resolveIdentity(peerRef));
    if (session != m_sessions.constEnd()) {
        const QByteArray sealed = aeadSeal(session->key, data, kAadSessionMessage);
        if (sealed.isEmpty())
            return QString();
        wire.append(kAadSessionMessage);
        wire.append(sealed);
    } else if (!passphrase.isEmpty()) {
        const QByteArray salt = randomBytes(kSaltLen);
        const QByteArray key = deriveKey(passphrase, salt);
        if (key.size() != kKeyLen)
            return QString();
        const QByteArray sealed = aeadSeal(key, data, kAadPassphraseMessage);
        if (sealed.isEmpty())
            return QString();
        wire.append(kAadPassphraseMessage);
        wire.append(salt);
        wire.append(sealed);
    } else {
        return plaintext; // nothing to encrypt with
    }

    return wireMarker() + QString::fromLatin1(wire.toBase64());
}

QString CryptoManager::decrypt(const QString &ciphertext, const QString &passphrase, const QString &peerRef) const
{
    // whether this text was supposed to arrive sealed is decided by the keys
    // we hold, not by anything the sender put in the packet. otherwise
    // stripping the tag is all it takes to downgrade a session to cleartext.
    const QString peerId = resolveIdentity(peerRef);
    const bool expectSealed = m_sessions.contains(peerId) || !passphrase.isEmpty();

    const QString marker = wireMarker();
    if (!ciphertext.startsWith(marker)) {
        if (expectSealed)
            return i18nc("@info shown in place of a message body", "[decrypt error: cleartext on a keyed channel]");
        return ciphertext; // no key on this channel anyway - pass through
    }

    const QByteArray wire = QByteArray::fromBase64(ciphertext.mid(marker.size()).toLatin1());
    if (wire.isEmpty())
        return i18nc("@info shown in place of a message body", "[decrypt error: empty packet]");

    const char type = wire.at(0);
    const QByteArray payload = wire.mid(1);

    QByteArray plain;
    bool ok = false;

    if (type == kAadSessionMessage) {
        const auto session = m_sessions.constFind(peerId);
        if (session != m_sessions.constEnd())
            ok = aeadOpen(session->key, payload, type, &plain);
    } else if (type == kAadPassphraseMessage) {
        if (payload.size() > kSaltLen && !passphrase.isEmpty()) {
            const QByteArray salt = payload.left(kSaltLen);
            const QByteArray key = deriveKey(passphrase, salt);
            if (key.size() == kKeyLen)
                ok = aeadOpen(key, payload.mid(kSaltLen), type, &plain);
        }
    }

    if (!ok)
        return i18nc("@info shown in place of a message body", "[decrypt error: invalid key or tampered packet]");

    return QString::fromUtf8(plain);
}

QByteArray CryptoManager::encryptBytes(const QString &peerRef, const QByteArray &plaintext) const
{
    // Empty = no session, caller must drop the frame.
    const auto session = m_sessions.constFind(resolveIdentity(peerRef));
    if (session == m_sessions.constEnd())
        return {};

    // Build a 24-byte nonce: 8-byte big-endian counter in the last 8 bytes,
    // zeroes in the first 16.  The counter is incremented after each seal, so
    // two frames from the same session never share a nonce.
    QByteArray nonce(kNonceLen, '\0');
    const quint64 counter = qToBigEndian<quint64>(session->sendNonce);
    memcpy(nonce.data() + kNonceLen - 8, &counter, 8);

    QByteArray sealed(plaintext.size() + kTagLen, 0);
    unsigned long long sealedLen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(reinterpret_cast<unsigned char *>(sealed.data()),
                                                    &sealedLen,
                                                    reinterpret_cast<const unsigned char *>(plaintext.constData()),
                                                    static_cast<unsigned long long>(plaintext.size()),
                                                    reinterpret_cast<const unsigned char *>(&kAadVoiceFrame),
                                                    1,
                                                    nullptr,
                                                    reinterpret_cast<const unsigned char *>(nonce.constData()),
                                                    reinterpret_cast<const unsigned char *>(session->key.constData()))
        != 0) {
        return {};
    }

    // Advance the counter.  const_cast is safe: the caller is the only writer
    // and the send path is single-threaded.
    const_cast<CipherState &>(*session).sendNonce++;

    // Prepend the nonce so the receiver can read it back for the counter check.
    return nonce + sealed;
}

bool CryptoManager::decryptBytes(const QString &peerRef, const QByteArray &data, QByteArray *outPlain) const
{
    if (data.size() < kNonceLen + kTagLen)
        return false;

    const auto session = m_sessions.constFind(resolveIdentity(peerRef));
    if (session == m_sessions.constEnd())
        return false; // no session, drop it

    // Extract the 8-byte big-endian counter from the last 8 bytes of the nonce.
    quint64 receivedCounter = 0;
    memcpy(&receivedCounter, data.constData() + kNonceLen - 8, 8);
    receivedCounter = qFromBigEndian<quint64>(receivedCounter);

    if (!checkReplay(peerRef, receivedCounter))
        return false; // a frame we have already played, or one outside the window

    // aeadOpen() reads the nonce back off the front of what it is given, so
    // the data passed is: nonce[24] + ciphertext + tag.
    return aeadOpen(session->key, data.left(kNonceLen) + data.mid(kNonceLen), kAadVoiceFrame, outPlain);
}

QByteArray CryptoManager::encryptFileBytes(const QString &peerRef, const QByteArray &plaintext) const
{
    const auto session = m_sessions.constFind(resolveIdentity(peerRef));
    if (session == m_sessions.constEnd())
        return {};

    const QByteArray sealed = aeadSeal(session->key, plaintext, kAadFileBytes);
    if (sealed.size() < kNonceLen + kTagLen)
        return {};
    return sealed;
}

bool CryptoManager::decryptFileBytes(const QString &peerRef, const QByteArray &data, QByteArray *outPlain) const
{
    if (data.size() < kNonceLen + kTagLen)
        return false;

    const auto session = m_sessions.constFind(resolveIdentity(peerRef));
    if (session == m_sessions.constEnd())
        return false;

    return aeadOpen(session->key, data, kAadFileBytes, outPlain);
}

QByteArray CryptoManager::cacheKeyFor(const QString &passphrase, const QByteArray &salt)
{
    QByteArray pass = passphrase.toUtf8();
    Wiper wipePass(pass);
    QCryptographicHash tag(QCryptographicHash::Sha256);
    tag.addData(salt);
    tag.addData(pass);
    return tag.result();
}

void CryptoManager::decryptAsync(const QString &ciphertext,
                                 const QString &passphrase,
                                 const QString &peerRef,
                                 const std::function<void(const QString &plain, bool delivered)> &done)
{
    // The queue, the caches and the in-flight flag are single-threaded by
    // design; the receive path on the GUI thread is the only caller. Called
    // from anywhere else the queue races - say so loudly in debug builds.
    Q_ASSERT_X(QThread::currentThread() == thread(), "CryptoManager::decryptAsync", "the async decrypt queue is confined to the CryptoManager's own thread");

    // The classification is exactly decrypt()'s, minus the derivation: session
    // keys and cache hits resolve on the spot, and only a passphrase message
    // with an unseen salt has to wait for the worker thread. The strings have
    // to match decrypt()'s, or a message would show a different error depending
    // on whether a slow message queued in front of it.
    const QString peerId = resolveIdentity(peerRef);
    const bool expectSealed = m_sessions.contains(peerId) || !passphrase.isEmpty();
    const QString marker = wireMarker();

    const QString errorString = i18nc("@info shown in place of a message body", "[decrypt error: invalid key or tampered packet]");

    QString fastResult; // set when the message needs no derivation
    bool needsDerivation = false;
    QByteArray salt;
    QByteArray rest;

    if (!ciphertext.startsWith(marker)) {
        fastResult = expectSealed ? i18nc("@info shown in place of a message body", "[decrypt error: cleartext on a keyed channel]") : ciphertext;
    } else {
        const QByteArray wire = QByteArray::fromBase64(ciphertext.mid(marker.size()).toLatin1());
        if (wire.isEmpty()) {
            fastResult = i18nc("@info shown in place of a message body", "[decrypt error: empty packet]");
        } else {
            const char type = wire.at(0);
            const QByteArray payload = wire.mid(1);
            if (type == kAadSessionMessage) {
                const auto session = m_sessions.constFind(peerId);
                QByteArray plain;
                if (session == m_sessions.constEnd() || !aeadOpen(session->key, payload, type, &plain))
                    fastResult = errorString;
                else
                    fastResult = QString::fromUtf8(plain);
            } else if (type == kAadPassphraseMessage && payload.size() > kSaltLen && !passphrase.isEmpty()) {
                salt = payload.left(kSaltLen);
                rest = payload.mid(kSaltLen);
                const auto cached = m_passphraseKeyCache.constFind(cacheKeyFor(passphrase, salt));
                if (cached != m_passphraseKeyCache.constEnd()) {
                    QByteArray plain;
                    if (!aeadOpen(*cached, rest, type, &plain))
                        fastResult = errorString;
                    else
                        fastResult = QString::fromUtf8(plain);
                } else {
                    needsDerivation = true;
                }
            } else {
                fastResult = errorString;
            }
        }
    }

    if (!needsDerivation) {
        // A fast message outruns nothing when the line is empty; otherwise it
        // takes the back of the queue so it cannot overtake a passphrase
        // message that is already in line in front of it.
        if (m_pendingDecrypts.isEmpty() && !m_derivationInFlight) {
            done(fastResult, true);
            return;
        }
        // Queue full and this message only needs a slot to stay in order, not a
        // derivation: under a flood it costs nothing to drop, so drop it rather
        // than grow the queue past the cap.
        if (m_pendingDecrypts.size() >= kMaxPendingDecrypts) {
            done({}, false);
            return;
        }
        PendingDecrypt entry;
        entry.result = fastResult;
        entry.done = done;
        m_pendingDecrypts.append(entry);
        return;
    }

    // Queue is full or body is too large — drop it. delivered=false so it
    // doesn't show up as a decrypt error row.
    if (rest.size() > kMaxAsyncPayloadBytes || m_pendingDecrypts.size() >= kMaxPendingDecrypts) {
        done({}, false);
        return;
    }

    PendingDecrypt entry;
    entry.needsDerivation = true;
    entry.salt = salt;
    entry.payload = rest;
    entry.passphrase = passphrase;
    entry.done = done;
    m_pendingDecrypts.append(entry);

    if (!m_derivationInFlight)
        startDerivation();
}

void CryptoManager::startDerivation()
{
    if (m_pendingDecrypts.isEmpty() || m_derivationInFlight)
        return;

    const PendingDecrypt &head = m_pendingDecrypts.first();
    if (!head.needsDerivation)
        return; // the drain loop resolves fast entries without the worker

    if (!m_deriveThread) {
        m_deriveThread = new QThread(this);
        m_deriveWorker = new DeriveWorker;
        m_deriveWorker->moveToThread(m_deriveThread);
        connect(m_deriveWorker, &DeriveWorker::derived, this, &CryptoManager::onDerived);
        m_deriveThread->start();
    }

    m_derivationInFlight = true;
    QMetaObject::invokeMethod(m_deriveWorker, "derive", Qt::QueuedConnection, Q_ARG(QString, head.passphrase), Q_ARG(QByteArray, head.salt));
}

void CryptoManager::onDerived(const QByteArray &salt, std::shared_ptr<QByteArray> key, bool ok)
{
    m_derivationInFlight = false;
    if (m_pendingDecrypts.isEmpty())
        return;

    PendingDecrypt entry = m_pendingDecrypts.takeFirst();
    QByteArray plain;
    bool opened = false;
    if (ok && entry.needsDerivation && entry.salt == salt) {
        // Cache the derived key — same salt won't need a second derivation.
        const QByteArray cacheKey = cacheKeyFor(entry.passphrase, salt);
        m_passphraseKeyCache[cacheKey] = *key;
        opened = aeadOpen(*key, entry.payload, kAadPassphraseMessage, &plain);
    }

    const QString result =
        (opened && ok) ? QString::fromUtf8(plain) : i18nc("@info shown in place of a message body", "[decrypt error: invalid key or tampered packet]");
    entry.done(result, true);

    // Drain fast entries (session-keyed, cache hits) in queue order.
    // Don't let a session message jump ahead of a passphrase message.
    while (!m_pendingDecrypts.isEmpty() && !m_pendingDecrypts.first().needsDerivation) {
        PendingDecrypt fast = m_pendingDecrypts.takeFirst();
        fast.done(fast.result, true);
    }
    if (!m_pendingDecrypts.isEmpty())
        startDerivation();
}

} // namespace koutnet

#include "CryptoManager.moc"
