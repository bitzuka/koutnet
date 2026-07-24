#include "UnreadManager.h"

void UnreadManager::increment(const QString &chatId)
{
    m_counts[chatId] = m_counts.value(chatId, 0) + 1;
    emit unreadChanged(chatId, m_counts[chatId]);
    emit totalChanged();
}

void UnreadManager::markRead(const QString &chatId)
{
    if (m_counts.value(chatId, 0) > 0) {
        m_counts[chatId] = 0;
        emit unreadChanged(chatId, 0);
        emit totalChanged();
    }
}

int UnreadManager::get(const QString &chatId) const
{
    return m_counts.value(chatId, 0);
}

int UnreadManager::total() const
{
    int sum = 0;
    for (int v : m_counts)
        sum += v;
    return sum;
}
