// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Voice call manager (P2P calls, group calls via per-peer jitter buffers)
#pragma once

#include <QByteArray>
#include <QObject>
#include <QSet>
#include <QString>
#include <functional>

namespace koutnet
{

class NetworkManager;
class CryptoManager;
class AudioEngine;

class VoiceCallManager : public QObject
{
    Q_OBJECT

public:
    // Same CryptoManager instance NetworkManager holds. Voice frames are encrypted
    // here as raw XChaCha20-Poly1305, because only this class knows which IPs are live.
    explicit VoiceCallManager(NetworkManager *net, CryptoManager *crypto, QObject *parent = nullptr);

    Q_INVOKABLE bool call(const QString &ip);
    Q_INVOKABLE void hangup(const QString &ip);
    Q_INVOKABLE void hangupAll();

    Q_INVOKABLE void setMute(bool muted);
    Q_INVOKABLE void setDeafen(bool deafened);

    Q_INVOKABLE void setVad(bool enabled);

    Q_INVOKABLE void setAudioInputDevice(const QString &id);
    Q_INVOKABLE void setAudioOutputDevice(const QString &id);
    Q_INVOKABLE void setAudioVolume(qreal volume);

    const QSet<QString> &activeCalls() const
    {
        return m_active;
    }

Q_SIGNALS:
    void callStarted(QString ip);

    // Any call going on, in either direction. NetworkManager listens to this to
    // answer incoming call requests with the busy reply while one is live.
    void activeCallsChanged();

private Q_SLOTS:
    void onCaptured(const QByteArray &data);
    void onPeerAudio(const QString &ip, const QByteArray &data);

private:
    NetworkManager *m_net;
    CryptoManager *m_crypto;
    AudioEngine *m_audio = nullptr;
    QSet<QString> m_active;
    bool m_muted = false;
    bool m_deafened = false;
};

} // namespace koutnet
