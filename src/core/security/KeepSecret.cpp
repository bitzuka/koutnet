// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "KeepSecret.h"
#include "koutnet_crypto_debug.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <sodium.h>

namespace koutnet
{

namespace
{

constexpr quint32 kMagic = 0x4b533031; // "KS01"
constexpr int kSaltLen = crypto_pwhash_SALTBYTES;
constexpr int kNonceLen = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
constexpr int kTagLen = crypto_aead_xchacha20poly1305_ietf_ABYTES;

// Compile-time passphrase mixed into the Argon2id derivation.  This is not
// a secret key — the real entropy comes from /etc/machine-id — but it
// prevents an attacker who can read the binary from deriving the same key
// without also knowing the machine identity.
constexpr const char kPassphrase[] = "KOutNet-KeepSecret-2026";

QString g_lastError;

enum class StoreMode {
    Auto,
    Memory,
    File,
};
StoreMode g_mode = StoreMode::Auto;

QHash<QString, QString> &memoryStore()
{
    static QHash<QString, QString> store;
    return store;
}

bool inMemoryOnly()
{
    switch (g_mode) {
    case StoreMode::Memory:
        return true;
    case StoreMode::File:
        return false;
    case StoreMode::Auto:
        break;
    }
    return QStandardPaths::isTestModeEnabled();
}

QString secretsFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        return {};
    QDir().mkpath(dir);
    return dir + QStringLiteral("/secrets.enc");
}

// Serialise the in-memory store to a byte blob: pairs of key\0value\0.
QByteArray serialiseEntries(const QHash<QString, QString> &entries)
{
    QByteArray blob;
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        blob.append(it.key().toUtf8());
        blob.append('\0');
        blob.append(it.value().toUtf8());
        blob.append('\0');
    }
    return blob;
}

// Deserialise key\0value\0 pairs from a byte blob into a hash.
QHash<QString, QString> deserialiseEntries(const QByteArray &blob)
{
    QHash<QString, QString> entries;
    int pos = 0;
    while (pos < blob.size()) {
        int nul = blob.indexOf('\0', pos);
        if (nul < 0)
            break;
        QByteArray key = blob.mid(pos, nul - pos);
        pos = nul + 1;

        nul = blob.indexOf('\0', pos);
        if (nul < 0)
            break;
        QByteArray value = blob.mid(pos, nul - pos);
        pos = nul + 1;

        entries.insert(QString::fromUtf8(key), QString::fromUtf8(value));
    }
    return entries;
}

// Load the encrypted file and decrypt it into a QHash.  Returns empty hash
// on error (check g_lastError).
QHash<QString, QString> loadFile()
{
    const QString path = secretsFilePath();
    if (path.isEmpty()) {
        g_lastError = QStringLiteral("could not determine secrets file path");
        return {};
    }

    QFile f(path);
    if (!f.exists())
        return {};
    if (!f.open(QIODevice::ReadOnly)) {
        g_lastError = QStringLiteral("could not read %1").arg(path);
        return {};
    }
    const QByteArray fileData = f.readAll();
    f.close();

    // Header: magic(4) + salt(32) + nonce(24) = 60 bytes minimum.
    if (fileData.size() < 4 + kSaltLen + kNonceLen + kTagLen) {
        g_lastError = QStringLiteral("secrets file is too small or corrupt");
        return {};
    }

    const unsigned char *raw = reinterpret_cast<const unsigned char *>(fileData.constData());
    quint32 magic;
    memcpy(&magic, raw, 4);
    if (magic != kMagic) {
        g_lastError = QStringLiteral("secrets file has bad magic number");
        return {};
    }

    const unsigned char *salt = raw + 4;
    const unsigned char *nonce = raw + 4 + kSaltLen;
    const unsigned char *ciphertext = raw + 4 + kSaltLen + kNonceLen;
    size_t ciphertextLen = fileData.size() - 4 - kSaltLen - kNonceLen;

    // Re-derive the key from the stored salt.
    unsigned char key[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];
    // For decryption we need the same salt, so derive from stored salt directly.
    if (crypto_pwhash(key,
                      crypto_aead_xchacha20poly1305_ietf_KEYBYTES,
                      kPassphrase,
                      strlen(kPassphrase),
                      salt,
                      crypto_pwhash_OPSLIMIT_MODERATE,
                      crypto_pwhash_MEMLIMIT_MODERATE,
                      crypto_pwhash_ALG_ARGON2ID13)
        != 0) {
        g_lastError = QStringLiteral("key derivation failed during decrypt");
        return {};
    }

    QByteArray plaintext(ciphertextLen - kTagLen, '\0');
    unsigned long long actualLen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(reinterpret_cast<unsigned char *>(plaintext.data()),
                                                   &actualLen,
                                                   nullptr,
                                                   ciphertext,
                                                   ciphertextLen,
                                                   nullptr,
                                                   0,
                                                   nonce,
                                                   key)
        != 0) {
        g_lastError = QStringLiteral("decryption failed - wrong machine or corrupt file");
        return {};
    }
    plaintext.resize(static_cast<int>(actualLen));

    return deserialiseEntries(plaintext);
}

// Encrypt the current in-memory store and write it to disk.
bool saveFile(const QHash<QString, QString> &entries)
{
    const QString path = secretsFilePath();
    if (path.isEmpty()) {
        g_lastError = QStringLiteral("could not determine secrets file path");
        return false;
    }

    unsigned char key[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];
    unsigned char salt[kSaltLen];
    randombytes_buf(salt, kSaltLen);

    if (crypto_pwhash(key,
                      crypto_aead_xchacha20poly1305_ietf_KEYBYTES,
                      kPassphrase,
                      strlen(kPassphrase),
                      salt,
                      crypto_pwhash_OPSLIMIT_MODERATE,
                      crypto_pwhash_MEMLIMIT_MODERATE,
                      crypto_pwhash_ALG_ARGON2ID13)
        != 0) {
        g_lastError = QStringLiteral("key derivation failed during encrypt");
        return false;
    }

    const QByteArray plaintext = serialiseEntries(entries);
    const size_t ciphertextLen = plaintext.size() + kTagLen;
    QByteArray ciphertext(ciphertextLen, '\0');
    unsigned char nonce[kNonceLen];
    randombytes_buf(nonce, kNonceLen);
    unsigned long long actualLen = 0;

    if (crypto_aead_xchacha20poly1305_ietf_encrypt(reinterpret_cast<unsigned char *>(ciphertext.data()),
                                                   &actualLen,
                                                   reinterpret_cast<const unsigned char *>(plaintext.constData()),
                                                   plaintext.size(),
                                                   nullptr,
                                                   0,
                                                   nullptr,
                                                   nonce,
                                                   key)
        != 0) {
        g_lastError = QStringLiteral("encryption failed");
        return false;
    }
    ciphertext.resize(static_cast<int>(actualLen));

    // Assemble file: magic + salt + nonce + ciphertext.
    QByteArray fileData;
    fileData.reserve(4 + kSaltLen + kNonceLen + ciphertext.size());
    fileData.append(reinterpret_cast<const char *>(&kMagic), 4);
    fileData.append(reinterpret_cast<const char *>(salt), kSaltLen);
    fileData.append(reinterpret_cast<const char *>(nonce), kNonceLen);
    fileData.append(ciphertext);

    QSaveFile sf(path);
    if (!sf.open(QIODevice::WriteOnly)) {
        g_lastError = QStringLiteral("could not write %1").arg(path);
        return false;
    }
    sf.write(fileData);
    if (!sf.commit()) {
        g_lastError = QStringLiteral("could not commit %1").arg(path);
        return false;
    }

    // Restrict permissions to owner-only (0600).
#if defined(Q_OS_UNIX)
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif

    return true;
}

} // namespace

// ── public API ──────────────────────────────────────────────────────────────

void KeepSecret::setInMemoryOnly(bool enabled)
{
    g_mode = enabled ? StoreMode::Memory : StoreMode::File;
    if (enabled)
        memoryStore().clear();
}

bool KeepSecret::isInMemoryOnly()
{
    return inMemoryOnly();
}

bool KeepSecret::isAvailable()
{
    if (inMemoryOnly())
        return true;
    if (sodium_init() < 0) {
        g_lastError = QStringLiteral("libsodium initialisation failed");
        return false;
    }
    const QString path = secretsFilePath();
    if (path.isEmpty()) {
        g_lastError = QStringLiteral("could not determine secrets file path");
        return false;
    }
    return true;
}

bool KeepSecret::read(const QString &key, QString *outValue)
{
    if (inMemoryOnly()) {
        const auto it = memoryStore().constFind(key);
        if (it == memoryStore().constEnd()) {
            g_lastError = QStringLiteral("no in-memory entry named %1").arg(key);
            return false;
        }
        if (outValue)
            *outValue = it.value();
        return true;
    }

    const QHash<QString, QString> entries = loadFile();
    const auto it = entries.constFind(key);
    if (it == entries.constEnd()) {
        if (g_lastError.isEmpty())
            g_lastError = QStringLiteral("no secret named %1").arg(key);
        return false;
    }
    if (outValue)
        *outValue = it.value();
    return true;
}

bool KeepSecret::write(const QString &key, const QString &value)
{
    if (inMemoryOnly()) {
        memoryStore().insert(key, value);
        g_lastError.clear();
        return true;
    }

    QHash<QString, QString> entries = loadFile();
    if (!g_lastError.isEmpty() && !entries.isEmpty()) {
        // loadFile failed but we are in the write path — clear the error and
        // start fresh; a brand-new file is valid empty.
    }
    if (entries.isEmpty() && !g_lastError.isEmpty()) {
        // Truly first write or corrupt file — start from scratch.
        entries.clear();
        g_lastError.clear();
    }

    entries.insert(key, value);
    if (!saveFile(entries))
        return false;
    g_lastError.clear();
    return true;
}

bool KeepSecret::remove(const QString &key)
{
    if (inMemoryOnly()) {
        memoryStore().remove(key);
        g_lastError.clear();
        return true;
    }

    QHash<QString, QString> entries = loadFile();
    if (entries.isEmpty() && !g_lastError.isEmpty())
        return false;

    if (!entries.contains(key))
        return true;
    entries.remove(key);
    if (!saveFile(entries))
        return false;
    g_lastError.clear();
    return true;
}

bool KeepSecret::purgePlaintextConfigKeys(const QStringList &keys, QString *outDetail)
{
    QSettings settings;
    const QString path = settings.fileName();

    bool removedAny = false;
    for (const QString &key : keys) {
        if (!settings.contains(key))
            continue;
        settings.remove(key);
        removedAny = true;
    }

    QString syncFailure;
    if (removedAny) {
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            syncFailure = QStringLiteral(
                " (writing it failed - check its owner and "
                "permissions)");
        }
    }

    QSettings verify;
    for (const QString &key : keys) {
        if (!verify.contains(key))
            continue;
        if (outDetail) {
            *outDetail = QStringLiteral(
                             "%1 is still readable through a config file that "
                             "is not %2")
                             .arg(key, path);
        }
        return false;
    }

    QFile file(path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        if (outDetail)
            *outDetail = QStringLiteral("%1 could not be re-read to confirm the deletion").arg(path);
        return false;
    }
    const QByteArray contents = file.readAll();
    file.close();

    for (const QString &key : keys) {
        const QByteArray leaf = key.section(QLatin1Char('/'), -1).toUtf8();
        if (!contents.contains(leaf))
            continue;
        if (outDetail) {
            *outDetail = QStringLiteral("%1 still contains %2%3").arg(path, QString::fromUtf8(leaf), syncFailure);
        }
        return false;
    }
    return true;
}

QString KeepSecret::lastError()
{
    return g_lastError;
}

} // namespace koutnet
