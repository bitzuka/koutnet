// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import koutnet.app

// WNS (internal network browser) tab. Layout placeholder - legacy
// NetScape rendered custom markup by hand; the plan is a from-scratch
// rendering engine (own layout/paint pipeline, not a Qt WebEngine
// wrapper), wired in separately.
Item {
    id: root
    readonly property var theme: ThemeManager.colors

    Rectangle { anchors.fill: parent; color: theme.bg }

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        text: "Keenly"
        explanation: i18n("A custom rendering engine is the next step")
        icon.name: "internet-web-browser"
    }
}
