// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import koutnet.app

// Separate from EmojiPicker because that one is a port and this one is not: keeping
// them apart is what stops the port from being edited every time the popup wants a
// different width.
QQC2.Popup {
    id: root

    signal picked(string emoji)

    // See Main.qml: reparented into the overlay, which is its own theme chain.
    Kirigami.Theme.inherit: false
    Kirigami.Theme.highlightColor: Brand.accent

    parent: QQC2.Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    width: Kirigami.Units.gridUnit * 22
    height: Kirigami.Units.gridUnit * 20
    padding: Kirigami.Units.smallSpacing

    // Otherwise the next open shows the tail of somebody else's search.
    onOpened: {
        picker.clearSearchField()
        picker.forceActiveFocus()
    }

    contentItem: EmojiPicker {
        id: picker
        onChosen: (emoji) => {
            root.picked(emoji)
            root.close()
        }
    }
}
