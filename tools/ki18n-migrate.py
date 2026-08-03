#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
"""Rewrite the custom translation calls in QML into ki18n calls.

The old scheme looked a key up in i18n/en.json at runtime. Gettext, which
ki18n is built on, uses the English text itself as the key, so every call
site has to carry the sentence instead of a name for it. This script does
that substitution and drops the per file tr() helper that went with the
old scheme.

Names of our own subsystems are not sentences and must not reach a
translator, so they are written out as plain strings.
"""

import json
import re
import sys
from pathlib import Path

# Proper nouns. A translator offered "Violla" will eventually translate it.
NOT_TRANSLATABLE = {"KOutNet", "Violla", "Keenly"}

HELPER = re.compile(
    r"\n[ \t]*function tr\(key\) \{\n"
    r"[ \t]*return \(Translations\.current, Translations\.t\(key\)\)\n"
    r"[ \t]*\}\n+"
)

CALL = re.compile(r"\b(?:root\.)?tr\(\s*\"([^\"]+)\"\s*\)")


def quoted(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main(argv):
    root = Path(__file__).resolve().parent.parent
    catalog = json.loads((root / "i18n" / "en.json").read_text())

    missing = set()
    touched = 0

    for name in argv:
        path = Path(name)
        before = path.read_text()

        def swap(match):
            key = match.group(1)
            if key not in catalog:
                missing.add(key)
                return match.group(0)
            text = catalog[key]
            if text in NOT_TRANSLATABLE:
                return quoted(text)
            return "i18n(" + quoted(text) + ")"

        after, count = CALL.subn(swap, before)
        after = HELPER.sub("\n", after)
        after = re.sub(r"\n{3,}", "\n\n", after)

        if after != before:
            path.write_text(after)
            touched += 1
            print("{}: {} calls".format(path, count))

    if missing:
        print("\nkeys absent from en.json, left alone:")
        for key in sorted(missing):
            print("  " + key)

    print("\nfiles changed: {}".format(touched))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
