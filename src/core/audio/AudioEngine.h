// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#pragma once

#include <QAudioFormat>
#include <QByteArray>
#include <QIODevice>
#include <QObject>
#include <QString>

#include <atomic>

#include "AudioMixer.h"

class QAudioSource;
class QAudioSink;

namespace koutnet
{

class AudioEngine : public QObject
{
    Q_OBJECT

public:
    static constexpr int kSampleRate = 16000;
    static constexpr int kChannels = 1;
    static constexpr int kChunkSamples = 512; // ~32ms @16kHz
    static constexpr int kChunkBytes = kChunkSamples * 2; // int16

    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    bool startCapture();
    void stopAll();
    void cleanup();

    bool running() const
    {
        return m_running;
    }

// written on the gui thread, read by PlaybackDevice::readData() on qt's audio
// thread: a torn read of a plain qreal lands as a volume nobody asked for.
    void setMuted(bool muted)
    {
        m_muted.store(muted, std::memory_order_relaxed);
    }
    bool muted() const
    {
        return m_muted.load(std::memory_order_relaxed);
    }

    void setDeafened(bool deafened)
    {
        m_deafened.store(deafened, std::memory_order_relaxed);
    }
    bool deafened() const
    {
        return m_deafened.load(std::memory_order_relaxed);
    }

    void setVolume(qreal v)
    {
        m_volume.store(v, std::memory_order_relaxed);
    }
    qreal volume() const
    {
        return m_volume.load(std::memory_order_relaxed);
    }

// read at startCapture(): a change during a call takes effect on the next one.
    void setInputDeviceId(const QString &id)
    {
        m_inputId = id;
    }
    void setOutputDeviceId(const QString &id)
    {
        m_outputId = id;
    }

    void setVadEnabled(bool enabled)
    {
        m_vadEnabled = enabled;
    }
    bool vadEnabled() const
    {
        return m_vadEnabled;
    }

    void pushPeerAudio(const QString &ip, const QByteArray &data);
    AudioMixer &mixer()
    {
        return m_mixer;
    }

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
    QIODevice *m_captureDevice = nullptr; // owned by m_source
    PlaybackDevice *m_playbackDevice = nullptr;

    AudioMixer m_mixer;
    bool m_running = false;
    std::atomic<bool> m_muted = false;
    std::atomic<bool> m_deafened = false;
    bool m_vadEnabled = true;
    QString m_inputId;
    QString m_outputId;
    std::atomic<qreal> m_volume = 1.0;

    QByteArray m_captureAccum; // accumulates partial reads up to kChunkBytes

    bool m_speakLast = false;
    int m_speakFrameCtr = 0;
};

} // namespace koutnet
