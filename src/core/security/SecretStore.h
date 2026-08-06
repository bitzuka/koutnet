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
    // Keeps every secret in this process and never opens KWallet at all.
    //
    // There is one wallet per session and QStandardPaths test mode does not move
    // it, so a test run that reaches KWallet writes its throwaway keys into the
    // user's real keyring and leaves them there - two dozen entries called
    // identity_priv_b64_peer-a and the like, sitting among the real ones. On by
    // default whenever QStandardPaths test mode is on, which every suite under
    // src/tests already switches on in its main(), so the isolation does not
    // depend on anyone remembering this call; the suites make it anyway, because
    // a promise about the user's keyring belongs where it is relied on.
    static void setInMemoryOnly(bool enabled);
    static bool isInMemoryOnly();

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
