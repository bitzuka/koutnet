// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "MatrixManager.h"

#include "core/constructor/AppSettings.h"
#include "core/security/SecretStore.h"

#include "koutnet_matrix_debug.h"

#include <KLocalizedString>

#include <QUrl>

#include <Quotient/connection.h>

namespace
{
// Wallet entry for the Matrix access token. Flat, like every other name in the
// KOutNet folder - see CryptoManager and AppSettings.
QString tokenWalletKey()
{
    return QStringLiteral("matrix_access_token");
}

// What the homeserver files this login under in the user's device list.
QString deviceDisplayName()
{
    return QStringLiteral("KOutNet");
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
        m_pendingPassword.clear();
        dropConnection();
        setState(State::Failed, i18nc("@info:status Matrix login failed", "The homeserver did not answer in time."));
    });
}

MatrixManager::~MatrixManager() = default;

bool MatrixManager::loggedIn() const
{
    return m_state == State::Syncing || m_state == State::Online;
}

bool MatrixManager::busy() const
{
    return m_state == State::Connecting;
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
    case State::Failed:
        break;
    }
    return m_lastError.isEmpty() ? i18nc("@info:status Matrix connection state", "Sign-in failed") : m_lastError;
}

void MatrixManager::setState(State state, const QString &error)
{
    if (m_state == state && m_lastError == error)
        return;
    m_state = state;
    m_lastError = error;
    if (!error.isEmpty())
        qCWarning(KOUTNET_LOG_MATRIX) << "matrix session:" << error;
    Q_EMIT stateChanged();
}

Quotient::Connection *MatrixManager::makeConnection()
{
    dropConnection();

    auto *c = new Quotient::Connection(this);
    // Explicitly off. libQuotient 0.9 always compiles E2EE in, so this is a
    // decision rather than a limitation: device verification and key backup are
    // out of scope for this pass, and a half-built E2EE that silently fails to
    // decrypt is worse than an honest refusal. MatrixRoomBridge says so in the
    // room rather than showing empty bubbles.
    c->enableEncryption(false);
    // Without the cache every start is a full initial sync of every room.
    c->setCacheState(true);
    c->setLazyLoading(true);

    connect(c, &Quotient::Connection::connected, this, &MatrixManager::onConnected);
    connect(c, &Quotient::Connection::loginError, this, [this](const QString &message, const QString &details) {
        m_pendingPassword.clear();
        m_loginTimeout.stop();
        dropConnection();
        setState(State::Failed, details.isEmpty() ? message : message + QLatin1Char(' ') + details);
    });
    connect(c, &Quotient::Connection::resolveError, this, [this](const QString &error) {
        m_pendingPassword.clear();
        m_loginTimeout.stop();
        dropConnection();
        setState(State::Failed, error);
    });
    connect(c, &Quotient::Connection::syncDone, this, [this]() {
        setState(State::Online);
    });
    connect(c, &Quotient::Connection::syncError, this, [this](const QString &message) {
        // Not a failure of the session: libQuotient retries by itself, and a
        // dropped wifi must not throw away a token that is still good.
        qCWarning(KOUTNET_LOG_MATRIX) << "sync error" << message;
        setState(State::Syncing);
    });
    connect(c, &Quotient::Connection::loggedOut, this, [this]() {
        clearStoredSession();
        dropConnection();
        setState(State::LoggedOut);
    });

    m_connection = c;
    Q_EMIT connectionChanged();
    return c;
}

void MatrixManager::dropConnection()
{
    if (!m_connection)
        return;
    auto *c = m_connection;
    m_connection = nullptr;
    c->disconnect(this);
    c->stopSync();
    c->deleteLater();
    Q_EMIT connectionChanged();
}

void MatrixManager::login(const QString &homeserverUrl, const QString &userIdOrLocalpart, const QString &password)
{
    if (userIdOrLocalpart.isEmpty() || password.isEmpty()) {
        setState(State::Failed, i18nc("@info:status Matrix login", "A user name and a password are needed."));
        return;
    }

    auto *c = makeConnection();
    setState(State::Connecting);
    m_loginTimeout.start();

    if (homeserverUrl.trimmed().isEmpty()) {
        // No server given, so the domain has to come out of a full MXID.
        // loginWithPassword() resolves it for us; if there is no domain either,
        // it reports through loginError.
        c->loginWithPassword(userIdOrLocalpart, password, deviceDisplayName());
        return;
    }

    // fromUserInput so "matrix.org" reaches the homeserver as https://matrix.org
    // rather than as a relative path.
    const QUrl base = QUrl::fromUserInput(homeserverUrl.trimmed());
    if (!base.isValid()) {
        m_loginTimeout.stop();
        dropConnection();
        setState(State::Failed, i18nc("@info:status Matrix login, %1 is what the user typed", "%1 is not a valid homeserver address.", homeserverUrl));
        return;
    }

    // The password is held only until the login flows come back, which is the
    // first moment loginWithPassword() can be called against a known server.
    m_pendingUser = userIdOrLocalpart;
    m_pendingPassword = password;
    connect(
        c,
        &Quotient::Connection::loginFlowsChanged,
        this,
        [this]() {
            if (!m_connection || m_pendingPassword.isEmpty())
                return;
            const QString user = m_pendingUser;
            const QString pass = m_pendingPassword;
            m_pendingUser.clear();
            m_pendingPassword.clear();
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
    if (!koutnet::SecretStore::read(tokenWalletKey(), &token) || token.isEmpty()) {
        // The token is the session. Without it the recorded user id and device id
        // are litter that would make the interface claim a session that is gone.
        qCWarning(KOUTNET_LOG_MATRIX) << "a Matrix session is recorded but its token is not in the wallet";
        clearStoredSession();
        return false;
    }

    auto *c = makeConnection();
    setState(State::Connecting);
    m_loginTimeout.start();
    if (!server.isEmpty()) {
        const QUrl base = QUrl::fromUserInput(server);
        if (base.isValid())
            c->setHomeserver(base);
    }
    c->assumeIdentity(user, device, token);
    return true;
}

void MatrixManager::onConnected()
{
    m_loginTimeout.stop();
    m_pendingUser.clear();
    m_pendingPassword.clear();
    if (!m_connection)
        return;

    storeSession();
    // Reads whatever the last run cached, so the room list is on screen before
    // the first sync comes back.
    m_connection->loadState();
    setState(State::Syncing);
    m_connection->syncLoop();
}

void MatrixManager::storeSession()
{
    if (!m_connection || !m_settings)
        return;

    const QString token = QString::fromUtf8(m_connection->accessToken());
    if (!koutnet::SecretStore::write(tokenWalletKey(), token)) {
        // Deliberately not falling back to the config file. The rest of the
        // session is not written either, so the next start asks again rather
        // than finding half a session it cannot use.
        Q_EMIT sessionNotPersisted(koutnet::SecretStore::lastError());
        return;
    }

    m_settings->setMatrixUserId(m_connection->userId());
    m_settings->setMatrixDeviceId(m_connection->deviceId());
    m_settings->setMatrixHomeserver(m_connection->homeserver().toString());
    m_settings->save();
}

void MatrixManager::clearStoredSession()
{
    koutnet::SecretStore::remove(tokenWalletKey());
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
    if (!m_connection) {
        clearStoredSession();
        setState(State::LoggedOut);
        return;
    }
    // The token is invalidated server-side by logout(); loggedOut() then clears
    // the wallet entry. Dropping the connection here instead would leave a live
    // token on the homeserver with nothing able to revoke it.
    m_connection->logout();
}

} // namespace koutnet
