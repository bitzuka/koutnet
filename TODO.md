<!--
SPDX-FileCopyrightText: 2026 bitzuka <matveypotyzhno@gmail.com>
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# Roadmap

Things that are planned but not written yet. This file replaces the empty
placeholder directories that used to stand in for them.

## Toward KDE

- Finish the move to KLocalizedString. The custom JSON dictionary still
  drives every string in the interface.
- Add Messages.sh so the KDE translation scripts can find our strings.
- Add .kde-ci.yml.
- Write tests. There are none at all right now, which is the weakest part
  of the case for review.

## Features

- Keenly, the built in browser. QtWebEngine covers the open web; internal
  KOutNet pages need a renderer of their own.
- Group chats and public rooms. Direct messages work, everything above
  them is unbuilt.
- Call interface for one to one and group calls. The audio engine exists,
  the windows around it do not.
- Lock screen and tray icon.
- Avatar, banner and background transfer between peers. Presence only
  carries the profile revision number, so the files never follow.
- Encrypt file transfers with the session key instead of sending them in
  the clear.
- Length prefixed framing on the relay connection. TCP does not preserve
  message boundaries and the current reader assumes it does.
