// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet Matrix session.
//
// K-Server mode is Matrix. This class owns the Quotient connection and nothing
// else: login, resume, the sync loop and logout. Rooms and messages belong to
// MatrixRoomBridge, and the LAN side is untouched.
//
// The access token is a bearer credential, kept in the secret store and never
// in the config file, like CryptoManager does with the identity keys.
//
// The Olm pickle key is the exception: libQuotient owns it and stores it via
// QtKeychain under the application name. That is why the app name must never
// change, and why a wallet that cannot be written drops encryption.
//
// Two rules the states keep: no failure is silent (every error ends in Failed
// with a message), and every non-failure state has a deadline. Nothing the
// user asks for waits on the homeserver: logout clears locally first.
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

namespace Quotient
{
class Connection;
}

namespace koutnet
{

class AppSettings;

class MatrixManager : public QObject
{
    Q_OBJECT

// booleans and a status string instead of an enum, since this is a context
// property and QML could not name the enumerators anyway
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    // full MXID of the signed-in account, empty when signed out
    Q_PROPERTY(QString userId READ userId NOTIFY stateChanged)
    Q_PROPERTY(QString homeserver READ homeserver NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
// whether E2EE is actually available, not just requested. read this before
// showing a padlock; false with no connection or a failed key store
    Q_PROPERTY(bool encryptionActive READ encryptionActive NOTIFY encryptionActiveChanged)

public:
    enum class State {
        LoggedOut,
        Connecting,
        Syncing,
        Online,
// was Online and the sync loop is now failing; the token may still be good,
// but the interface must stop claiming everything arrives
        Reconnecting,
        Failed,
    };
    Q_ENUM(State)

// a homeserver that never answers would say "Connecting" forever, so cap it
    static constexpr int kLoginTimeoutMs = 30000;
// syncing needs its own, longer deadline; a resumed or blocked session used
// to sit on "Syncing..." until the process died
    static constexpr int kSyncTimeoutMs = 60000;
// keep the connection alive this long after a login timeout, so in-flight
// jobs finish on their own
    static constexpr int kLoginAbandonMs = 60000;
// wait this long for the homeserver to confirm a logout before dropping it
    static constexpr int kLogoutGraceMs = 15000;

    explicit MatrixManager(AppSettings *settings, QObject *parent = nullptr);
    ~MatrixManager() override;

    bool loggedIn() const;
    bool busy() const;
    bool encryptionActive() const;
    QString userId() const;
    QString homeserver() const;
    QString statusText() const;
    QString lastError() const
    {
        return m_lastError;
    }

// null until login or resume; MatrixRoomBridge listens for connectionChanged
    Quotient::Connection *connection() const
    {
        return m_connection;
    }

// homeserverUrl may be empty when a full MXID is given; libQuotient then
// resolves the server from the domain
    Q_INVOKABLE void login(const QString &homeserverUrl, const QString &userIdOrLocalpart, const QString &password);
    Q_INVOKABLE void logout();

// whether the account keeps an encrypted copy of its keys on the homeserver.
// cheap to read and safe to call twice, so the UI can show "restore keys"
    Q_INVOKABLE bool keyBackupAvailable() const;
    // true once keyBackupUnlocked() fired; the is- prefix avoids clashing with
    // the signal of the same name, which would break insert()
    Q_INVOKABLE bool isKeyBackupUnlocked() const;
    // tries the typed string as a recovery key first, then as a passphrase.
    // both go to the homeserver, so the result comes back by signal
    Q_INVOKABLE void unlockKeyBackup(const QString &recoveryKeyOrPassphrase);

    // reads the session saved last run; false when there is nothing to resume
    bool resumeSession();

Q_SIGNALS:
    void stateChanged();
    // the connection object changed (not just its state); holders must re-read it
    void connectionChanged();
    // encryptionActive() changed; separate from stateChanged since E2EE settles
    // after connect and before Online
    void encryptionActiveChanged();
    // a session with no wallet; the UI must say so instead of implying persistence
    void sessionNotPersisted(QString reason);

    // one sentence per failure; statusText shows the same words on the sign-in
    // page, this is for the main window
    void sessionError(QString message);

    // the backed-up room keys were decrypted and loaded; the UI shows a toast
    void keyBackupUnlocked();
    // the typed string did not unlock the backup (wrong key, or none behind it)
    void keyBackupFailed(QString reason);

private:
    void setState(State state, const QString &error = QString());
    Quotient::Connection *makeConnection();
    // closes the connection and logs that it was us; libQuotient blames the
    // network for aborted jobs, so we log the real reason first
    void retireConnection(const QString &why);
    void onConnected();
    void armSyncDeadline();
    void onSyncDeadline();
    void storeSession();
    void clearStoredSession();
    void retellBackupAvailability();

    QPointer<AppSettings> m_settings;
    Quotient::Connection *m_connection = nullptr;
    State m_state = State::LoggedOut;
    QString m_lastError;
    QTimer m_loginTimeout;
    // fires when nothing has synced for kSyncTimeoutMs; separate from the login
    // deadline since a resume hands straight from one to the other
    QTimer m_syncTimeout;
    // the last sync error, kept so the deadline can report the real reason
    QString m_lastSyncError;
    // a resumed session is not proven by connected(); the first completed sync is,
    // and this says we are still waiting for it
    bool m_resuming = false;
    // whether the room keys were already loaded from backup; kept here because
    // the handler that did it is gone by the time the UI asks
    bool m_keyBackupUnlocked = false;
    // set while login() waits for the server; the password is cleared afterwards
    QString m_pendingUser;
    QString m_pendingPassword;
};

} // namespace koutnet
