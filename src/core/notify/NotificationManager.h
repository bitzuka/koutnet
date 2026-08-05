// SPDX-FileCopyrightText: 2020 Tobias Fella <tobias.fella@kde.org>
// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Adapted from NeoChat's src/app/notificationsmanager.h/.cpp.
//
// What is taken from upstream is the recipe rather than the plumbing: one
// KNotification per conversation, closed and replaced when the next message in
// that conversation arrives so a chatter cannot stack thirty popups; the
// default action that raises the window on the right chat, carrying the xdg
// activation token so the compositor actually allows the raise; a
// KNotificationReplyAction so a reply can be typed without opening the window;
// and the applicationState check that decides whether to post at all.
//
// Everything Matrix is gone: the GetNotificationsJob polling, the server-side
// push rules, the invite notifications and the avatar compositing all describe
// a homeserver, and KOutNet hears about a message directly from
// NetworkManager. There is nothing to poll, so there is no job here.
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

class KNotification;

namespace koutnet
{

// Desktop notifications for messages and calls, through KNotification and the
// installed koutnet.notifyrc, so the user configures them where they configure
// every other application's - System Settings, not a dialog of this one's.
class NotificationManager : public QObject
{
    Q_OBJECT

public:
    explicit NotificationManager(QObject *parent = nullptr);

    // chatId is what the notification is keyed on, so a second message in the
    // same conversation replaces the first rather than adding to the pile.
    // Silently does nothing while the window has focus.
    Q_INVOKABLE void notifyMessage(const QString &chatId, const QString &sender, const QString &text);
    // Calls always post: a ringing telephone is the case where the user is
    // looking at something else, and the window having focus does not mean the
    // person in front of it has noticed.
    Q_INVOKABLE void notifyCall(const QString &callerName, const QString &callerIp);
    // The conversation was opened, so its notification has been answered.
    Q_INVOKABLE void clearChat(const QString &chatId);
    Q_INVOKABLE void clearCall(const QString &callerIp);

Q_SIGNALS:
    // The user clicked the notification body. The window handles raising itself
    // and opening the conversation.
    void chatRequested(const QString &chatId);
    // Typed into the notification's inline reply field.
    void replyRequested(const QString &chatId, const QString &text);
    void callAnswerRequested(const QString &callerIp);
    void callRejectRequested(const QString &callerIp);

private:
    // Whether a popup would be telling the user something they can already see.
    static bool windowHasFocus();
    // Every KNotification this class makes goes through here, because the
    // component name has to be set by hand - see the note in the .cpp.
    KNotification *makeNotification(const QString &eventId) const;
    // Hands the compositor's activation token to Qt so the window is allowed to
    // raise itself. Has to happen here rather than in QML: the only way through
    // is an environment variable Qt reads on the next activation.
    static void adoptActivationToken(KNotification *notification);

    QHash<QString, QPointer<KNotification>> m_messageNotifications;
    QHash<QString, QPointer<KNotification>> m_callNotifications;
};

} // namespace koutnet
