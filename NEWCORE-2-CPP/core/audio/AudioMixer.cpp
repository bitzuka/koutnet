// KOutNet — Per-peer jitter buffer + audio mixer
// Ported from gdf_core.py (PeerBuffer / AudioMixer, NT Server 1.8) -> C++/Qt6
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

    // Hard cap — drop oldest bytes on overflow (matches legacy behaviour).
    const int cap = kFrameBytes * kMaxFrames;
    if (m_buf.size() > cap) {
        const int excess = m_buf.size() - cap;
        m_buf.remove(0, excess);
        ++m_drops;
    }
}

QByteArray PeerBuffer::pull()
{
    QMutexLocker lock(&m_mutex);
    if (!m_ready || m_buf.size() < kFrameBytes)
        return {};

    const QByteArray frame = m_buf.left(kFrameBytes);
    m_buf.remove(0, kFrameBytes);

    // If buffer drains below half target, reset pre-fill (matches legacy).
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
    qDeleteAll(m_peers);
}

PeerBuffer &AudioMixer::addPeer(const QString &ip)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_peers.find(ip);
    if (it == m_peers.end())
        it = m_peers.insert(ip, new PeerBuffer());
    return *it.value();
}

void AudioMixer::removePeer(const QString &ip)
{
    QMutexLocker lock(&m_mutex);
    delete m_peers.take(ip);
}

void AudioMixer::push(const QString &ip, const QByteArray &data)
{
    PeerBuffer *buf = nullptr;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_peers.find(ip);
        if (it == m_peers.end())
            it = m_peers.insert(ip, new PeerBuffer());
        buf = it.value();
    }
    buf->push(data);
}

QByteArray AudioMixer::mix()
{
    QList<PeerBuffer *> peers;
    {
        QMutexLocker lock(&m_mutex);
        peers = m_peers.values();
    }

    QByteArray out(PeerBuffer::kFrameBytes, 0);
    auto *outSamples = reinterpret_cast<qint16 *>(out.data());
    bool gotAny = false;

    for (auto *buf : std::as_const(peers)) {
        const QByteArray frame = buf->pull();
        if (frame.isEmpty())
            continue; // peer buffer not ready -> contributes silence
        gotAny = true;
        const auto *src = reinterpret_cast<const qint16 *>(frame.constData());
        for (int i = 0; i < PeerBuffer::kFrameSamples; ++i) {
            const int sum = int(outSamples[i]) + int(src[i]);
            outSamples[i] = static_cast<qint16>(std::clamp(sum, -32768, 32767));
        }
    }

    if (!gotAny)
        out.fill(0);
    return out;
}

void AudioMixer::dropAll()
{
    QMutexLocker lock(&m_mutex);
    for (auto *buf : std::as_const(m_peers))
        buf->clear();
}

int AudioMixer::peerCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_peers.size();
}

} // namespace koutnet
