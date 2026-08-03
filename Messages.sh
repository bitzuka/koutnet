#!/bin/sh
# SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
# Called by the KDE translation scripts to collect every translatable
# string in the project into a single template. The build directory holds
# copies of the QML files, including ones that no longer exist in the
# source tree, so it stays out of the search.
$XGETTEXT $(find NEWCORE-2-CPP -path '*/build' -prune -o \
    \( -name '*.cpp' -o -name '*.h' -o -name '*.qml' \) -print | sort) \
    -o "$podir/koutnet.pot"
