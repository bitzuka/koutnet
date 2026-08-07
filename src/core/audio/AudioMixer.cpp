// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "AudioMixer.h"

#include <QMutexLocker>
#include <algorithm>
#include <cstring>

namespace koutnet
{

void PeerBuffer::push(const QByteArray &data)
{
    QMutexLocker lock(&m_mutex);
    m_buf.append(data);

    const int frames = m_buf.size() / kFrameBytes;
    if (frames >= kTargetFrames)
        m_ready = true;

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

    // below half the pre-fill, wait for a full buffer again: a brief pause
    // beats stutter.
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
    // snapshot and unlock before mixing: holding the mixer mutex through the
    // mix would stall the gui thread on add/removePeer.
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
            // sum in a wider int and clamp: plain int16 addition wraps once a few
            // people talk at once, which crackles instead of just getting louder.
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
