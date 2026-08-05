// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import org.kde.kirigami as Kirigami
import koutnet.app

// Keenly, the WNS (internal network) browser. This comment is the one statement of
// what it is meant to be: a from-scratch rendering engine with its own layout and
// paint pipeline, for the custom markup KOutNet peers serve each other. It is not
// a Qt WebEngine wrapper and it does not target the open web, which is why
// QtWebEngine is not a dependency of this project.
//
// Nothing behind it works yet, so Main.qml leaves it out of the global drawer. The
// file stays in the QML module so it keeps getting compiled and linted instead of
// rotting out of tree. Put the entry back when it renders a page.
Kirigami.Page {
    id: root

    // Not translated: it is the name of the thing.
    title: "Keenly"

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        icon.name: "internet-web-browser"
        text: "Keenly"
        explanation: i18nc("@info", "A custom rendering engine is the next step")
    }
}
