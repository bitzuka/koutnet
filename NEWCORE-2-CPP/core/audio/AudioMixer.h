// KOutNet - Per-peer jitter buffer + audio mixer
#pragma once

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QString>
#include <memory>

namespace koutnet {

// One peer's jitter buffer. Packets clump and stall depending on the route
// they took, so playing each frame the moment it lands stutters. Let a small
// backlog build up to kTargetFrames first, then drain at a steady pace.
class PeerBuffer {
public:
    static constexpr int kFrameSamples = 512;              // samples per frame
    static constexpr int kFrameBytes   = kFrameSamples * 2; // int16
    static constexpr int kTargetFrames = 6;                 // pre-fill (~192ms @16kHz)
    static constexpr int kMaxFrames    = 25;                // hard cap  (~800ms)

    void push(const QByteArray &data);
    QByteArray pull(); // returns empty QByteArray if not ready (caller treats as silence)
    void clear();

    int dropCount() const { return m_drops; }

private:
    QByteArray m_buf;
    bool m_ready = false;
    int m_drops = 0;
    mutable QMutex m_mutex;
};

// Mixes every active peer into one stream for the speakers. mix() takes a
// frame from each PeerBuffer and sums them with clipping, so three people
// talking at once does not wrap around into noise.
//
// mix() runs on Qt Multimedia's audio thread. push(), addPeer() and
// removePeer() run on the GUI thread. Buffers are shared_ptr because
// removePeer() can free one while the audio thread is reading it, and that
// race is close to impossible to reproduce on demand.
class AudioMixer {
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
