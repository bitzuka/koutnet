// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#pragma once

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QString>
#include <memory>

namespace koutnet
{

class PeerBuffer
{
public:
    static constexpr int kFrameSamples = 512; // samples per frame
    static constexpr int kFrameBytes = kFrameSamples * 2; // int16
    static constexpr int kTargetFrames = 6; // pre-fill (~192ms @16kHz)
    static constexpr int kMaxFrames = 25; // hard cap  (~800ms)

    void push(const QByteArray &data);
    QByteArray pull(); // returns empty QByteArray if not ready (caller treats as silence)
    void clear();

private:
    QByteArray m_buf;
    bool m_ready = false;
    mutable QMutex m_mutex;
};

// mix() runs on qt multimedia's audio thread while
// push()/addPeer()/removePeer() run on the gui thread; shared_ptr because
// removePeer() can free a buffer mid-read.
class AudioMixer
{
public:
    ~AudioMixer();

    PeerBuffer &addPeer(const QString &ip);
    void removePeer(const QString &ip);
    void push(const QString &ip, const QByteArray &data);
    QByteArray mix(); // always returns kFrameBytes of PCM (silence if nobody ready)
    void dropAll();
    int peerCount() const;

private:
    QHash<QString, std::shared_ptr<PeerBuffer>> m_peers;
    mutable QMutex m_mutex;
};

} // namespace koutnet
