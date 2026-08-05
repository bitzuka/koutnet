// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QTimer>

// Relative wall-clock strings for the chat surface: how long ago a peer was
// last reachable, and the stamp on a conversation row.
//
// KCoreAddons was the first place looked. KFormat::formatRelativeDateTime() is
// calendar-relative ("Today, 14:32", "Yesterday, 14:32", then an absolute date)
// and formatSpelloutDuration() spells a length of time ("1 hour 10 minutes")
// with no sense of it being in the past - neither produces the elapsed shape
// wanted here ("2 minutes ago", "a week ago"). So the buckets below are written
// out, every one of them a whole i18ncp sentence rather than a number glued to a
// unit, and formatRelativeDate() is still used for the one job it does fit: the
// date on a conversation row older than today.
class RelativeTime : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // One timer for the whole window. Every label is a binding that reads this,
    // so they all age together on one wakeup - a Timer per row was the other
    // way, and a contact list is exactly where that adds up.
    Q_PROPERTY(qint64 now READ now NOTIFY nowChanged)

public:
    explicit RelativeTime(QObject *parent = nullptr);

    static RelativeTime *create(QQmlEngine *, QJSEngine *)
    {
        return new RelativeTime;
    }

    qint64 now() const
    {
        return m_now;
    }

    // The whole status line for a peer: "online" when it is reachable, and
    // otherwise how long ago it stopped being. nowSecs is passed in rather than
    // read from the clock so the result is a binding on now() and updates
    // without a message arriving.
    Q_INVOKABLE QString presenceLabel(bool online, double lastSeenSecs, qint64 nowSecs) const;

    // The time against a conversation row: clock time today, then day or date.
    Q_INVOKABLE QString chatStamp(double whenSecs, qint64 nowSecs) const;

Q_SIGNALS:
    void nowChanged();

private:
    // Short enough that "just now" turns into "a minute ago" without a visible
    // lag, long enough to be free. Nothing here needs second resolution.
    static constexpr int kTickMs = 15'000;

    qint64 m_now = 0;
    QTimer m_tick;
};
