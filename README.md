# KOutNet

[![build](https://github.com/bitzuka/koutnet/actions/workflows/build.yml/badge.svg)](https://github.com/bitzuka/koutnet/actions/workflows/build.yml)
[![lint](https://github.com/bitzuka/koutnet/actions/workflows/lint.yml/badge.svg)](https://github.com/bitzuka/koutnet/actions/workflows/lint.yml)
![License](https://img.shields.io/badge/License-GPL--3.0--only%20OR%20LicenseRef--KDE--Accepted--GPL-red?style=plastic&logo=gnu&logoColor=white)

An encrypted messenger that does not need a service in the middle. On a local
network or a VPN it finds its peers by itself over UDP and talks to them
directly. Text, voice and files go peer to peer; nothing is uploaded anywhere
first.

Written in C++20 with Qt 6 and Kirigami. Version 0.1, a pre-alpha developer
build, not released yet.

## Status

Developed and tested on Linux. The CMake sets `WIN32_EXECUTABLE` and
`MACOSX_BUNDLE`, and there is no Linux-only code outside the ARP scan, but
neither Windows nor macOS is built or tested by anyone right now. Treat them
as unsupported until somebody says otherwise.

## What works

- **Discovery.** UDP broadcast on port 42000, mDNS multicast on 224.0.0.251, a
  /24 unicast sweep as a fallback where broadcast is filtered, and the Linux
  ARP cache (`/proc/net/arp`). Peers that none of that reaches can be added
  by address on the settings page: each presence cycle unicasts them a
  packet until they answer. A VPN adapter is just another local interface,
  so the same code covers LAN and tunnel.
- **Encryption.** X25519 ECDH over the presence handshake (libsodium's
  crypto_kx), identities signed with Ed25519, XChaCha20-Poly1305 on messages
  and voice frames, Argon2id for the shared group passphrase, and libsodium's
  crypto_auth (HMAC-SHA-512/256) on control packets. A replay window over nonce
  and timestamp, plus per-IP rate limiting, both with hard caps so a flood
  cannot grow them without bound.
- **File encryption.** File bytes are sealed with the session key before
  chunking, with a tag of its own so a file can never be replayed as a voice
  frame. A peer without a session for the sender still gets the old
  cleartext transfer, flagged as such; a transfer that fails to decrypt is
  dropped rather than saved.
- **Key storage.** Private keys and the group passphrase go into KWallet.
  Without a wallet the app keeps them in memory for that session and refuses
  to write them anywhere in plain text.
- **Chat.** One-to-one and group messages, avatars, reactions, edit, delete,
  read receipts, typing indicators, per-chat history and unread counts. Every
  group seals under a key of its own, kept in KWallet and handed to an invitee
  only under the session with them; groups that predate this keep working on
  the shared passphrase.
- **Voice calls.** TCP with a four-byte length prefix per frame, a per-peer
  jitter buffer, and a mixer that clamps rather than wraps, so several people
  talking at once does not turn into noise. The same channel carries Matrix
  room calls: the room signals them with m.call events, and the media runs
  under a per-call shared key installed in place of the handshake.
- **File transfer.** Chunked at 48000 bytes over UDP, reassembled with a size
  cap, a concurrency cap and a TTL on incomplete transfers.
- **The rest of the window.** A notes page, a media player, and a three-column
  layout - conversation list, conversation, details - that folds to one column
  on a narrow window. The interface is Kirigami
  and kirigami-addons throughout, so it takes its colours from the Plasma colour
  scheme; the only choice the app makes for itself is dark, light or follow the
  desktop. Every user-visible string goes through ki18n and the catalogs are in
  `po/`, though only the English source is complete so far.
- **Matrix mode.** Signing in to a homeserver puts your joined rooms in the
  same conversation list as the peers found on the local network, marked with
  a badge and otherwise identical; plain text and encrypted messages read and
  send, and the session is resumed on the next start without asking again -
  after the homeserver itself has confirmed the stored token still belongs to
  the configured account. Rooms are first-class conversations: reactions,
  edits, redactions, read receipts, typing, invites, creation, kick and ban,
  one-on-one chats, attachments and avatars all work, and a left room leaves
  the list with its history. The session is opened with encryption on, the
  room key backup unlocks with a recovery key, and sessions can be verified
  against another client of yours by comparing emoji - which is also what
  convinces that other client to share its room keys here. It is libQuotient
  underneath, the same library NeoChat is built on, so a room here and a room
  there are the same room and this project does not have to invent federation.
  LAN mode is untouched by any of it and stays entirely KOutNet's own
  protocol.
- **A transport seam.** Every chat is a `ChatBackend` registered in one
  registry under the prefix its address starts with, and the window knows only
  the interface - `transportName()`, the capability flags for calls, edits,
  typing and room-shaped metadata - and nothing about any one of them. LAN mode
  and Matrix implement the seam today; a transport written to it plugs in
  without the window growing a branch.

## What does not work yet

- **Parts of the E2EE support layer.** The session itself is encrypted and
  messages it holds keys for decrypt and send, the key backup unlocks with a
  recovery key, and emoji verification shares room keys from another device.
  What is still missing is cross-signing and the automatic key gossip between
  your own devices. A message this device has no key for shows a notice in
  the timeline instead of its text, and sending to such a room is refused
  when the session's key store never started. Spaces over Matrix are still to
  come.

- **Telegram and Rocket.Chat.** The seam they would plug into is in place and
  their chat-id prefixes are reserved, but no transport has been written for
  either yet.

## Dependencies

- **Qt 6.4+**: Core, Gui, Widgets, Quick, QuickControls2, QuickDialogs2,
  Multimedia, Network, Positioning. Test as well, if you build the test suite.
- **KDE Frameworks 6.8+**: Config, CoreAddons, I18n (I18nQml comes in the same
  package), IdleTime, Kirigami, Notifications, StatusNotifierItem, Wallet.
  ColorScheme as well, from 6.6, for the dark/light/system setting. The floor
  is 6.8 rather than 6.0 because `Kirigami.Units.cornerRadius`, which the
  timeline and the pickers round their corners with, was added in 6.8.
- **kirigami-addons 1.8+**: FormCard for the settings and about pages, the list
  delegates for the conversation rows, the maximize component for the image
  viewer and the convergent context menu for the per-message menu. CMake fails
  at configure time with the package name if it is missing.
- **libQuotient 0.9+**, built for Qt 6: the Matrix SDK, and the same one
  NeoChat uses. Required, not optional - CMake fails at configure time with
  the package name if it is missing. It brings libolm, qtkeychain for Qt 6 and
  Qt6::Sql along as its own dependencies. Note that it publishes `cxx_std_23`
  as an interface requirement, so linking it raises this project past the
  C++20 it otherwise asks for.
- **libsodium 1.0.18+**, found through pkg-config: every cryptographic
  primitive in `CryptoManager` is one of its calls. The application no longer
  links OpenSSL; the crypto test still does, because the forged-peer helper it
  builds attacker keys with has not been ported yet.
- **extra-cmake-modules** 6.8+ and CMake 3.21+.

The CI builds on openSUSE Tumbleweed, and the dependency list there is written
as the exact cmake()/pkgconfig() capabilities `find_package()` asks for, so it
cannot drift out of step:

```
zypper install gcc-c++ cmake ninja pkgconf-pkg-config kf6-extra-cmake-modules \
  'cmake(Qt6Core)' 'cmake(Qt6Gui)' 'cmake(Qt6Qml)' 'cmake(Qt6Quick)' \
  'cmake(Qt6QuickControls2)' 'cmake(Qt6QuickDialogs2)' 'cmake(Qt6Multimedia)' \
  'cmake(Qt6Network)' 'cmake(Qt6Positioning)' 'cmake(Qt6Widgets)' 'cmake(Qt6Test)' \
  'cmake(KF6Config)' \
  'cmake(KF6CoreAddons)' 'cmake(KF6I18n)' 'cmake(KF6IdleTime)' \
  'cmake(KF6Kirigami)' 'cmake(KF6Notifications)' 'cmake(KF6StatusNotifierItem)' \
  'cmake(KF6Wallet)' 'cmake(KF6ColorScheme)' 'cmake(KF6KirigamiAddons)' \
  'cmake(QuotientQt6)' 'pkgconfig(libsodium)' 'pkgconfig(libopenssl)'
```

On Debian or Ubuntu the same set is, roughly:

```
qt6-base-dev qt6-declarative-dev qt6-multimedia-dev qt6-positioning-dev libkf6config-dev
libkf6coreaddons-dev libkf6i18n-dev libkf6idletime-dev libkf6kirigami-dev
libkf6kirigamiaddons-dev libkf6notifications-dev libkf6statusnotifieritem-dev
libkf6colorscheme-dev libkf6wallet-dev extra-cmake-modules libsodium-dev
libssl-dev libquotient-dev
```

Note that the kirigami-addons dev package name varies between releases
(`kirigami-addons-dev` in some), and that the stable releases of these
distributions have so far carried Frameworks too old to configure the project
at all - the CI comment on the matter is blunt. A current KDE neon or a
rolling release is the safer bet.

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

Pass `-DBUILD_TESTING=OFF` when configuring an application-only build.

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

Eight suites, built by default, plus the AppStream check KDECMakeSettings adds;
pass `-DBUILD_TESTING=OFF` to skip the suites.

- `koutnet-crypto-manager` - key handling, the XChaCha20-Poly1305 and
  passphrase paths, replay and rate limiting, and what happens to malformed
  input.
- `koutnet-network-manager` - the packet path, driven by handing
  `handleDatagram()` the bytes a datagram would have carried. No sockets are
  bound, so it runs on a machine with no network at all.
- `koutnet-file-transfer-handler` - chunk reassembly, the caps, and filename
  handling.
- `koutnet-reversed-chat-model` - the proxy that walks the conversation model
  backwards for the timeline, where an off-by-one is a conversation whose
  messages all carry the wrong sender and nothing else would report it.
- `koutnet-chat-backend-registry` - the routing over the chat-id prefix table,
  with ten-line fake backends standing in for the real transports; no sockets,
  no libQuotient and no wallet anywhere near it.
- `koutnet-matrix-wiring` - the Matrix addressing and file-naming path, kept
  free of Quotient types exactly so the decisions worth checking run without a
  homeserver or a network.
- `koutnet-matrix-login` - the server-independent Matrix SSO redirect and
  recovery-key validation helpers.
- `koutnet-chat-address` - the chat-id prefixes used to route each transport.
- `appstreamtest` - validates the installed metainfo against `appstreamcli`.

```bash
ctest --test-dir build --output-on-failure
```

They need no display and no `kwalletd`, and they will not touch one that is
running either: `SecretStore` is switched to an in-memory store for the length
of a test run. That is a fix rather than politeness - there is one wallet per
session and `QStandardPaths` test mode does not move it, so runs before this
wrote their throwaway identity keys into the developer's own keyring and left
them there. Keys staying in memory rather than quietly landing in a config file
is still one of the paths under test.

If an earlier run left entries behind, `tools/wallet-cleanup.sh` lists them and,
given `--remove`, deletes them. `kwallet-query` can read an entry but not remove
one, so it goes through the same D-Bus interface KWallet itself uses.

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
