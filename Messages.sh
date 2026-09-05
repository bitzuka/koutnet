#! /usr/bin/env bash
# SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
# Called by the KDE translation scripts to collect every translatable
# string in the project into a single template. The build directory holds
# copies of the QML files, including ones that no longer exist in the
# source tree, and the test suite talks only to itself, so both stay out
# of the search.
#
# The file list goes through a NUL-delimited pipe rather than unquoted
# command substitution, so a path with a space in it cannot silently turn
# into two arguments. The keyword flags are what make xgettext recognise
# the ki18n family - without them it extracts nothing.

find src \( -path '*/build' -o -path 'src/tests' \) -prune -o \
    \( -name '*.cpp' -o -name '*.h' -o -name '*.qml' \) -print0 \
    | sort -z \
    | xargs -0 $XGETTEXT --add-comments=Translators: --from-code=UTF-8 \
        -ki18n:1 -ki18nc:1c,2 -ki18np:1,2 -ki18ncp:1c,2,3 -ktr2i18n:1 \
        -kI18N_NOOP:1 -kI18NC_NOOP:1c,2 \
        -o "$podir/koutnet.pot"
