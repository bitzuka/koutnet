// KOutNet — Voice call manager (P2P calls, group calls via per-peer jitter buffers)
// Ported from gdf_network.py (VoiceCallManager, NT Server 1.8) -> C++/Qt6
#include "VoiceCallManager.h"
#include "NetworkManager.h"
#include "../core/audio/AudioEngine.h"

namespace koutnet {

VoiceCallManager::VoiceCallManager(NetworkManager *net, QObject *parent)
    : QObject(parent), m_net(net)
{
    m_audio = new AudioEngine(this);

    connect(m_audio, &AudioEngine::audioCaptured, this, &VoiceCallManager::onCaptured);
    connect(m_audio, &AudioEngine::speaking, this, [this](bool isSpeaking) {
        for (const auto &cb : std::as_const(m_speakingCallbacks))
            cb(isSpeaking);
    });

    // Route incoming voice bytes from the network layer, keyed by peer IP,
    // into that peer's jitter buffer via AudioEngine.
    connect(m_net, &NetworkManager::voiceDataFrom, this, &VoiceCallManager::onPeerAudio);
}

bool VoiceCallManager::call(const QString &ip)
{
    if (m_active.contains(ip))
        return true; // already in a call with this peer

    if (!m_audio->running()) {
        if (!m_audio->startCapture())
            return false; // no mic/speaker available — matches legacy PYAUDIO_AVAILABLE=false path
    }

    m_audio->mixer().addPeer(ip);

    if (!m_net->connectVoice(ip)) {
        // Voice TCP connect failed — don't leave a half-open call.
        m_audio->mixer().removePeer(ip);
        if (m_active.isEmpty())
            m_audio->stopAll();
        return false;
    }

    m_active.insert(ip);
    emit callStarted(ip);
    return true;
}

void VoiceCallManager::hangup(const QString &ip)
{
    if (!m_active.contains(ip))
        return;

    m_active.remove(ip);
    m_audio->mixer().removePeer(ip);
    m_net->disconnectVoice(ip);

    if (m_active.isEmpty())
        m_audio->stopAll();

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
    m_audio->setMuted(muted);
}

bool VoiceCallManager::toggleMute()
{
    setMute(!m_muted);
    return m_muted;
}

void VoiceCallManager::setVad(bool enabled)
{
    m_audio->setVadEnabled(enabled);
}

void VoiceCallManager::subscribeSpeaking(const SpeakingCallback &cb)
{
    m_speakingCallbacks.append(cb);
}

void VoiceCallManager::cleanup()
{
    hangupAll();
    m_audio->cleanup();
}

void VoiceCallManager::onCaptured(const QByteArray &data)
{
    // Send mic audio to every active peer.
    for (const auto &ip : std::as_const(m_active))
        m_net->sendVoice(ip, data);
}

void VoiceCallManager::onPeerAudio(const QString &ip, const QByteArray &data)
{
    // Incoming audio from a peer -> push into their jitter buffer.
    if (m_active.contains(ip))
        m_audio->pushPeerAudio(ip, data);
}

} // namespace koutnet
