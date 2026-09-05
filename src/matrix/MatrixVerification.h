// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - interactive device verification for the Matrix session.
//
// An unverified device is the reason an otherwise working session still cannot
// read an encrypted room: other clients decide who to send megolm keys to by
// looking at the trust table, and a device nobody has vouched for is left out
// of it. Everything else in K-Server mode can be right and the room will still
// be a column of "no key for this". This class is what fixes that.
//
// It owns the interaction and not the protocol. libQuotient's
// KeyVerificationSession runs the SAS exchange by itself once it has been
// pointed at a device; from READY onwards it sends the start, the key and the
// accept without being asked. There are exactly four moments where a human has
// to decide something, and they are the only four things this class calls:
//   - sendRequest()      : we are asking a device of ours to verify
//   - sendReady()        : something asked us and the user said yes
//   - sendMac()          : the emoji matched
//   - cancelVerification(): the emoji did not, or the user walked away
//
// Two invariants, both of them the kind that crash or lie if they are dropped.
//
// First, nothing here may touch libQuotient's trust API before the key store
// exists. Connection::startSelfVerification() opens with
// database()->prepareQuery() and database() is null for the whole life of a
// session whose encryption never started - the same unguarded pattern
// MatrixRoomBridge's canAskAboutTrust() exists for. available() is that guard
// and every entry point below reads it first.
//
// Second, this must never say a device is verified when it is not. The states
// below distinguish "verified", "not verified" and "could not ask", because the
// interface that reports them has to as well, and a session that ends in
// anything other than the user confirming a matching emoji ends unverified.
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

namespace Quotient
{
class Connection;
class KeyVerificationSession;
}

namespace koutnet
{

class MatrixManager;

class MatrixVerification : public QObject
{
    Q_OBJECT

    // Booleans and ready-made sentences rather than an exported enum, for the
    // same reason MatrixManager does it: this object reaches QML as a context
    // property, and a context property's type is not registered, so QML has no
    // way to name the enumerators.

    // Whether a verification can be started at all. False whenever there is no
    // connection or its key store never came up. Nothing below does anything
    // while this is false.
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // A session is running and the dialog should be up.
    Q_PROPERTY(bool active READ active NOTIFY changed)
    // The remote side asked us, as opposed to us asking it. Decides whether the
    // dialog opens on "accept or reject" or on "waiting for the other device".
    Q_PROPERTY(bool incoming READ incoming NOTIFY changed)
    // An incoming request nobody has accepted yet.
    Q_PROPERTY(bool awaitingAccept READ awaitingAccept NOTIFY changed)
    // The emoji are on screen and the answer is the user's. This is the only
    // state in which confirmMatch() means anything.
    Q_PROPERTY(bool comparing READ comparing NOTIFY changed)
    // Terminal, and which way it went. finished() with verified() false covers
    // a cancel, a timeout, a mismatch and a protocol error alike - they differ
    // in the sentence, not in the outcome, because none of them verified
    // anything.
    Q_PROPERTY(bool finished READ finished NOTIFY changed)
    Q_PROPERTY(bool verified READ verified NOTIFY changed)

    Q_PROPERTY(QString remoteUserId READ remoteUserId NOTIFY changed)
    Q_PROPERTY(QString remoteDeviceId READ remoteDeviceId NOTIFY changed)
    // Whether the far side is somebody else rather than another session of the
    // user's own. Worth saying out loud in the dialog: the two mean different
    // things and only one of them is about this account's own keys.
    Q_PROPERTY(bool userVerification READ userVerification NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)

    // The seven SAS emoji, as [{ emoji, description }]. Empty except while
    // comparing() is true.
    Q_PROPERTY(QVariantList emojis READ emojis NOTIFY changed)

public:
    explicit MatrixVerification(MatrixManager *manager, QObject *parent = nullptr);

    bool available() const;
    bool active() const;
    bool incoming() const;
    bool awaitingAccept() const;
    bool comparing() const;
    bool finished() const
    {
        return m_finished;
    }
    bool verified() const
    {
        return m_verified;
    }
    bool userVerification() const;
    QString remoteUserId() const;
    QString remoteDeviceId() const;
    QString statusText() const;
    QVariantList emojis() const;

    // This account's other sessions, most useful first, as
    // [{ deviceId, displayName, lastSeenIp, verified, isCurrent }]. Read from
    // libQuotient's trust table, so "verified" is what this session actually
    // knows rather than what the homeserver claims. The display names arrive
    // separately - see refreshDevices() - because the trust table does not
    // carry them and a list of opaque device ids is not something a user can
    // pick their own phone out of.
    Q_INVOKABLE QVariantList ownDevices() const;
    // Asks the homeserver for the device list again. The result lands in
    // devicesChanged(), not in the return value.
    Q_INVOKABLE void refreshDevices();

    // Start verifying one device of this account. deviceId must be one of
    // ownDevices(); anything else is refused rather than sent.
    Q_INVOKABLE void verifyOwnDevice(const QString &deviceId);
    // Ask every already-verified session of this account at once. Useful only
    // once at least one of them is verified - see the note on the
    // implementation - so the interface offers verifyOwnDevice() first.
    Q_INVOKABLE void verifyFromVerifiedSessions();
    // Start verifying another user (cross-signing). userId must be a member of
    // a room we share; deviceId is the device to verify. The SAS flow is the
    // same as for own devices but the trust conclusion cross-signs the user's
    // master key rather than a single device key.
    Q_INVOKABLE void verifyUser(const QString &userId, const QString &deviceId);

    // The four decisions. All of them are no-ops unless the session is in the
    // state that gives them a meaning, so a double click cannot send twice.
    Q_INVOKABLE void acceptRequest();
    Q_INVOKABLE void confirmMatch();
    Q_INVOKABLE void rejectMismatch();
    Q_INVOKABLE void cancel();

    // Clears a finished session so the dialog can close. Does not touch the
    // network: by this point there is nothing left to tell anybody.
    Q_INVOKABLE void dismiss();

Q_SIGNALS:
    // Any of the properties above. One signal because they move together and
    // the dialog re-reads all of them anyway.
    void changed();
    // A session appeared - ours or theirs. The window opens the dialog on this
    // rather than polling active().
    void sessionStarted();
    // Terminal. ok is true only when a device was actually verified.
    void sessionFinished(bool ok, QString message);
    void devicesChanged();

private:
    void attach(Quotient::Connection *connection);
    void adopt(Quotient::KeyVerificationSession *session);
    void onSessionStateChanged();
    Quotient::Connection *connection() const;

    QPointer<MatrixManager> m_manager;
    // One at a time. A second request arriving while one is up is left alone
    // rather than queued or answered: libQuotient times every session out after
    // two minutes, and silently swapping the dialog out from under somebody who
    // is halfway through comparing emoji is how the wrong device gets trusted.
    QPointer<Quotient::KeyVerificationSession> m_session;
    bool m_incoming = false;
    bool m_finished = false;
    bool m_verified = false;
    QString m_outcome;
    QString m_remoteUserId;
    QString m_remoteDeviceId;
    bool m_userVerification = false;
    // Device id to display name, from the last GetDevicesJob. Only ever used to
    // label a row; the ids themselves come from the trust table.
    QHash<QString, QString> m_deviceNames;
    QHash<QString, QString> m_deviceLastSeen;
    // Bumped whenever the connection is replaced, so an in-flight device list
    // from the old one cannot label the new account's devices.
    int m_devicesGeneration = 0;
};

} // namespace koutnet
