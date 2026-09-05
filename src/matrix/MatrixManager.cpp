// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "MatrixManager.h"

#include <QTimer>

#include "MatrixLoginUtils.h"
#include "core/constructor/AppSettings.h"
#include "core/security/KeepSecret.h"

#include "koutnet_matrix_debug.h"

#include <KLocalizedString>

#include <QDesktopServices>
#include <QUrl>
#include <QUrlQuery>

#include <Quotient/connection.h>
#include <Quotient/csapi/whoami.h>
#include <Quotient/e2ee/sssshandler.h>

namespace
{
// Store entry for the Matrix access token. Flat, like every other name in the
// KOutNet folder - see CryptoManager and AppSettings.
QString tokenStoreKey(const QString &userId)
{
    // One slot per account. A single shared slot is how a sign-in as somebody
    // else used to hand the next session a token naming a different owner:
    // the configuration said one user id, the store answered with another
    // user's token, and only Quotient's log line said so.
    return QStringLiteral("matrix_access_token_") + userId;
}

// The slot every install before per-account keys wrote to. Read for migration
// and removed the first time anything clears or rewrites the session.
const QString &legacyTokenStoreKey()
{
    static const QString key = QStringLiteral("matrix_access_token");
    return key;
}

// What the homeserver files this login under in the user's device list.
QString deviceDisplayName()
{
    return QStringLiteral("KOutNet");
}

QString joinDetail(const QString &message, const QString &details)
{
    return details.isEmpty() ? message : message + QLatin1Char(' ') + details;
}
} // namespace

namespace koutnet
{

MatrixManager::MatrixManager(AppSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_loginTimeout.setSingleShot(true);
    m_loginTimeout.setInterval(kLoginTimeoutMs);
    connect(&m_loginTimeout, &QTimer::timeout, this, [this]() {
        if (m_state != State::Connecting)
            return;

        // With E2EE on, connected() is emitted only after libQuotient has been
        // to the keychain for the pickle key and unpickled the Olm account, so a
        // fresh login that already holds a token is not waiting on the network
        // at all - it is waiting on a store that has probably put a prompt
        // somewhere. Blaming the homeserver for that was the wrong sentence.
        // Only for a fresh login: assumeIdentity() sets the token before it has
        // asked anybody anything, so a resume cannot be told apart this way.
        const bool serverAnswered = !m_resuming && m_connection && m_connection->isLoggedIn();

        // The connection is deliberately left running. Aborting it here is what
        // turned one slow homeserver into a screenful of "stopped without ready
        // network reply" - our own cleanup, printed in libQuotient's voice and
        // read for an hour as a network fault. The jobs get to finish or fail on
        // their own, and a late success is still accepted below.
        //
        // The pending credentials are left in place on purpose: the sign-in
        // proper is performed by the deferred lambdas in login(), and they gate
        // on those fields. Erasing them here is what made the late success the
        // comment above promises unreachable - the address was never sent back,
        // so nothing ever logged in, and "still running" meant "running into a
        // wall". Left alone, whichever lambda wakes up last still logs the
        // attempt in; whoever wins, it is the newest credentials on the newest
        // connection, which is the only outcome the two lambdas could ever have
        // produced in the normal flow.
        setState(State::Failed,
                 serverAnswered
                     ? i18nc("@info:status Matrix login stalled after the password was accepted",
                             "The sign-in was accepted but the encryption keys could not be opened in time. "
                             "Unlock the secret store if it is asking, and try again.")
                     : i18nc("@info:status Matrix login failed",
                             "The homeserver did not answer in time. Nothing was signed in; the attempt is still running and will report itself in the log."));

        const QPointer<Quotient::Connection> attempt(m_connection);
        QTimer::singleShot(kLoginAbandonMs, this, [this, attempt]() {
            // Only if this same attempt is still the current one and still has
            // not produced anything; a login that came good owns the slot now.
            if (m_connection == nullptr || m_connection != attempt || m_state != State::Failed)
                return;
            retireConnection(QStringLiteral("the login attempt was given up on after it stopped answering"));
        });
    });

    m_syncTimeout.setSingleShot(true);
    m_syncTimeout.setInterval(kSyncTimeoutMs);
    connect(&m_syncTimeout, &QTimer::timeout, this, &MatrixManager::onSyncDeadline);

    // The SSO browser flow ends with the homeserver sending the browser to
    // koutnet://sso?loginToken=...; registering here means that click is routed
    // back into onSsoRedirect() instead of being lost to the desktop.
    QDesktopServices::setUrlHandler(QStringLiteral("koutnet"), this, "onSsoRedirect");
}

MatrixManager::~MatrixManager() = default;

bool MatrixManager::loggedIn() const
{
    return m_state == State::Syncing || m_state == State::Online || m_state == State::Reconnecting;
}

bool MatrixManager::busy() const
{
    return m_state == State::Connecting;
}

bool MatrixManager::encryptionActive() const
{
    return m_connection && m_connection->encryptionEnabled();
}

QString MatrixManager::userId() const
{
    return m_connection ? m_connection->userId() : QString();
}

QString MatrixManager::homeserver() const
{
    return m_connection ? m_connection->homeserver().toString() : QString();
}

QString MatrixManager::statusText() const
{
    switch (m_state) {
    case State::LoggedOut:
        return i18nc("@info:status Matrix connection state", "Not signed in");
    case State::Connecting:
        return i18nc("@info:status Matrix connection state", "Signing in...");
    case State::Syncing:
        return i18nc("@info:status Matrix connection state", "Syncing...");
    case State::Online:
        return i18nc("@info:status Matrix connection state, %1 is a Matrix user id", "Signed in as %1", userId());
    case State::Reconnecting:
        return m_lastError.isEmpty()
            ? i18nc("@info:status Matrix connection state", "Reconnecting...")
            : i18nc("@info:status Matrix connection state, %1 is what the homeserver or the network reported", "Reconnecting after: %1", m_lastError);
    case State::Failed:
        break;
    }
    return m_lastError.isEmpty() ? i18nc("@info:status Matrix connection state", "Sign-in failed") : m_lastError;
}

void MatrixManager::setState(State state, const QString &error)
{
    if (m_state == state && m_lastError == error)
        return;
    // Only the states that mean something went wrong are announced; the status
    // line carries the rest, and a notification per reconnect is noise.
    const bool announce = !error.isEmpty() && (state == State::Failed || state == State::Reconnecting);
    m_state = state;
    m_lastError = error;
    if (!error.isEmpty())
        qCWarning(KOUTNET_LOG_MATRIX) << "matrix session:" << error;
    Q_EMIT stateChanged();
    // After stateChanged() so that whatever reads the manager while showing the
    // message already agrees with it.
    if (announce)
        Q_EMIT sessionError(error);
}

Quotient::Connection *MatrixManager::makeConnection()
{
    retireConnection(QStringLiteral("a new session is starting"));

    auto *c = new Quotient::Connection(this);
    // Before anything else, and before any login: enableEncryption() refuses to
    // do anything once the connection holds a token, and says so only in a
    // warning nobody reads. Most of Matrix is encrypted - a direct chat is by
    // default - so a client that cannot open an encrypted room is a client that
    // cannot open most rooms.
    c->enableEncryption(true);
    // Left alone deliberately. Whether a new direct chat is created encrypted is
    // a decision this build does not make yet, because it does not create direct
    // chats; libQuotient's default stands until it does.

    // Without the cache every start is a full initial sync of every room.
    c->setCacheState(true);
    c->setLazyLoading(true);

    connect(c, &Quotient::Connection::connected, this, &MatrixManager::onConnected);
    // libQuotient turns encryption off by itself when the key store cannot be
    // set up - an unreachable keychain, a pickle key of the wrong length, an Olm
    // account that will not unpickle. The session carries on unencrypted, which
    // is survivable, but only if it is said out loud: everything downstream
    // decides what to draw from encryptionActive(), and an encrypted room in a
    // session like this can be neither read nor written.
    connect(c, &Quotient::Connection::encryptionChanged, this, [this](bool enabled) {
        Q_EMIT encryptionActiveChanged();
        if (enabled)
            return;
        // Reported rather than made a state: the session still works and the
        // state machine has nowhere honest to put "signed in but unencrypted".
        const QString message = i18nc("@info:status the Matrix session could not set up its encryption key store",
                                      "Encryption could not be started for this session, so encrypted rooms will stay unreadable. "
                                      "This usually means the secret store could not be opened.");
        qCWarning(KOUTNET_LOG_MATRIX) << "matrix session:" << message;
        Q_EMIT sessionError(message);
    });
    // The account has no cross-signing keys. This happens on a fresh account
    // or on one whose keys were reset; until the master, self-signing and
    // user-signing keys exist, other devices will not send us room keys.
    connect(c, &Quotient::Connection::crossSigningSetupRequired, this, [this]() {
        qCInfo(KOUTNET_LOG_MATRIX) << "cross-signing keys not found; setup required";
        Q_EMIT crossSigningRequired();
    });
    connect(c, &Quotient::Connection::loginError, this, [this](const QString &message, const QString &details) {
        m_pendingUser.clear();
        m_pendingPassword.clear();
        m_loginTimeout.stop();
        m_syncTimeout.stop();
        const bool wasResume = m_resuming;
        m_resuming = false;
        const QString detail = joinDetail(message, details);

        retireConnection(QStringLiteral("the homeserver rejected the sign-in"));
        if (!wasResume) {
            setState(State::Failed, detail);
            return;
        }
        // The homeserver answered and the answer was no, so the stored session is
        // the thing that is wrong. Kept, it would be retried on every start and
        // fail the same way every time with nothing said.
        clearStoredSession();
        setState(State::Failed,
                 i18nc("@info:status a stored Matrix session was refused, %1 is what the homeserver said",
                       "The saved sign-in is no longer accepted (%1). Sign in again.",
                       detail));
    });
    connect(c, &Quotient::Connection::resolveError, this, [this](const QString &error) {
        m_pendingUser.clear();
        m_pendingPassword.clear();
        m_loginTimeout.stop();
        m_syncTimeout.stop();
        m_resuming = false;
        retireConnection(QStringLiteral("the homeserver address could not be resolved"));
        setState(State::Failed, error);
    });
    connect(c, &Quotient::Connection::syncDone, this, [this]() {
        m_syncTimeout.stop();
        m_lastSyncError.clear();
        if (m_resuming) {
            // The first completed sync is the only proof a resumed token is still
            // good - see the note on m_resuming - so the session is confirmed here
            // rather than in onConnected().
            m_resuming = false;
            storeSession();
        }
        setState(State::Online);
    });
    connect(c, &Quotient::Connection::syncError, this, [this](const QString &message, const QString &details) {
        m_lastSyncError = joinDetail(message, details);
        qCWarning(KOUTNET_LOG_MATRIX) << "sync error" << m_lastSyncError;
        // Not a failure of the session by itself: libQuotient retries, and a
        // dropped wifi must not throw away a token that is still good. What must
        // not happen is the interface going on claiming messages are arriving,
        // so the state moves and the deadline below decides when to give up.
        if (m_state == State::Online || m_state == State::Reconnecting)
            setState(State::Reconnecting, m_lastSyncError);
        if (!m_syncTimeout.isActive())
            armSyncDeadline();
    });
    connect(c, &Quotient::Connection::loggedOut, this, [this]() {
        // Reached when the homeserver ends the session on its own - a token
        // revoked from another device. Our own logout() detaches first.
        clearStoredSession();
        retireConnection(QStringLiteral("the homeserver ended the session"));
        // Left in Failed rather than moved on to LoggedOut: the two look the same
        // to the sign-in page except that Failed still carries the reason.
        setState(State::Failed, i18nc("@info:status", "The homeserver signed this device out."));
    });

    m_connection = c;
    Q_EMIT connectionChanged();
    // The connection object is what encryptionActive() reads, so replacing or
    // dropping it moves that answer too.
    Q_EMIT encryptionActiveChanged();
    return c;
}

void MatrixManager::retireConnection(const QString &why)
{
    if (!m_connection)
        return;
    auto *c = m_connection;
    m_connection = nullptr;
    // Printed before the abort rather than after it: stopping the connection
    // makes libQuotient log every job still in flight as "stopped without ready
    // network reply", which reads as a server hanging up. This line is what says
    // the hanging up was ours.
    qCInfo(KOUTNET_LOG_MATRIX).noquote() << QStringLiteral("closing the Matrix connection -") << why
                                         << QStringLiteral("- any 'stopped without ready network reply' below is this cleanup, not the homeserver");
    c->disconnect(this);
    c->stopSync();
    c->deleteLater();
    Q_EMIT connectionChanged();
    Q_EMIT encryptionActiveChanged();
}

void MatrixManager::armSyncDeadline()
{
    m_syncTimeout.start();
}

void MatrixManager::onSyncDeadline()
{
    if (m_state != State::Syncing && m_state != State::Reconnecting)
        return;

    const QString reason = m_lastSyncError.isEmpty()
        ? i18nc("@info:status Matrix sync gave up with nothing reported", "The homeserver stopped answering; no messages are arriving.")
        : i18nc("@info:status Matrix sync gave up, %1 is what libQuotient reported", "Syncing failed: %1", m_lastSyncError);

    retireConnection(QStringLiteral("no sync completed before the deadline"));
    // The stored session stays. A network that went away is not a token that was
    // revoked, and the next start should be allowed to try it.
    setState(State::Failed, reason);
}

bool MatrixManager::keyBackupAvailable() const
{
    // The backup lives in account data, one event, and asking costs a lookup in
    // a cache libQuotient already keeps. An absent backup is an empty object,
    // which accountDataJson() reports as "{}".
    return m_connection != nullptr && !m_connection->accountDataJson(QStringLiteral("m.megolm_backup.v1")).isEmpty();
}

bool MatrixManager::isKeyBackupUnlocked() const
{
    return m_connection != nullptr && m_keyBackupUnlocked;
}

void MatrixManager::unlockKeyBackup(const QString &recoveryKeyOrPassphrase)
{
    auto *connection = m_connection;
    if (connection == nullptr)
        return;

    // A recovery key and a passphrase share one field in the UI. The key has a
    // fixed shape (twelve groups of four base58 chars), so when the string looks
    // like one we try the security-key call first and fall back to the
    // passphrase; otherwise the other way round. Trying the right one first means
    // most unlocks succeed without the deliberate miss-and-retry.
    const bool tryAsKey = matrix::looksLikeRecoveryKey(recoveryKeyOrPassphrase);

    auto *handler = new Quotient::SSSSHandler();
    handler->setConnection(connection);
    connect(handler, &Quotient::SSSSHandler::keyBackupUnlocked, this, [this, handler]() {
        m_keyBackupUnlocked = true;
        Q_EMIT keyBackupUnlocked();
        connect(handler, &Quotient::SSSSHandler::finished, handler, &QObject::deleteLater);
    });
    connect(handler, &Quotient::SSSSHandler::error, this, [this, handler, recoveryKeyOrPassphrase, tryAsKey]() {
        // One retry as the other interpretation of the field, then report. The
        // first tap is disconnected so the second error reaches only the report.
        disconnect(handler, &Quotient::SSSSHandler::error, this, nullptr);
        if (recoveryKeyOrPassphrase.isEmpty()) {
            Q_EMIT keyBackupFailed(i18nc("@info:status", "No recovery key or passphrase was given."));
            handler->deleteLater();
            return;
        }
        connect(handler, &Quotient::SSSSHandler::error, this, [this, handler]() {
            Q_EMIT keyBackupFailed(i18nc("@info:status", "That did not unlock the room key backup."));
            handler->deleteLater();
        });
        if (tryAsKey)
            handler->unlockSSSSWithPassphrase(recoveryKeyOrPassphrase);
        else
            handler->unlockSSSSFromSecurityKey(recoveryKeyOrPassphrase);
    });

    if (recoveryKeyOrPassphrase.isEmpty())
        handler->unlockSSSSFromCrossSigning();
    else if (tryAsKey)
        handler->unlockSSSSFromSecurityKey(recoveryKeyOrPassphrase);
    else
        handler->unlockSSSSWithPassphrase(recoveryKeyOrPassphrase);
}

void MatrixManager::login(const QString &homeserverUrl, const QString &userIdOrLocalpart, const QString &password)
{
    if (userIdOrLocalpart.isEmpty() || password.isEmpty()) {
        setState(State::Failed, i18nc("@info:status Matrix login", "A user name and a password are needed."));
        return;
    }

    m_resuming = false;
    m_lastSyncError.clear();
    m_syncTimeout.stop();

    auto *c = makeConnection();
    setState(State::Connecting);
    m_loginTimeout.start();

    // resolveServer handles the .well-known delegation that setHomeserver
    // skips, and matrix.org does delegate. Given no server, the domain has to
    // come out of the MXID.
    // resolveServer wants an MXID and takes the domain out of it, so a
    // homeserver typed on its own has to be pasted back onto the user name.
    const QString server = homeserverUrl.trimmed();
    QString target = userIdOrLocalpart.trimmed();
    if (!target.startsWith(QLatin1Char('@')))
        target.prepend(QLatin1Char('@'));
    if (!target.contains(QLatin1Char(':')) && !server.isEmpty()) {
        QString host = server;
        host.remove(QStringLiteral("https://"));
        host.remove(QStringLiteral("http://"));
        while (host.endsWith(QLatin1Char('/')))
            host.chop(1);
        target += QLatin1Char(':') + host;
    }
    if (!target.contains(QLatin1Char(':'))) {
        m_loginTimeout.stop();
        retireConnection(QStringLiteral("no domain in the user id and no homeserver given"));
        setState(State::Failed, i18nc("@info:status Matrix login", "Give a homeserver, or write the user ID in full as @name:server."));
        return;
    }

    // Delegation is followed in two cases: when the user asked for it, and when
    // there is nothing else to go on. An empty homeserver field leaves only the
    // MXID's domain, and finding the server from that is exactly what the
    // .well-known record is for, so the switch has no say here - see the note
    // on matrixFollowDelegation in the kcfg.
    const bool followDelegation = m_settings && m_settings->matrixFollowDelegation();
    if (server.isEmpty() || followDelegation) {
        m_pendingUser = target;
        m_pendingPassword = password;
        connect(
            c,
            &Quotient::Connection::homeserverChanged,
            this,
            [this](const QUrl &) {
                if (!m_connection || m_pendingPassword.isEmpty())
                    return;
                const QString user = m_pendingUser;
                const QString pass = m_pendingPassword;
                m_pendingUser.clear();
                m_pendingPassword.clear();
                m_connection->loginWithPassword(user, pass, deviceDisplayName());
            },
            Qt::SingleShotConnection);
        c->resolveServer(target);
        return;
    }

    // An address typed by hand, with delegation turned off: taken at its word.
    // matrix.org delegates to matrix-client.matrix.org, which some networks do
    // not carry while the plain domain answers fine.
    //
    // setHomeserver() plus a wait for the flows is what actually suppresses the
    // lookup, and the wait is the load-bearing half. libQuotient only skips
    // resolveServer() when the base URL is valid *and* the password flow is
    // already known to be supported (Connection::Private::ensureHomeserver);
    // with the flows not yet in, loginWithPassword() resolves the server from
    // the MXID and follows the record after all.
    QUrl base = QUrl::fromUserInput(server);
    if (base.scheme().isEmpty())
        base.setScheme(QStringLiteral("https"));
    // Wait for the flows before logging in. loginWithPassword derives the
    // server from the MXID when they are not loaded yet, which throws away
    // both the scheme and the port of whatever was typed here.
    m_pendingUser = target;
    m_pendingPassword = password;
    connect(
        c,
        &Quotient::Connection::loginFlowsChanged,
        this,
        [this, base]() {
            if (!m_connection || m_pendingPassword.isEmpty())
                return;
            const QString user = m_pendingUser;
            const QString pass = m_pendingPassword;
            m_pendingUser.clear();
            m_pendingPassword.clear();
            // The other half of the invariant above. Flows that came back empty
            // - the address is not a homeserver, or the network ate the
            // request - would send loginWithPassword() down the resolve path
            // and quietly undo the setting the user just chose. Better to stop
            // and say which address failed than to sign in somewhere else.
            if (!m_connection->supportsPasswordAuth()) {
                m_loginTimeout.stop();
                retireConnection(QStringLiteral("the typed homeserver offered no password login"));
                setState(State::Failed,
                         i18nc("@info:status a homeserver typed by hand did not answer usefully, %1 is that address",
                               "%1 did not offer a password sign-in. Check the address, or turn on following the "
                               "server's redirect so that the address it publishes is used instead.",
                               base.toString()));
                return;
            }
            m_connection->loginWithPassword(user, pass, deviceDisplayName());
        },
        Qt::SingleShotConnection);
    c->setHomeserver(base);
}

bool MatrixManager::resumeSession()
{
    if (!m_settings)
        return false;

    const QString user = m_settings->matrixUserId();
    const QString device = m_settings->matrixDeviceId();
    const QString server = m_settings->matrixHomeserver();
    if (user.isEmpty() || device.isEmpty())
        return false;

    QString token;
    if ((!koutnet::KeepSecret::read(tokenStoreKey(user), &token) || token.isEmpty())
        && (!koutnet::KeepSecret::read(legacyTokenStoreKey(), &token) || token.isEmpty())) {
        // The token is the session. Without it the recorded user id and device id
        // are litter that would make the interface claim a session that is gone.
        clearStoredSession();
        setState(State::Failed,
                 i18nc("@info:status a stored Matrix session could not be reopened",
                       "The saved sign-in could not be read back from the secret store. Sign in again."));
        return false;
    }

    m_resuming = true;
    m_lastSyncError.clear();

    auto *c = makeConnection();
    setState(State::Connecting);
    m_loginTimeout.start();
    // A stored session never resolves anything: assumeIdentity() asks for no
    // particular login flow, so libQuotient is satisfied by a valid base URL
    // alone and never reaches for the .well-known record. The address that
    // worked last time is the address used again, whichever way the delegation
    // setting is turned.
    if (!server.isEmpty()) {
        const QUrl base = QUrl::fromUserInput(server);
        if (base.isValid())
            c->setHomeserver(base);
    }
    // Reaches connected() before the token has been shown to anybody: this call
    // sets the identity and checks it afterwards. Everything that treats the
    // resume as unproven until the first sync hangs off m_resuming.
    c->assumeIdentity(user, device, token);

    // A resumed token has never been shown to the homeserver at this point, and
    // a migrated one may name another owner entirely - the legacy store slot
    // was shared between accounts. Ask whose token this is before any sync runs:
    // sending as the wrong account is worse than asking for a fresh sign-in.
    c->callApi<Quotient::GetTokenOwnerJob>().then(this, [this, c, user](const Quotient::GetTokenOwnerJob *job) {
        if (job->userId() == user || c != m_connection)
            return;
        qCWarning(KOUTNET_LOG_MATRIX) << "resumed token belongs to" << job->userId() << "but the configuration says" << user;
        retireConnection(QStringLiteral("the resumed session belongs to another account"));
        clearStoredSession();
        setState(State::Failed,
                 i18nc("@info:status %1 is the account the token belongs to, %2 the account the configuration expected",
                       "The saved sign-in belongs to %1, not %2. Sign in again.",
                       job->userId(),
                       user));
    });
    return true;
}

QString MatrixManager::ssoLoginUrl(const QString &homeserverUrl)
{
    QUrl base = QUrl::fromUserInput(homeserverUrl.trimmed());
    if (base.scheme().isEmpty())
        base.setScheme(QStringLiteral("https"));
    if (!base.isValid() || base.host().isEmpty())
        return QString();
    // The QML path calls this function and then completeSsoLogin(), without
    // calling startSsoLogin(). Keep the homeserver with the generated URL so
    // the token-only completion API is independent of how the browser opened.
    m_ssoHomeserver = base.toString();
    // The redirect the homeserver is told to use; once SSO is done the browser
    // lands here with a loginToken we hand to loginWithToken().
    return matrix::ssoRedirectUrl(base, QUrl(QStringLiteral("koutnet://sso"))).toString();
}

void MatrixManager::startSsoLogin(const QString &homeserverUrl)
{
    const QString url = ssoLoginUrl(homeserverUrl);
    if (!url.isEmpty())
        QDesktopServices::openUrl(QUrl(url));
}

void MatrixManager::onSsoRedirect(const QUrl &url)
{
    // The homeserver appends loginToken to whichever redirectUrl we sent; it may
    // land in the query or the fragment depending on the server, so try both.
    QString token = QUrlQuery(url).queryItemValue(QStringLiteral("loginToken"));
    if (token.isEmpty() && !url.fragment().isEmpty())
        token = QUrlQuery(QLatin1Char('?') + url.fragment()).queryItemValue(QStringLiteral("loginToken"));
    if (!token.isEmpty())
        loginWithToken(m_ssoHomeserver, token);
}

void MatrixManager::completeSsoLogin(const QString &token)
{
    if (m_ssoHomeserver.isEmpty()) {
        setState(State::Failed, i18nc("@info:status Matrix SSO login", "Choose a homeserver before completing SSO sign-in."));
        return;
    }
    loginWithToken(m_ssoHomeserver, token);
}

void MatrixManager::loginWithToken(const QString &homeserverUrl, const QString &token)
{
    if (token.isEmpty()) {
        setState(State::Failed, i18nc("@info:status Matrix SSO login", "A login token is needed to sign in."));
        return;
    }

    m_resuming = false;
    m_lastSyncError.clear();
    m_syncTimeout.stop();

    auto *c = makeConnection();
    setState(State::Connecting);
    m_loginTimeout.start();

    // loginWithToken() cannot resolve the server from the token, so the
    // homeserver has to be set up front - the same way resumeSession() does for
    // a stored access token.
    const QString server = homeserverUrl.trimmed();
    if (!server.isEmpty()) {
        QUrl base = QUrl::fromUserInput(server);
        if (base.scheme().isEmpty())
            base.setScheme(QStringLiteral("https"));
        c->setHomeserver(base);
    }
    c->loginWithToken(token, deviceDisplayName());
}

void MatrixManager::onConnected()
{
    m_loginTimeout.stop();
    m_pendingUser.clear();
    m_pendingPassword.clear();
    if (!m_connection)
        return;

    // A resume has nothing new to write and nothing yet to confirm; its session
    // is stored again once a sync has proved the token still works.
    if (!m_resuming)
        storeSession();
    // Reads whatever the last run cached, so the room list is on screen before
    // the first sync comes back.
    m_connection->loadState();
    setState(State::Syncing);
    armSyncDeadline();
    m_connection->syncLoop();
}

void MatrixManager::storeSession()
{
    if (!m_connection || !m_settings)
        return;

    const QString token = QString::fromUtf8(m_connection->accessToken());
    if (!koutnet::KeepSecret::write(tokenStoreKey(m_connection->userId()), token)) {
        // Deliberately not falling back to the config file. The rest of the
        // session is not written either, so the next start asks again rather
        // than finding half a session it cannot use.
        Q_EMIT sessionNotPersisted(koutnet::KeepSecret::lastError());
        return;
    }

    m_settings->setMatrixUserId(m_connection->userId());
    m_settings->setMatrixDeviceId(m_connection->deviceId());
    m_settings->setMatrixHomeserver(m_connection->homeserver().toString());
    m_settings->save();
    // The pre-split slot, retired on the first successful write to the keyed one.
    koutnet::KeepSecret::remove(legacyTokenStoreKey());
}

void MatrixManager::clearStoredSession()
{
    if (m_settings && !m_settings->matrixUserId().isEmpty())
        koutnet::KeepSecret::remove(tokenStoreKey(m_settings->matrixUserId()));
    koutnet::KeepSecret::remove(legacyTokenStoreKey());
    if (!m_settings)
        return;
    m_settings->setMatrixUserId(QString());
    m_settings->setMatrixDeviceId(QString());
    // The homeserver stays: it is not a secret and it is what the login form
    // should offer next time.
    m_settings->save();
}

void MatrixManager::logout()
{
    m_loginTimeout.stop();
    m_syncTimeout.stop();
    m_pendingUser.clear();
    m_pendingPassword.clear();
    m_resuming = false;
    m_lastSyncError.clear();
    m_keyBackupUnlocked = false;

    // Local first, and whatever the homeserver does about it. Waiting for the
    // round trip is what left the button doing nothing against a server that
    // never answered: the token is out of the store and the ids out of the
    // config before anything is asked of the network, so signing out always
    // ends signed out.
    auto *c = m_connection;
    m_connection = nullptr;
    clearStoredSession();
    setState(State::LoggedOut);
    Q_EMIT connectionChanged();
    Q_EMIT encryptionActiveChanged();

    if (!c)
        return;

    // Detached before it is asked anything: its signals steer the state above,
    // and a logout the server refuses would otherwise put the interface back
    // where it started.
    c->disconnect(this);
    // Politeness, so the token stops working on the homeserver too. Nothing here
    // depends on the answer; libQuotient deletes the connection itself when the
    // job succeeds, and the timer below deals with the case where it never does.
    c->logout();

    const QPointer<Quotient::Connection> pending(c);
    QTimer::singleShot(kLogoutGraceMs, this, [pending]() {
        if (!pending)
            return;
        qCInfo(KOUTNET_LOG_MATRIX) << "the homeserver never acknowledged the sign-out; dropping the connection locally."
                                   << "The token may still be live server-side - revoke the KOutNet device from another client if that matters.";
        pending->stopSync();
        pending->deleteLater();
    });
}

} // namespace koutnet
