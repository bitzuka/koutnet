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
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)

    // Profile fields. "username" above doubles as the @handle (network
    // identity, unique-ish); displayName is the free-form nickname shown
    // big in the profile header, same split Discord/Telegram use between
    // a handle and a display name.
    Q_PROPERTY(QString displayName READ displayName WRITE setDisplayName NOTIFY displayNameChanged)
    Q_PROPERTY(QString avatarPath READ avatarPath WRITE setAvatarPath NOTIFY avatarPathChanged)
    Q_PROPERTY(QString bannerPath READ bannerPath WRITE setBannerPath NOTIFY bannerPathChanged)
    // Small custom image shown next to the display name, same idea as a
    // Telegram custom emoji status.
    Q_PROPERTY(QString nameBadgePath READ nameBadgePath WRITE setNameBadgePath NOTIFY nameBadgePathChanged)
    // Markdown-formatted "about me" text.
    Q_PROPERTY(QString bio READ bio WRITE setBio NOTIFY bioChanged)
    // false = Local account (this device only), true = Global account
    // (synced identity across K-Server/relay) — a UI-level toggle for now;
    // the actual sync behavior depends on the K-Server integration.
    Q_PROPERTY(bool globalAccount READ globalAccount WRITE setGlobalAccount NOTIFY globalAccountChanged)
    // Purely decorative animated rainbow ring around the profile avatar.
    // Off by default — it's a fun opt-in, not something sprung on people.
    Q_PROPERTY(bool profileGradientEnabled READ profileGradientEnabled WRITE setProfileGradientEnabled NOTIFY profileGradientEnabledChanged)

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

    QString language() const { return m_language; }
    void setLanguage(const QString &lang);

    QString displayName() const { return m_displayName; }
    void setDisplayName(const QString &name);

    QString avatarPath() const { return m_avatarPath; }
    void setAvatarPath(const QString &path);

    QString bannerPath() const { return m_bannerPath; }
    void setBannerPath(const QString &path);

    QString nameBadgePath() const { return m_nameBadgePath; }
    void setNameBadgePath(const QString &path);

    QString bio() const { return m_bio; }
    void setBio(const QString &text);

    bool globalAccount() const { return m_globalAccount; }
    void setGlobalAccount(bool enabled);

    bool profileGradientEnabled() const { return m_profileGradientEnabled; }
    void setProfileGradientEnabled(bool enabled);

signals:
    void usernameChanged();
    void vdsModeChanged();
    void relayChanged();
    void groupPassphraseChanged();
    void languageChanged();
    void displayNameChanged();
    void avatarPathChanged();
    void bannerPathChanged();
    void nameBadgePathChanged();
    void bioChanged();
    void globalAccountChanged();
    void profileGradientEnabledChanged();

private:
    void load();

    QString m_username;
    bool m_vdsMode = false;
    QString m_relayHost;
    int m_relayPort = 0;
    QString m_groupPassphrase;
    QString m_language;
    QString m_displayName;
    QString m_avatarPath;
    QString m_bannerPath;
    QString m_nameBadgePath;
    QString m_bio;
    bool m_globalAccount = false;
    bool m_profileGradientEnabled = false;
};

} // namespace koutnet
