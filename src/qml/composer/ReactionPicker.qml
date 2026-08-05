// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import koutnet.app

// The short list of reactions, plus a way to add a picture as one.
//
// Six and not the whole emoji keyboard: reacting is meant to be one click, and
// a grid of two thousand is the picker the composer already has.
QQC2.Popup {
    id: root

    // Which message the reaction is going on. Carried here rather than passed to
    // the signal by the caller, because the popup outlives the click that opened
    // it and the row it belongs to has to survive with it.
    property int targetRow: -1

    signal chosen(int row, string emoji)

    // Client-side only for now: not persisted, not sent to the peer. Making a
    // custom emoji durable and shared needs a store for the picture itself,
    // which is a piece of work of its own.
    property var customEmojis: []

    readonly property var quickEmojis: ["👍", "❤️", "😂", "😮", "😢", "🔥"]

    // See the note on Kirigami.Theme in Main.qml: this is reparented into the
    // window overlay, which starts a theme chain of its own.
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
        columns: 4
        columnSpacing: Kirigami.Units.smallSpacing
        rowSpacing: Kirigami.Units.smallSpacing

        Repeater {
            model: root.quickEmojis.concat(root.customEmojis)

            delegate: QQC2.ToolButton {
                required property var modelData

                implicitWidth: Kirigami.Units.gridUnit * 2
                implicitHeight: Kirigami.Units.gridUnit * 2

                // A picture reaction is a file path behind an "img:" marker; a
                // plain one is the character itself.
                readonly property bool isPicture: typeof modelData === "string" && modelData.indexOf("img:") === 0

                text: isPicture ? "" : modelData
                icon.source: isPicture ? modelData.substring(4) : ""
                display: isPicture ? QQC2.AbstractButton.IconOnly : QQC2.AbstractButton.TextOnly

                onClicked: {
                    root.chosen(root.targetRow, modelData)
                    root.close()
                }
            }
        }

        QQC2.ToolButton {
            implicitWidth: Kirigami.Units.gridUnit * 2
            implicitHeight: Kirigami.Units.gridUnit * 2
            display: QQC2.AbstractButton.IconOnly
            icon.name: "list-add"
            text: i18nc("@action:button add a picture to use as a reaction", "Add an emoji")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            QQC2.ToolTip.text: text
            onClicked: customEmojiDialog.open()
        }
    }

    FileDialog {
        id: customEmojiDialog
        title: i18nc("@title:window", "Choose an image for the emoji")
        nameFilters: [i18nc("@item:inlistbox file dialog filter, keep the glob patterns",
                            "Images (*.png *.jpg *.jpeg *.webp *.gif)")]
        onAccepted: {
            // Reassigned rather than pushed to: a JS array mutated in place
            // does not notify, and the grid would not redraw.
            root.customEmojis = root.customEmojis.concat(["img:" + selectedFile.toString()])
        }
    }
}
