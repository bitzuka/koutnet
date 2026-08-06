// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - secret storage backed by KWallet.
//
// Only the values that must never touch a config file live here: the Ed25519
// and X25519 private keys, and the group passphrase.
#pragma once

#include <QString>
#include <QStringList>

namespace koutnet
{

// Thin wrapper over KWallet::Wallet. One wallet handle per process, because
// opening it is a blocking round trip to the daemon.
//
// Every function returns false when the wallet cannot be used at all - no
// daemon, a session that is not KDE, a CI container. That is a hard failure
// on purpose: a caller that cannot store a secret must refuse to, never write
// it somewhere readable instead.
class SecretStore
{
public:
    static bool isAvailable();

    static bool read(const QString &key, QString *outValue);
    static bool write(const QString &key, const QString &value);
    static bool remove(const QString &key);

    // Deletes plaintext leftovers of the given absolute QSettings keys and
    // confirms against the file on disk that they are really gone. True also
    // when there was nothing to delete, so callers can run it on every start.
    //
    // Confirmation is not paranoia: QSettings::remove() reports nothing, an
    // unwritable config file makes sync() fail quietly, and a key that only
    // exists in a fallback location cannot be removed at all.
    static bool purgePlaintextConfigKeys(const QStringList &keys, QString *outDetail);

    static QString lastError();
};

} // namespace koutnet
