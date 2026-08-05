// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "RelativeTime.h"

#include <KFormat>
#include <KLocalizedString>

#include <QDateTime>
#include <QLocale>

RelativeTime::RelativeTime(QObject *parent)
    : QObject(parent)
    , m_now(QDateTime::currentSecsSinceEpoch())
{
    m_tick.setInterval(kTickMs);
    m_tick.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        const qint64 secs = QDateTime::currentSecsSinceEpoch();
        if (secs == m_now)
            return;
        m_now = secs;
        Q_EMIT nowChanged();
    });
    m_tick.start();
}

QString RelativeTime::presenceLabel(bool online, double lastSeenSecs, qint64 nowSecs) const
{
    if (online)
        return i18nc("@info:status the peer is reachable right now", "online");

    const auto stamp = static_cast<qint64>(lastSeenSecs);
    if (stamp <= 0)
        return i18nc("@info:status the peer has never been seen on the network", "offline");

    // A clock that went backwards, or a peer whose presence is a second into the
    // future. "just now" is the honest answer either way.
    const qint64 elapsed = qMax<qint64>(0, nowSecs - stamp);

    if (elapsed < 45)
        return i18nc("@info:status the peer was reachable a moment ago", "last seen just now");

    const qint64 minutes = elapsed / 60;
    if (minutes < 60) {
        return i18ncp("@info:status how long ago a peer was last reachable, %1 is a number of minutes",
                      "last seen a minute ago",
                      "last seen %1 minutes ago",
                      int(minutes));
    }

    // Calendar days rather than 86400-second blocks, or a peer last seen at
    // 23:50 is "9 hours ago" at 09:00 instead of "yesterday", which is not how
    // anybody reads a clock.
    const QDate seenOn = QDateTime::fromSecsSinceEpoch(stamp).date();
    const QDate today = QDateTime::fromSecsSinceEpoch(nowSecs).date();
    const qint64 hours = elapsed / 3600;
    if (seenOn == today) {
        return i18ncp("@info:status how long ago a peer was last reachable, %1 is a number of hours",
                      "last seen an hour ago",
                      "last seen %1 hours ago",
                      int(hours));
    }

    const qint64 days = seenOn.daysTo(today);
    if (days <= 1)
        return i18nc("@info:status the peer was last reachable on the previous day", "last seen yesterday");
    if (days < 7) {
        return i18ncp("@info:status how long ago a peer was last reachable, %1 is a number of days",
                      "last seen a day ago",
                      "last seen %1 days ago",
                      int(days));
    }
    if (days < 31) {
        return i18ncp("@info:status how long ago a peer was last reachable, %1 is a number of weeks",
                      "last seen a week ago",
                      "last seen %1 weeks ago",
                      int(days / 7));
    }
    if (days < 365) {
        return i18ncp("@info:status how long ago a peer was last reachable, %1 is a number of months",
                      "last seen a month ago",
                      "last seen %1 months ago",
                      int(days / 30));
    }
    return i18ncp("@info:status how long ago a peer was last reachable, %1 is a number of years",
                  "last seen a year ago",
                  "last seen %1 years ago",
                  int(days / 365));
}

QString RelativeTime::chatStamp(double whenSecs, qint64 nowSecs) const
{
    const auto stamp = static_cast<qint64>(whenSecs);
    if (stamp <= 0)
        return {};

    const QDateTime when = QDateTime::fromSecsSinceEpoch(stamp);
    const QDate today = QDateTime::fromSecsSinceEpoch(nowSecs).date();
    // Today is the common case and wants the clock, which is the one shape
    // KFormat does not give: formatRelativeDate() answers "Today" for it.
    if (when.date() == today)
        return QLocale().toString(when.time(), QLocale::ShortFormat);

    // Everything older is a day name or a date, which is exactly what
    // formatRelativeDate() is for - and it knows the calendar of the current
    // locale, which hand-written code here would not.
    return KFormat().formatRelativeDate(when.date(), QLocale::ShortFormat);
}

QString RelativeTime::daySeparator(double whenSecs, qint64 nowSecs) const
{
    const auto stamp = static_cast<qint64>(whenSecs);
    if (stamp <= 0)
        return {};

    // nowSecs is taken and not read. The argument is what makes a caller's
    // binding depend on now(), so the newest chip rewrites itself from "Today"
    // to "Yesterday" at midnight with the window left open.
    Q_UNUSED(nowSecs)
    return KFormat().formatRelativeDate(QDateTime::fromSecsSinceEpoch(stamp).date(), QLocale::LongFormat);
}
