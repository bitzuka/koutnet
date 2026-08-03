// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#pragma once

#include <QDir>
#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Chat history on disk as JSON, one file per chat_id, with an in-memory
// cache. Exposed to QML as a singleton.
//
// TODO: historySavingEnabled is local to this class. Hook it up to
// AppSettings now that the module exists.
class HistoryManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool historySavingEnabled READ historySavingEnabled WRITE setHistorySavingEnabled NOTIFY historySavingEnabledChanged)

public:
    explicit HistoryManager(QObject *parent = nullptr);

    static HistoryManager *create(QQmlEngine *, QJSEngine *)
    {
        return new HistoryManager;
    }

    bool historySavingEnabled() const
    {
        return m_savingEnabled;
    }
    void setHistorySavingEnabled(bool enabled);

    Q_INVOKABLE QVariantList load(const QString &chatId);
    Q_INVOKABLE void append(const QString &chatId, const QVariantMap &entry);
    // Rewrites a chat's whole log. append() is the normal way in; this is for
    // the changes that alter entries already stored rather than add one - a
    // read receipt marking earlier messages, for instance. Same rules as
    // append(): obeys historySavingEnabled, keeps the newest
    // kMaxMessagesPerChat, and never writes over a file that would not parse.
    Q_INVOKABLE void replaceAll(const QString &chatId, const QVariantList &entries);

    Q_INVOKABLE QVariantList loadCallLog();
    Q_INVOKABLE void addCall(const QVariantMap &entry);

Q_SIGNALS:
    void historySavingEnabledChanged();
    void historyAppended(const QString &chatId, const QVariantMap &entry);

private:
    static constexpr int kMaxMessagesPerChat = 1000;

    QString filePathFor(const QString &chatId) const;
    QDir historyDir() const;
    // The shared write step behind append() and replaceAll().
    void writeChatFile(const QString &chatId, const QVariantList &msgs);

    bool m_savingEnabled = true;
    QHash<QString, QVariantList> m_cache;
    // chats whose file exists but will not parse, so appends stay in memory
    // only instead of replacing a damaged log with a fresh one
    QSet<QString> m_unreadable;
};
