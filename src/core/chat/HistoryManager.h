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

// The toggle reads AppSettings in main.cpp: start value and change hook.
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
    Q_INVOKABLE void replaceAll(const QString &chatId, const QVariantList &entries);

    Q_INVOKABLE QVariantList loadCallLog();
    Q_INVOKABLE void addCall(const QVariantMap &entry);

    // The conversation list, stored beside the logs rather than derived from
    // them: a row carries what no message log holds - a switched-off peer's
    // name and last-seen.
    Q_INVOKABLE QVariantList loadChatIndex();
    Q_INVOKABLE void saveChatIndex(const QVariantList &entries);

    // The file naming scheme. Split out and public because it is the one part of
    // this class with a rule in it, and the rule can be checked without a
    // filesystem.
    //
    // legacyStemFor() is what every build up to now wrote: every character
    // outside [\w-] replaced by an underscore. That is not injective, and it was
    // not injective before Matrix either - "192.168.1.5" and the literal id
    // "192_168_1_5" are one file, as are "fe80::1" and "fe80..1". The Matrix
    // room ids "!a:b.c" and "!a.b:c" would have joined them. stemFor() appends a
    // digest of the id it really came from whenever a substitution happened, and
    // returns the id untouched when none did, so ids that were already safe as
    // file names keep the names they have.
    static QString stemFor(const QString &chatId);
    static QString legacyStemFor(const QString &chatId);

Q_SIGNALS:
    void historySavingEnabledChanged();
    void historyAppended(const QString &chatId, const QVariantMap &entry);

private:
    static constexpr int kMaxMessagesPerChat = 1000;

    // Not const: the first call for a chat moves a log written under the old
    // name to the new one.
    QString filePathFor(const QString &chatId);
    QDir historyDir() const;
    void writeChatFile(const QString &chatId, const QVariantList &msgs);

    bool m_savingEnabled = true;
    QHash<QString, QVariantList> m_cache;
    // chats whose file exists but will not parse, so appends stay in memory only
    QSet<QString> m_unreadable;
    // chats whose legacy file name has already been looked for, so the rename
    // costs two stat() calls once rather than one per appended message
    QSet<QString> m_namesChecked;
};
