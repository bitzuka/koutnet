// KOutNet — persisted app settings (username, connection mode, relay config)
// Backed by QSettings (INI/registry depending on platform). This is the
// module every "I_Do_It_Latet.!" comment in NetworkManager/CryptoManager
// was waiting on — wire those up as you touch each area.
#pragma once

#include <QObject>
#include <QString>

namespace koutnet {

class AppSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString username READ username WRITE setUsername NOTIFY usernameChanged)
    Q_PROPERTY(bool vdsMode READ vdsMode WRITE setVdsMode NOTIFY vdsModeChanged)
    Q_PROPERTY(QString relayHost READ relayHost WRITE setRelayHost NOTIFY relayChanged)
    Q_PROPERTY(int relayPort READ relayPort WRITE setRelayPort NOTIFY relayChanged)
    Q_PROPERTY(QString groupPassphrase READ groupPassphrase WRITE setGroupPassphrase NOTIFY groupPassphraseChanged)

public:
    explicit AppSettings(QObject *parent = nullptr);

    QString username() const { return m_username; }
    void setUsername(const QString &name);

    // false = LAN/VPN (default), true = VDS/relay — mirrors
    // NetworkManager::ConnectionMode without core/constructor needing to
    // depend on the network module's header.
    bool vdsMode() const { return m_vdsMode; }
    void setVdsMode(bool enabled);

    QString relayHost() const { return m_relayHost; }
    void setRelayHost(const QString &host);

    int relayPort() const { return m_relayPort; }
    void setRelayPort(int port);

    // Shared passphrase for group/public-chat encryption (PSK layer, see
    // CryptoManager). Empty = group chat stays unencrypted, matching current
    // NetworkManager::sendChat() behaviour.
    QString groupPassphrase() const { return m_groupPassphrase; }
    void setGroupPassphrase(const QString &passphrase);

signals:
    void usernameChanged();
    void vdsModeChanged();
    void relayChanged();
    void groupPassphraseChanged();

private:
    void load();

    QString m_username;
    bool m_vdsMode = false;
    QString m_relayHost;
    int m_relayPort = 0;
    QString m_groupPassphrase;
};

} // namespace koutnet
