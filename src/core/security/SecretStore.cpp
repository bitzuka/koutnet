// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "SecretStore.h"

#include <QDebug>
#include <QFile>
#include <QSettings>

#include <KWallet>

namespace koutnet
{

namespace
{

QString g_lastError;

// One folder for the whole application, created on first use.
QString folderName()
{
    return QStringLiteral("KOutNet");
}

// Returns an open wallet with our folder selected, or nullptr. The handle is
// cached because openWallet() blocks on the daemon, but it is re-opened when
// the daemon went away and came back mid-session.
KWallet::Wallet *openFolder()
{
    static KWallet::Wallet *wallet = nullptr;

    if (!wallet || !wallet->isOpen()) {
        delete wallet;
        // Synchronous on purpose: the identity keys are needed before the first
        // window appears, so there is nothing useful to do while waiting.
        wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0, KWallet::Wallet::Synchronous);
    }

    if (!wallet || !wallet->isOpen()) {
        delete wallet;
        wallet = nullptr;
        g_lastError = QStringLiteral(
            "no usable KWallet - is kwalletd running? KOutNet will not fall back to "
            "storing secrets in a plain config file.");
        return nullptr;
    }

    const QString folder = folderName();
    if (!wallet->hasFolder(folder) && !wallet->createFolder(folder)) {
        g_lastError = QStringLiteral("KWallet refused to create the KOutNet folder");
        return nullptr;
    }
    if (!wallet->setFolder(folder)) {
        g_lastError = QStringLiteral("KWallet refused to open the KOutNet folder");
        return nullptr;
    }

    g_lastError.clear();
    return wallet;
}

} // namespace

bool SecretStore::isAvailable()
{
    return openFolder() != nullptr;
}

bool SecretStore::read(const QString &key, QString *outValue)
{
    KWallet::Wallet *wallet = openFolder();
    if (!wallet)
        return false;

    QString value;
    if (wallet->readPassword(key, value) != 0) {
        g_lastError = QStringLiteral("no wallet entry named %1").arg(key);
        return false;
    }
    if (outValue)
        *outValue = value;
    return true;
}

bool SecretStore::write(const QString &key, const QString &value)
{
    KWallet::Wallet *wallet = openFolder();
    if (!wallet)
        return false;

    if (wallet->writePassword(key, value) != 0) {
        g_lastError = QStringLiteral("KWallet refused to store %1").arg(key);
        return false;
    }
    return true;
}

bool SecretStore::remove(const QString &key)
{
    KWallet::Wallet *wallet = openFolder();
    if (!wallet)
        return false;

    if (!wallet->hasEntry(key))
        return true;
    if (wallet->removeEntry(key) != 0) {
        g_lastError = QStringLiteral("KWallet refused to remove %1").arg(key);
        return false;
    }
    return true;
}

bool SecretStore::purgePlaintextConfigKeys(const QStringList &keys, QString *outDetail)
{
    // A QSettings of our own, with no group set, so the caller's keys are taken
    // as the absolute paths they are - and so a caller that still holds an
    // instance of its own does not get to decide when this write happens.
    QSettings settings;
    const QString path = settings.fileName();

    bool removedAny = false;
    for (const QString &key : keys) {
        if (!settings.contains(key))
            continue;
        settings.remove(key);
        removedAny = true;
    }

    // Kept rather than returned on: a failed sync() does not always mean the
    // file was left alone, so the checks below decide, and this only explains
    // why when they find the plaintext still there.
    QString syncFailure;
    if (removedAny) {
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            syncFailure = QStringLiteral(
                " (writing it failed - check its owner and "
                "permissions)");
        }
    }

    // Keys that came from a fallback location (an organisation-wide
    // ~/.config/<org>.conf, or a copy under /etc/xdg) survive remove() and
    // sync() without any error at all, because QSettings only ever writes the
    // primary file. A fresh instance still reading the key is the tell.
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

    // And the file itself has the last word, because both checks above go
    // through QSettings' shared in-memory copy, which remembers a removal that
    // never reached the disk and then answers "gone" for the rest of the
    // process. Matching the bare key name is deliberately broader than the
    // group-qualified path: any occurrence at all means it is still on disk.
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

QString SecretStore::lastError()
{
    return g_lastError;
}

} // namespace koutnet
