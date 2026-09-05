// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// setContextMenu() only takes a QMenu, which is why main.cpp builds a
// QApplication and not a QGuiApplication - a QMenu without one is a warning
// followed by a crash.
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

    // Non-zero puts the item into NeedsAttention and hangs an overlay on the
    // icon.
    Q_PROPERTY(int unreadCount READ unreadCount WRITE setUnreadCount NOTIFY unreadCountChanged)
    Q_PROPERTY(bool micMuted READ micMuted WRITE setMicMuted NOTIFY micMutedChanged)
    Q_PROPERTY(bool deafened READ deafened WRITE setDeafened NOTIFY deafenedChanged)
    // AppSettings::presence: 0 online, 1 away, 2 busy, 3 invisible.
    Q_PROPERTY(int presence READ presence WRITE setPresence NOTIFY presenceChanged)
    // So the one entry can say Show or Hide rather than being an unlabelled
    // toggle.
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
    // hides it without a round trip through QML.
    Q_INVOKABLE void attachWindow(QWindow *window);

Q_SIGNALS:
    void unreadCountChanged();
    void micMutedChanged();
    void deafenedChanged();
    void presenceChanged();
    void windowVisibleChanged();

    void showHideRequested();
    void muteToggleRequested();
    void deafenToggleRequested();
    void presenceRequested(int presence);
    void quitRequested();

private:
    void refresh();
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
