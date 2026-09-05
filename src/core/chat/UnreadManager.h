// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#pragma once

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QString>

class UnreadManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int total READ total NOTIFY totalChanged)

public:
    explicit UnreadManager(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    static UnreadManager *create(QQmlEngine *, QJSEngine *)
    {
        return new UnreadManager;
    }

    Q_INVOKABLE void increment(const QString &chatId);
    Q_INVOKABLE void markRead(const QString &chatId);
    Q_INVOKABLE int get(const QString &chatId) const;
    // Separate from increment() because no message arrived: it must not notify,
    // nor stack on this run's count.
    Q_INVOKABLE void restore(const QString &chatId, int count);
    int total() const;

Q_SIGNALS:
    void unreadChanged(const QString &chatId, int count);
    void totalChanged();

private:
    QHash<QString, int> m_counts;
};
