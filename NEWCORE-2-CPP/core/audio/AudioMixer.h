// KOutNet — Per-peer jitter buffer + audio mixer
// Ported from gdf_core.py (PeerBuffer / AudioMixer, NT Server 1.8) -> C++/Qt6
#pragma once

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QString>
#include <memory>

namespace koutnet {

// One peer's jitter buffer. Network packets don't arrive at a perfectly
// steady rhythm — they clump up and stall depending on the path they took —
// so we can't just play each frame the instant it shows up, or playback
// would stutter constantly. Instead we let a small backlog build up first
// (kTargetFrames), then drain it out at a steady pace. Think of it as a
// tiny buffer tank absorbing the network's jitter before it reaches the
// speakers.
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

// Combines every active peer's audio into a single stream to send to the
// speakers. One PeerBuffer per peer; mix() takes one frame from each and
// sums them together (with clipping, so three people talking at once
// doesn't wrap around into noise).
//
// Threading note, important: mix() is called from Qt Multimedia's audio
// callback, which runs on its own I/O thread — NOT the GUI thread. Meanwhile
// push()/addPeer()/removePeer() are called from the GUI thread as network
// packets arrive or calls start/end. So this class is genuinely shared
// between two threads at once, and every PeerBuffer is stored as a
// shared_ptr rather than a raw pointer for exactly one reason: if
// removePeer() deletes a peer's buffer on the GUI thread at the same moment
// the audio thread is mid-way through reading from it inside mix(), a raw
// pointer would leave the audio thread holding a dangling pointer — a
// crash that's nearly impossible to reproduce on demand. Handing out a
// shared_ptr instead means whoever's using the buffer keeps it alive for as
// long as they're using it, even if it's simultaneously been removed from
// the peer list.
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
