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

} // namespace koutnet
