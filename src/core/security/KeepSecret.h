// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - encrypted file-based secret storage.
//
// Only the values that must never touch a config file live here: the Ed25519
// and X25519 private keys, and the group passphrase.
#pragma once

#include <QString>
#include <QStringList>

namespace koutnet
{

// Encrypted file-backed secret store.  Secrets are serialised into a single
// blob, encrypted with XChaCha20-Poly1305 via libsodium, and written to
// $XDG_DATA_HOME/koutnet/secrets.enc.  The key is derived per-machine with
// Argon2id from a compile-time passphrase and /etc/machine-id.
//
// Every function returns false when the store cannot be used at all - a read-
// only filesystem, missing libsodium, a CI container.  That is a hard failure
// on purpose: a caller that cannot store a secret must refuse to, never write
// it somewhere readable instead.
class KeepSecret
{
public:
    // Keeps every secret in this process and never touches the file at all.
    //
    // On by default whenever QStandardPaths test mode is on, which every suite
    // under src/tests already switches on in its main(), so the isolation does
    // not depend on anyone remembering this call; the suites make it anyway,
    // because a promise about the user's secrets belongs where it is relied on.
    static void setInMemoryOnly(bool enabled);
    static bool isInMemoryOnly();

    static bool isAvailable();

    static bool read(const QString &key, QString *outValue);
    static bool write(const QString &key, const QString &value);
    static bool remove(const QString &key);

    // Deletes plaintext leftovers of the given absolute QSettings keys and
    // confirms against the file on disk that they are really gone.  True also
    // when there was nothing to delete, so callers can run it on every start.
    static bool purgePlaintextConfigKeys(const QStringList &keys, QString *outDetail);

    static QString lastError();
};

} // namespace koutnet
