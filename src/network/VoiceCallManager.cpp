// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Voice call manager (P2P calls, group calls via per-peer jitter buffers)
#include "VoiceCallManager.h"
#include "../core/audio/AudioEngine.h"
#include "../core/security/CryptoManager.h"
#include "NetworkManager.h"

namespace koutnet
{

VoiceCallManager::VoiceCallManager(NetworkManager *net, CryptoManager *crypto, QObject *parent)
    : QObject(parent)
    , m_net(net)
    , m_crypto(crypto)
{
    m_audio = new AudioEngine(this);

    connect(m_audio, &AudioEngine::audioCaptured, this, &VoiceCallManager::onCaptured);
    connect(m_net, &NetworkManager::voiceDataFrom, this, &VoiceCallManager::onPeerAudio);

    // connectVoice() no longer blocks, so a refused or dropped voice socket is the
    // only thing that says a call is over. hangup() ignores IPs it does not hold.
    connect(m_net, &NetworkManager::voiceDisconnected, this, &VoiceCallManager::hangup);
}

bool VoiceCallManager::call(const QString &ip)
{
    if (m_active.contains(ip))
        return true; // already in a call with this peer

    if (!m_audio->running()) {
        if (!m_audio->startCapture())
            return false; // no mic or speaker available
    }

    m_audio->mixer().addPeer(ip);

    if (!m_net->connectVoice(ip)) {
        m_audio->mixer().removePeer(ip);
        if (m_active.isEmpty())
            m_audio->stopAll();
        return false;
    }

    m_active.insert(ip);
    Q_EMIT callStarted(ip);
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

    // The UI hears about it through NetworkManager::callEnded, which the socket
    // side emits when the disconnect lands; a remote hangup arrives the same way.
}

void VoiceCallManager::hangupAll()
{
    const auto ips = m_active; // copy - hangup() mutates m_active
    for (const auto &ip : ips)
        hangup(ip);
}

void VoiceCallManager::setMute(bool muted)
{
    m_muted = muted;
    // Deafened outranks the mute flag: while it is on the microphone stays shut.
    m_audio->setMuted(muted || m_deafened);
}

void VoiceCallManager::setDeafen(bool deafened)
{
    m_deafened = deafened;
    m_audio->setDeafened(deafened);
    // Restores the mute the user chose, which is why m_muted was not overwritten.
    m_audio->setMuted(m_muted || deafened);
}

void VoiceCallManager::setVad(bool enabled)
{
    m_audio->setVadEnabled(enabled);
}

void VoiceCallManager::setAudioInputDevice(const QString &id)
{
    m_audio->setInputDeviceId(id);
}

void VoiceCallManager::setAudioOutputDevice(const QString &id)
{
    m_audio->setOutputDeviceId(id);
}

void VoiceCallManager::setAudioVolume(qreal volume)
{
    m_audio->setVolume(qBound(0.0, volume, 1.0));
}

void VoiceCallManager::onCaptured(const QByteArray &data)
{
    // A peer we hold no session key for is skipped rather than sent cleartext:
    // a gap in the audio is recoverable, a leaked call is not.
    if (!m_crypto)
        return;

    for (const auto &ip : std::as_const(m_active)) {
        const QByteArray toSend = m_crypto->encryptBytes(ip, data);
        if (toSend.isEmpty())
            continue;
        m_net->sendVoice(ip, toSend);
    }
}

void VoiceCallManager::onPeerAudio(const QString &ip, const QByteArray &data)
{
    if (!m_active.contains(ip) || !m_crypto)
        return;

    // decryptBytes says no both for a tampered frame and for a peer we hold no
    // session with, and neither is audio worth playing.
    QByteArray plain;
    if (!m_crypto->decryptBytes(ip, data, &plain))
        return;

    m_audio->pushPeerAudio(ip, plain);
}

} // namespace koutnet
