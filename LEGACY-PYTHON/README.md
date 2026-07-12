<div align="center">


# GoidaPhone NT Server 1.8

**Encrypted P2P LAN & VPN Messenger**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Python](https://img.shields.io/badge/Python-3.11%2B-yellow.svg)](https://python.org)
[![PyQt6](https://img.shields.io/badge/PyQt6-6.7%2B-green.svg)](https://pypi.org/project/PyQt6/)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20Windows-lightgrey.svg)]()
[![Status](https://img.shields.io/badge/Status-%20Released-orange.svg)]()

*No servers. No cloud. No surveillance. Just your network.*

[Features](#features) · [Installation](#installation) · [Usage](#usage) · [Security](#security) 

</div>

---

## What is GoidaPhone NT Server 1.8?

GoidaPhone NT Server 1.8 is a **fully encrypted, serverless peer-to-peer messenger** for local networks and VPN. Every message is end-to-end encrypted using modern cryptography. There are no central servers — your data never leaves your network.

Built with Python and PyQt6, it runs on Linux and Windows with a polished, themeable interface that rivals commercial messengers.

> **NT Server 1.8 is a complete protocol rewrite.** It is **not compatible** with older GoidaPhone versions (0.1 – 1.7544). Those versions use a fundamentally different, unencrypted protocol. NT Server 1.8 is a new ecosystem.

---

## Features

### 💬 Messaging
- Public channel for everyone on the network
- Private encrypted 1-on-1 chats
- Group chats with custom icons and member roles
- Message reactions, replies, forwarding, pinning, editing, deletion
- Disappearing messages (TTL)
- Polls with live vote tracking
- Stickers, GIFs, images, video with inline preview
- File transfer up to any size on LAN
- Emoji-only messages rendered large (1–3 emoji = big, 4+ = normal)
- Drag & drop files and images into chat
- Ctrl+V to paste screenshots directly

### 📞 Voice Calls
- 1-on-1 encrypted voice calls
- Group voice calls (multi-peer mixing)
- Voice activity detection (VAD) — green ring lights up when speaking
- Noise suppression via WebRTC VAD
- Screen sharing
- Camera support
- Floating call window with avatar, timer, mute/speaker controls

### 🎵 Mewa Player
- Full-featured music player with playlist support
- 10-band equalizer with presets (Bass+, Rock, Classical…)
- Online radio with 16 built-in stations (laut.fm, Antenne Bayern, Radio Paradise…)
- Auto-reconnect on stream drop
- Automatic lyrics fetching via lrclib.net (no API key needed)
- Synced LRC lyrics with timestamp stripping

### 🌐 WNS Browser
- Built-in Chromium browser (QtWebEngine)
- Full Chrome User-Agent — Chrome Web Store accessible
- Tabs, bookmarks, history, incognito mode
- Userscripts support (Tampermonkey-style)
- GreasyFork integration

### 🔐 Security — GoidaCRYPTO
GoidaPhone NT Server 1.8 implements a **multi-layer security architecture**:

| Layer | Description |
|-------|-------------|
| **Protocol** | X25519 ECDH key exchange + Ed25519 identity signatures |
| **Encryption** | AES-256-GCM per-session keys, HKDF-SHA256 derivation |
| **Replay protection** | HMAC-SHA256 nonce cache (5000 entries) |
| **SecureVault** | AES-256-GCM local storage, PBKDF2-HMAC-SHA256 (600k iterations) |
| **L2** | Encrypted chat history (vault key) |
| **L3** | Secure RAM wipe on exit |
| **L4** | Stealth Mode (no taskbar/Alt+Tab) |
| **L5** | Screenshot blocking |
| **L6** | Auto clipboard clear (30s) |
| **L7** | Idle lock with PIN |
| **L8** | Anti-keystroke timing analysis |
| **L9** | Auto-expiring messages (TTL) |
| **L10** | Decoy password (fake profile) |
| **L11–L19** | Traffic padding, LAN-only mode, audit log, key rotation, PFS… |
| **L20 🔴** | Paranoid mode — all layers active |

### 🎨 Themes & Customization
- 13 built-in themes including 5 gradient themes:
  **Aurora, Sunset, Ocean, Neon, Sakura**
- Custom theme slots (3 for Premium users)
- Custom app icon and splash screen
- Adjustable UI scale
- Colored nicknames, custom emoji badges

### ⭐ Premium
- 30-day license via 12-digit activation code
- Gold ✦ badge in profile
- Colored nickname
- Custom emoji next to name
- 3 custom theme slots
- Purchase: [t.me/WinoraCompany](https://t.me/WinoraCompany)

### 🛠 Power Features
- **ZLink Terminal** — built-in admin terminal with `/help` commands
- **Quick Setup Wizard** — guided first-run configuration
- **Interactive Tutorial** — 12-step onboarding overlay
- **Auto-updates** — checks GitHub Releases automatically
- **BSOD Death Screen** — styled crash reporter with QR code and auto-restart
- **systemd-style startup log** — Braille spinner, color-coded status lines
- **Notes** — personal scratchpad built into the app
- **Call log** — history of all voice calls with duration

---

## Installation

### Requirements

```
Python 3.11+
PyQt6 >= 6.7.0
PyQt6-WebEngine >= 6.7.0
pyaudio
cryptography
```

### Quick Start

```bash
# Clone the repository
git clone https://github.com/nft1212/GoidaPhone-NT-Server-1.8-OPEN.git
cd GoidaPhone-NT-Server-1.8-OPEN

# Install dependencies
pip install PyQt6 PyQt6-WebEngine pyaudio cryptography

# Run
python3 gdf.py
```

### Linux (Gentoo / Arch / Debian)

```bash
# Gentoo
sudo emerge -av dev-python/pyqt6 dev-python/cryptography portaudio

# Debian/Ubuntu
sudo apt install portaudio19-dev python3-pip
pip install PyQt6 PyQt6-WebEngine pyaudio cryptography --break-system-packages

python3 gdf.py
```

### Windows

```bash
pip install PyQt6 PyQt6-WebEngine pyaudio cryptography
python gdf.py
```

Or download the pre-built `.exe` from [Releases](https://github.com/nft1212/GoidaPhone-NT-Server-1.8-OPEN/releases).

### Optional (enhances functionality)

```bash
# Better radio & audio streaming
sudo emerge media-video/mpv   # Gentoo
# or
sudo apt install mpv          # Debian/Ubuntu

# WebRTC noise suppression
pip install webrtcvad --break-system-packages
```

---

## Usage

### Connecting to others

GoidaPhone automatically discovers peers on the **same LAN or VPN**:

- **Same Wi-Fi / LAN network** → peers appear automatically, no configuration needed
- **Different networks** → use a VPN: [Radmin VPN](https://www.radmin-vpn.com/), [ZeroTier](https://www.zerotier.com/), [Hamachi](https://vpn.net/)

### First Run

1. Launch `gdf.py`
2. Complete the **Quick Setup Wizard** (name, theme, sounds)
3. Go through the **Tutorial** (or skip — it's available anytime in Help → Tutorial)
4. Start chatting

### Slash Commands

| Command | Description |
|---------|-------------|
| `/poll "Question?" Option1 Option2` | Create a live poll |
| `/ttl 60` | Set message auto-delete timer (seconds) |
| `/ttl 0` | Disable auto-delete |
| `/me action` | Sends *username action* |
| `/help` | Show all commands |

---

## Network Protocol

```
PROTOCOL_VERSION = 3
COMPAT_VERSION   = 2   (minimum accepted)

Transport:  UDP (broadcast discovery) + TCP (file transfer)
Port:       17385 (UDP) / 17386 (TCP)  — configurable

Handshake:  X25519 ECDH → HKDF-SHA256 → AES-256-GCM session key
Signing:    Ed25519 identity key signs every packet
Nonces:     12-byte random per message (2^96 space)
Auth tag:   16-byte GCM tag (tampering → instant reject)
```

> **Compatibility note:** GoidaPhone 0.1 – 1.7544 use a completely different unencrypted protocol and **cannot communicate** with NT Server 1.8. NT Server 1.8 is a separate ecosystem.

---

## Project Structure

```
gdf.py              — main application (single file)
icon.png            — application icon
splashq.jpg         — splash screen image
~/.config/GoidaPhone/
    *.ini           — settings (QSettings)
    vault.gcrypto   — GoidaCRYPTO SecureVault (AES-256-GCM)
    received_files/ — received images, videos, files
    history/        — chat history (JSON)
```

---


### ✅ Completed in 1.8.0

- [x] X25519 + Ed25519 + AES-256-GCM E2E encryption
- [x] GoidaCRYPTO SecureVault (20 security layers)
- [x] Voice calls with VAD
- [x] Group calls
- [x] Screen sharing
- [x] Mewa player with EQ and radio
- [x] WNS Chromium browser
- [x] ZLink terminal
- [x] 13 themes including 5 gradient themes
- [x] Premium license system
- [x] Auto-updates via GitHub Releases
- [x] Quick Setup Wizard + 12-step Tutorial
- [x] Polls with live voting
- [x] Disappearing messages
- [x] BSOD crash reporter

### 📅 Release: April 12, 2026

The official stable release of GoidaPhone NT Server 1.8 is scheduled for **April 12, 2026**.

---

## Contributing

Contributions are welcome. Please open an issue before submitting a pull request for major changes.

```bash
git clone https://github.com/nft1212/GoidaPhone-NT-Server-1.8-OPEN.git
cd GoidaPhone-NT-Server-1.8-OPEN
# make your changes to gdf.py
# test locally
# open a pull request
```

---

## License

```
GoidaPhone NT Server 1.8
Copyright (C) 2026  Winora Company

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.
```

---

## Contact

- **Telegram:** [t.me/WinoraCompany](https://t.me/WinoraCompany)
- **GitHub Issues:** [Open an issue](https://github.com/nft1212/GoidaPhone-NT-Server-1.8-OPEN/issues)

---

<div align="center">

Made with ❤️ by **Winora Company** · © 2026

*GoidaPhone NT Server 1.8 — Because your conversations are yours alone.*

</div>
