// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - persisted app settings (username, connection mode, relay config)
// Everything that goes on disk is described in koutnetsettings.kcfg and
// generated from it. What is left here is what the generator cannot know: the
// group passphrase belongs in KWallet, the config file predates KConfig, and
// somebody has to decide when the file is written.
#pragma once

#include "koutnetsettings.h"

#include <QString>

class QTimer;

namespace koutnet
{

class AppSettings : public KOutNetSettings
{
    Q_OBJECT
    // Deliberately not an entry in the kcfg: a passphrase in a config file is
    // a passphrase every process of this user can read. SecretStore owns it,
    // and a session without a wallet keeps it for that session only.
    Q_PROPERTY(QString groupPassphrase READ groupPassphrase WRITE setGroupPassphrase NOTIFY groupPassphraseChanged)

public:
    explicit AppSettings(QObject *parent = nullptr);
    ~AppSettings() override;

    // Shared passphrase for group encryption (the PSK layer in CryptoManager).
    // Empty is not "send in the clear": NetworkManager::sendGroupMessage()
    // refuses to send at all, because a user who never set one has no reason
    // to expect their group messages on the wire in plain text.
    QString groupPassphrase() const
    {
        return m_groupPassphrase;
    }
    void setGroupPassphrase(const QString &passphrase);

Q_SIGNALS:
    void groupPassphraseChanged();

    // The group passphrase lives in KWallet, so a session without a wallet
    // cannot keep it at all. The UI has to say so rather than let the user
    // believe it was saved.
    void secretStoreUnavailable(const QString &reason);

private Q_SLOTS:
    // The generated setters only touch memory, so the file gets written from
    // here instead. Coalescing is the point: dragging the volume slider used
    // to rewrite the whole config file once per pixel.
    void scheduleSave();

private:
    void connectAutoSave();
    void migrateEscapedValues();
    void adoptLegacyConnectionMode();
    void adoptHandleAsDisplayName();
    void loadGroupPassphrase();
    // Deletes the clear-text passphrase and confirms it is gone. Safe to call on
    // every start; it is a no-op once the config file is clean.
    void dropLegacyPassphrase();
    void reportSecretStoreProblem(const QString &reason);

    QString m_groupPassphrase;
    QTimer *m_saveTimer = nullptr;
};

} // namespace koutnet
