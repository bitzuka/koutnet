// KOutNet — Per-peer jitter buffer + audio mixer
// Ported from gdf_core.py (PeerBuffer / AudioMixer, NT Server 1.8) -> C++/Qt6
#pragma once

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QString>

namespace koutnet {

// Adaptive jitter buffer for one remote peer. Holds kTargetFrames frames
// before playback starts, then drains one frame per pull().
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

// Mixes audio from multiple peers into one output stream. Each peer has its
// own PeerBuffer. mix() pulls one frame per peer, sums with clipping.
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
    QHash<QString, PeerBuffer *> m_peers;
    mutable QMutex m_mutex;
};

} // namespace koutnet
