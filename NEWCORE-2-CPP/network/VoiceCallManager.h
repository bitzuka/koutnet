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
class CryptoManager;
class AudioEngine;

class VoiceCallManager : public QObject {
    Q_OBJECT

public:
    // CryptoManager is the same shared instance used by NetworkManager — see
    // its constructor comment. Voice frames are encrypted/decrypted here
    // (raw AES-GCM bytes, no JSON/base64 overhead) rather than in
    // NetworkManager, since only VoiceCallManager knows which IPs are
    // actually active calls.
    explicit VoiceCallManager(NetworkManager *net, CryptoManager *crypto,
                              QObject *parent = nullptr);

    // NOTE: these need Q_INVOKABLE to be callable from QML at all — a plain
    // "public:" method is invisible to Qt's meta-object system, so without
    // this annotation voiceCallManager.call(ip) etc. silently fail as a
    // QML runtime "is not a function" error even though the C++ method
    // exists and works fine when called from other C++ code.
    Q_INVOKABLE bool call(const QString &ip);
    Q_INVOKABLE void hangup(const QString &ip);
    Q_INVOKABLE void hangupAll();

    Q_INVOKABLE void setMute(bool muted);
    Q_INVOKABLE bool toggleMute();
    bool isMuted() const { return m_muted; }

    Q_INVOKABLE void setVad(bool enabled);

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
    CryptoManager *m_crypto;
    AudioEngine *m_audio = nullptr;
    QSet<QString> m_active;
    bool m_muted = false;
    QVector<SpeakingCallback> m_speakingCallbacks;
};

} // namespace koutnet
