// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import koutnet.app

// The window round the emoji panel.
//
// Separate from EmojiPicker because that one is a port and this one is not: the
// panel is NeoChat's, laid out as a ColumnLayout with no opinion about where it
// sits, and this is the small amount of local glue that gives it a size, a place
// and a way out. Keeping them apart is what stops the port from being edited
// every time the popup wants a different width.
QQC2.Popup {
    id: root

    signal picked(string emoji)

    // See the note on Kirigami.Theme in Main.qml: reparented into the window
    // overlay, which starts a theme chain of its own.
    Kirigami.Theme.inherit: false
    Kirigami.Theme.highlightColor: Brand.accent

    parent: QQC2.Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    width: Kirigami.Units.gridUnit * 22
    height: Kirigami.Units.gridUnit * 20
    padding: Kirigami.Units.smallSpacing

    // The search box keeps whatever was typed last otherwise, so the next open
    // shows the tail of somebody else's search instead of a category.
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
