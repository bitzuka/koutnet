// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import koutnet.app

// Keenly, the WNS (internal network) browser. This comment is the one
// statement of what it is meant to be: a from-scratch rendering engine with
// its own layout and paint pipeline, for the custom markup KOutNet peers
// serve each other. It is not a Qt WebEngine wrapper and it does not target
// the open web, which is why QtWebEngine is not a dependency of this project.
//
// Nothing behind it works yet, so Main.qml leaves it out of the tab strip.
// The file stays in the QML module so it keeps getting compiled and linted
// instead of rotting out of tree. Put the tab back when it renders a page.
Item {
    id: root
    readonly property var theme: ThemeManager.colors

    Rectangle { anchors.fill: parent; color: theme.bg }

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        text: "Keenly"
        explanation: i18nc("@info", "A custom rendering engine is the next step")
        icon.name: "internet-web-browser"
    }
}
