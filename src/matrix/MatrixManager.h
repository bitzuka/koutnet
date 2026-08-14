// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - the Matrix session.
//
// K-Server mode is Matrix. This class owns the one Quotient::Connection and
// nothing else does: login, resume, the sync loop and logout. It knows nothing
// about rooms or messages, which is MatrixRoomBridge's job, and nothing at all
// about the LAN protocol - NetworkManager is untouched by any of this.
//
// The access token is a bearer credential: it goes to SecretStore/KWallet and
// never to the config file. A session on a machine with no wallet lasts for
// that run and is not written down, which is the same rule CryptoManager
// applies to the identity keys.
//
// The Olm pickle key is the one secret in this class that does not follow that
// rule, and cannot. libQuotient 0.9 owns it end to end: connectionencryptiondata_p.cpp
// generates it, stores it through QtKeychain under the service name qAppName()
// and the entry "<mxid>-Pickle", and reads it back before unpickling the Olm
// account. There is no setter and no signal, so SecretStore is not offered the
// chance. On a KDE session QtKeychain talks to the same Secret Service that
// backs KWallet, so the key does land in the user's wallet - just under a name
// this code did not choose. Two things follow and both are invariants. The
// application name set in main() must never change, because the pickle key is
// filed under it and a renamed application cannot find its own Olm account.
// And a wallet that cannot be written leaves libQuotient with no key store, at
// which point it turns encryption off underneath us - which is what
// encryptionActive() is for.
//
// Two rules the states below exist to keep. First, no failure is allowed to be
// quiet: every way this can go wrong ends in Failed with a sentence, and every
// state that is not a failure has a deadline behind it, because "Syncing..."
// forever is a lie the interface told for two hours while a network block sat
// underneath it. Second, nothing the user asks for waits on the homeserver's
// permission: logout() clears the local session first and asks afterwards.
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

    // Booleans and a ready-made status line rather than an exported enum: this
    // object reaches QML as a context property, and a context property's type is
    // not registered, so QML could not name the enumerators anyway.
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString homeserver READ homeserver NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    // Whether this session can actually do end-to-end encryption, as opposed to
    // having asked to. False before there is a connection, and false again if
    // libQuotient gave up on the key store. Nothing may draw a padlock without
    // reading this first.
    Q_PROPERTY(bool encryptionActive READ encryptionActive NOTIFY encryptionActiveChanged)

public:
    enum class State {
        LoggedOut,
        Connecting,
        Syncing,
        Online,
        // Was Online and the sync loop started failing. A separate state because
        // the token is probably still good and libQuotient retries by itself, but
        // the interface must stop claiming everything is arriving.
        Reconnecting,
        Failed,
    };
    Q_ENUM(State)

    // A homeserver that never answers leaves the interface saying "Connecting"
    // for as long as the process lives, so the attempt is given a deadline.
    static constexpr int kLoginTimeoutMs = 30000;
    // Syncing needs a deadline of its own and a longer one. It is the state a
    // resumed session lands in before anything has verified the token, and the
    // state a blocked network leaves a fresh login in, and until this existed
    // both of those said "Syncing..." for the life of the process.
    static constexpr int kSyncTimeoutMs = 60000;
    // How long after a reported login timeout the connection is still kept
    // alive. The jobs in flight are the evidence; they get to finish or fail on
    // their own terms before anything of ours cancels them.
    static constexpr int kLoginAbandonMs = 60000;
    // How long a logout that has already been applied locally waits for the
    // homeserver to acknowledge it before the connection is thrown away anyway.
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

    // Null until a login or a resume has produced one. MatrixRoomBridge watches
    // connectionChanged() rather than caching it.
    Quotient::Connection *connection() const
    {
        return m_connection;
    }

    // homeserverUrl may be empty when userIdOrLocalpart is a full MXID, in which
    // case libQuotient resolves the server from its domain.
    Q_INVOKABLE void login(const QString &homeserverUrl, const QString &userIdOrLocalpart, const QString &password);
    Q_INVOKABLE void logout();

    // Whether the signed-in account keeps an encrypted copy of its room keys on
    // the homeserver (m.megolm_backup.v1 account data). Reading it costs
    // nothing and asking twice is harmless, so the interface may draw the
    // "restore keys" affordance straight off this.
    Q_INVOKABLE bool keyBackupAvailable() const;
    // True once keyBackupUnlocked() has been emitted for this session. A
    // session that never unlocks stays on the button; a session that has asks
    // the interface to say the keys are back instead. is- prefixed: the plain
    // name collides with the signal below and calls pick the signal, which
    // returns void, and a void in an insert() is a compile error.
    Q_INVOKABLE bool isKeyBackupUnlocked() const;
    // Tries the string the user typed against the account's key backup. The
    // order is deliberate and matches NeoChat's: it is most likely a recovery
    // key (which libQuotient validates by checksum before any network call),
    // and if that fails it is tried as the backup passphrase. Both attempts
    // are homeserver round trips, so the outcome arrives by signal.
    Q_INVOKABLE void unlockKeyBackup(const QString &recoveryKeyOrPassphrase);

    // Reads back the session the last run stored. False when there is nothing to
    // resume, which is not an error.
    bool resumeSession();

Q_SIGNALS:
    void stateChanged();
    // The connection object itself was replaced or dropped, as opposed to its
    // state changing. Everything holding a Quotient::Connection * must re-read.
    void connectionChanged();
    // encryptionActive() moved. Separate from stateChanged() because E2EE is
    // settled after connected() and long before the session is Online.
    void encryptionActiveChanged();
    // A wallet-less session. The interface has to say so rather than let the
    // user believe the login will still be there tomorrow.
    void sessionNotPersisted(QString reason);

    // Every way this session can go wrong, once, as a sentence. statusText()
    // carries the same words for whoever is looking at the sign-in page; this is
    // for the main window, which is where the user actually is when a sync dies.
    void sessionError(QString message);

    // The room keys from the homeserver backup were decrypted and loaded. The
    // messages that were "no key for this" start decrypting from here on, so
    // the interface answers with a toast.
    void keyBackupUnlocked();
    // The string that was typed did not unlock the backup - wrong recovery key
    // or passphrase, or no backup behind that string at all.
    void keyBackupFailed(QString reason);

private:
    void setState(State state, const QString &error = QString());
    Quotient::Connection *makeConnection();
    // Closes the connection and says in the log that the closing was ours.
    // libQuotient reports every job it aborts as "stopped without ready network
    // reply", which reads exactly like a server that hung up, so the reason is
    // printed first and in our own category.
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
    // Fires when nothing has synced for kSyncTimeoutMs. Separate from the login
    // deadline because the two cover different halves of "the interface is
    // waiting" and a resume passes straight from one to the other.
    QTimer m_syncTimeout;
    // Whatever the last syncError said. Kept so the deadline can report the
    // reason libQuotient already knows instead of a bare "timed out".
    QString m_lastSyncError;
    // A resumed session is not proven by connected(): assumeIdentity() emits
    // that the moment the identity is set, before the token has been shown to
    // anybody. The first completed sync is the proof, and this says we are still
    // waiting for it.
    bool m_resuming = false;
    // Whether this session already loaded its room keys from the homeserver
    // backup. Read by QML; kept here because the SSSSHandler that did the work
    // was a transient object and is gone by the time the interface asks.
    bool m_keyBackupUnlocked = false;
    // Set while login() is waiting for the homeserver's login flows, so that the
    // password is not kept a moment longer than the round trip needs it.
    QString m_pendingUser;
    QString m_pendingPassword;
};

} // namespace koutnet
