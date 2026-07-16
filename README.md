# KOutNet

![C++](https://img.shields.io/badge/C++-20-blue?style=plastic&logo=c%2B%2B&logoColor=white)
![Qt](https://img.shields.io/badge/Qt-6.4+-41CD52?style=plastic&logo=qt&logoColor=white)
![Kirigami](https://img.shields.io/badge/UI-Kirigami-4A90E2?style=plastic&logo=kde&logoColor=white)
![GPLv3](https://img.shields.io/badge/License-GPLv3-red?style=plastic&logo=gnu&logoColor=white)
> P2P/VPN + VDS messenger under construction

## What's inside

**NetworkManager** — networking core:
- UDP broadcasts (224.0.0.251, LAN, /24 scanning)
- Presence + ECDH handshake (CryptoManager)
- HMAC packet signing (ECDH session)
- TCP server for voice
- Relay mode (TODO: not working, host not set)
- File transfer chunked at 60KB

**CryptoManager** — cryptography:
- ECDH (secp256k1) — handshake via presence
- AES-256-GCM for messages
- HMAC-SHA256 for packets
- Replay attack protection (nonce + timestamp)
- Rate limiting (custom, not Qt)

**AudioEngine** — audio:
- Streaming capture/playback via QAudio
- Mixer (AudioMixer)
- Jitter buffer (in VoiceCallManager)
- Opus encoding (TODO: not connected)

**QML UI**:
- Contact list (ContactDelegate)
- Chat (ChatPage) — send text/files
- Splash (SplashScreen) — 2.2 seconds
- Adaptive (compactMode at 480px)

**Protocol.h** — all message types

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
./KOutNet
