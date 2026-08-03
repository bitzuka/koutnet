// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Per-peer jitter buffer + audio mixer
#include "AudioMixer.h"

#include <QMutexLocker>
#include <algorithm>
#include <cstring>

namespace koutnet {

void PeerBuffer::push(const QByteArray &data)
{
    QMutexLocker lock(&m_mutex);
    m_buf.append(data);

    const int frames = m_buf.size() / kFrameBytes;
    if (frames >= kTargetFrames)
        m_ready = true;

    // If the network throws more at us than we can play back (a burst after
    // a stall, for example), we'd rather drop the oldest, stalest audio and
    // keep latency bounded than let the buffer grow forever and have
    // playback fall further and further behind live.
    const int cap = kFrameBytes * kMaxFrames;
    if (m_buf.size() > cap) {
        const int excess = m_buf.size() - cap;
        m_buf.remove(0, excess);
    }
}

QByteArray PeerBuffer::pull()
{
    QMutexLocker lock(&m_mutex);
    if (!m_ready || m_buf.size() < kFrameBytes)
        return {}; // not enough buffered yet - caller plays silence this frame

    const QByteArray frame = m_buf.left(kFrameBytes);
    m_buf.remove(0, kFrameBytes);

    // Once we drop below half the target pre-fill, stop playing until we've
    // built back up to a full buffer again - better a brief pause than
    // stuttering frame-by-frame as the buffer runs dry.
    if (m_buf.size() / kFrameBytes < kTargetFrames / 2)
        m_ready = false;

    return frame;
}

void PeerBuffer::clear()
{
    QMutexLocker lock(&m_mutex);
    m_buf.clear();
    m_ready = false;
}

AudioMixer::~AudioMixer()
{
    QMutexLocker lock(&m_mutex);
    m_peers.clear(); // shared_ptrs free their PeerBuffers once nobody else is using them
}

PeerBuffer &AudioMixer::addPeer(const QString &ip)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_peers.find(ip);
    if (it == m_peers.end())
        it = m_peers.insert(ip, std::make_shared<PeerBuffer>());
    return *it.value();
}

void AudioMixer::removePeer(const QString &ip)
{
    QMutexLocker lock(&m_mutex);
    // Drops our reference only. If the audio thread is mid-mix() holding a
    // copy, the buffer outlives the call until that copy goes too.
    m_peers.take(ip);
}

void AudioMixer::push(const QString &ip, const QByteArray &data)
{
    std::shared_ptr<PeerBuffer> buf;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_peers.find(ip);
        if (it == m_peers.end())
            it = m_peers.insert(ip, std::make_shared<PeerBuffer>());
        buf = it.value(); // copy the shared_ptr - keeps it alive past unlock
    }
    buf->push(data);
}

QByteArray AudioMixer::mix()
{
    // Snapshot the peers and drop the lock before mixing. Holding the mixer
    // mutex through the mix would stall the GUI thread whenever it adds or
    // removes a peer. Each buffer has its own mutex, so this is safe.
    QList<std::shared_ptr<PeerBuffer>> peers;
    {
        QMutexLocker lock(&m_mutex);
        peers = m_peers.values();
    }

    QByteArray out(PeerBuffer::kFrameBytes, 0); // starts silent
    auto *outSamples = reinterpret_cast<qint16 *>(out.data());

    for (const auto &buf : std::as_const(peers)) {
        const QByteArray frame = buf->pull();
        if (frame.isEmpty())
            continue; // this peer has nothing ready - treat as silence, keep going

        const auto *src = reinterpret_cast<const qint16 *>(frame.constData());
        for (int i = 0; i < PeerBuffer::kFrameSamples; ++i) {
            // Plain addition can overflow int16 once a few people talk at
            // once, so we add in a wider int and clamp back down - this is
            // what stops loud group calls from wrapping around into
            // crackling noise instead of just getting louder.
            const int sum = int(outSamples[i]) + int(src[i]);
            outSamples[i] = static_cast<qint16>(std::clamp(sum, -32768, 32767));
        }
    }

    return out; // already silent by default if nobody had a frame ready
}

void AudioMixer::dropAll()
{
    QList<std::shared_ptr<PeerBuffer>> peers;
    {
        QMutexLocker lock(&m_mutex);
        peers = m_peers.values();
    }
    for (const auto &buf : std::as_const(peers))
        buf->clear();
}

int AudioMixer::peerCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_peers.size();
}

} // namespace koutnet
