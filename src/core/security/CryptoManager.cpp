// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "CryptoManager.h"
#include "SecretStore.h"
#include "koutnet_crypto_debug.h"

#include <KLocalizedString>

#include <QCryptographicHash>
#include <QDateTime>
#include <QSettings>
#include <QTimer>
#include <QtEndian> // qToBigEndian/qFromBigEndian, for the voice frame timestamp

#include <algorithm> // nth_element, for the replay cache eviction
#include <cmath>
#include <cstring> // memcmp, for the crypto_kx role tiebreak

#include <sodium.h>

namespace koutnet
{

// The wire and the wallet both assume these, so a libsodium that disagreed would
// have to be found here rather than in a packet that will not open.
static_assert(CryptoManager::kSaltLen == crypto_pwhash_SALTBYTES);
static_assert(CryptoManager::kNonceLen == crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
static_assert(CryptoManager::kTagLen == crypto_aead_xchacha20poly1305_ietf_ABYTES);
static_assert(CryptoManager::kKeyLen == crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
static_assert(CryptoManager::kKeyLen == crypto_kx_SESSIONKEYBYTES);
static_assert(CryptoManager::kKeyLen == crypto_auth_KEYBYTES);

namespace
{

// Everything below is undefined before this has run, so it gates construction
// rather than being left to whoever writes the next main(). The static makes it
// once per process and thread-safe; sodium_init() returns 1 when already done
// and only a negative result means the library cannot be used.
bool sodiumReady()
{
    static const bool ok = sodium_init() >= 0;
    return ok;
}

// Argon2id at the interactive setting: 64 MiB and 2 passes. Deliberately not
// MODERATE, because a decrypting peer derives against a salt the *sender* chose,
// so every message with an unseen salt is a fresh derivation - the memory cost
// is an amplifier pointed at us, not only at an attacker guessing the passphrase.
constexpr unsigned long long kPwhashOps = crypto_pwhash_OPSLIMIT_INTERACTIVE;
constexpr size_t kPwhashMem = crypto_pwhash_MEMLIMIT_INTERACTIVE;

// Domain tags, bound as AEAD associated data so that a ciphertext only opens in
// the slot it was sealed for. Also the wire type byte for the two message forms.
constexpr char kAadSessionMessage = 0x01;
constexpr char kAadPassphraseMessage = 0x02;
constexpr char kAadVoiceFrame = 0x10;

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

// Overwrite key material before the buffer is freed. QByteArray and QString only
// release the block, so a private key, a shared secret or a passphrase stays
// legible in the heap - and a core dump, a swap file or a hibernation image is
// where it turns up. data() detaches first, so this wipes only the copy in hand:
// correct for a buffer nothing else references, which every call site below is. A
// session key going into m_sessionKeys is moved rather than copied, and wiped there.
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

// Wallet entry names; the QSettings paths they replaced are in migrateLegacyKeys().
// An empty scope is the application's own identity, so its names stay as they were.
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
    cleanse(m_identitySk);
    cleanse(m_dhSk);

    for (auto it = m_sessionKeys.begin(); it != m_sessionKeys.end(); ++it) {
        cleanse(it->rx);
        cleanse(it->tx);
    }
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

// The private keys live in KWallet, never in QSettings. A session without a
// wallet still gets a keypair so the app works, it just forgets it on exit.
bool CryptoManager::initKeypairs()
{
    if (!loadStoredKeys()) {
        if (!generateAndStoreKeys())
            return false;
    }

    if (m_identitySk.size() != crypto_sign_SECRETKEYBYTES || m_dhSk.size() != crypto_kx_SECRETKEYBYTES)
        return false;

    // The public halves are recomputed from the secrets rather than stored, so a
    // wallet entry that was tampered with cannot make us advertise a key we
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

// Deliberately not part of the migration branch: deleting the plaintext used to be
// a side effect of the one run that filled the wallet, so a deletion that never
// reached the disk was never retried, and the readable copy stayed forever.
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

// The config file can carry a different key pair than the wallet: a run where the
// wallet was unreachable generated a throwaway identity, or an old config file was
// restored. That plaintext still has to go, but not before the wallet has a copy.
bool CryptoManager::stashSupersededPlaintextKeys()
{
    QString legacyId;
    QString legacyDh;
    QString walletId;
    QString walletDh;
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
    if (!SecretStore::read(identityWalletKey(m_storageScope), &idB64) || !SecretStore::read(dhWalletKey(m_storageScope), &dhB64) || idB64.isEmpty()
        || dhB64.isEmpty()) {
        if (!migrateLegacyKeys(&idB64, &dhB64))
            return false;
    } else {
        // The wallet is the only copy used from here on, so anything left in the config
        // file is pure liability. Checked on every start because an earlier run may
        // have filled the wallet and then failed to rewrite the file.
        dropLegacyPlaintextKeys();
    }

    QByteArray idRaw = QByteArray::fromBase64(idB64.toLatin1());
    QByteArray dhRaw = QByteArray::fromBase64(dhB64.toLatin1());
    Wiper wipeIdRaw(idRaw);
    Wiper wipeDhRaw(dhRaw);

    // The wallet holds the Ed25519 seed and the X25519 scalar, both 32 bytes -
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

    // A wallet we cannot reach is not a reason to refuse to run, but it is a reason
    // to say so: this keypair lasts the session, so peers see a new fingerprint next.
    if (!SecretStore::write(identityWalletKey(m_storageScope), idB64) || !SecretStore::write(dhWalletKey(m_storageScope), dhB64)) {
        qCCritical(KOUTNET_LOG_CRYPTO,
                   "could not store the identity keys in KWallet (%s). Running with a "
                   "throwaway identity for this session - it is NOT written to disk in "
                   "plain text.",
                   qUtf8Printable(SecretStore::lastError()));
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

// The handle every map in here is keyed on. A digest and not the key itself, so it
// is short enough for a packet and a log line, and cannot be confused with an address.
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

    // An address belongs to one identity at a time: whoever proved a handshake from it
    // most recently owns it. Both peers keep their sessions, only the shortcut moves.
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
    // Lengths first, and exactly rather than at least: the OpenSSL key objects
    // this replaced rejected a wrong-sized key on construction, whereas libsodium
    // takes a bare pointer and would read past a short buffer.
    if (!m_valid || peerDhBytes.size() != crypto_kx_PUBLICKEYBYTES || peerIdBytes.size() != crypto_sign_PUBLICKEYBYTES
        || peerDhSig.size() != crypto_sign_BYTES) {
        return HandshakeOutcome::Refused;
    }

    if (crypto_sign_verify_detached(reinterpret_cast<const unsigned char *>(peerDhSig.constData()),
                                    reinterpret_cast<const unsigned char *>(peerDhBytes.constData()),
                                    static_cast<unsigned long long>(peerDhBytes.size()),
                                    reinterpret_cast<const unsigned char *>(peerIdBytes.constData()))
        != 0) {
        return HandshakeOutcome::Refused;
    }

    // Past this line the payload has proved itself: whoever wrote it holds the private
    // half of id_pub, whatever address it came from. The only thing worth keying on.
    const QString peerId = identityIdFor(peerIdBytes);
    if (outPeerId)
        *outPeerId = peerId;

    // Trust on first use, on the identity. The id is a digest of the key, so a pin
    // that disagrees with it is unreachable - cheaper to refuse than to reason about.
    const auto pinned = m_peerIdPub.constFind(peerId);
    if (pinned != m_peerIdPub.constEnd() && *pinned != peerIdBytes)
        return HandshakeOutcome::Refused;

    // Someone else is already at this address and still has a live session. The
    // identity above is not in doubt, so this is not a trust decision - but handing a
    // stranger the slot of a peer the user is talking to is the visible half of a
    // takeover. The newcomer can have a session from an address that is not taken.
    const QString sitting = m_addressToId.value(observedAddress);
    if (!sitting.isEmpty() && sitting != peerId && m_sessionKeys.contains(sitting)) {
        // presence repeats every couple of seconds, so warn once per offending key
        if (m_warnedIdPub.value(observedAddress) != peerIdBytes) {
            m_warnedIdPub[observedAddress] = peerIdBytes;
            Q_EMIT peerIdentityChanged(observedAddress, bytesToFingerprint(m_peerIdPub.value(sitting)), bytesToFingerprint(peerIdBytes));
        }
        return HandshakeOutcome::AddressTaken; // the session we already had stays live and usable
    }

    // crypto_kx is asymmetric: one end must run the client half and the other the
    // server half, or the rx/tx pairs do not line up. Ordering the two public keys
    // decides that without a round trip and gives the same answer on both sides.
    const int order = std::memcmp(m_dhPubBytes.constData(), peerDhBytes.constData(), crypto_kx_PUBLICKEYBYTES);
    if (order == 0) {
        // Our own DH key came back at us, so this is a reflection - and there
        // would be no role left for the other end to take.
        return HandshakeOutcome::Refused;
    }

    QByteArray rx(kKeyLen, 0);
    QByteArray tx(kKeyLen, 0);
    Wiper wipeRx(rx);
    Wiper wipeTx(tx);

    // Also where a degenerate peer key dies: crypto_kx runs the scalar
    // multiplication, and libsodium refuses an all-zero result rather than
    // handing back a shared secret every machine would agree on.
    auto *rxp = reinterpret_cast<unsigned char *>(rx.data());
    auto *txp = reinterpret_cast<unsigned char *>(tx.data());
    const auto *ourPk = reinterpret_cast<const unsigned char *>(m_dhPubBytes.constData());
    const auto *ourSk = reinterpret_cast<const unsigned char *>(m_dhSk.constData());
    const auto *theirPk = reinterpret_cast<const unsigned char *>(peerDhBytes.constData());
    const int kxRc =
        order < 0 ? crypto_kx_client_session_keys(rxp, txp, ourPk, ourSk, theirPk) : crypto_kx_server_session_keys(rxp, txp, ourPk, ourSk, theirPk);
    if (kxRc != 0)
        return HandshakeOutcome::Refused;

    // The keys this replaces go first: a repeat handshake would otherwise leave the
    // previous pair unwiped in the heap. The hash then owns the only copy, and the
    // destructor is where that one gets wiped.
    const auto existing = m_sessionKeys.find(peerId);
    if (existing != m_sessionKeys.end()) {
        cleanse(existing->rx);
        cleanse(existing->tx);
    }

    // Moved, not copied, so the hash owns the only copy and the Wipers above turn
    // into no-ops on this path while still covering every refusal before it.
    m_sessionKeys[peerId] = SessionKeys{std::move(rx), std::move(tx)};
    m_peerIdPub[peerId] = peerIdBytes;
    noteObservedAddress(peerId, observedAddress);
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

// XChaCha20-Poly1305 rather than crypto_secretstream: this is a datagram
// protocol. secretstream is a single ordered byte stream with per-message state,
// so a lost or reordered UDP packet - routine for voice - desynchronises the
// receiver permanently, and it offers nothing to a protocol whose frames are
// already independent. The 192-bit nonce is the point of the choice: it is wide
// enough to draw at random per frame, so there is no counter to persist across
// restarts and no way for two frames to collide on one.
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
    QByteArray pass = passphrase.toUtf8();
    Wiper wipePass(pass);

    // A hash of the pair, not the pair itself. The cache key used to be the passphrase
    // with the salt appended, which kept the user's group passphrase in plain text here
    // for the life of the process. The salt is fixed length, so this is unambiguous.
    QCryptographicHash tag(QCryptographicHash::Sha256);
    tag.addData(salt);
    tag.addData(pass);
    const QByteArray cacheKey = tag.result();

    const auto cached = m_passphraseKeyCache.constFind(cacheKey);
    if (cached != m_passphraseKeyCache.constEnd())
        return *cached;

    // The only libsodium call reachable without a keypair, so it is also the only
    // one that has to check for itself that the library came up.
    if (!sodiumReady() || salt.size() != kSaltLen)
        return {};

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

QString CryptoManager::signPacket(const QString &peerRef, const QByteArray &payload) const
{
    // constFind rather than value(): a copy of the session key would be one more
    // buffer holding it, and one more thing to remember to wipe.
    const auto key = m_sessionKeys.constFind(resolveIdentity(peerRef));
    if (key == m_sessionKeys.constEnd())
        return QString();

    // Signed with tx, checked by the peer against its rx. Same shape on the wire
    // as the HMAC-SHA256 this replaces - base64 of 32 bytes - on crypto_auth's
    // HMAC-SHA512-256 instead.
    if (key->tx.size() != crypto_auth_KEYBYTES)
        return QString();

    unsigned char mac[crypto_auth_BYTES];
    if (crypto_auth(mac,
                    reinterpret_cast<const unsigned char *>(payload.constData()),
                    static_cast<unsigned long long>(payload.size()),
                    reinterpret_cast<const unsigned char *>(key->tx.constData()))
        != 0) {
        return QString();
    }

    const QString out = QString::fromLatin1(QByteArray(reinterpret_cast<char *>(mac), int(sizeof(mac))).toBase64());
    sodium_memzero(mac, sizeof(mac));
    return out;
}

bool CryptoManager::verifyPacket(const QString &peerRef, const QByteArray &payload, const QString &sigB64) const
{
    // no key means nothing was verified, so the answer is no. deciding which
    // pre-session packets may pass anyway is the caller's job, not ours.
    const auto key = m_sessionKeys.constFind(resolveIdentity(peerRef));
    if (key == m_sessionKeys.constEnd())
        return false;

    // Checked against rx, which is the peer's tx. crypto_auth_verify wants
    // exactly crypto_auth_BYTES and compares in constant time.
    const QByteArray given = QByteArray::fromBase64(sigB64.toLatin1());
    if (key->rx.size() != crypto_auth_KEYBYTES || given.size() != crypto_auth_BYTES)
        return false;

    return crypto_auth_verify(reinterpret_cast<const unsigned char *>(given.constData()),
                              reinterpret_cast<const unsigned char *>(payload.constData()),
                              static_cast<unsigned long long>(payload.size()),
                              reinterpret_cast<const unsigned char *>(key->rx.constData()))
        == 0;
}

bool CryptoManager::checkReplay(const QString &peerRef, const QString &nonceHex, double ts) const
{
    const double now = nowEpoch();
    if (std::abs(now - ts) > kReplayWindowSec)
        return false;

    // The identity when there is one, so moving to another interface does not hand
    // the sender a fresh window to replay into. A stranger falls back to its address.
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

    // A peer sending fresh nonces faster than the TTL retires them outruns the sweep
    // above, so the newest half is kept. Ordered by arrival and not by the clock: a
    // flood puts thousands of these in one millisecond. Forgetting an old nonce lets a
    // packet from that far back be replayed once, which beats a bucket that grows.
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

void CryptoManager::evictOldestNoncePeers() const
{
    // Under a flood from spoofed source addresses this runs for every packet, so it
    // does not sort - it takes the least recently used bucket and drops it.
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
    // any left over the cap go together. Unreachable unless the hashes disagree.
    if (m_seenNonces.size() > kMaxNoncePeers) {
        m_seenNonces.clear();
        m_nonceBucketTouched.clear();
    }
}

bool CryptoManager::checkRate(const QString &sourceAddress, int maxPerSec) const
{
    const double now = nowEpoch();

    // Keyed on the source address, so a flood from spoofed ones grows this the way
    // it grew the replay cache. A window nobody added to for a second is worth none.
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
    const auto sessionKey = m_sessionKeys.constFind(resolveIdentity(peerRef));
    if (sessionKey != m_sessionKeys.constEnd()) {
        const QByteArray sealed = aeadSeal(sessionKey->tx, data, kAadSessionMessage);
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
    const bool expectSealed = m_sessionKeys.contains(peerId) || !passphrase.isEmpty();

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
        const auto sessionKey = m_sessionKeys.constFind(peerId);
        if (sessionKey != m_sessionKeys.constEnd())
            ok = aeadOpen(sessionKey->rx, payload, type, &plain);
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
    // empty means "not encrypted", and the caller has to drop the frame. voice
    // in the clear is not a graceful degradation, it is the bug.
    const auto key = m_sessionKeys.constFind(resolveIdentity(peerRef));
    if (key == m_sessionKeys.constEnd())
        return {};

    QByteArray sealed = aeadSeal(key->tx, plaintext, kAadVoiceFrame);
    if (sealed.size() < kNonceLen + kTagLen)
        return {};

    // The timestamp rides next to the nonce so the receiver's replay window can
    // judge the frame like any other packet; without it a captured frame could
    // be played back forever, nonce and all.
    const quint64 tsMs = qToBigEndian<quint64>(static_cast<quint64>(nowEpoch() * 1000.0));
    sealed.insert(kNonceLen, reinterpret_cast<const char *>(&tsMs), kVoiceTsLen);
    return sealed;
}

bool CryptoManager::decryptBytes(const QString &peerRef, const QByteArray &data, QByteArray *outPlain) const
{
    if (data.size() < kNonceLen + kVoiceTsLen + kTagLen)
        return false;

    const auto key = m_sessionKeys.constFind(resolveIdentity(peerRef));
    if (key == m_sessionKeys.constEnd())
        return false; // unkeyed frames are not audio we are willing to play

    quint64 tsMs = 0;
    memcpy(&tsMs, data.constData() + kNonceLen, kVoiceTsLen);
    tsMs = qFromBigEndian<quint64>(tsMs);
    if (!checkReplay(peerRef, QString::fromLatin1(data.left(kNonceLen).toHex()), double(tsMs) / 1000.0))
        return false; // a frame we have already played, or one outside the window

    // aeadOpen() reads the nonce back off the front of what it is given, so the
    // timestamp has to come back out before the ciphertext goes in.
    return aeadOpen(key->rx, data.left(kNonceLen) + data.mid(kNonceLen + kVoiceTsLen), kAadVoiceFrame, outPlain);
}

} // namespace koutnet
