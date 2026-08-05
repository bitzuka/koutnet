// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "TrayIcon.h"

#include <QAction>
#include <QActionGroup>
#include <QIcon>
#include <QMenu>
#include <QWindow>

#include <KLocalizedString>
#include <KStatusNotifierItem>

namespace koutnet
{

namespace
{
// The four presences, in the order AppSettings::presence numbers them. The icon
// names are breeze status icons; the strings are what the menu shows.
struct PresenceEntry {
    int value;
    const char *iconName;
};
constexpr PresenceEntry kPresences[] = {
    {0, "user-online"},
    {1, "user-away"},
    {2, "user-busy"},
    {3, "user-invisible"},
};

QString presenceLabel(int presence)
{
    switch (presence) {
    case 1:
        return i18nc("@item:inmenu presence, the user is not at the machine", "Away");
    case 2:
        return i18nc("@item:inmenu presence, the user does not want to be disturbed", "Busy");
    case 3:
        return i18nc("@item:inmenu presence, appear offline to peers", "Invisible");
    default:
        return i18nc("@item:inmenu presence, reachable and saying so", "Online");
    }
}
} // namespace

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
{
    // The id is what the tray keys its own per-item settings on, so it matches
    // the KAboutData component name like the notifyrc does.
    m_item = new KStatusNotifierItem(QStringLiteral("koutnet"), this);
    m_item->setTitle(i18nc("@title the application, as the system tray names it", "KOutNet"));
    // Communications rather than ApplicationStatus: it puts the item with the
    // other messengers in trays that group by category.
    m_item->setCategory(KStatusNotifierItem::Communications);
    // The standard set is Quit plus a Minimize/Restore pair, and both of those
    // are already below with labels that say what they do.
    m_item->setStandardActionsEnabled(false);

    auto *menu = new QMenu();

    m_showHideAction = menu->addAction(QString());
    connect(m_showHideAction, &QAction::triggered, this, &TrayIcon::showHideRequested);

    menu->addSeparator();

    // Not checkable, for the same reason the drawer entries are not: a checkable
    // action writes its own checked state when triggered, which is then no
    // longer whatever the window thinks. The label carries the state instead.
    m_muteAction = menu->addAction(QString());
    connect(m_muteAction, &QAction::triggered, this, &TrayIcon::muteToggleRequested);

    m_deafenAction = menu->addAction(QString());
    connect(m_deafenAction, &QAction::triggered, this, &TrayIcon::deafenToggleRequested);

    menu->addSeparator();

    // Presence is one of four rather than a toggle, so here a check mark is the
    // right control - and an exclusive group is what stops two being ticked.
    auto *presenceMenu = menu->addMenu(i18nc("@title:menu what to tell peers you are", "Presence"));
    presenceMenu->setIcon(QIcon::fromTheme(QStringLiteral("user-online")));
    m_presenceGroup = new QActionGroup(this);
    m_presenceGroup->setExclusive(true);
    for (const auto &entry : kPresences) {
        auto *action = presenceMenu->addAction(QIcon::fromTheme(QString::fromLatin1(entry.iconName)), presenceLabel(entry.value));
        action->setCheckable(true);
        action->setData(entry.value);
        m_presenceGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, entry] {
            Q_EMIT presenceRequested(entry.value);
        });
    }

    menu->addSeparator();

    auto *quitAction = menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), i18nc("@action:inmenu", "Quit"));
    connect(quitAction, &QAction::triggered, this, &TrayIcon::quitRequested);

    // Takes ownership of the menu, which is why it is not parented above.
    m_item->setContextMenu(menu);

    refresh();
    refreshMenuLabels();
}

TrayIcon::~TrayIcon() = default;

void TrayIcon::attachWindow(QWindow *window)
{
    if (m_item)
        m_item->setAssociatedWindow(window);
}

void TrayIcon::setUnreadCount(int count)
{
    const int clamped = count < 0 ? 0 : count;
    if (m_unreadCount == clamped)
        return;
    m_unreadCount = clamped;
    refresh();
    Q_EMIT unreadCountChanged();
}

void TrayIcon::setMicMuted(bool muted)
{
    if (m_micMuted == muted)
        return;
    m_micMuted = muted;
    refreshMenuLabels();
    Q_EMIT micMutedChanged();
}

void TrayIcon::setDeafened(bool deafened)
{
    if (m_deafened == deafened)
        return;
    m_deafened = deafened;
    refreshMenuLabels();
    Q_EMIT deafenedChanged();
}

void TrayIcon::setPresence(int presence)
{
    if (m_presence == presence)
        return;
    m_presence = presence;
    refresh();
    refreshMenuLabels();
    Q_EMIT presenceChanged();
}

void TrayIcon::setWindowVisible(bool visible)
{
    if (m_windowVisible == visible)
        return;
    m_windowVisible = visible;
    refreshMenuLabels();
    Q_EMIT windowVisibleChanged();
}

void TrayIcon::refresh()
{
    if (!m_item)
        return;

    m_item->setIconByName(QStringLiteral("io.github.bitzuka.KOutNet"));

    // NeedsAttention is what makes a tray that hides passive items show this
    // one, which is exactly the case where there is something to read.
    if (m_unreadCount > 0) {
        m_item->setStatus(KStatusNotifierItem::NeedsAttention);
        m_item->setOverlayIconByName(QStringLiteral("mail-unread"));
        m_item->setToolTip(QStringLiteral("io.github.bitzuka.KOutNet"),
                           i18nc("@info:tooltip the system tray icon", "KOutNet"),
                           i18ncp("@info:tooltip %1 is a number of unread messages", "%1 unread message", "%1 unread messages", m_unreadCount));
        return;
    }

    m_item->setStatus(KStatusNotifierItem::Active);
    // Cleared rather than left behind: an overlay for a count of zero is a badge
    // saying there is something to read when there is not.
    m_item->setOverlayIconByName(QString());
    m_item->setToolTip(QStringLiteral("io.github.bitzuka.KOutNet"), i18nc("@info:tooltip the system tray icon", "KOutNet"), presenceLabel(m_presence));
}

void TrayIcon::refreshMenuLabels()
{
    if (m_showHideAction) {
        m_showHideAction->setText(m_windowVisible ? i18nc("@action:inmenu put the window away without quitting", "Hide the window")
                                                  : i18nc("@action:inmenu bring the window back from the tray", "Show the window"));
        m_showHideAction->setIcon(QIcon::fromTheme(m_windowVisible ? QStringLiteral("window-minimize") : QStringLiteral("window")));
    }

    if (m_muteAction) {
        m_muteAction->setText(m_micMuted ? i18nc("@action:inmenu let your microphone be heard again", "Unmute microphone")
                                         : i18nc("@action:inmenu silence your own microphone", "Mute microphone"));
        m_muteAction->setIcon(
            QIcon::fromTheme((m_micMuted || m_deafened) ? QStringLiteral("microphone-sensitivity-muted") : QStringLiteral("audio-input-microphone")));
        // Deafen already holds the microphone down, so offering to unmute it
        // while it cannot be heard either way would be a control that does
        // nothing. Same rule as the drawer entry.
        m_muteAction->setEnabled(!m_deafened);
    }

    if (m_deafenAction) {
        m_deafenAction->setText(m_deafened ? i18nc("@action:inmenu start hearing calls again", "Undeafen")
                                           : i18nc("@action:inmenu stop hearing calls, and stop being heard", "Deafen"));
        m_deafenAction->setIcon(QIcon::fromTheme(m_deafened ? QStringLiteral("audio-volume-muted") : QStringLiteral("audio-volume-high")));
    }

    if (m_presenceGroup) {
        const auto actions = m_presenceGroup->actions();
        for (QAction *action : actions)
            action->setChecked(action->data().toInt() == m_presence);
    }
}

} // namespace koutnet
