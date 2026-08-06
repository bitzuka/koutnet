// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "AppSettings.h"
#include "../security/SecretStore.h"
#include "koutnet_crypto_debug.h"

#include <KConfigGroup>

#include <QMetaMethod>
#include <QMetaProperty>
#include <QSettings>
#include <QTimer>

namespace koutnet
{

namespace
{

// Wallet entry for the group-chat passphrase, formerly "app/group_passphrase".
QString passphraseWalletKey()
{
    return QStringLiteral("group_passphrase");
}

QStringList legacyConfigKeys()
{
    return {QStringLiteral("app/group_passphrase")};
}

QString configGroupName()
{
    return QStringLiteral("app");
}

// What has already been done to this file. Each step below is gated on its own
// number rather than on the current one, so adding a step does not re-run the
// ones before it: version 1 alone would otherwise send a file that had only ever
// needed the unescaping pass back through QSettings a second time.
//
// 1: values rewritten as UTF-8, undoing QSettings' \xNNNN escaping.
// 2: the three K-Server connection modes collapsed into one.
constexpr int kConfigVersionUnescaped = 1;
constexpr int kConfigVersionMergedKServer = 2;
constexpr int kCurrentConfigVersion = kConfigVersionMergedKServer;

// Long enough that a dragged slider settles into a single write, short enough
// that a crash immediately after a change is the only way to lose one.
constexpr int kSaveDelayMs = 1000;

} // namespace

AppSettings::AppSettings(QObject *parent)
    : KOutNetSettings()
{
    setParent(parent);

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(kSaveDelayMs);
    connect(m_saveTimer, &QTimer::timeout, this, [this]() {
        save();
    });

    // First, because it is the only step that still edits the file through
    // QSettings, and the last writer of any given run has to be KConfig.
    loadGroupPassphrase();
    migrateEscapedValues();
    // Before adoptLegacyConnectionMode(), which writes a mode in today's
    // numbering: the other way round and the remap would move the value that one
    // just wrote.
    migrateConnectionModes();
    adoptLegacyConnectionMode();
    adoptHandleAsDisplayName();
    // One place, after every step, rather than each stamping its own version.
    setConfigVersion(kCurrentConfigVersion);
    save();

    connectAutoSave();
}

AppSettings::~AppSettings()
{
    // A pending change is a change the user made; quitting is not a reason to
    // drop it.
    if (m_saveTimer->isActive()) {
        m_saveTimer->stop();
        save();
    }
}

// One connection per generated property, from the metaobject rather than by hand.
void AppSettings::connectAutoSave()
{
    const QMetaObject *mo = metaObject();
    const QMetaMethod slot = mo->method(mo->indexOfSlot("scheduleSave()"));
    const QMetaObject &generated = KOutNetSettings::staticMetaObject;
    for (int i = generated.propertyOffset(); i < generated.propertyCount(); ++i) {
        const QMetaProperty property = generated.property(i);
        if (property.hasNotifySignal())
            connect(this, property.notifySignal(), this, slot);
    }
}

void AppSettings::scheduleSave()
{
    m_saveTimer->start();
}

// QSettings stores anything outside ASCII as \xNNNN and quotes anything with a
// comma in it; KConfig hands both back verbatim, so a Cyrillic display name
// showed up as visible backslashes. Read the old file once through the writer
// that produced it.
void AppSettings::migrateEscapedValues()
{
    if (configVersion() >= kConfigVersionUnescaped)
        return;

    QSettings legacy;
    const KConfigSkeletonItem::List entries = items();
    for (KConfigSkeletonItem *entry : entries) {
        const QVariant value = legacy.value(configGroupName() + QLatin1Char('/') + entry->key());
        if (!value.isValid())
            continue;
        entry->setProperty(value);
    }
}

// ConnectionMode used to have five entries: LAN/VPN, a K-Server this user runs, a
// K-Server somebody else runs, a plain relay, and the maintainer's deployment.
// Three of those were one protocol at three addresses, so they are one mode now
// with the address as a setting, and relay moved down to fill the gap they left.
//
// Old -> new, which is the whole of it:
//   0 LanOrVpn          -> 0 LanOrVpn
//   1 KServerSelfHosted -> 1 KServer
//   2 KServerClient     -> 1 KServer
//   3 Relay             -> 2 Relay
//   4 MaintainerVds     -> 1 KServer
//
// Nobody loses a working connection: of the three that merged, none had a
// transport behind it - modeAvailable() said so for all three - so a config
// holding 1, 2 or 4 was a config for a mode that had never connected to anything.
// 0 and 3 are the two that worked, and both keep their meaning.
void AppSettings::migrateConnectionModes()
{
    if (configVersion() >= kConfigVersionMergedKServer)
        return;

    // Only a file that has the key. Without this, a pre-KConfig file whose mode
    // is still the old vds_mode bool would get a remap applied to the default 0
    // and then be written by adoptLegacyConnectionMode() anyway.
    KConfigGroup group(config(), configGroupName());
    if (!group.hasKey(QStringLiteral("connection_mode")))
        return;

    switch (connectionMode()) {
    case 1:
    case 2:
    case 4:
        setConnectionMode(1);
        break;
    case 3:
        setConnectionMode(2);
        break;
    default:
        // 0 is LAN/VPN in both numberings. Anything else was never a mode this
        // application wrote, and the kcfg clamps it to the range on read.
        break;
    }
}

void AppSettings::adoptLegacyConnectionMode()
{
    KConfigGroup group(config(), configGroupName());
    if (!group.hasKey(QStringLiteral("vds_mode")) || group.hasKey(QStringLiteral("connection_mode")))
        return;

    // The mode used to be a bool. Anyone whose old key said true was on the
    // relay, which is mode 2 since the K-Server modes merged.
    setConnectionMode(group.readEntry(QStringLiteral("vds_mode"), false) ? 2 : 0);
}

void AppSettings::adoptHandleAsDisplayName()
{
    KConfigGroup group(config(), configGroupName());
    if (group.hasKey(QStringLiteral("display_name")))
        return;

    // The display name has always fallen back to the handle rather than to the
    // machine name, so a file that never had the key has to keep looking the
    // same. Writing it down also means renaming the handle no longer renames the
    // profile.
    setDisplayName(username());
}

void AppSettings::loadGroupPassphrase()
{
    m_groupPassphrase.clear();

    const QString walletKey = passphraseWalletKey();
    if (SecretStore::read(walletKey, &m_groupPassphrase)) {
        // Same reason as in CryptoManager: the wallet copy is the only one we
        // use, and a previous run may have written it and then failed to rewrite
        // the file.
        dropLegacyPassphrase();
        return;
    }

    // Older builds kept the passphrase next to the window geometry, in clear
    // text. Move it, and only forget the old copy once the wallet has it.
    const QString legacy = QSettings().value(legacyConfigKeys().at(0)).toString();
    if (legacy.isEmpty())
        return;

    m_groupPassphrase = legacy;
    if (!SecretStore::write(walletKey, legacy)) {
        qCCritical(KOUTNET_LOG_CRYPTO,
                   "the group passphrase is still in the config file in clear text because "
                   "KWallet is unavailable (%s)",
                   qUtf8Printable(SecretStore::lastError()));
        reportSecretStoreProblem(SecretStore::lastError());
        return;
    }
    dropLegacyPassphrase();
}

void AppSettings::dropLegacyPassphrase()
{
    QString detail;
    if (SecretStore::purgePlaintextConfigKeys(legacyConfigKeys(), &detail))
        return;

    qCCritical(KOUTNET_LOG_CRYPTO,
               "KWallet holds the group passphrase, but the clear-text copy could NOT be "
               "deleted from the config file: %s",
               qUtf8Printable(detail));
    reportSecretStoreProblem(detail);
}

void AppSettings::reportSecretStoreProblem(const QString &reason)
{
    QTimer::singleShot(0, this, [this, reason]() {
        Q_EMIT secretStoreUnavailable(reason);
    });
}

void AppSettings::setGroupPassphrase(const QString &passphrase)
{
    if (m_groupPassphrase == passphrase)
        return;
    m_groupPassphrase = passphrase;
    // A passphrase that cannot go into the wallet still applies to this session,
    // but it is not written anywhere - there is no acceptable second-best place
    // for it.
    const bool stored = passphrase.isEmpty() ? SecretStore::remove(passphraseWalletKey()) : SecretStore::write(passphraseWalletKey(), passphrase);
    if (!stored) {
        Q_EMIT secretStoreUnavailable(SecretStore::lastError());
        qCCritical(KOUTNET_LOG_CRYPTO, "the group passphrase was not saved (%s)", qUtf8Printable(SecretStore::lastError()));
    }
    Q_EMIT groupPassphraseChanged();
}

} // namespace koutnet
