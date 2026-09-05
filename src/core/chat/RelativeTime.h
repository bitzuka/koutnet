// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QTimer>

// Relative wall-clock strings for the chat surface. KFormat gives
// calendar-relative ("Today, 14:32") and spelled-out durations, neither of which
// is the elapsed shape wanted here, so the buckets below are written out as
// whole i18ncp sentences.
class RelativeTime : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // One timer for the whole window, so every label ages on one wakeup - a
    // Timer per row adds up in a contact list.
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

    // nowSecs is passed in rather than read from the clock so the result is a
    // binding on now() and updates without a message arriving.
    Q_INVOKABLE QString presenceLabel(bool online, double lastSeenSecs, qint64 nowSecs) const;

    Q_INVOKABLE QString chatStamp(double whenSecs, qint64 nowSecs) const;

    Q_INVOKABLE QString daySeparator(double whenSecs, qint64 nowSecs) const;

Q_SIGNALS:
    void nowChanged();

private:
    static constexpr int kTickMs = 15'000;

    qint64 m_now = 0;
    QTimer m_tick;
};
