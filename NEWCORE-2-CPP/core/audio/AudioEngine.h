// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - real-time voice engine: capture, mix, playback.
//
// QAudioSource/QAudioSink in pull mode rather than a polling thread. Letting
// the Qt Multimedia backend drive timing is cheaper on low-RAM machines.
#pragma once

#include <QObject>
#include <QAudioFormat>
#include <QByteArray>
#include <QString>
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

    // Empty id means "system default". Read at startCapture() time, so
    // a change made during a call takes effect on the next one rather
    // than tearing down a live stream mid-sentence.
    void setInputDeviceId(const QString &id) { m_inputId = id; }
    void setOutputDeviceId(const QString &id) { m_outputId = id; }

    void setVadEnabled(bool enabled) { m_vadEnabled = enabled; }
    bool vadEnabled() const { return m_vadEnabled; }

    void pushPeerAudio(const QString &ip, const QByteArray &data);
    AudioMixer &mixer() { return m_mixer; }

Q_SIGNALS:
    void audioCaptured(QByteArray raw);
    void speaking(bool isSpeaking);

private Q_SLOTS:
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
    QString m_inputId;
    QString m_outputId;
    qreal m_volume = 1.0;

    QByteArray m_captureAccum; // accumulates partial reads up to kChunkBytes

    bool m_speakLast = false;
    int m_speakFrameCtr = 0;
};

} // namespace koutnet
