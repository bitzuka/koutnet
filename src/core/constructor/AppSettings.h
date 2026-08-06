// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// Everything that goes on disk is described in koutnetsettings.kcfg. What is
// left here is what the generator cannot know: the group passphrase belongs in
// KWallet, and somebody has to decide when the file is written.
#pragma once

#include "koutnetsettings.h"

#include <QString>

class QTimer;

namespace koutnet
{

class AppSettings : public KOutNetSettings
{
    Q_OBJECT
    // Deliberately not an entry in the kcfg: a passphrase in a config file is a
    // passphrase every process of this user can read. SecretStore owns it, and a
    // session without a wallet keeps it for that session only.
    Q_PROPERTY(QString groupPassphrase READ groupPassphrase WRITE setGroupPassphrase NOTIFY groupPassphraseChanged)

public:
    explicit AppSettings(QObject *parent = nullptr);
    ~AppSettings() override;

    // Shared passphrase for group encryption (the PSK layer in CryptoManager).
    // Empty is not "send in the clear": NetworkManager::sendGroupMessage()
    // refuses to send, because a user who never set one has no reason to expect
    // plaintext on the wire.
    QString groupPassphrase() const
    {
        return m_groupPassphrase;
    }
    void setGroupPassphrase(const QString &passphrase);

Q_SIGNALS:
    void groupPassphraseChanged();

    // The group passphrase lives in KWallet, so a session without a wallet
    // cannot keep it at all. The UI has to say so rather than let the user
    // believe it saved.
    void secretStoreUnavailable(const QString &reason);

private Q_SLOTS:
    // The generated setters only touch memory, so the file gets written from
    // here. Coalescing is the point: dragging the volume slider rewrote the file
    // per pixel.
    void scheduleSave();

private:
    void connectAutoSave();
    void migrateEscapedValues();
    // Remaps a connection mode written before the three K-Server modes became
    // one. Gated on configVersion, so it runs once per config file.
    void migrateConnectionModes();
    void adoptLegacyConnectionMode();
    void adoptHandleAsDisplayName();
    void loadGroupPassphrase();
    void dropLegacyPassphrase();
    void reportSecretStoreProblem(const QString &reason);

    QString m_groupPassphrase;
    QTimer *m_saveTimer = nullptr;
};

} // namespace koutnet
