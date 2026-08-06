// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "ChatListModel.h"
#include "ChatAddress.h"
#include "HistoryManager.h"
#include "UnreadManager.h"

#include <KLocalizedString>

#include <QVariantMap>

#include <algorithm>

namespace
{
QString unknownPeerName()
{
    return i18nc("@info a peer that has published no name of its own", "Unknown peer");
}

constexpr int kMaxPreviewChars = 120;

QString clampPreview(const QString &text)
{
    QString flat = text.simplified();
    if (flat.size() > kMaxPreviewChars)
        flat.truncate(kMaxPreviewChars);
    return flat;
}
} // namespace

ChatListModel::ChatListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_saveTimer.setInterval(400);
    m_saveTimer.setSingleShot(true);
    connect(&m_saveTimer, &QTimer::timeout, this, &ChatListModel::save);
}

bool ChatListModel::isTrackable(const QString &chatId)
{
    // Reserved ids belong to HistoryManager's own logs - the call log and this
    // model's index share that shape.
    return !chatId.isEmpty() && !chatId.startsWith(QLatin1String("__"));
}

int ChatListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

QVariant ChatListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Entry &e = m_rows.at(index.row());
    switch (role) {
    case ChatIdRole:
        return e.chatId;
    case DisplayNameRole:
        return e.displayName.isEmpty() ? unknownPeerName() : e.displayName;
    case AvatarLetterRole: {
        const QString name = e.displayName.isEmpty() ? unknownPeerName() : e.displayName;
        return name.isEmpty() ? QStringLiteral("?") : name.left(1).toUpper();
    }
    case PreviewRole:
        if (e.preview.isEmpty())
            return QString();
        // Composed here, not in the delegate, so the whole line is one string a
        // translator can reorder.
        return e.previewIsOwn ? i18nc("@info:status conversation list preview of a message this user sent, %1 is the message", "You: %1", e.preview) : e.preview;
    case StampSecsRole:
        return e.lastActivity;
    case LastSeenSecsRole:
        return e.lastSeen;
    case UnreadCountRole:
        return e.unread;
    case OnlineRole:
        return e.online;
    case TransportRole:
        return koutnet::chatid::transportName(e.chatId);
    default:
        return {};
    }
}

QHash<int, QByteArray> ChatListModel::roleNames() const
{
    return {
        {ChatIdRole, "chatId"},
        {DisplayNameRole, "displayName"},
        {AvatarLetterRole, "avatarLetter"},
        {PreviewRole, "preview"},
        {StampSecsRole, "stampSecs"},
        {LastSeenSecsRole, "lastSeenSecs"},
        {UnreadCountRole, "unreadCount"},
        {OnlineRole, "online"},
        {TransportRole, "transport"},
    };
}

QObject *ChatListModel::historyManagerObj() const
{
    return m_history;
}

void ChatListModel::setHistoryManagerObj(QObject *obj)
{
    auto *h = qobject_cast<HistoryManager *>(obj);
    if (m_history == h)
        return;
    m_history = h;
    Q_EMIT historyManagerChanged();
    load();
}

QObject *ChatListModel::unreadManagerObj() const
{
    return m_unread;
}

void ChatListModel::setUnreadManagerObj(QObject *obj)
{
    auto *u = qobject_cast<UnreadManager *>(obj);
    if (m_unread == u)
        return;
    if (m_unread)
        disconnect(m_unread, nullptr, this, nullptr);
    m_unread = u;
    if (m_unread) {
        connect(m_unread, &UnreadManager::unreadChanged, this, [this](const QString &chatId, int count) {
            const int row = indexOfChat(chatId);
            if (row < 0 || m_rows.at(row).unread == count)
                return;
            m_rows[row].unread = count;
            refreshRow(row, {UnreadCountRole});
            scheduleSave();
        });
    }
    Q_EMIT unreadManagerChanged();
    load();
}

int ChatListModel::indexOfChat(const QString &chatId) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).chatId == chatId)
            return i;
    }
    return -1;
}

bool ChatListModel::hasChat(const QString &chatId) const
{
    return indexOfChat(chatId) >= 0;
}

QVariantMap ChatListModel::chatInfo(const QString &chatId) const
{
    const int row = indexOfChat(chatId);
    if (row < 0)
        return {};

    const Entry &e = m_rows.at(row);
    QVariantMap m;
    m[QStringLiteral("chatId")] = e.chatId;
    m[QStringLiteral("displayName")] = e.displayName.isEmpty() ? unknownPeerName() : e.displayName;
    m[QStringLiteral("preview")] = e.preview;
    m[QStringLiteral("stampSecs")] = e.lastActivity;
    m[QStringLiteral("lastSeenSecs")] = e.lastSeen;
    m[QStringLiteral("unreadCount")] = e.unread;
    m[QStringLiteral("online")] = e.online;
    m[QStringLiteral("transport")] = koutnet::chatid::transportName(e.chatId);
    return m;
}

int ChatListModel::destinationFor(double activity, int excludeRow) const
{
    int idx = 0;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (i == excludeRow)
            continue;
        if (m_rows.at(i).lastActivity <= activity)
            break;
        ++idx;
    }
    return idx;
}

void ChatListModel::moveToSortedPosition(int row)
{
    const int target = destinationFor(m_rows.at(row).lastActivity, row);
    if (target == row)
        return;

    // beginMoveRows() wants the destination in the coordinates of the list as it
    // stands now, and taking the row out shifts everything after it.
    const int wireDestination = target > row ? target + 1 : target;
    if (!beginMoveRows(QModelIndex(), row, row, QModelIndex(), wireDestination))
        return;
    const Entry moved = m_rows.takeAt(row);
    m_rows.insert(target, moved);
    endMoveRows();
}

void ChatListModel::refreshRow(int row, const QVector<int> &roles)
{
    const QModelIndex idx = index(row);
    Q_EMIT dataChanged(idx, idx, roles);
}

void ChatListModel::openChat(const QString &chatId, const QString &displayName)
{
    if (!isTrackable(chatId))
        return;

    const int existing = indexOfChat(chatId);
    if (existing >= 0) {
        if (!displayName.isEmpty() && m_rows.at(existing).displayName != displayName) {
            m_rows[existing].displayName = displayName;
            refreshRow(existing, {DisplayNameRole, AvatarLetterRole});
            scheduleSave();
        }
        return;
    }

    Entry e;
    e.chatId = chatId;
    e.displayName = displayName;
    const int at = destinationFor(0.0, -1);
    beginInsertRows(QModelIndex(), at, at);
    m_rows.insert(at, e);
    endInsertRows();
    scheduleSave();
}

void ChatListModel::noteMessage(const QString &chatId, const QString &preview, bool isOwn, double ts)
{
    if (!isTrackable(chatId))
        return;

    const int row = indexOfChat(chatId);
    if (row < 0) {
        Entry e;
        e.chatId = chatId;
        e.preview = clampPreview(preview);
        e.previewIsOwn = isOwn;
        e.lastActivity = ts;
        if (m_unread)
            e.unread = m_unread->get(chatId);
        const int at = destinationFor(ts, -1);
        beginInsertRows(QModelIndex(), at, at);
        m_rows.insert(at, e);
        endInsertRows();
        scheduleSave();
        return;
    }

    m_rows[row].preview = clampPreview(preview);
    m_rows[row].previewIsOwn = isOwn;
    if (ts > m_rows.at(row).lastActivity)
        m_rows[row].lastActivity = ts;
    refreshRow(row, {PreviewRole, StampSecsRole});
    moveToSortedPosition(row);
    scheduleSave();
}

void ChatListModel::setPresence(const QString &chatId, bool online, double lastSeenSecs, const QString &displayName)
{
    const int row = indexOfChat(chatId);
    if (row < 0)
        return; // a peer with no conversation is not a row here, by design

    QVector<int> changed;
    bool wentOffline = false;
    if (m_rows.at(row).online != online) {
        wentOffline = !online;
        m_rows[row].online = online;
        changed.append(OnlineRole);
    }
    // Only ever forward: a presence packet that overtook an older one must not
    // make the peer look as though it had gone quiet in between.
    if (lastSeenSecs > m_rows.at(row).lastSeen) {
        m_rows[row].lastSeen = lastSeenSecs;
        changed.append(LastSeenSecsRole);
    }
    bool renamed = false;
    if (!displayName.isEmpty() && m_rows.at(row).displayName != displayName) {
        m_rows[row].displayName = displayName;
        renamed = true;
        changed.append(DisplayNameRole);
        changed.append(AvatarLetterRole);
    }
    if (changed.isEmpty())
        return;

    refreshRow(row, changed);

    // Not on every stamp: a live peer refreshes this every few seconds, which
    // would rewrite the whole index per packet for a number nothing reads until
    // the peer goes. Cost is a stamp lost if we are killed while it is still up.
    if (wentOffline || renamed)
        scheduleSave();
}

void ChatListModel::removeChat(const QString &chatId)
{
    const int row = indexOfChat(chatId);
    if (row < 0)
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_rows.remove(row);
    endRemoveRows();
    scheduleSave();
}

void ChatListModel::load()
{
    // Both managers are assigned as separate properties from QML, so this runs
    // twice.
    if (m_loaded || !m_history)
        return;
    m_loaded = true;

    const QVariantList raw = m_history->loadChatIndex();
    if (raw.isEmpty())
        return;

    QVector<Entry> loaded;
    loaded.reserve(raw.size());
    for (const QVariant &v : raw) {
        const QVariantMap m = v.toMap();
        Entry e;
        e.chatId = m.value(QStringLiteral("chat_id")).toString();
        if (!isTrackable(e.chatId))
            continue;
        e.displayName = m.value(QStringLiteral("display_name")).toString();
        e.preview = m.value(QStringLiteral("preview")).toString();
        e.previewIsOwn = m.value(QStringLiteral("preview_is_own")).toBool();
        e.lastActivity = m.value(QStringLiteral("last_activity")).toDouble();
        e.lastSeen = m.value(QStringLiteral("last_seen")).toDouble();
        e.unread = m.value(QStringLiteral("unread")).toInt();
        loaded.append(e);
    }
    std::stable_sort(loaded.begin(), loaded.end(), [](const Entry &a, const Entry &b) {
        return a.lastActivity > b.lastActivity;
    });

    beginResetModel();
    m_rows = loaded;
    endResetModel();

    // UnreadManager starts empty every run, so restored counts have to be put
    // back into it or the badge and the total would disagree.
    if (m_unread) {
        for (const Entry &e : m_rows) {
            if (e.unread > 0)
                m_unread->restore(e.chatId, e.unread);
        }
    }
}

void ChatListModel::scheduleSave()
{
    if (m_history)
        m_saveTimer.start();
}

void ChatListModel::save()
{
    if (!m_history)
        return;

    QVariantList out;
    out.reserve(m_rows.size());
    for (const Entry &e : m_rows) {
        QVariantMap m;
        m[QStringLiteral("chat_id")] = e.chatId;
        m[QStringLiteral("display_name")] = e.displayName;
        m[QStringLiteral("preview")] = e.preview;
        m[QStringLiteral("preview_is_own")] = e.previewIsOwn;
        m[QStringLiteral("last_activity")] = e.lastActivity;
        m[QStringLiteral("last_seen")] = e.lastSeen;
        m[QStringLiteral("unread")] = e.unread;
        out.append(m);
    }
    m_history->saveChatIndex(out);
}
