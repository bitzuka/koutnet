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

public:
    enum class State {
        LoggedOut,
        Connecting,
        Syncing,
        Online,
        Failed,
    };
    Q_ENUM(State)

    // A homeserver that never answers leaves the interface saying "Connecting"
    // for as long as the process lives, so the attempt is given a deadline.
    static constexpr int kLoginTimeoutMs = 30000;

    explicit MatrixManager(AppSettings *settings, QObject *parent = nullptr);
    ~MatrixManager() override;

    bool loggedIn() const;
    bool busy() const;
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

    // Reads back the session the last run stored. False when there is nothing to
    // resume, which is not an error.
    bool resumeSession();

Q_SIGNALS:
    void stateChanged();
    // The connection object itself was replaced or dropped, as opposed to its
    // state changing. Everything holding a Quotient::Connection * must re-read.
    void connectionChanged();
    // A wallet-less session. The interface has to say so rather than let the
    // user believe the login will still be there tomorrow.
    void sessionNotPersisted(QString reason);

private:
    void setState(State state, const QString &error = QString());
    Quotient::Connection *makeConnection();
    void dropConnection();
    void onConnected();
    void storeSession();
    void clearStoredSession();

    QPointer<AppSettings> m_settings;
    Quotient::Connection *m_connection = nullptr;
    State m_state = State::LoggedOut;
    QString m_lastError;
    QTimer m_loginTimeout;
    // Set while login() is waiting for the homeserver's login flows, so that the
    // password is not kept a moment longer than the round trip needs it.
    QString m_pendingUser;
    QString m_pendingPassword;
};

} // namespace koutnet
