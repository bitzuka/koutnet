// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import koutnet.app

// Personal scratchpad. In-memory only for now - several sheets and a Markdown
// preview are implemented client-side, but nothing here is wired to HistoryManager
// yet (no confirmed API for it), so sheets are lost on restart until that is
// connected.
//
// The sheet tabs used to be Rectangles with a hand-written radius and a MouseArea
// each. A TabBar is the same thing with keyboard navigation and a focus ring.
Kirigami.Page {
    id: root

    property int currentSheet: 0

    title: i18nc("@title notes tab", "Notes")
    padding: 0

    // See the note on Kirigami.Theme in Main.qml.
    Kirigami.Theme.highlightColor: Brand.accent

    // The number is always there, so the label is one plural msgid rather than a
    // word with a digit stuck on the end.
    function sheetName(number) {
        return i18ncp("@item note sheet name", "Sheet %1", "Sheet %1", number)
    }

    function sheetBody() {
        return sheetsModel.count > root.currentSheet ? sheetsModel.get(root.currentSheet).body : ""
    }

    ListModel {
        id: sheetsModel
        ListElement { title: ""; body: "" }
        Component.onCompleted: {
            if (count > 0 && get(0).title === "")
                setProperty(0, "title", root.sheetName(1))
        }
    }

    actions: [
        Kirigami.Action {
            text: previewToggle.checked ? i18nc("@action:button", "Editor")
                                        : i18nc("@action:button", "Preview")
            icon.name: previewToggle.checked ? "document-edit" : "view-preview"
            onTriggered: previewToggle.checked = !previewToggle.checked
        },
        Kirigami.Action {
            text: i18nc("@action:button add a note sheet", "New sheet")
            icon.name: "list-add"
            onTriggered: {
                sheetsModel.append({ title: root.sheetName(sheetsModel.count + 1), body: "" })
                root.currentSheet = sheetsModel.count - 1
            }
        },
        Kirigami.Action {
            text: i18nc("@action:button remove the current note sheet", "Delete sheet")
            icon.name: "edit-delete"
            enabled: sheetsModel.count > 1
            onTriggered: {
                sheetsModel.remove(root.currentSheet)
                root.currentSheet = Math.max(0, root.currentSheet - 1)
            }
        }
    ]

    // Not a control the user sees; it only remembers which of the two views the
    // action above last asked for.
    QtObject {
        id: previewToggle
        property bool checked: false
    }

    header: QQC2.TabBar {
        currentIndex: root.currentSheet
        onCurrentIndexChanged: root.currentSheet = currentIndex

        Repeater {
            model: sheetsModel

            delegate: QQC2.TabButton {
                required property string title
                text: title
            }
        }
    }

    QQC2.ScrollView {
        anchors.fill: parent
        visible: !previewToggle.checked

        QQC2.TextArea {
            id: notesArea
            placeholderText: i18nc("@info:placeholder", "Write something")
            wrapMode: TextArea.Wrap
            text: root.sheetBody()
            onTextChanged: {
                if (sheetsModel.count > root.currentSheet)
                    sheetsModel.setProperty(root.currentSheet, "body", text)
            }
        }
    }

    QQC2.ScrollView {
        anchors.fill: parent
        visible: previewToggle.checked

        Kirigami.SelectableLabel {
            width: root.width - Kirigami.Units.largeSpacing * 2
            padding: Kirigami.Units.largeSpacing
            wrapMode: Text.Wrap
            textFormat: Text.MarkdownText
            text: root.sheetBody()
        }
    }
}
