# KOutNet

![C++](https://img.shields.io/badge/C++-20-blue?style=plastic&logo=c%2B%2B&logoColor=white)
![Qt](https://img.shields.io/badge/Qt-6.4+-41CD52?style=plastic&logo=qt&logoColor=white)
![Kirigami](https://img.shields.io/badge/UI-Kirigami-4A90E2?style=plastic&logo=kde&logoColor=white)
![GPLv3](https://img.shields.io/badge/License-GPLv3-red?style=plastic&logo=gnu&logoColor=white)
> P2P/VPN + VDS messenger under construction

## What is this?
**KOutNet** is a *non-commercial*, completely free messaging platform available for Windows (10+) and Linux (kernel 6.12.xx+). It is being developed with a strong focus on security, usability, and stability. Unlike traditional messengers, KOutNet offers a **2-in-1 architecture**, allowing users to switch between usage scenarios depending on their tasks, providing configuration flexibility.

1. **Autonomous mode without internet or VPN connection (LAN/VPN/P2P)**
Operates exclusively within your local network or tunnel. All features are available locally — from messaging and calls to your KOutNet profile.

2. **Dedicated server mode (LAN/VPS/Dedicated)**
You can use our VDS server (all data remains with you, minimal server load), or you can deploy your own K-Server and configure it however you like.

## What's inside?

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

**QML UI**:
- Contact list (ContactDelegate)
- Chat (ChatPage) — send text/files
- Welcome screen
- Adaptive (compactMode at 480px)

**Protocol.h** — all message types

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
./KOutNet
