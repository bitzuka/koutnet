// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
pragma Singleton

import QtQuick

// The only colour this application owns. Everything else is the Plasma colour
// scheme read through Kirigami.Theme, so a hex value anywhere but here is a bug.
QtObject {
    // Taken down until white text on it clears 4.5:1; it is read against.
    readonly property color accent: "#c6398a"
}
