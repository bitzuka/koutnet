#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 bitzuka <matveypotyzhno@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
"""Fail if a source file picked up a character that is not on a keyboard.

Only C++ and QML sources are checked. i18n/*.json is exempt by design: the
translations are the one place non-ASCII is the actual content. Data inside
sources is exempt too, hence the string-literal skip below.
"""

import glob
import re
import sys

SKIP = ("zapret-linux", "zapret-windows")
LITERAL = re.compile(r'"(?:[^"\\\\]|\\\\.)*"|\'(?:[^\'\\\\]|\\\\.)*\'')

bad = []
for path in sorted(glob.glob("NEWCORE-2-CPP/**/*", recursive=True)):
    if not path.endswith((".cpp", ".h", ".qml")) or any(s in path for s in SKIP):
        continue
    for num, line in enumerate(open(path, encoding="utf-8"), 1):
        stripped = LITERAL.sub("", line)
        offenders = sorted({c for c in stripped if ord(c) > 127})
        if offenders:
            bad.append((path, num, offenders, line.rstrip()))

for path, num, offenders, line in bad:
    print("%s:%d: %s  %s" % (path, num, " ".join(offenders), line.strip()[:70]))

if bad:
    print("\n%d line(s) carry non-ASCII outside string literals." % len(bad))
    sys.exit(1)
print("clean: no non-ASCII outside string literals")
