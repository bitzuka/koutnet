// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "MatrixVerification.h"

#include "MatrixManager.h"

#include "koutnet_matrix_debug.h"

#include <KLocalizedString>

#include <Quotient/connection.h>
#include <Quotient/csapi/device_management.h>
#include <Quotient/database.h>
#include <Quotient/keyverificationsession.h>

using Session = Quotient::KeyVerificationSession;

namespace koutnet
{

MatrixVerification::MatrixVerification(MatrixManager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
{
    if (m_manager) {
        connect(m_manager, &MatrixManager::connectionChanged, this, [this]() {
            // The old connection took its sessions with it when it died. Say so
            // rather than leaving a dialog pointed at nothing.
            if (m_session)
                m_session->disconnect(this);
            m_session.clear();
            m_deviceNames.clear();
            m_deviceLastSeen.clear();
            m_finished = false;
            m_verified = false;
            m_outcome.clear();
            attach(connection());
            Q_EMIT changed();
            Q_EMIT devicesChanged();
        });
        attach(connection());
    }
}

Quotient::Connection *MatrixVerification::connection() const
{
    return m_manager ? m_manager->connection() : nullptr;
}

void MatrixVerification::attach(Quotient::Connection *c)
{
    if (c == nullptr)
        return;

    // Every session, ours and theirs, arrives here: libQuotient emits this from
    // setupKeyVerificationSession(), which is what both the incoming to-device
    // request and our own startKeyVerificationSession() go through. One handler
    // is therefore the whole of the incoming half of this class.
    connect(c, &Quotient::Connection::newKeyVerificationSession, this, &MatrixVerification::adopt);

    // A verification that succeeded moves the trust table, and the room column
    // and the member card are reading that table. Without this the padlock only
    // catches up on the next sync.
    connect(c, &Quotient::Connection::sessionVerified, this, [this]() {
        Q_EMIT changed();
        Q_EMIT devicesChanged();
    });
    connect(c, &Quotient::Connection::devicesListLoaded, this, &MatrixVerification::devicesChanged);
}

bool MatrixVerification::available() const
{
    // database() and not just encryptionEnabled(): startSelfVerification() and
    // the trust queries dereference the database with no null check of their
    // own, and a session whose key store failed has encryption reported off but
    // reaches this code all the same. See the header.
    const Quotient::Connection *c = connection();
    return c != nullptr && c->encryptionEnabled() && c->database() != nullptr;
}

bool MatrixVerification::active() const
{
    return !m_session.isNull() || m_finished;
}

bool MatrixVerification::incoming() const
{
    return m_incoming;
}

bool MatrixVerification::awaitingAccept() const
{
    return !m_session.isNull() && m_incoming && m_session->state() == Session::INCOMING;
}

bool MatrixVerification::comparing() const
{
    return !m_session.isNull() && m_session->state() == Session::WAITINGFORVERIFICATION;
}

bool MatrixVerification::userVerification() const
{
    return m_userVerification;
}

QString MatrixVerification::remoteUserId() const
{
    return m_remoteUserId;
}

QString MatrixVerification::remoteDeviceId() const
{
    return m_remoteDeviceId;
}

QVariantList MatrixVerification::emojis() const
{
    QVariantList out;
    if (m_session.isNull())
        return out;
    const QVector<Quotient::EmojiEntry> entries = m_session->sasEmojis();
    out.reserve(entries.size());
    for (const Quotient::EmojiEntry &entry : entries) {
        out.append(QVariantMap{
            {QStringLiteral("emoji"), entry.emoji},
            {QStringLiteral("description"), entry.description},
        });
    }
    return out;
}

QString MatrixVerification::statusText() const
{
    if (m_finished)
        return m_outcome;
    if (m_session.isNull())
        return QString();

    switch (m_session->state()) {
    case Session::INCOMING:
        return m_incoming ? i18nc("@info:status another Matrix device has asked to verify this one", "Another session wants to verify with this one.")
                          : i18nc("@info:status a verification has been prepared but not sent yet", "Preparing...");
    case Session::WAITINGFORREADY:
        return i18nc("@info:status a verification request has been sent to another device",
                     "Waiting for the other session to accept. Open it and answer the request there.");
    case Session::READY:
    case Session::WAITINGFORACCEPT:
    case Session::ACCEPTED:
    case Session::WAITINGFORKEY:
        return i18nc("@info:status the two devices are exchanging verification keys", "Exchanging keys...");
    case Session::WAITINGFORVERIFICATION:
        return i18nc("@info:status the emoji are on screen and the user has to compare them", "Check that both sessions show these emoji, in this order.");
    case Session::WAITINGFORMAC:
        return i18nc("@info:status this side has confirmed and is waiting for the other", "Waiting for the other session to confirm.");
    case Session::CANCELED:
    case Session::DONE:
        break;
    }
    return m_outcome;
}

QVariantList MatrixVerification::ownDevices() const
{
    QVariantList out;
    const Quotient::Connection *c = connection();
    if (!available())
        return out;

    const QString self = c->userId();
    const QStringList ids = c->devicesForUser(self);
    out.reserve(ids.size());
    for (const QString &id : ids) {
        const bool isCurrent = id == c->deviceId();
        out.append(QVariantMap{
            {QStringLiteral("deviceId"), id},
            {QStringLiteral("displayName"), m_deviceNames.value(id)},
            {QStringLiteral("lastSeenIp"), m_deviceLastSeen.value(id)},
            {QStringLiteral("verified"), c->isVerifiedDevice(self, id)},
            {QStringLiteral("isCurrent"), isCurrent},
        });
    }
    return out;
}

void MatrixVerification::refreshDevices()
{
    Quotient::Connection *c = connection();
    if (c == nullptr)
        return;

    // Names only. Whether a device is trusted is never taken from this job -
    // the homeserver is the party a verification exists to not have to trust,
    // and it would happily label anything "verified".
    c->callApi<Quotient::GetDevicesJob>().onResult(this, [this](const Quotient::GetDevicesJob *job) {
        if (job->error() != Quotient::BaseJob::Success) {
            qCWarning(KOUTNET_LOG_MATRIX) << "could not list this account's devices:" << job->errorString();
            return;
        }
        m_deviceNames.clear();
        m_deviceLastSeen.clear();
        const QVector<Quotient::Device> devices = job->devices();
        for (const Quotient::Device &device : devices) {
            m_deviceNames.insert(device.deviceId, device.displayName);
            m_deviceLastSeen.insert(device.deviceId, device.lastSeenIp);
        }
        Q_EMIT devicesChanged();
    });
}

void MatrixVerification::adopt(Quotient::KeyVerificationSession *session)
{
    if (session == nullptr)
        return;

    if (!m_session.isNull() && m_session != session) {
        // Left to time out rather than cancelled: cancelling would tell the
        // other end this account refused, when the truth is only that somebody
        // is already halfway through a comparison here.
        qCInfo(KOUTNET_LOG_MATRIX) << "a second key verification session arrived while one was running; ignoring" << session->remoteDeviceId();
        return;
    }
    if (m_session == session)
        return;

    m_session = session;
    m_finished = false;
    m_verified = false;
    m_outcome.clear();
    // Through the metaobject because there is no getter: libQuotient declares
    // remoteUserId as a MEMBER property over a private field, so this is the
    // only way to it from outside. remoteDeviceId does have one.
    m_remoteUserId = session->property("remoteUserId").toString();
    m_remoteDeviceId = session->remoteDeviceId();
    m_userVerification = session->userVerification();
    // An outgoing session is built in INCOMING and only leaves it when
    // sendRequest() is called, so the state cannot tell the two apart. What can
    // is who owns the transaction: ours is the one verifyOwnDevice() is about
    // to send a request for, and it sets this to false immediately afterwards.
    m_incoming = session->state() == Session::INCOMING;

    connect(session, &Session::stateChanged, this, &MatrixVerification::onSessionStateChanged);
    connect(session, &Session::sasEmojisChanged, this, &MatrixVerification::changed);
    connect(session, &Session::errorChanged, this, &MatrixVerification::changed);

    Q_EMIT changed();
    Q_EMIT sessionStarted();
}

void MatrixVerification::onSessionStateChanged()
{
    if (m_session.isNull())
        return;

    const Session::State state = m_session->state();
    // DONE and CANCELED are both terminal, and they are handled here rather
    // than on finished() because libQuotient does not always emit it: when the
    // other side's MAC arrives before the user has confirmed, sendMac() sets
    // DONE directly and neither emits finished() nor deletes the session. A
    // dialog waiting for finished() would sit on a completed verification.
    if (state == Session::DONE) {
        m_verified = true;
        m_finished = true;
        m_outcome = m_userVerification ? i18nc("@info:status a Matrix user was verified, %1 is their user id", "%1 is verified.", m_remoteUserId)
                                       : i18nc("@info:status a Matrix device was verified, %1 is the device id",
                                               "Session %1 is verified. Encrypted rooms should start filling in as its keys arrive.",
                                               m_remoteDeviceId);
        qCInfo(KOUTNET_LOG_MATRIX) << "key verification succeeded with" << m_remoteUserId << m_remoteDeviceId;
        m_session->disconnect(this);
        m_session.clear();
        Q_EMIT changed();
        Q_EMIT devicesChanged();
        Q_EMIT sessionFinished(true, m_outcome);
        return;
    }

    if (state == Session::CANCELED) {
        m_verified = false;
        m_finished = true;
        switch (m_session->error()) {
        case Session::TIMEOUT:
        case Session::REMOTE_TIMEOUT:
            m_outcome = i18nc("@info:status a device verification ran out of time", "The verification timed out. Nothing was verified.");
            break;
        case Session::USER:
            m_outcome = i18nc("@info:status a device verification was cancelled on this side", "The verification was cancelled. Nothing was verified.");
            break;
        case Session::REMOTE_USER:
            m_outcome = i18nc("@info:status a device verification was cancelled by the other device",
                              "The other session cancelled the verification. Nothing was verified.");
            break;
        case Session::MISMATCHED_SAS:
        case Session::REMOTE_MISMATCHED_SAS:
        case Session::MISMATCHED_COMMITMENT:
        case Session::REMOTE_MISMATCHED_COMMITMENT:
        case Session::KEY_MISMATCH:
        case Session::REMOTE_KEY_MISMATCH:
            // Deliberately the loudest sentence in this file. A mismatch is the
            // one outcome that is evidence of something rather than absence of
            // it, and it must not read like a cancel.
            m_outcome = i18nc("@info:status the two devices showed different verification emoji",
                              "The two sessions did not match. Nothing was verified, and somebody may be "
                              "listening in - do not use this session for anything private until you have "
                              "checked it another way.");
            break;
        default:
            m_outcome =
                i18nc("@info:status a device verification failed for a protocol reason", "The verification could not be completed. Nothing was verified.");
            break;
        }
        qCWarning(KOUTNET_LOG_MATRIX) << "key verification with" << m_remoteUserId << m_remoteDeviceId << "ended unverified, error" << int(m_session->error());
        m_session->disconnect(this);
        m_session.clear();
        Q_EMIT changed();
        Q_EMIT sessionFinished(false, m_outcome);
        return;
    }

    Q_EMIT changed();
}

void MatrixVerification::verifyOwnDevice(const QString &deviceId)
{
    Quotient::Connection *c = connection();
    if (!available() || deviceId.isEmpty())
        return;
    if (!m_session.isNull())
        return;
    if (deviceId == c->deviceId())
        return;
    // Only ever a device this account already owns. Passing an arbitrary id
    // here would send a to-device request to whatever the caller named, which
    // is not what a button labelled "verify one of your sessions" means.
    if (!c->devicesForUser(c->userId()).contains(deviceId)) {
        qCWarning(KOUTNET_LOG_MATRIX) << "refusing to verify" << deviceId << "- not a known session of this account";
        return;
    }

    Session *session = c->startKeyVerificationSession(c->userId(), deviceId);
    if (session == nullptr) {
        qCWarning(KOUTNET_LOG_MATRIX) << "libQuotient refused to start a verification session";
        return;
    }
    // adopt() ran inside startKeyVerificationSession() - libQuotient emits
    // newKeyVerificationSession() from there - and guessed "incoming" from the
    // state, which is INCOMING for a freshly built outgoing session too. This
    // is the only place that knows better.
    m_incoming = false;
    // The library does not send the request by itself; the session sits in
    // INCOMING until this call, which is what makes the other client ring.
    session->sendRequest();
    Q_EMIT changed();
}

void MatrixVerification::verifyFromVerifiedSessions()
{
    if (!available() || !m_session.isNull())
        return;
    // Only reaches sessions already marked selfVerified in the local trust
    // table, so on a device that has never verified anything it does nothing at
    // all. That is libQuotient's behaviour and not a bug here, but it does mean
    // this cannot be the only way in - hence verifyOwnDevice().
    connection()->startSelfVerification();
}

void MatrixVerification::acceptRequest()
{
    if (m_session.isNull() || !awaitingAccept())
        return;
    m_session->sendReady();
    Q_EMIT changed();
}

void MatrixVerification::confirmMatch()
{
    // Guarded on the state and not just on the button being visible: this is
    // the click that marks another device trusted, and it must be impossible to
    // land it before the emoji have actually been compared.
    if (m_session.isNull() || m_session->state() != Session::WAITINGFORVERIFICATION)
        return;
    m_session->sendMac();
    Q_EMIT changed();
}

void MatrixVerification::rejectMismatch()
{
    if (m_session.isNull())
        return;
    m_session->cancelVerification(Session::MISMATCHED_SAS);
}

void MatrixVerification::cancel()
{
    if (m_session.isNull())
        return;
    m_session->cancelVerification(Session::USER);
}

void MatrixVerification::dismiss()
{
    if (!m_session.isNull())
        return;
    m_finished = false;
    m_verified = false;
    m_outcome.clear();
    m_remoteUserId.clear();
    m_remoteDeviceId.clear();
    m_incoming = false;
    m_userVerification = false;
    Q_EMIT changed();
}

} // namespace koutnet
