// KOutNet — Voice call manager (P2P calls, group calls via per-peer jitter buffers)
// Ported from gdf_network.py ( NT Server 1.8) → C++/Qt6
#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QByteArray>
#include <functional>

namespace koutnet {

class NetworkManager;
// TODO: AudioEngine lives in core/audio (not yet ported) — forward-declared.
class AudioEngine;

class VoiceCallManager : public QObject {
    Q_OBJECT

public:
    explicit VoiceCallManager(NetworkManager *net, QObject *parent = nullptr);

    bool call(const QString &ip);
    void hangup(const QString &ip);
    void hangupAll();

    void setMute(bool muted);
    bool toggleMute();
    bool isMuted() const { return m_muted; }

    void setVad(bool enabled);

    const QSet<QString> &activeCalls() const { return m_active; }

    // Speaking-state subscription (forwarded from AudioEngine VAD)
    using SpeakingCallback = std::function<void(bool)>;
    void subscribeSpeaking(const SpeakingCallback &cb);

    void cleanup();

signals:
    void callStarted(QString ip);
    void callEnded(QString ip);

private slots:
    void onCaptured(const QByteArray &data);
    void onPeerAudio(const QString &ip, const QByteArray &data);

private:
    NetworkManager *m_net;
    AudioEngine *m_audio = nullptr; // TODO: construct once core/audio is ported
    QSet<QString> m_active;
    bool m_muted = false;
    QVector<SpeakingCallback> m_speakingCallbacks;
};

} // namespace koutnet
