<!--
SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# Contributing to KOutNet

Thanks for taking the time to contribute. This document covers how to get
a build running, what CI expects from a patch, and where things happen
right now.

Please also read the [Code of Conduct](CODE_OF_CONDUCT.md) - it applies
everywhere in this project, including issues and pull requests.

## Where things happen

Right now everything is on GitHub: <https://github.com/bitzuka/koutnet>.
Issues, pull requests and discussion all go there.

KOutNet is heading for the KDE Incubator. Once that goes through,
development moves to <https://invent.kde.org> and bugs to
<https://bugs.kde.org/enter_bug.cgi?product=koutnet>, which is already the
address DrKonqi offers after a crash. Until the move, the GitHub tracker is
the one that gets read - please don't file duplicate reports upstream at
KDE Bugzilla yet.

## Before you start

For anything beyond a small fix, open an issue first, or comment on an
existing one, so the approach can be discussed before you put time into
code. This is especially true for anything touching `CryptoManager`,
`NetworkManager` or `FileTransferHandler` - changes there deserve a conversation
before a PR.

## Setting up a build

See the [Dependencies](README.md#dependencies) and [Build](README.md#build)
sections of the README for the full package list per distribution. In short:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/bin/KOutNet
```

Pass `-DBUILD_TESTING=OFF` if you only want to build the application and
skip the eight test suites.

## Running the tests

```bash
ctest --test-dir build --output-on-failure
```

The suites need no display and no external daemon - `KeepSecret` runs
against an in-memory store for the duration of a test run, so nothing
touches your real secrets file.

Add or extend a test suite when you touch packet handling, chunk
reassembly, crypto, or anything else with sharp edges. `koutnet-file-transfer-handler`
and `koutnet-crypto-manager` are the ones most patches to those areas will
need to extend.

## Code style

- The build enforces `QT_NO_KEYWORDS` and `QT_NO_CAST_FROM_ASCII`. Use
  `Q_SIGNALS`/`Q_SLOTS`/`Q_EMIT`, not the lowercase keywords, and
  `QStringLiteral`, not bare string literals.
- Formatting follows the KDE `.clang-format` at the repository root, checked
  with clang-format-18 in CI (`clang-format --dry-run --Werror`).
  `kde_configure_git_pre_commit_hook` installs a pre-commit hook in a git
  checkout that formats for you, so you don't have to remember to run it
  by hand.
- Every user-visible string needs `i18nc` with a real, descriptive context -
  not a placeholder. `Messages.sh` regenerates `po/koutnet.pot` from the
  sources.

## Licensing

Licensing follows [REUSE](https://reuse.software/). `reuse lint` runs in
CI, and every new file needs an SPDX header:

```
SPDX-FileCopyrightText: <year> <your name> <<email>>
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
```

Use whatever name and email you want attributed publicly; it does not need
to match your GitHub account.

If you're bringing in code adapted from elsewhere (as the emoji picker,
message text handler and poll classes were from NeoChat), say so in the PR
and keep the SPDX header pointing at the original author, with a comment on
which upstream file it came from.

## Before opening a pull request

CI runs three jobs on every push and PR - `reuse lint`, AppStream/desktop
metadata validation, and `clang-format-18` - and none of them need Qt to
run, so it's worth checking locally first:

```bash
reuse lint
find src \( -name '*.cpp' -o -name '*.h' \) -print0 \
  | xargs -0 clang-format-18 --dry-run --Werror
ctest --test-dir build --output-on-failure
```

A pull request that's still a work in progress is fine to open as a draft -
that's often the best time to get early feedback on direction before
polishing formatting and tests.

## Translations

Translatable strings live in `po/`; `Messages.sh` regenerates the template
from source. If you want to add or improve a language, that's welcome
independently of any code change - open a PR against the relevant `po/<lang>/`
files.
