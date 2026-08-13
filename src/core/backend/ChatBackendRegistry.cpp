// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "core/backend/ChatBackendRegistry.h"

namespace koutnet
{

ChatBackendRegistry::ChatBackendRegistry(QObject *parent)
    : QObject(parent)
{
}

void ChatBackendRegistry::registerBackend(ChatBackend *backend)
{
    if (backend && !m_backends.contains(backend))
        m_backends.append(backend);
}

ChatBackend *ChatBackendRegistry::backendFor(const QString &chatId) const
{
    for (ChatBackend *backend : m_backends) {
        if (backend->canHandle(chatId))
            return backend;
    }
    return nullptr;
}

bool ChatBackendRegistry::sendText(const QString &chatId, const QString &text)
{
    ChatBackend *backend = backendFor(chatId);
    return backend && backend->sendText(chatId, text);
}

bool ChatBackendRegistry::sendFile(const QString &chatId, const QString &localFilePath)
{
    ChatBackend *backend = backendFor(chatId);
    return backend && backend->sendFile(chatId, localFilePath);
}

void ChatBackendRegistry::markRead(const QString &chatId)
{
    if (ChatBackend *backend = backendFor(chatId))
        backend->markRead(chatId);
}

void ChatBackendRegistry::sendTyping(const QString &chatId)
{
    if (ChatBackend *backend = backendFor(chatId))
        backend->sendTyping(chatId);
}

void ChatBackendRegistry::sendReaction(const QString &chatId, double ts, const QString &emoji, bool added)
{
    if (ChatBackend *backend = backendFor(chatId))
        backend->sendReaction(chatId, ts, emoji, added);
}

bool ChatBackendRegistry::sendEdit(const QString &chatId, double ts, const QString &newText)
{
    ChatBackend *backend = backendFor(chatId);
    return backend && backend->sendEdit(chatId, ts, newText);
}

bool ChatBackendRegistry::sendDelete(const QString &chatId, double ts)
{
    ChatBackend *backend = backendFor(chatId);
    return backend && backend->sendDelete(chatId, ts);
}

bool ChatBackendRegistry::leaveChat(const QString &chatId)
{
    ChatBackend *backend = backendFor(chatId);
    return backend && backend->leaveChat(chatId);
}

QVariantMap ChatBackendRegistry::roomInfo(const QString &chatId) const
{
    if (ChatBackend *backend = backendFor(chatId))
        return backend->roomInfo(chatId);
    return {};
}

QVariantList ChatBackendRegistry::roomMembers(const QString &chatId) const
{
    if (ChatBackend *backend = backendFor(chatId))
        return backend->roomMembers(chatId);
    return {};
}

QVariantMap ChatBackendRegistry::memberInfo(const QString &chatId, const QString &userId) const
{
    if (ChatBackend *backend = backendFor(chatId))
        return backend->memberInfo(chatId, userId);
    return {};
}

bool ChatBackendRegistry::serverOwnsTimeline(const QString &chatId) const
{
    if (ChatBackend *backend = backendFor(chatId))
        return backend->serverOwnsTimeline(chatId);
    return false;
}

bool ChatBackendRegistry::hasRooms(const QString &chatId) const
{
    if (ChatBackend *backend = backendFor(chatId))
        return backend->hasRooms(chatId);
    return false;
}

bool ChatBackendRegistry::supportsCalls(const QString &chatId) const
{
    if (ChatBackend *backend = backendFor(chatId))
        return backend->supportsCalls(chatId);
    return false;
}

bool ChatBackendRegistry::supportsTyping(const QString &chatId) const
{
    if (ChatBackend *backend = backendFor(chatId))
        return backend->supportsTyping(chatId);
    return false;
}

bool ChatBackendRegistry::supportsEdits(const QString &chatId) const
{
    if (ChatBackend *backend = backendFor(chatId))
        return backend->supportsEdits(chatId);
    return false;
}

bool ChatBackendRegistry::supportsReactions(const QString &chatId) const
{
    if (ChatBackend *backend = backendFor(chatId))
        return backend->supportsReactions(chatId);
    return false;
}

QString ChatBackendRegistry::transportName(const QString &chatId) const
{
    return chatid::transportName(chatId);
}

} // namespace koutnet
