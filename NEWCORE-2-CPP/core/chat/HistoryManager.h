#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QDir>

// Persists chat history to disk as JSON, one file per chat_id, with an
// in-memory cache. Exposed to QML as a singleton.
//
// NOTE: legacy Python gated saving behind S().save_history (AppSettings).
// This class exposes its own historySavingEnabled property instead of
// hard-depending on AppSettings' exact API — wire setHistorySavingEnabled()
// up to the real AppSettings signal once that property name is confirmed.
class HistoryManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool historySavingEnabled READ historySavingEnabled
               WRITE setHistorySavingEnabled NOTIFY historySavingEnabledChanged)

public:
    explicit HistoryManager(QObject *parent = nullptr);

    static HistoryManager *create(QQmlEngine *, QJSEngine *)
    {
        return new HistoryManager;
    }

    bool historySavingEnabled() const { return m_savingEnabled; }
    void setHistorySavingEnabled(bool enabled);

    Q_INVOKABLE QVariantList load(const QString &chatId);
    Q_INVOKABLE void append(const QString &chatId, const QVariantMap &entry);

    Q_INVOKABLE QVariantList loadCallLog();
    Q_INVOKABLE void addCall(const QVariantMap &entry);

signals:
    void historySavingEnabledChanged();
    void historyAppended(const QString &chatId, const QVariantMap &entry);

private:
    static constexpr int kMaxMessagesPerChat = 1000;

    QString filePathFor(const QString &chatId) const;
    QDir historyDir() const;

    bool m_savingEnabled = true;
    QHash<QString, QVariantList> m_cache;
};
