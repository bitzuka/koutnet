// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - status notifier item
//
// KStatusNotifierItem rather than a QSystemTrayIcon or anything hand-rolled: it
// speaks the StatusNotifierItem D-Bus protocol, which is what Plasma's tray
// actually consumes, and it gets the overlay icon, the attention state and the
// exported menu for free.
//
// The menu is a QMenu, because that is the only thing setContextMenu() takes.
// That is why main.cpp builds a QApplication and not a QGuiApplication - see the
// note there; a QMenu without one is a warning followed by a crash.
//
// State flows one way in each direction. QML writes the properties below,
// because the window is what knows whether the microphone is muted and how many
// messages are unread; this class only draws them. Clicking a menu entry comes
// back as one of the signals, and QML decides what that means. Nothing here
// reaches into the network or audio layers.
#pragma once

#include <QObject>
#include <QString>

class KStatusNotifierItem;
class QAction;
class QActionGroup;
class QWindow;

namespace koutnet
{

class TrayIcon : public QObject
{
    Q_OBJECT

    // Unread messages across every conversation. Non-zero puts the item into
    // NeedsAttention and hangs an overlay on the icon, which is the whole reason
    // a tray icon is worth having on a messenger.
    Q_PROPERTY(int unreadCount READ unreadCount WRITE setUnreadCount NOTIFY unreadCountChanged)
    Q_PROPERTY(bool micMuted READ micMuted WRITE setMicMuted NOTIFY micMutedChanged)
    Q_PROPERTY(bool deafened READ deafened WRITE setDeafened NOTIFY deafenedChanged)
    // AppSettings::presence: 0 online, 1 away, 2 busy, 3 invisible.
    Q_PROPERTY(int presence READ presence WRITE setPresence NOTIFY presenceChanged)
    // Whether the window is up, so the one entry can say Show or Hide rather
    // than being a toggle whose label never admits which way it goes.
    Q_PROPERTY(bool windowVisible READ windowVisible WRITE setWindowVisible NOTIFY windowVisibleChanged)

public:
    explicit TrayIcon(QObject *parent = nullptr);
    ~TrayIcon() override;

    int unreadCount() const
    {
        return m_unreadCount;
    }
    void setUnreadCount(int count);

    bool micMuted() const
    {
        return m_micMuted;
    }
    void setMicMuted(bool muted);

    bool deafened() const
    {
        return m_deafened;
    }
    void setDeafened(bool deafened);

    int presence() const
    {
        return m_presence;
    }
    void setPresence(int presence);

    bool windowVisible() const
    {
        return m_windowVisible;
    }
    void setWindowVisible(bool visible);

    // Hands the item the window it belongs to, so a click on the icon raises or
    // hides it without a round trip through QML. Takes a QWindow, which is what
    // a QML Window already is, so Main.qml can pass itself.
    Q_INVOKABLE void attachWindow(QWindow *window);

Q_SIGNALS:
    void unreadCountChanged();
    void micMutedChanged();
    void deafenedChanged();
    void presenceChanged();
    void windowVisibleChanged();

    // The menu was used. The window owns the microphone and the page stack, so
    // all this class does is say which entry was picked.
    void showHideRequested();
    void muteToggleRequested();
    void deafenToggleRequested();
    void presenceRequested(int presence);
    void quitRequested();

private:
    // Icon, overlay, attention state and tooltip, all of which are functions of
    // the properties above. One place so they cannot drift apart.
    void refresh();
    // Rebuilds only the labels that carry state, which is the mute and deafen
    // pair and the Show/Hide entry.
    void refreshMenuLabels();

    KStatusNotifierItem *m_item = nullptr;
    QAction *m_showHideAction = nullptr;
    QAction *m_muteAction = nullptr;
    QAction *m_deafenAction = nullptr;
    QActionGroup *m_presenceGroup = nullptr;

    int m_unreadCount = 0;
    bool m_micMuted = false;
    bool m_deafened = false;
    int m_presence = 0;
    bool m_windowVisible = true;
};

} // namespace koutnet
