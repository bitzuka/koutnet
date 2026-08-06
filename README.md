# KOutNet

![License](https://img.shields.io/badge/License-GPL--3.0--only%20OR%20LicenseRef--KDE--Accepted--GPL-red?style=plastic&logo=gnu&logoColor=white)

An encrypted messenger that does not need a service in the middle. On a local
network or a VPN it finds its peers by itself over UDP and talks to them
directly. Text, voice and files go peer to peer; nothing is uploaded anywhere
first.

Written in C++20 with Qt 6 and Kirigami. Version 0.1, a developer build, not
released yet.

## Status

Developed and tested on Linux. The CMake sets `WIN32_EXECUTABLE` and
`MACOSX_BUNDLE`, and there is no Linux-only code outside the ARP scan, but
neither Windows nor macOS is built or tested by anyone right now. Treat them
as unsupported until somebody says otherwise.

## What works

- **Discovery.** UDP broadcast on port 42000, mDNS multicast on 224.0.0.251, a
  /24 unicast sweep as a fallback where broadcast is filtered, and the Linux
  ARP cache (`/proc/net/arp`). A VPN adapter is just another local interface,
  so the same code covers LAN and tunnel.
- **Encryption.** X25519 ECDH over the presence handshake, identities signed
  with Ed25519, AES-256-GCM on messages and voice frames, PBKDF2-SHA256 for
  the shared group passphrase, HMAC-SHA256 on control packets. A replay window
  over nonce and timestamp, plus per-IP rate limiting, both with hard caps so
  a flood cannot grow them without bound.
- **Key storage.** Private keys and the group passphrase go into KWallet.
  Without a wallet the app keeps them in memory for that session and refuses
  to write them anywhere in plain text.
- **Chat.** One-to-one and group messages, reactions, edit, delete, read
  receipts, typing indicators, per-chat history and unread counts.
- **Voice calls.** TCP with a four-byte length prefix per frame, a per-peer
  jitter buffer, and a mixer that clamps rather than wraps, so several people
  talking at once does not turn into noise.
- **File transfer.** Chunked at 60000 bytes over UDP, reassembled with a size
  cap, a concurrency cap and a TTL on incomplete transfers.
- **The rest of the window.** A notes page, a media player, and a two-column
  layout that folds to one column on a narrow window. The interface is Kirigami
  and kirigami-addons throughout, so it takes its colours from the Plasma colour
  scheme; the only choice the app makes for itself is dark, light or follow the
  desktop. Every user-visible string goes through ki18n; the catalogs are in
  `po/`.
- **K-Server mode, which is Matrix.** Signing in to a homeserver puts your
  joined rooms in the same conversation list as the peers found on the local
  network, marked with a badge and otherwise identical; plain text messages
  read and send, and the session is resumed on the next start without asking
  again. It is libQuotient underneath, the same library NeoChat is built on, so
  a room here and a room there are the same room and this project does not have
  to invent federation. LAN mode is untouched by any of it and stays entirely
  KOutNet's own protocol.

## What does not work yet

- **Encrypted Matrix rooms.** The session is opened with encryption switched
  off, so an end-to-end encrypted room shows one notice saying so instead of
  its messages, and refuses to send rather than posting in the clear. Device
  verification, key backup, attachments, reactions, invites, room creation and
  spaces over Matrix are all still to come, as are Matrix voice calls.

- **Relay / VDS mode.** The transport is written and the reconnect backoff
  works, but no relay host ships with the app, so the mode needs a server you
  supply yourself through `NetworkManager::setRelayServer()`. There is no
  public KOutNet relay.
- **File encryption.** File bytes are chunked and sent as they are. Messages
  and voice are encrypted; files are not, yet.
- **Per-group keys.** Every group currently shares the one app-wide
  passphrase. Per-group keys, or an ECDH fan-out per member, are the plan.
- **Keenly**, the internal-network browser. The file is still in the tree
  (`src/qml/tabs/KeenlyPage.qml`) but it is not in the global drawer, because
  there is nothing behind it yet.

## Dependencies

- **Qt 6.4+**: Core, Gui, Quick, QuickControls2, QuickDialogs2, Multimedia,
  Network. Test as well, if you build the test suite.
- **KDE Frameworks 6.8+**: I18n, I18nQml, Kirigami, Wallet, CoreAddons,
  Config. ColorScheme as well, from 6.6, for the dark/light/system setting.
  The floor is 6.8 rather than 6.0 because `Kirigami.Units.cornerRadius`, which
  the timeline and the pickers round their corners with, was added in 6.8.
- **kirigami-addons 1.8+**: FormCard for the settings and about pages, the list
  delegates for the conversation rows, the maximize component for the image
  viewer and the convergent context menu for the per-message menu. CMake fails
  at configure time with the package name if it is missing.
- **libQuotient 0.9+**, built for Qt 6: the Matrix SDK behind K-Server mode, and
  the same one NeoChat uses. Required, not optional - CMake fails at configure
  time with the package name if it is missing. It brings libolm, qtkeychain for
  Qt 6 and Qt6::Sql along as its own dependencies. Note that it publishes
  `cxx_std_23` as an interface requirement, so linking it raises this project
  past the C++20 it otherwise asks for.
- **OpenSSL** (libcrypto).
- **extra-cmake-modules** 6.8+ and CMake 3.21+.

On Debian or Ubuntu, roughly:

```
qt6-base-dev qt6-declarative-dev qt6-multimedia-dev libkf6config-dev
libkf6coreaddons-dev libkf6i18n-dev libkf6kirigami-dev libkf6kirigamiaddons-dev
libkf6colorscheme-dev libkf6wallet-dev extra-cmake-modules libssl-dev
libquotient-dev
```

On Gentoo, `net-libs/libquotient` (which pulls `dev-libs/olm` and
`dev-libs/qtkeychain[qt6]`) alongside the usual `dev-qt` and `kde-frameworks`
packages.

Distro packages tend to lag behind what `.kde-ci.yml` asks for, so an older
release may not carry new enough Frameworks.

## Build

Configure from the repository root:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

KDECMakeSettings collects executables in the build tree rather than next to
the objects, so it runs without installing:

```bash
./build/bin/KOutNet
```

To install instead:

```bash
cmake --install build
```

## Tests

Three suites, built by default; pass `-DBUILD_TESTING=OFF` to skip them.

- `koutnet-crypto-manager` - key handling, the AES-GCM and passphrase paths,
  replay and rate limiting, and what happens to malformed input.
- `koutnet-network-manager` - the packet path, driven by handing
  `handleDatagram()` the bytes a datagram would have carried. No sockets are
  bound, so it runs on a machine with no network at all.
- `koutnet-file-transfer-handler` - chunk reassembly, the caps, and filename
  handling.

```bash
ctest --test-dir build --output-on-failure
```

They need no display and no `kwalletd`. Running without a wallet is not a
workaround, it is one of the paths under test: the keys have to stay in memory
rather than quietly land in a config file.

## Contributing

Patches, bug reports and questions are welcome. Right now everything happens
on GitHub: <https://github.com/bitzuka/koutnet>.

KOutNet is heading for the KDE Incubator. Once that goes through, development
moves to <https://invent.kde.org> and bugs to
<https://bugs.kde.org/enter_bug.cgi?product=koutnet>, which is already the
address DrKonqi offers after a crash. Until the move, the GitHub tracker is
the one that gets read.

A few things worth knowing before sending a patch:

- The build enforces `QT_NO_KEYWORDS` and `QT_NO_CAST_FROM_ASCII`, so it is
  `Q_SIGNALS`/`Q_SLOTS`/`Q_EMIT` and `QStringLiteral`, not the lowercase
  keywords and bare string literals.
- Formatting is the KDE `.clang-format` at the root.
  `kde_configure_git_pre_commit_hook` installs a pre-commit hook in a git
  checkout that checks it for you.
- Every user-visible string needs `i18nc` with a real context. `Messages.sh`
  regenerates the template.
- Licensing follows REUSE. `reuse lint` runs in CI, and every new file needs
  its SPDX header.

## License

GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL. Full texts are in `LICENSES/`.

## Borrowed code

The emoji picker, the emoji tables and the message text handler come from
[NeoChat](https://invent.kde.org/network/neochat), as does the notification
plumbing. Their authors are credited in the SPDX headers of the files
concerned, and each of those files says which NeoChat file it came from.
Writing any of it a second time would have been work for its own sake.
