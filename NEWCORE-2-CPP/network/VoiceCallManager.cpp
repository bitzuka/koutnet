// KOutNet — Voice call manager
// Ported from gdf_network.py (GoidaPhone NT Server 1.8) → C++/Qt6
#include "VoiceCallManager.h"
#include "NetworkManager.h"

// NOTE: This file references AudioEngine (core/audio module — not ported yet).
// Calls into m_audio are stubbed with TODOs below; wire them up once that
// module lands so mic capture / jitter buffers / VAD actually work.

namespace koutnet {

VoiceCallManager::VoiceCallManager(NetworkManager *net, QObject *parent)
    : QObject(parent), m_net(net)
{
    connect(m_net, &NetworkManager::voiceDataFrom, this, &VoiceCallManager::onPeerAudio);
    // TODO once AudioEngine exists:
    //   connect(m_audio, &AudioEngine::audioCaptured, this, &VoiceCallManager::onCaptured);
    //   connect(m_audio, &AudioEngine::speaking, this, [this](bool active) {
    //       for (auto &cb : m_speakingCallbacks) cb(active);
    //   });
}

bool VoiceCallManager::call(const QString &ip)
{
    if (m_active.contains(ip))
        return true;

    // TODO: if (!m_audio->isRunning() && !m_audio->startCapture()) return false;
    m_active.insert(ip);

    // TODO: auto *peerBuffer = m_audio->mixer()->addPeer(ip);
    // TODO: apply jitter target + VAD from AppSettings, matching Python:
    //   jt = S().get("jitter_frames", 6); pb->TARGET = clamp(jt, 2, 20);

    emit callStarted(ip);
    return true;
}

void VoiceCallManager::hangup(const QString &ip)
{
    m_active.remove(ip);
    // TODO: m_audio->mixer()->removePeer(ip);
    m_net->sendCallEnd(ip);
    m_net->disconnectVoice(ip);
    if (m_active.isEmpty()) {
        // TODO: m_audio->stopAll();
    }
    emit callEnded(ip);
}

void VoiceCallManager::hangupAll()
{
    const auto ips = m_active; // copy — hangup() mutates m_active
    for (const auto &ip : ips)
        hangup(ip);
}

void VoiceCallManager::setMute(bool muted)
{
    m_muted = muted;
    // TODO: m_audio->setMuted(muted);
}

bool VoiceCallManager::toggleMute()
{
    setMute(!m_muted);
    return m_muted;
}

void VoiceCallManager::setVad(bool enabled)
{
    // TODO: m_audio->setVadEnabled(enabled);
    Q_UNUSED(enabled);
}

void VoiceCallManager::subscribeSpeaking(const SpeakingCallback &cb)
{
    m_speakingCallbacks.append(cb);
}

void VoiceCallManager::onCaptured(const QByteArray &data)
{
    // Send mic audio to all active peers.
    for (const auto &ip : std::as_const(m_active))
        m_net->sendVoice(ip, data);
}

void VoiceCallManager::onPeerAudio(const QString &ip, const QByteArray &data)
{
    if (!m_active.contains(ip))
        return;
    // TODO: m_audio->pushPeerAudio(ip, data); — feeds jitter buffer for playback
    Q_UNUSED(data);
}

void VoiceCallManager::cleanup()
{
    hangupAll();
    // TODO: m_audio->cleanup();
}

} // namespace koutnet
