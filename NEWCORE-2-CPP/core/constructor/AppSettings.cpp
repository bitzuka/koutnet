#include "AppSettings.h"

#include <QSettings>
#include <QHostInfo>

namespace koutnet {

AppSettings::AppSettings(QObject *parent) : QObject(parent)
{
    load();
}

void AppSettings::load()
{
    QSettings settings;
    // Falls back to the machine's hostname rather than a blank field, so a
    // fresh install has a usable (if generic) name on the network instead
    // of showing up to peers as an empty string.
    m_username = settings.value("app/username", QHostInfo::localHostName()).toString();
    m_vdsMode = settings.value("app/vds_mode", false).toBool();
    m_relayHost = settings.value("app/relay_host", QString()).toString();
    m_relayPort = settings.value("app/relay_port", 0).toInt();
    m_groupPassphrase = settings.value("app/group_passphrase", QString()).toString();
    m_language = settings.value("app/language", "ru").toString();
    m_displayName = settings.value("app/display_name", m_username).toString();
    m_avatarPath = settings.value("app/avatar_path", QString()).toString();
    m_bannerPath = settings.value("app/banner_path", QString()).toString();
    m_profileBackgroundPath = settings.value("app/profile_background_path", QString()).toString();
    m_nameBadgePath = settings.value("app/name_badge_path", QString()).toString();
    m_bio = settings.value("app/bio", QString()).toString();
    m_globalAccount = settings.value("app/global_account", false).toBool();
    m_globalAccountRegistered = settings.value("app/global_account_registered", false).toBool();
}

void AppSettings::setUsername(const QString &name)
{
    if (m_username == name)
        return;
    m_username = name;
    QSettings().setValue("app/username", m_username);
    emit usernameChanged();
}

void AppSettings::setVdsMode(bool enabled)
{
    if (m_vdsMode == enabled)
        return;
    m_vdsMode = enabled;
    QSettings().setValue("app/vds_mode", m_vdsMode);
    emit vdsModeChanged();
}

void AppSettings::setRelayHost(const QString &host)
{
    if (m_relayHost == host)
        return;
    m_relayHost = host;
    QSettings().setValue("app/relay_host", m_relayHost);
    emit relayChanged();
}

void AppSettings::setRelayPort(int port)
{
    if (m_relayPort == port)
        return;
    m_relayPort = port;
    QSettings().setValue("app/relay_port", m_relayPort);
    emit relayChanged();
}

void AppSettings::setGroupPassphrase(const QString &passphrase)
{
    if (m_groupPassphrase == passphrase)
        return;
    m_groupPassphrase = passphrase;
    QSettings().setValue("app/group_passphrase", m_groupPassphrase);
    emit groupPassphraseChanged();
}

void AppSettings::setLanguage(const QString &lang)
{
    if (m_language == lang)
        return;
    m_language = lang;
    QSettings().setValue("app/language", m_language);
    emit languageChanged();
}

void AppSettings::setDisplayName(const QString &name)
{
    if (m_displayName == name)
        return;
    m_displayName = name;
    QSettings().setValue("app/display_name", m_displayName);
    emit displayNameChanged();
}

void AppSettings::setAvatarPath(const QString &path)
{
    if (m_avatarPath == path)
        return;
    m_avatarPath = path;
    QSettings().setValue("app/avatar_path", m_avatarPath);
    emit avatarPathChanged();
}

void AppSettings::setBannerPath(const QString &path)
{
    if (m_bannerPath == path)
        return;
    m_bannerPath = path;
    QSettings().setValue("app/banner_path", m_bannerPath);
    emit bannerPathChanged();
}

void AppSettings::setProfileBackgroundPath(const QString &path)
{
    if (m_profileBackgroundPath == path)
        return;
    m_profileBackgroundPath = path;
    QSettings().setValue("app/profile_background_path", m_profileBackgroundPath);
    emit profileBackgroundPathChanged();
}

void AppSettings::setNameBadgePath(const QString &path)
{
    if (m_nameBadgePath == path)
        return;
    m_nameBadgePath = path;
    QSettings().setValue("app/name_badge_path", m_nameBadgePath);
    emit nameBadgePathChanged();
}

void AppSettings::setBio(const QString &text)
{
    if (m_bio == text)
        return;
    m_bio = text;
    QSettings().setValue("app/bio", m_bio);
    emit bioChanged();
}

void AppSettings::setGlobalAccount(bool enabled)
{
    if (m_globalAccount == enabled)
        return;
    m_globalAccount = enabled;
    QSettings().setValue("app/global_account", m_globalAccount);
    emit globalAccountChanged();
}

void AppSettings::setGlobalAccountRegistered(bool registered)
{
    if (m_globalAccountRegistered == registered)
        return;
    m_globalAccountRegistered = registered;
    QSettings().setValue("app/global_account_registered", m_globalAccountRegistered);
    emit globalAccountRegisteredChanged();
}

} // namespace koutnet
