// KOutNet — Real-time voice engine (capture, mix, playback)
// Ported from gdf_core.py (AudioEngine, NT Server 1.8) -> C++/Qt6
//
// Uses QAudioSource/QAudioSink in pull mode instead of a dedicated polling
// thread (the legacy PyAudio version ran its own QThread loop) — this lets
// the Qt Multimedia backend drive timing, which is lighter on low-RAM
// machines than a busy-reading thread.
#pragma once

#include <QObject>
#include <QAudioFormat>
#include <QByteArray>
#include <QIODevice>

#include "AudioMixer.h"

class QAudioSource;
class QAudioSink;

namespace koutnet {

class AudioEngine : public QObject {
    Q_OBJECT

public:
    static constexpr int kSampleRate  = 16000;
    static constexpr int kChannels    = 1;
    static constexpr int kChunkSamples = 512;               // ~32ms @16kHz
    static constexpr int kChunkBytes   = kChunkSamples * 2; // int16

    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    bool startCapture();
    void stopAll();
    void cleanup();

    bool running() const { return m_running; }

    void setMuted(bool muted) { m_muted = muted; }
    bool muted() const { return m_muted; }

    void setVolume(qreal v) { m_volume = v; }
    qreal volume() const { return m_volume; }

    void setVadEnabled(bool enabled) { m_vadEnabled = enabled; }
    bool vadEnabled() const { return m_vadEnabled; }

    void pushPeerAudio(const QString &ip, const QByteArray &data);
    AudioMixer &mixer() { return m_mixer; }

signals:
    void audioCaptured(QByteArray raw);
    void speaking(bool isSpeaking);

private slots:
    void onCaptureReady();

private:
    class PlaybackDevice; // QIODevice subclass, defined in .cpp

    QAudioFormat format() const;
    bool isSpeechAmplitude(const QByteArray &raw) const;

    QAudioSource *m_source = nullptr;
    QAudioSink *m_sink = nullptr;
    QIODevice *m_captureDevice = nullptr;     // owned by m_source
    PlaybackDevice *m_playbackDevice = nullptr;

    AudioMixer m_mixer;
    bool m_running = false;
    bool m_muted = false;
    bool m_vadEnabled = true;
    qreal m_volume = 1.0;

    QByteArray m_captureAccum; // accumulates partial reads up to kChunkBytes

    bool m_speakLast = false;
    int m_speakFrameCtr = 0;
};

} // namespace koutnet
