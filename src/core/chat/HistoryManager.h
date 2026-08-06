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
    Q_INVOKABLE void replaceAll(const QString &chatId, const QVariantList &entries);

    Q_INVOKABLE QVariantList loadCallLog();
    Q_INVOKABLE void addCall(const QVariantMap &entry);

    // The conversation list, stored beside the logs rather than derived from
    // them: filePathFor() replaces every character outside [\w-], so
    // "192.168.1.5" and "192-168-1-5" both land on 192_168_1_5.json and neither
    // reads back. A row also carries what no message log holds - a switched-off
    // peer's name and last-seen.
    Q_INVOKABLE QVariantList loadChatIndex();
    Q_INVOKABLE void saveChatIndex(const QVariantList &entries);

Q_SIGNALS:
    void historySavingEnabledChanged();
    void historyAppended(const QString &chatId, const QVariantMap &entry);

private:
    static constexpr int kMaxMessagesPerChat = 1000;

    QString filePathFor(const QString &chatId) const;
    QDir historyDir() const;
    void writeChatFile(const QString &chatId, const QVariantList &msgs);

    bool m_savingEnabled = true;
    QHash<QString, QVariantList> m_cache;
    // chats whose file exists but will not parse, so appends stay in memory only
    QSet<QString> m_unreadable;
};
