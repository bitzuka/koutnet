#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
"""Carry the old JSON dictionaries over into Gettext catalogs.

The old scheme kept a key per string. Gettext keys on the English text, so
the bridge between them is en.json: look up which key held a given English
sentence, then read that key out of every other language file.

Strings that never make it across are reported rather than dropped
quietly, because a silent gap here shows up as an untranslated interface.
"""

import json
import re
from pathlib import Path

root = Path(__file__).resolve().parent.parent
pot = (root / "po" / "koutnet.pot").read_text()

# Every msgid in the template, with its context when it has one.
entries = []
for block in pot.split("\n\n"):
    ctxt = re.search(r'^msgctxt "(.*)"$', block, re.M)
    mid = re.search(r'^msgid "(.*)"$', block, re.M)
    if not mid or not mid.group(1):
        continue
    entries.append((ctxt.group(1) if ctxt else None, mid.group(1)))

def unescape(s):
    return s.replace('\\"', '"').replace("\\\\", "\\")

def escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')

english = json.loads((root / "i18n" / "en.json").read_text())
by_text = {}
for key, text in english.items():
    by_text.setdefault(text, key)

for path in sorted((root / "i18n").glob("*.json")):
    lang = path.stem
    if lang == "en":
        continue
    words = json.loads(path.read_text())

    lines = [
        'msgid ""',
        'msgstr ""',
        '"Project-Id-Version: koutnet\\n"',
        '"Language: {}\\n"'.format(lang),
        '"MIME-Version: 1.0\\n"',
        '"Content-Type: text/plain; charset=UTF-8\\n"',
        '"Content-Transfer-Encoding: 8bit\\n"',
        "",
    ]

    hits = 0
    misses = []
    for ctxt, mid in entries:
        source = unescape(mid)
        key = by_text.get(source)
        translated = words.get(key) if key else None
        if translated and translated != source:
            hits += 1
        else:
            misses.append(source)
            translated = ""
        if ctxt:
            lines.append('msgctxt "{}"'.format(ctxt))
        lines.append('msgid "{}"'.format(escape(source)))
        lines.append('msgstr "{}"'.format(escape(translated)))
        lines.append("")

    out = root / "po" / (lang + ".po")
    out.write_text("\n".join(lines))
    print("{}: {} of {} translated".format(lang, hits, len(entries)))

print("\nnot carried over (same for every language):")
for text in misses[:15]:
    print("  " + text)
print("  ... {} in total".format(len(misses)))
