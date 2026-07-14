// KOutNet — Real-time voice engine (capture, mix, playback)
// Ported from gdf_core.py (AudioEngine, NT Server 1.8) -> C++/Qt6
#include "AudioEngine.h"

#include <QAudioSource>
#include <QAudioSink>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QtMath>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace koutnet {

// Pull-mode playback device: QAudioSink calls readData() whenever it needs
// more samples to keep the output buffer full. We hand it freshly mixed
// frames from AudioMixer on demand — no separate playback thread needed.
class AudioEngine::PlaybackDevice : public QIODevice {
public:
    explicit PlaybackDevice(AudioEngine *engine) : m_engine(engine)
    {
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

    bool isSequential() const override { return true; }

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        qint64 written = 0;
        while (written + PeerBuffer::kFrameBytes <= maxlen) {
            QByteArray mixed = m_engine->m_mixer.mix();
            if (!qFuzzyCompare(m_engine->m_volume, 1.0)) {
                auto *samples = reinterpret_cast<qint16 *>(mixed.data());
                const int count = mixed.size() / 2;
                for (int i = 0; i < count; ++i) {
                    const int scaled = qRound(samples[i] * m_engine->m_volume);
                    samples[i] = static_cast<qint16>(std::clamp(scaled, -32768, 32767));
                }
            }
            std::memcpy(data + written, mixed.constData(), mixed.size());
            written += mixed.size();
        }
        if (written < maxlen) {
            std::memset(data + written, 0, maxlen - written);
            written = maxlen;
        }
        return written;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    AudioEngine *m_engine;
};

QAudioFormat AudioEngine::format() const
{
    QAudioFormat fmt;
    fmt.setSampleRate(kSampleRate);
    fmt.setChannelCount(kChannels);
    fmt.setSampleFormat(QAudioFormat::Int16);
    return fmt;
}

AudioEngine::AudioEngine(QObject *parent) : QObject(parent) {}

AudioEngine::~AudioEngine()
{
    cleanup();
}

bool AudioEngine::startCapture()
{
    stopAll();

    const QAudioFormat fmt = format();
    const QAudioDevice inDev = QMediaDevices::defaultAudioInput();
    const QAudioDevice outDev = QMediaDevices::defaultAudioOutput();
    if (inDev.isNull() || outDev.isNull())
        return false; // no mic/speaker — voice calls disabled, same as legacy PYAUDIO_AVAILABLE=false path

    m_source = new QAudioSource(inDev, fmt, this);
    m_captureDevice = m_source->start();
    if (!m_captureDevice) {
        stopAll();
        return false;
    }
    connect(m_captureDevice, &QIODevice::readyRead, this, &AudioEngine::onCaptureReady);

    m_sink = new QAudioSink(outDev, fmt, this);
    m_playbackDevice = new PlaybackDevice(this);
    m_sink->start(m_playbackDevice);

    m_running = true;
    return true;
}

void AudioEngine::stopAll()
{
    m_running = false;

    if (m_source) {
        m_source->stop();
        m_source->deleteLater();
        m_source = nullptr;
    }
    m_captureDevice = nullptr; // owned by QAudioSource, freed with it

    if (m_sink) {
        m_sink->stop();
        m_sink->deleteLater();
        m_sink = nullptr;
    }
    if (m_playbackDevice) {
        m_playbackDevice->close();
        m_playbackDevice->deleteLater();
        m_playbackDevice = nullptr;
    }
    m_captureAccum.clear();
}

void AudioEngine::cleanup()
{
    stopAll();
    m_mixer.dropAll();
}

void AudioEngine::pushPeerAudio(const QString &ip, const QByteArray &data)
{
    m_mixer.push(ip, data);
}

bool AudioEngine::isSpeechAmplitude(const QByteArray &raw) const
{
    // Amplitude-based VAD fallback (matches the legacy Python fallback path
    // used when webrtcvad isn't available — RMS threshold ~800).
    const auto *samples = reinterpret_cast<const qint16 *>(raw.constData());
    const int count = raw.size() / 2;
    if (count == 0)
        return false;

    double sumSq = 0.0;
    for (int i = 0; i < count; ++i)
        sumSq += double(samples[i]) * double(samples[i]);
    const double rms = std::sqrt(sumSq / count);
    return rms > 800.0;
}

void AudioEngine::onCaptureReady()
{
    if (!m_captureDevice)
        return;

    m_captureAccum.append(m_captureDevice->readAll());

    while (m_captureAccum.size() >= kChunkBytes) {
        const QByteArray raw = m_captureAccum.left(kChunkBytes);
        m_captureAccum.remove(0, kChunkBytes);

        if (m_muted) {
            if (m_speakLast) {
                m_speakLast = false;
                emit speaking(false);
            }
            continue;
        }

        const bool isSpeech = m_vadEnabled ? isSpeechAmplitude(raw) : true;
        if (isSpeech)
            emit audioCaptured(raw);

        // Debounce speaking indicator: only emit on state change, checked
        // every 4 frames (~128ms), matching the legacy engine.
        if (++m_speakFrameCtr >= 4) {
            m_speakFrameCtr = 0;
            if (isSpeech != m_speakLast) {
                m_speakLast = isSpeech;
                emit speaking(isSpeech);
            }
        }
    }
}

} // namespace koutnet
