// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QHash>

// Tracks unread message counts per chat_id.
// chat_id is either an IP (private chat), "public", or "group_<gid>".
class UnreadManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int total READ total NOTIFY totalChanged)

public:
    explicit UnreadManager(QObject *parent = nullptr) : QObject(parent) {}

    static UnreadManager *create(QQmlEngine *, QJSEngine *)
    {
        return new UnreadManager;
    }

    Q_INVOKABLE void increment(const QString &chatId);
    Q_INVOKABLE void markRead(const QString &chatId);
    Q_INVOKABLE int get(const QString &chatId) const;
    int total() const;

Q_SIGNALS:
    void unreadChanged(const QString &chatId, int count);
    void totalChanged();

private:
    QHash<QString, int> m_counts;
};
