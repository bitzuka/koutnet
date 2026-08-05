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

    // The conversation list, stored beside the logs rather than derived from
    // them. Deriving it is not possible: filePathFor() replaces every character
    // outside [\w-] to build a filename, so "192.168.1.5" and "192-168-1-5" both
    // land on 192_168_1_5.json and neither can be read back out of it. A row also
    // has to carry things no message log holds - the name and last-seen stamp of
    // a peer that is currently switched off, and a chat the user has opened but
    // not written in yet.
    //
    // Same reserved-id trick as the call log above, so this inherits the atomic
    // write, the cache and the refusal to overwrite a file that would not parse,
    // and introduces no new storage of its own.
    Q_INVOKABLE QVariantList loadChatIndex();
    Q_INVOKABLE void saveChatIndex(const QVariantList &entries);

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
