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
    //
    // Always posts something now. Which event it posts is the point: the three
    // notifyrc events differ only in their Action= line, so what the user gets -
    // a popup, a sound, or both - follows where their attention is. See
    // eventForAttention() in the .cpp.
    Q_INVOKABLE void notifyMessage(const QString &chatId, const QString &sender, const QString &text);

    // Minutes of no input after which the user counts as away. Fed from
    // AppSettings; re-arms the KIdleTime timeout when it changes.
    Q_INVOKABLE void setAwayAfterMinutes(int minutes);
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
    // Where the user's attention is, which is the only thing that decides
    // between the three message events.
    enum class Attention {
        // Looking at the window. A popup would be telling them what is already
        // on the screen, so this one is a sound and nothing else.
        Watching,
        // The window is behind something, or minimised. A popup, silently: they
        // are at the machine, so the screen is enough.
        Elsewhere,
        // No input for a while. Both, because neither on its own is going to be
        // noticed by somebody who is not there.
        Away,
    };

    Attention attention() const;
    // The notifyrc event id for a state. The Action= line in
    // packaging/koutnet.notifyrc is what turns it into a popup, a sound or both,
    // which also means the user can overrule any of it in System Settings.
    static QString eventForAttention(Attention state);

    // KIdleTime, if the platform has a backend for it. Wayland needs the
    // compositor to speak ext-idle-notify-v1; without that the idle timeout
    // never fires and Away simply never happens, which degrades to the
    // two-state behaviour rather than to a broken one.
    //
    // Two functions rather than one because the timeout is re-registered
    // whenever the setting changes and the connections must not be. They are
    // made with lambdas, and Qt::UniqueConnection does not apply to those - a
    // single setup function called twice would quietly connect twice.
    void connectIdleWatch();
    void rearmIdleTimeout();

    // Whether a popup would be telling the user something they can already see.
    static bool windowHasFocus();
    // Every KNotification this class makes goes through here, because the
    // component name has to be set by hand - see the note in the .cpp.
    KNotification *makeNotification(const QString &eventId) const;
    // Hands the compositor's activation token to Qt so the window is allowed to
    // raise itself. Has to happen here rather than in QML: the only way through
    // is an environment variable Qt reads on the next activation.
    static void adoptActivationToken(KNotification *notification);

    // Only the events that actually put a popup up are tracked. There is
    // nothing to replace for a sound that has already played, and keeping those
    // here would have the next message closing a notification that no longer
    // exists.
    QHash<QString, QPointer<KNotification>> m_messageNotifications;
    QHash<QString, QPointer<KNotification>> m_callNotifications;

    int m_awayAfterMinutes = 5;
    // The KIdleTime identifier for the timeout above, so changing the setting
    // can take the old one back out. -1 is "never registered one".
    int m_idleTimeoutId = -1;
    bool m_userIdle = false;
};

} // namespace koutnet
