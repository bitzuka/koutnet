// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - secret storage backed by KWallet.
//
// Only the values that must never touch a config file live here: the Ed25519
// and X25519 private keys, and the group passphrase. Everything else stays in
// QSettings where a user can read and edit it.
#pragma once

#include <QString>
#include <QStringList>

namespace koutnet
{

// Thin wrapper over KWallet::Wallet. There is one wallet handle per process
// because opening it is a blocking round trip to the daemon, and all of our
// entries share a single folder.
//
// Every function returns false when the wallet cannot be used at all - no
// daemon running, a session that is not KDE, a CI container. That is a hard
// failure on purpose: a caller that cannot store a secret must refuse to
// store it, never write it somewhere readable instead.
class SecretStore
{
public:
    static bool isAvailable();

    // Reads into outValue. False both for "no wallet" and "no such entry",
    // which are the same thing to every caller we have.
    static bool read(const QString &key, QString *outValue);
    static bool write(const QString &key, const QString &value);
    // True when the entry is gone, including when it was never there.
    static bool remove(const QString &key);

    // Deletes plaintext leftovers of the given absolute QSettings keys (for
    // example "security/dh_priv_b64") and confirms against the file on disk
    // that they are really gone. True also when there was nothing to delete,
    // so callers can run this on every start.
    //
    // Confirmation is not paranoia: QSettings::remove() reports nothing, an
    // unwritable config file makes sync() fail quietly, and a key that only
    // exists in a fallback location cannot be removed at all. outDetail gets
    // a sentence naming the file, for the log and for the user.
    static bool purgePlaintextConfigKeys(const QStringList &keys, QString *outDetail);

    // Why the last call failed, ready to put in front of a user.
    static QString lastError();
};

} // namespace koutnet
