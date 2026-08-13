// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - what a chat transport must be able to do.
//
// One backend per transport. A backend owns its protocol (NetworkManager for
// LAN/VPN, MatrixRoomBridge for Matrix, and whatever talks to Rocket.Chat or
// Telegram will be one too) and answers two questions: whether a chat id is
// its to handle, and - through the capability flags - how the interface must
// treat a conversation on it. The window routes every action through
// ChatBackendRegistry, keyed on the chat id alone, so no QML file ever needs
// to know a prefix: adding a transport is writing one backend class,
// registering it in main.cpp and - if its rows look different - one
// Connections block for its receive signals.
//
// Deliberately no Q_OBJECT and no signals here: this is the seam between the
// window and the protocols, and receive paths are genuinely per-transport
// (LAN multiplexes everything through one message packet, Matrix has one
// signal per row kind). A backend emits its own signals; Main.qml listens to
// the backend objects it registered.
//
// Everything is keyed on the chat id string - the one namespace every model
// and the history on disk already share (see core/chat/ChatAddress.h).
#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "core/chat/ChatAddress.h"

namespace koutnet
{

class ChatBackend : public QObject
{
public:
    explicit ChatBackend(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    // Which transport this backend is, and whether a chat id belongs to it.
    virtual chatid::Transport transport() const = 0;
    virtual bool canHandle(const QString &chatId) const = 0;

    // How the interface must treat a conversation on this transport. Each flag
    // gates a real UI flow, and a backend answering no to one is not a bug but
    // a statement: the window must not offer what the transport does not have.
    //
    // serverOwnsTimeline: rows for this chat come back through the ingest path
    // with the id the whole room sees, so the window must not render a local
    // row before sending (Matrix, and Rocket.Chat and Telegram will be the
    // same). LAN renders locally and sends a datagram, so it is false.
    //
    // hasRooms: chats on this transport carry room furniture - a member list,
    // an info column, a leave action (Matrix, Rocket.Chat, Telegram). LAN
    // chats are a peer card or nothing.
    //
    // supportsCalls / supportsTyping / supportsEdits / supportsReactions: the
    // LAN protocol has all four and Matrix none (its bridge skips reactions);
    // the window offers them only when the flag says so.
    virtual bool serverOwnsTimeline(const QString &chatId) const = 0;
    virtual bool hasRooms(const QString &chatId) const = 0;
    virtual bool supportsCalls(const QString &chatId) const = 0;
    virtual bool supportsTyping(const QString &chatId) const = 0;
    virtual bool supportsEdits(const QString &chatId) const = 0;
    virtual bool supportsReactions(const QString &chatId) const = 0;

    // Outgoing actions. False means refused here (unknown chat, not signed in,
    // the transport's own reason): the caller must not invent a result.
    virtual bool sendText(const QString &chatId, const QString &text) = 0;
    virtual bool sendFile(const QString &chatId, const QString &localFilePath) = 0;
    virtual void markRead(const QString &chatId) = 0;
    virtual void sendTyping(const QString &chatId) = 0;
    virtual void sendReaction(const QString &chatId, double ts, const QString &emoji, bool added) = 0;
    virtual bool leaveChat(const QString &chatId) = 0;

    // Room-shaped metadata. Empty values when the chat has none of it, which
    // the window tests by looking for the id key it expects. The roomInfo()
    // map is the contract every room-shaped chat is drawn from; the keys a
    // window may rely on are: roomId (present exactly when there is a room),
    // displayName, topic, avatarUrl (empty, a plain http(s) URL, or an mxc://
    // the Matrix network factory rewrites), joinedCount, encrypted. A backend
    // may add its own keys on top; the window must not read a key it does not
    // know.
    virtual QVariantMap roomInfo(const QString &chatId) const = 0;
    virtual QVariantList roomMembers(const QString &chatId) const = 0;
    virtual QVariantMap memberInfo(const QString &chatId, const QString &userId) const = 0;
};

} // namespace koutnet
