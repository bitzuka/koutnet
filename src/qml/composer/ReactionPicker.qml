// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import koutnet.app

// Six and not the whole emoji keyboard: reacting is meant to be one click, and a
// grid of two thousand is the picker the composer already has.
QQC2.Popup {
    id: root

    // Carried here rather than passed to the signal, because the popup outlives the
    // click that opened it and the row it belongs to has to survive with it.
    property int targetRow: -1

    signal chosen(int row, string emoji)

    readonly property var quickEmojis: ["👍", "❤️", "😂", "😮", "😢", "🔥"]

    // See Main.qml: reparented into the overlay, which is its own theme chain.
    Kirigami.Theme.inherit: false
    Kirigami.Theme.highlightColor: Brand.accent

    parent: QQC2.Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: Kirigami.Units.largeSpacing

    function openFor(row) {
        root.targetRow = row
        root.open()
    }

    contentItem: GridLayout {
        // Two rows of three now that the picker is exactly the six; four columns
        // left a row of four over a row of two.
        columns: 3
        columnSpacing: Kirigami.Units.smallSpacing
        rowSpacing: Kirigami.Units.smallSpacing

        Repeater {
            model: root.quickEmojis

            delegate: QQC2.ToolButton {
                id: pick

                required property string modelData

                implicitWidth: Kirigami.Units.gridUnit * 2
                implicitHeight: Kirigami.Units.gridUnit * 2

                Accessible.name: pick.modelData

                onClicked: {
                    root.chosen(root.targetRow, pick.modelData)
                    root.close()
                }

                // A contentItem of its own, because the button's default label draws
                // the character in the interface font and a desktop UI font has no
                // emoji in it - the six choices came out as a row of empty buttons.
                contentItem: QQC2.Label {
                    text: pick.modelData
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.family: "emoji"
                    font.pixelSize: Math.round(height - Kirigami.Units.largeSpacing)
                    elide: Text.ElideNone
                    wrapMode: Text.NoWrap
                    textFormat: Text.PlainText
                    color: Kirigami.Theme.textColor
                }
            }
        }
    }
}
