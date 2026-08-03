// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "AudioDevices.h"
#include "koutnet_audio_debug.h"

#include <KLocalizedString>

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <QBuffer>
#include <QDebug>
#include <QIODevice>
#include <QMediaDevices>
#include <QtMath>

#if QT_VERSION < QT_VERSION_CHECK(6, 7, 0)
// Qt renamed the QAudio namespace to QtAudio in 6.7. CMakeLists still declares
// 6.4 as the floor, so alias it back rather than hard-requiring the newer Qt.
namespace QtAudio = QAudio;
#endif

namespace koutnet {
namespace {

constexpr int kProbeRate = 48000;
constexpr int kToneHz = 440;
constexpr int kToneMs = 700;

QVariantList describe(const QList<QAudioDevice> &devices)
{
    QVariantList out;
    out.reserve(devices.size());
    for (const QAudioDevice &device : devices) {
        QVariantMap entry;
        entry[QStringLiteral("id")] = QString::fromUtf8(device.id());
        entry[QStringLiteral("description")] = device.description();
        out.append(entry);
    }
    return out;
}

// Falls back to the system default when the saved device is gone, which is the
// normal case for a USB headset that was unplugged since the setting was made.
QAudioDevice pick(const QList<QAudioDevice> &devices, const QString &id,
                  const QAudioDevice &fallback)
{
    if (id.isEmpty())
        return fallback;
    const QByteArray wanted = id.toUtf8();
    for (const QAudioDevice &device : devices) {
        if (device.id() == wanted)
            return device;
    }
    return fallback;
}

QAudioFormat probeFormat(const QAudioDevice &device)
{
    QAudioFormat fmt;
    fmt.setSampleRate(kProbeRate);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);
    if (device.isFormatSupported(fmt))
        return fmt;
    return device.preferredFormat();
}

qreal rmsOf(const QByteArray &chunk, const QAudioFormat &fmt)
{
    double sum = 0.0;
    int count = 0;

    if (fmt.sampleFormat() == QAudioFormat::Int16) {
        count = int(chunk.size() / sizeof(qint16));
        const auto *s = reinterpret_cast<const qint16 *>(chunk.constData());
        for (int i = 0; i < count; ++i) {
            const double v = s[i] / 32768.0;
            sum += v * v;
        }
    } else if (fmt.sampleFormat() == QAudioFormat::Float) {
        count = int(chunk.size() / sizeof(float));
        const auto *s = reinterpret_cast<const float *>(chunk.constData());
        for (int i = 0; i < count; ++i) {
            const double v = double(s[i]);
            sum += v * v;
        }
    }

    if (count <= 0)
        return 0.0;
    return qSqrt(sum / count);
}

// Ramped at both ends so the speaker test does not start and end on a click.
QByteArray makeTone(const QAudioFormat &fmt)
{
    const int rate = fmt.sampleRate() > 0 ? fmt.sampleRate() : kProbeRate;
    const int channels = qMax(1, fmt.channelCount());
    const int frames = rate * kToneMs / 1000;
    const int fade = qMin(frames / 4, rate / 50);
    const double step = 2.0 * M_PI * kToneHz / rate;
    const bool isFloat = (fmt.sampleFormat() == QAudioFormat::Float);

    QByteArray out;
    out.resize(qsizetype(frames) * channels * (isFloat ? qsizetype(sizeof(float))
                                                       : qsizetype(sizeof(qint16))));
    auto *asFloat = reinterpret_cast<float *>(out.data());
    auto *asInt = reinterpret_cast<qint16 *>(out.data());

    for (int f = 0; f < frames; ++f) {
        double gain = 0.35;
        if (fade > 0) {
            if (f < fade)
                gain *= double(f) / fade;
            else if (f > frames - fade)
                gain *= double(frames - f) / fade;
        }
        const double sample = qSin(step * f) * gain;
        for (int c = 0; c < channels; ++c) {
            const int idx = f * channels + c;
            if (isFloat)
                asFloat[idx] = float(sample);
            else
                asInt[idx] = qint16(qBound(-1.0, sample, 1.0) * 32767.0);
        }
    }
    return out;
}

} // namespace

AudioDevices::AudioDevices(QObject *parent)
    : QObject(parent), m_devices(new QMediaDevices(this))
{
    connect(m_devices, &QMediaDevices::audioInputsChanged,
            this, &AudioDevices::devicesChanged);
    connect(m_devices, &QMediaDevices::audioOutputsChanged,
            this, &AudioDevices::devicesChanged);
}

AudioDevices::~AudioDevices()
{
    stopMicTest();
    stopTestTone();
}

QVariantList AudioDevices::inputs() const
{
    return describe(QMediaDevices::audioInputs());
}

QVariantList AudioDevices::outputs() const
{
    return describe(QMediaDevices::audioOutputs());
}

void AudioDevices::setLevel(qreal level)
{
    if (qFuzzyCompare(m_level + 1.0, level + 1.0))
        return;
    m_level = level;
    Q_EMIT micLevelChanged();
}

void AudioDevices::startMicTest(const QString &deviceId)
{
    if (m_source)
        stopMicTest();

    const QAudioDevice device = pick(QMediaDevices::audioInputs(), deviceId,
                                     QMediaDevices::defaultAudioInput());
    if (device.isNull()) {
        Q_EMIT error(i18nc("@info:status", "No audio input device available."));
        return;
    }

    const QAudioFormat fmt = probeFormat(device);
    if (!device.isFormatSupported(fmt)) {
        // The message to the user cannot name a format without turning into
        // noise, and the format is the only useful part of a bug report here.
        qCWarning(KOUTNET_LOG_AUDIO) << "input device" << device.description()
                                     << "rejects" << fmt;
        Q_EMIT error(i18nc("@info:status", "The input device rejects every format we can read."));
        return;
    }

    m_source = new QAudioSource(device, fmt, this);
    m_capture = m_source->start();
    if (!m_capture) {
        qCWarning(KOUTNET_LOG_AUDIO) << "could not open input device" << device.description();
        delete m_source;
        m_source = nullptr;
        Q_EMIT error(i18nc("@info:status", "Could not open the input device."));
        return;
    }

    connect(m_capture, &QIODevice::readyRead, this, &AudioDevices::readMicChunk);
    Q_EMIT micTestRunningChanged();
}

void AudioDevices::readMicChunk()
{
    if (!m_source || !m_capture)
        return;
    const QByteArray chunk = m_capture->readAll();
    if (chunk.isEmpty())
        return;
    // Square root keeps quiet speech visible on a linear bar; raw RMS spends
    // most of its range down near zero and barely moves for normal talking.
    setLevel(qBound(0.0, qSqrt(rmsOf(chunk, m_source->format())) * 1.6, 1.0));
}

void AudioDevices::stopMicTest()
{
    if (!m_source)
        return;
    if (m_capture)
        m_capture->disconnect(this);
    m_source->stop();
    m_source->deleteLater();
    m_source = nullptr;
    m_capture = nullptr; // owned by the source
    setLevel(0.0);
    Q_EMIT micTestRunningChanged();
}

void AudioDevices::playTestTone(const QString &deviceId)
{
    if (m_sink)
        stopTestTone();

    const QAudioDevice device = pick(QMediaDevices::audioOutputs(), deviceId,
                                     QMediaDevices::defaultAudioOutput());
    if (device.isNull()) {
        Q_EMIT error(i18nc("@info:status", "No audio output device available."));
        return;
    }

    QAudioFormat fmt = probeFormat(device);
    if (!device.isFormatSupported(fmt))
        fmt = device.preferredFormat();

    m_tone = makeTone(fmt);
    m_toneBuffer = new QBuffer(&m_tone, this);
    if (!m_toneBuffer->open(QIODevice::ReadOnly)) {
        delete m_toneBuffer;
        m_toneBuffer = nullptr;
        Q_EMIT error(i18nc("@info:status", "Could not prepare the test tone."));
        return;
    }

    m_sink = new QAudioSink(device, fmt, this);
    connect(m_sink, &QAudioSink::stateChanged, this, [this](QtAudio::State state) {
        // IdleState means the buffer drained, which for a one-shot tone is the
        // end of playback rather than an underrun to recover from.
        if (state == QtAudio::IdleState || state == QtAudio::StoppedState)
            stopTestTone();
    });
    m_sink->start(m_toneBuffer);
    Q_EMIT tonePlayingChanged();
}

void AudioDevices::stopTestTone()
{
    if (!m_sink)
        return;
    m_sink->disconnect(this);
    m_sink->stop();
    m_sink->deleteLater();
    m_sink = nullptr;
    if (m_toneBuffer) {
        m_toneBuffer->close();
        m_toneBuffer->deleteLater();
        m_toneBuffer = nullptr;
    }
    Q_EMIT tonePlayingChanged();
}

} // namespace koutnet
