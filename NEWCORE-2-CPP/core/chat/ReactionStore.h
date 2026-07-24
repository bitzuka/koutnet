#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QHash>
#include <QVariantMap>
#include <QStringList>
#include <QTimer>

// Emoji reactions per message. Key = chat_id + "|||" + ts (3 decimal places).
// Value = { emoji: [usernames] }.
// Saves are debounced via a real QTimer (fixes the legacy Python version,
// which fired an un-cancellable singleShot(500) on every toggle instead of
// resetting a single timer).
class ReactionStore : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit ReactionStore(QObject *parent = nullptr);

    static ReactionStore *create(QQmlEngine *, QJSEngine *)
    {
        return new ReactionStore;
    }

    Q_INVOKABLE void add(const QString &chatId, double ts, const QString &emoji, const QString &username);
    Q_INVOKABLE void remove(const QString &chatId, double ts, const QString &emoji, const QString &username);
    // Returns true if the reaction was added, false if it was removed.
    Q_INVOKABLE bool toggle(const QString &chatId, double ts, const QString &emoji, const QString &username);
    Q_INVOKABLE QVariantMap get(const QString &chatId, double ts) const;
    // Returns list of {emoji, count} maps, sorted by count descending.
    Q_INVOKABLE QVariantList summary(const QString &chatId, double ts) const;

    void save();
    void load();

signals:
    void reactionsChanged(const QString &chatId, double ts);

private:
    static QString makeKey(const QString &chatId, double ts);

    QHash<QString, QHash<QString, QStringList>> m_data; // key -> emoji -> usernames
    QTimer m_saveTimer;
};
