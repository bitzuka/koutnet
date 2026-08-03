// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Voice call manager (P2P calls, group calls via per-peer jitter buffers)
#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QByteArray>
#include <functional>

namespace koutnet {

class NetworkManager;
class CryptoManager;
class AudioEngine;

class VoiceCallManager : public QObject {
    Q_OBJECT

public:
    // Same CryptoManager instance NetworkManager holds. Voice frames are
    // encrypted here rather than there, as raw AES-GCM with no JSON or
    // base64 around them, because only this class knows which IPs are live.
    explicit VoiceCallManager(NetworkManager *net, CryptoManager *crypto,
                              QObject *parent = nullptr);

    // Q_INVOKABLE is load-bearing here. Without it a QML call fails at
    // runtime with "is not a function" while the same call from C++ works.
    Q_INVOKABLE bool call(const QString &ip);
    Q_INVOKABLE void hangup(const QString &ip);
    Q_INVOKABLE void hangupAll();

    Q_INVOKABLE void setMute(bool muted);
    Q_INVOKABLE bool toggleMute();
    bool isMuted() const { return m_muted; }

    Q_INVOKABLE void setVad(bool enabled);

    // Settings-dialog passthroughs. The engine itself stays private;
    // QML has no business holding a pointer to the call audio path.
    Q_INVOKABLE void setAudioInputDevice(const QString &id);
    Q_INVOKABLE void setAudioOutputDevice(const QString &id);
    Q_INVOKABLE void setAudioVolume(qreal volume);

    const QSet<QString> &activeCalls() const { return m_active; }

    // Speaking-state subscription (forwarded from AudioEngine VAD)
    using SpeakingCallback = std::function<void(bool)>;
    void subscribeSpeaking(const SpeakingCallback &cb);

    void cleanup();

Q_SIGNALS:
    void callStarted(QString ip);
    void callEnded(QString ip);

private Q_SLOTS:
    void onCaptured(const QByteArray &data);
    void onPeerAudio(const QString &ip, const QByteArray &data);

private:
    NetworkManager *m_net;
    CryptoManager *m_crypto;
    AudioEngine *m_audio = nullptr;
    QSet<QString> m_active;
    bool m_muted = false;
    QVector<SpeakingCallback> m_speakingCallbacks;
};

} // namespace koutnet
