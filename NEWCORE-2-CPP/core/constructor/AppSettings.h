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

    // Audio device ids as reported by QAudioDevice::id(). Empty means
    // "follow the system default", which is also what we fall back to
    // when a saved device is no longer plugged in.
    Q_PROPERTY(QString audioInputId READ audioInputId WRITE setAudioInputId NOTIFY audioInputIdChanged)
    Q_PROPERTY(QString audioOutputId READ audioOutputId WRITE setAudioOutputId NOTIFY audioOutputIdChanged)
    // Playback volume as a percentage, so the settings slider and the
    // stored value read the same way.
    Q_PROPERTY(int audioVolume READ audioVolume WRITE setAudioVolume NOTIFY audioVolumeChanged)
    Q_PROPERTY(bool micMuted READ micMuted WRITE setMicMuted NOTIFY micMutedChanged)
    Q_PROPERTY(bool vadEnabled READ vadEnabled WRITE setVadEnabled NOTIFY vadEnabledChanged)

    // Welcome screen at startup. Ticking "do not show again" clears
    // this, and the main window then comes up directly.
    Q_PROPERTY(bool showWelcome READ showWelcome WRITE setShowWelcome NOTIFY showWelcomeChanged)
    // Stored only. There is no updater yet, and the welcome screen says
    // as much next to the checkbox.
    Q_PROPERTY(bool checkUpdatesOnStart READ checkUpdatesOnStart WRITE setCheckUpdatesOnStart NOTIFY checkUpdatesOnStartChanged)

    // Profile fields. "username" above doubles as the @handle (network
    // identity, unique-ish); displayName is the free-form nickname shown
    // big in the profile header, same split Discord/Telegram use between
    // a handle and a display name.
    Q_PROPERTY(QString displayName READ displayName WRITE setDisplayName NOTIFY displayNameChanged)
    Q_PROPERTY(QString avatarPath READ avatarPath WRITE setAvatarPath NOTIFY avatarPathChanged)
    Q_PROPERTY(QString bannerPath READ bannerPath WRITE setBannerPath NOTIFY bannerPathChanged)
    // Full-bleed backdrop behind the whole profile page, separate from the
    // banner strip up top — same relationship Twitter/X has between a
    // header banner and a themed background.
    Q_PROPERTY(QString profileBackgroundPath READ profileBackgroundPath WRITE setProfileBackgroundPath NOTIFY profileBackgroundPathChanged)
    // Small custom image shown next to the display name, same idea as a
    // Telegram custom emoji status.
    Q_PROPERTY(QString nameBadgePath READ nameBadgePath WRITE setNameBadgePath NOTIFY nameBadgePathChanged)
    // Markdown-formatted "about me" text.
    Q_PROPERTY(QString bio READ bio WRITE setBio NOTIFY bioChanged)
    // false = Local account (this device only), true = Global account
    // (synced identity across K-Server/relay) — a UI-level toggle for now;
    // the actual sync behavior depends on the K-Server integration.
    Q_PROPERTY(bool globalAccount READ globalAccount WRITE setGlobalAccount NOTIFY globalAccountChanged)
    // Whether the selected Global identity has actually completed
    // registration against a K-Server. Local is always implicitly
    // "registered" (it's just this device), so this flag only matters
    // while globalAccount is true.
    Q_PROPERTY(bool globalAccountRegistered READ globalAccountRegistered WRITE setGlobalAccountRegistered NOTIFY globalAccountRegisteredChanged)

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

    QString audioInputId() const { return m_audioInputId; }
    void setAudioInputId(const QString &id);

    QString audioOutputId() const { return m_audioOutputId; }
    void setAudioOutputId(const QString &id);

    int audioVolume() const { return m_audioVolume; }
    void setAudioVolume(int percent);

    bool micMuted() const { return m_micMuted; }
    void setMicMuted(bool muted);

    bool vadEnabled() const { return m_vadEnabled; }
    void setVadEnabled(bool enabled);

    bool showWelcome() const { return m_showWelcome; }
    void setShowWelcome(bool show);

    bool checkUpdatesOnStart() const { return m_checkUpdatesOnStart; }
    void setCheckUpdatesOnStart(bool check);

    QString displayName() const { return m_displayName; }
    void setDisplayName(const QString &name);

    QString avatarPath() const { return m_avatarPath; }
    void setAvatarPath(const QString &path);

    QString bannerPath() const { return m_bannerPath; }
    void setBannerPath(const QString &path);

    QString profileBackgroundPath() const { return m_profileBackgroundPath; }
    void setProfileBackgroundPath(const QString &path);

    QString nameBadgePath() const { return m_nameBadgePath; }
    void setNameBadgePath(const QString &path);

    QString bio() const { return m_bio; }
    void setBio(const QString &text);

    bool globalAccount() const { return m_globalAccount; }
    void setGlobalAccount(bool enabled);

    bool globalAccountRegistered() const { return m_globalAccountRegistered; }
    void setGlobalAccountRegistered(bool registered);

signals:
    void usernameChanged();
    void vdsModeChanged();
    void relayChanged();
    void groupPassphraseChanged();
    void languageChanged();
    void audioInputIdChanged();
    void audioOutputIdChanged();
    void audioVolumeChanged();
    void micMutedChanged();
    void vadEnabledChanged();
    void showWelcomeChanged();
    void checkUpdatesOnStartChanged();
    void displayNameChanged();
    void avatarPathChanged();
    void bannerPathChanged();
    void profileBackgroundPathChanged();
    void nameBadgePathChanged();
    void bioChanged();
    void globalAccountChanged();
    void globalAccountRegisteredChanged();

private:
    void load();

    QString m_username;
    bool m_vdsMode = false;
    QString m_relayHost;
    int m_relayPort = 0;
    QString m_groupPassphrase;
    QString m_language;
    QString m_audioInputId;
    QString m_audioOutputId;
    int m_audioVolume = 100;
    bool m_micMuted = false;
    bool m_vadEnabled = true;
    bool m_showWelcome = true;
    bool m_checkUpdatesOnStart = false;
    QString m_displayName;
    QString m_avatarPath;
    QString m_bannerPath;
    QString m_profileBackgroundPath;
    QString m_nameBadgePath;
    QString m_bio;
    bool m_globalAccount = false;
    bool m_globalAccountRegistered = false;
};

} // namespace koutnet
