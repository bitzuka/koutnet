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

namespace koutnet {

namespace {

// Wallet entry for the shared group-chat passphrase, formerly the QSettings
// key "app/group_passphrase".
QString passphraseWalletKey()
{
    return QStringLiteral("group_passphrase");
}

// Where older builds kept the same passphrase, in clear text.
QStringList legacyConfigKeys()
{
    return { QStringLiteral("app/group_passphrase") };
}

// The single group the kcfg writes to, needed here for the two keys the
// generated code knows nothing about.
QString configGroupName()
{
    return QStringLiteral("app");
}

// A file written before this port carries none of KConfig's own escaping, so
// one pass through QSettings normalises it for good.
constexpr int kCurrentConfigVersion = 1;

// Long enough that a dragged slider settles into a single write, short enough
// that a crash immediately after a change is the only way to lose one.
constexpr int kSaveDelayMs = 1000;

} // namespace

AppSettings::AppSettings(QObject *parent) : KOutNetSettings()
{
    setParent(parent);

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(kSaveDelayMs);
    connect(m_saveTimer, &QTimer::timeout, this, [this]() { save(); });

    // First, because it is the only step that still edits the file through
    // QSettings, and the last writer of any given run has to be KConfig.
    loadGroupPassphrase();
    migrateEscapedValues();
    adoptLegacyConnectionMode();
    adoptHandleAsDisplayName();
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

// One connection per generated property, taken from the metaobject rather than
// written out twenty times.
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
// would show up as visible backslashes. Read the old file once through the
// writer that produced it and let the save in the constructor put the values
// back as UTF-8.
void AppSettings::migrateEscapedValues()
{
    if (configVersion() >= kCurrentConfigVersion)
        return;

    QSettings legacy;
    const KConfigSkeletonItem::List entries = items();
    for (KConfigSkeletonItem *entry : entries) {
        const QVariant value = legacy.value(configGroupName() + QLatin1Char('/') + entry->key());
        if (!value.isValid())
            continue;
        entry->setProperty(value);
    }
    setConfigVersion(kCurrentConfigVersion);
}

void AppSettings::adoptLegacyConnectionMode()
{
    KConfigGroup group(config(), configGroupName());
    if (!group.hasKey(QStringLiteral("vds_mode")) || group.hasKey(QStringLiteral("connection_mode")))
        return;

    // The mode used to be a bool. Anyone whose old key said true was on the
    // relay, which is mode 3 in the five-mode enum.
    setConnectionMode(group.readEntry(QStringLiteral("vds_mode"), false) ? 3 : 0);
}

void AppSettings::adoptHandleAsDisplayName()
{
    KConfigGroup group(config(), configGroupName());
    if (group.hasKey(QStringLiteral("display_name")))
        return;

    // The display name has always fallen back to the handle rather than to the
    // machine name, so a file that never had the key has to keep looking the
    // same. It is written down now, which also means that renaming the handle
    // later no longer renames the profile along with it.
    setDisplayName(username());
}

void AppSettings::loadGroupPassphrase()
{
    m_groupPassphrase.clear();

    const QString walletKey = passphraseWalletKey();
    if (SecretStore::read(walletKey, &m_groupPassphrase)) {
        // Same reason as in CryptoManager: the wallet copy is the only one we
        // use, and a previous run may have written it and then failed to
        // rewrite the config file, which nothing would ever notice again.
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
    // Queued because this runs from the constructor, before anything is
    // connected. QML connects while the engine loads, which still happens
    // before the event loop starts, so a zero timer is early enough.
    QTimer::singleShot(0, this, [this, reason]() {
        Q_EMIT secretStoreUnavailable(reason);
    });
}

void AppSettings::setGroupPassphrase(const QString &passphrase)
{
    if (m_groupPassphrase == passphrase)
        return;
    m_groupPassphrase = passphrase;
    // A passphrase that cannot go into the wallet still applies to this
    // session, but it is not written anywhere - there is no acceptable
    // second-best place for it.
    const bool stored = passphrase.isEmpty() ? SecretStore::remove(passphraseWalletKey())
                                             : SecretStore::write(passphraseWalletKey(), passphrase);
    if (!stored) {
        Q_EMIT secretStoreUnavailable(SecretStore::lastError());
        qCCritical(KOUTNET_LOG_CRYPTO, "the group passphrase was not saved (%s)",
                   qUtf8Printable(SecretStore::lastError()));
    }
    Q_EMIT groupPassphraseChanged();
}

} // namespace koutnet
