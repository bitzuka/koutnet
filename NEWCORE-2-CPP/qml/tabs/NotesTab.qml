import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import koutnet.app

// Personal scratchpad tab. In-memory only for now — multiple sheets and
// a Markdown preview are implemented client-side, but nothing here is
// wired to HistoryManager yet (no confirmed API for it), so sheets are
// lost on restart until that's connected.
Item {
    id: root
    readonly property var theme: ThemeManager.colors

    function tr(key) {
        return (Translations.current, Translations.t(key))
    }

    ListModel {
        id: sheetsModel
        ListElement { title: ""; body: "" }
        Component.onCompleted: {
            if (count > 0 && get(0).title === "")
                setProperty(0, "title", root.tr("notes.new_sheet") + " 1")
        }
    }
    property int currentSheet: 0

    Rectangle { anchors.fill: parent; color: theme.bg }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            Kirigami.Heading { level: 3; text: root.tr("tab_main_notes"); color: root.theme.text; Layout.fillWidth: true }
            ToolButton {
                text: previewMode.checked ? root.tr("notes.edit_mode") : root.tr("notes.preview_mode")
                onClicked: previewMode.checked = !previewMode.checked
            }
            property bool dummy: false
        }
        CheckBox { id: previewMode; visible: false }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Repeater {
                model: sheetsModel
                delegate: Rectangle {
                    Layout.preferredHeight: 28
                    Layout.preferredWidth: sheetLabel.implicitWidth + 20
                    radius: 6
                    color: root.currentSheet === index ? root.theme.item_sel : root.theme.bg3
                    Text {
                        id: sheetLabel
                        anchors.centerIn: parent
                        text: model.title
                        color: root.currentSheet === index ? "white" : root.theme.text
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.currentSheet = index
                    }
                }
            }

            ToolButton {
                icon.name: "list-add"
                onClicked: {
                    sheetsModel.append({ title: root.tr("notes.new_sheet") + " " + (sheetsModel.count + 1), body: "" })
                    root.currentSheet = sheetsModel.count - 1
                }
            }
            ToolButton {
                icon.name: "edit-delete"
                enabled: sheetsModel.count > 1
                onClicked: {
                    sheetsModel.remove(root.currentSheet)
                    root.currentSheet = Math.max(0, root.currentSheet - 1)
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !previewMode.checked

            TextArea {
                id: notesArea
                placeholderText: root.tr("notes_writesmth")
                wrapMode: TextArea.Wrap
                color: root.theme.text
                background: Rectangle { color: root.theme.bg3; radius: 6; border.color: root.theme.border }
                text: sheetsModel.count > root.currentSheet ? sheetsModel.get(root.currentSheet).body : ""
                onTextChanged: {
                    if (sheetsModel.count > root.currentSheet)
                        sheetsModel.setProperty(root.currentSheet, "body", text)
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: previewMode.checked

            Text {
                width: parent.width
                wrapMode: Text.Wrap
                textFormat: Text.MarkdownText
                color: root.theme.text
                text: sheetsModel.count > root.currentSheet ? sheetsModel.get(root.currentSheet).body : ""
            }
        }
    }
}
