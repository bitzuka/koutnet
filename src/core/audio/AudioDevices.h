// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// separate from AudioEngine on purpose: these probes run with no call up,
// and one shared QAudioSource would let a mic test kill a live call.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QVariantList>

class QAudioDevice;
class QAudioSink;
class QAudioSource;
class QBuffer;
class QIODevice;
class QMediaDevices;

namespace koutnet
{

class AudioDevices : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList inputs READ inputs NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY devicesChanged)
    Q_PROPERTY(bool micTestRunning READ micTestRunning NOTIFY micTestRunningChanged)
    Q_PROPERTY(qreal micLevel READ micLevel NOTIFY micLevelChanged)
    Q_PROPERTY(bool tonePlaying READ tonePlaying NOTIFY tonePlayingChanged)

public:
    explicit AudioDevices(QObject *parent = nullptr);
    ~AudioDevices() override;

    QVariantList inputs() const;
    QVariantList outputs() const;

    bool micTestRunning() const
    {
        return m_source != nullptr;
    }
    bool tonePlaying() const
    {
        return m_sink != nullptr;
    }
    qreal micLevel() const
    {
        return m_level;
    }

    Q_INVOKABLE void startMicTest(const QString &deviceId);
    Q_INVOKABLE void stopMicTest();
    Q_INVOKABLE void playTestTone(const QString &deviceId);
    Q_INVOKABLE void stopTestTone();

Q_SIGNALS:
    void devicesChanged();
    void micTestRunningChanged();
    void micLevelChanged();
    void tonePlayingChanged();
    void error(const QString &message);

private:
    void readMicChunk();
    void setLevel(qreal level);

    QMediaDevices *m_devices = nullptr;

    QAudioSource *m_source = nullptr;
    QIODevice *m_capture = nullptr; // owned by m_source
    qreal m_level = 0.0;

    QAudioSink *m_sink = nullptr;
    QBuffer *m_toneBuffer = nullptr;
    QByteArray m_tone;
};

} // namespace koutnet
