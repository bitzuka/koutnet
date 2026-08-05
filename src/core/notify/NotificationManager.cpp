// SPDX-FileCopyrightText: 2020 Tobias Fella <tobias.fella@kde.org>
// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Adapted from NeoChat's src/app/notificationsmanager.cpp. See the note in the
// header for what was taken and what was left behind.

#include "NotificationManager.h"

#include <memory>

#include <QGuiApplication>

#include <KLocalizedString>
#include <KNotification>
#include <KNotificationPermission>
#include <KNotificationReplyAction>

namespace koutnet
{

NotificationManager::NotificationManager(QObject *parent)
    : QObject(parent)
{
    // Asked for once, at startup, rather than at the first message: the answer
    // is a system dialog, and putting one in front of somebody the moment a
    // message lands is worse than asking before anything has happened. On a
    // plain desktop session this is already granted and does nothing.
    if (KNotificationPermission::checkPermission() == Qt::PermissionStatus::Undetermined) {
        KNotificationPermission::requestPermission(this, [](Qt::PermissionStatus) {
            // Nothing to do either way. A denied permission makes sendEvent a
            // no-op, which is the behaviour that was asked for.
        });
    }
}

void NotificationManager::adoptActivationToken(KNotification *notification)
{
    const QString token = notification->xdgActivationToken();
    if (token.isEmpty()) {
        return;
    }
    // Wayland refuses a raise that the user did not ask for. Clicking a
    // notification is asking, and the token is the proof; Qt picks it up from
    // here the next time a window calls requestActivate().
    qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
}

bool NotificationManager::windowHasFocus()
{
    return QGuiApplication::applicationState() == Qt::ApplicationActive;
}

KNotification *NotificationManager::makeNotification(const QString &eventId) const
{
    auto *notification = new KNotification(eventId);
    // Set by hand rather than left to default. KNotification falls back to
    // QCoreApplication::applicationName() to find the notifyrc, and main.cpp
    // sets that to "KOutNet" so the existing config path under ~/.config keeps
    // working - which would send it looking for KOutNet.notifyrc. The installed
    // file is koutnet.notifyrc, after the KAboutData component name.
    notification->setComponentName(QStringLiteral("koutnet"));
    return notification;
}

void NotificationManager::notifyMessage(const QString &chatId, const QString &sender, const QString &text)
{
    if (chatId.isEmpty() || windowHasFocus()) {
        return;
    }

    // Upstream closes the previous notification for the room before posting the
    // next. Same idea: one popup per conversation, showing its latest message.
    if (auto *previous = m_messageNotifications.value(chatId).data()) {
        previous->close();
    }

    auto *notification = makeNotification(QStringLiteral("message"));
    m_messageNotifications.insert(chatId, notification);
    connect(notification, &KNotification::closed, this, [this, chatId, notification] {
        if (m_messageNotifications.value(chatId).data() == notification) {
            m_messageNotifications.remove(chatId);
        }
    });

    notification->setTitle(sender.isEmpty() ? chatId : sender);
    // Escaped because the notification body is parsed as markup by most servers,
    // and a message is a string somebody else chose.
    notification->setText(text.toHtmlEscaped());

    auto *defaultAction = notification->addDefaultAction(i18nc("@action:button clicking the notification opens the conversation", "Open the conversation"));
    connect(defaultAction, &KNotificationAction::activated, this, [this, chatId, notification] {
        adoptActivationToken(notification);
        Q_EMIT chatRequested(chatId);
    });

    auto replyAction = std::make_unique<KNotificationReplyAction>(i18nc("@action:button answer the message from the notification itself", "Reply"));
    replyAction->setPlaceholderText(i18nc("@info:placeholder inline reply field on a notification", "Reply..."));
    connect(replyAction.get(), &KNotificationReplyAction::replied, this, [this, chatId](const QString &reply) {
        Q_EMIT replyRequested(chatId, reply);
    });
    notification->setReplyAction(std::move(replyAction));

    notification->sendEvent();
}

void NotificationManager::notifyCall(const QString &callerName, const QString &callerIp)
{
    if (callerIp.isEmpty()) {
        return;
    }

    if (auto *previous = m_callNotifications.value(callerIp).data()) {
        previous->close();
    }

    auto *notification = makeNotification(QStringLiteral("call"));
    m_callNotifications.insert(callerIp, notification);
    connect(notification, &KNotification::closed, this, [this, callerIp, notification] {
        if (m_callNotifications.value(callerIp).data() == notification) {
            m_callNotifications.remove(callerIp);
        }
    });

    notification->setTitle(i18nc("@title:window notification for a ringing call", "Incoming call"));
    notification->setText(i18nc("@info %1 is the name of the caller", "%1 is calling you", callerName.isEmpty() ? callerIp : callerName));

    auto *defaultAction = notification->addDefaultAction(i18nc("@action:button bring the call window forward", "Show the call"));
    connect(defaultAction, &KNotificationAction::activated, this, [this, callerIp, notification] {
        adoptActivationToken(notification);
        Q_EMIT chatRequested(callerIp);
    });

    auto *answer = notification->addAction(i18nc("@action:button pick up the call", "Answer"));
    connect(answer, &KNotificationAction::activated, this, [this, callerIp, notification] {
        adoptActivationToken(notification);
        Q_EMIT callAnswerRequested(callerIp);
    });
    auto *reject = notification->addAction(i18nc("@action:button turn the call down", "Decline"));
    connect(reject, &KNotificationAction::activated, this, [this, callerIp] {
        Q_EMIT callRejectRequested(callerIp);
    });

    notification->sendEvent();
}

void NotificationManager::clearChat(const QString &chatId)
{
    if (auto *notification = m_messageNotifications.value(chatId).data()) {
        notification->close();
    }
    m_messageNotifications.remove(chatId);
}

void NotificationManager::clearCall(const QString &callerIp)
{
    if (auto *notification = m_callNotifications.value(callerIp).data()) {
        notification->close();
    }
    m_callNotifications.remove(callerIp);
}

} // namespace koutnet
