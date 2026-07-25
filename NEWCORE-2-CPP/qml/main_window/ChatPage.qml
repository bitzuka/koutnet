import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Window
import org.kde.kirigami as Kirigami
import koutnet.app

Kirigami.Page {
    id: root

    property string peerIp: ""
    property string displayTitle: peerIp
    property var messagesModel: null
    property bool showBackButton: false

    signal returnToListRequested()
    signal sendRequested(string text)
    signal attachRequested(string localFilePath)
    signal callRequested()
    // ChatModel currently has no delete/forward API (see ChatModel.h —
    // only sendMessage/sendFile/receiveMessage/receiveFile/toggleReaction/
    // markOwnMessagesRead/markAllRead exist). These bubble up so Main.qml
    // can show an honest "not implemented yet" message instead of this
    // page pretending to call a C++ method that doesn't exist.
    signal forwardRequested(int index)
    signal deleteRequested(int index)

    property string replyToText: ""
    readonly property var theme: ThemeManager.colors

    // Quick-reaction palette — mirrors legacy chat reaction picker.
    readonly property var quickEmojis: ["👍", "❤️", "😂", "😮", "😢", "🔥"]

    title: root.displayTitle
    padding: 0

    background: Rectangle { color: root.theme.bg }

    actions: [
        Kirigami.Action {
            text: Translations.t("chat.back")
            icon.name: "go-previous"
            visible: root.showBackButton
            onTriggered: root.returnToListRequested()
        },
        Kirigami.Action {
            text: Translations.t("call.button")
            icon.name: "call-start"
            onTriggered: root.callRequested()
        }
    ]

    // ── Full-screen image viewer — click on a photo bubble opens this. ──
    Window {
        id: imageViewer
        visible: false
        flags: Qt.Dialog
        modality: Qt.WindowModal
        color: "black"
        width: Math.min(Screen.width * 0.9, 1200)
        height: Math.min(Screen.height * 0.9, 900)
        property string source: ""
        title: Translations.t("chat.image_viewer_title")

        Image {
            anchors.fill: parent
            anchors.margins: 8
            source: imageViewer.source
            fillMode: Image.PreserveAspectFit
        }

        MouseArea {
            anchors.fill: parent
            onClicked: imageViewer.close()
        }

        Shortcut {
            sequence: "Escape"
            onActivated: imageViewer.close()
        }
    }

    // Invisible helper for clipboard copy — QtQuick has no bare "set
    // clipboard text" call without extra C++ plumbing, but TextEdit's
    // copy() does exactly this via the normal Qt clipboard, so this avoids
    // inventing a fake clipboardHelper singleton that doesn't exist in the
    // C++ side.
    TextEdit {
        id: clipboardHelper
        visible: false
        function copyText(str) {
            text = str
            selectAll()
            copy()
        }
    }

    // ── Reused per-message context menu: reply / copy / forward /
    //    react / delete. Opened on right-click for both text and image
    //    bubbles (legacy used exactly this set). Forward/delete are wired
    //    as signals since ChatModel doesn't expose those operations yet. ──
    Menu {
        id: messageMenu
        property int msgIndex: -1
        property string msgText: ""

        MenuItem {
            text: Translations.t("msg_reply")
            onTriggered: {
                root.replyToText = messageMenu.msgText
                inputField.forceActiveFocus()
            }
        }
        MenuItem {
            text: Translations.t("msg_copy")
            onTriggered: {
                if (messageMenu.msgText.length > 0)
                    clipboardHelper.copyText(messageMenu.msgText)
            }
        }
        MenuItem {
            text: Translations.t("msg_forward")
            onTriggered: root.forwardRequested(messageMenu.msgIndex)
        }
        Menu {
            title: Translations.t("msg_reactions")
            Instantiator {
                model: root.quickEmojis
                delegate: MenuItem {
                    text: modelData
                    onTriggered: root.messagesModel.toggleReaction(messageMenu.msgIndex, modelData, "me")
                }
                onObjectAdded: (index, object) => parent.insertItem(index, object)
                onObjectRemoved: (index, object) => parent.removeItem(object)
            }
        }
        MenuSeparator {}
        MenuItem {
            text: Translations.t("msg_delete")
            onTriggered: root.deleteRequested(messageMenu.msgIndex)
        }
    }

    function openMessageMenu(index, text) {
        messageMenu.msgIndex = index
        messageMenu.msgText = text
        messageMenu.popup()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ListView {
            id: messagesList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.messagesModel

            onCountChanged: Qt.callLater(positionViewAtEnd)
            Component.onCompleted: positionViewAtEnd()

            delegate: Item {
                width: messagesList.width
                height: model.isSystem ? sysLabel.implicitHeight + 12 : contentColumn.height + Kirigami.Units.smallSpacing

                Label {
                    id: sysLabel
                    visible: model.isSystem === true
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: model.text
                    font.italic: true
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    color: root.theme.text_dim
                }

                Column {
                    id: contentColumn
                    visible: model.isSystem !== true
                    anchors.right: model.isOwn ? parent.right : undefined
                    anchors.left: model.isOwn ? undefined : parent.left
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: 2
                    width: Math.min(bubble.implicitWidth + 16, messagesList.width * 0.7)

                    Label {
                        visible: !model.isOwn && model.sender && model.sender.length > 0
                        text: model.sender
                        font.bold: true
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        color: model.color || root.theme.accent
                    }

                    Rectangle {
                        id: bubble
                        width: model.isFile === true
                            ? Math.min(240, messagesList.width * 0.7)
                            : Math.min(implicitWidth, messagesList.width * 0.7)
                        implicitWidth: bubbleColumn.implicitWidth + Kirigami.Units.largeSpacing * 2
                        height: bubbleColumn.implicitHeight + Kirigami.Units.smallSpacing * 2
                        radius: 10
                        color: model.isOwn ? root.theme.msg_own : root.theme.msg_other
                        border.color: root.theme.border
                        border.width: 1

                        ColumnLayout {
                            id: bubbleColumn
                            anchors.fill: parent
                            anchors.margins: Kirigami.Units.smallSpacing
                            spacing: 4

                            Rectangle {
                                Layout.fillWidth: true
                                visible: model.replyToText && model.replyToText.length > 0
                                implicitHeight: replyLabel.implicitHeight + 8
                                color: Qt.rgba(1, 1, 1, 0.08)
                                radius: 4
                                border.color: root.theme.accent
                                border.width: 1
                                Label {
                                    id: replyLabel
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    text: model.replyToText
                                    elide: Text.ElideRight
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                                    color: root.theme.text_dim
                                }
                            }

                            // Image bubble — left click opens the full-screen
                            // viewer, right click opens the same message
                            // context menu as text bubbles.
                            Loader {
                                Layout.fillWidth: true
                                active: model.isFile === true && model.isImage === true
                                sourceComponent: Item {
                                    width: Math.min(220, messagesList.width * 0.6)
                                    height: width * 0.7

                                    Image {
                                        anchors.fill: parent
                                        source: "file://" + model.filePath
                                        fillMode: Image.PreserveAspectFit
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: function(mouse) {
                                            if (mouse.button === Qt.RightButton) {
                                                root.openMessageMenu(index, model.text || "")
                                            } else {
                                                imageViewer.source = "file://" + model.filePath
                                                imageViewer.show()
                                                imageViewer.raise()
                                            }
                                        }
                                    }
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: !(model.isFile === true && model.isImage === true)
                                text: model.isFile === true ? ("📎 " + model.text) : model.text
                                wrapMode: Text.WordWrap
                                color: root.theme.text

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton
                                    onClicked: root.openMessageMenu(index, model.text || "")
                                }
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: 4
                                visible: !!(model && model.reactions && model.reactions.length > 0)
                                Repeater {
                                    model: (model && model.reactions) ? model.reactions : []
                                    delegate: Rectangle {
                                        radius: 10
                                        color: Qt.rgba(0, 0, 0, 0.25)
                                        implicitWidth: reactLabel.implicitWidth + 12
                                        implicitHeight: 22
                                        Label {
                                            id: reactLabel
                                            anchors.centerIn: parent
                                            text: modelData.emoji + " " + modelData.count
                                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                                            color: "white"
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: root.messagesModel.toggleReaction(index, modelData.emoji, "me")
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Row {
                        anchors.right: model.isOwn ? parent.right : undefined
                        anchors.left: model.isOwn ? undefined : parent.left
                        spacing: 3

                        Label {
                            visible: model.isEdited === true
                            text: Translations.t("edited_label")
                            font.italic: true
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            color: root.theme.text_dim
                        }

                        Text {
                            text: model.timeString || ""
                            color: root.theme.text_dim
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                        }

                        Text {
                            visible: model.isOwn === true
                            text: model.isRead === true ? "✓✓" : "✓"
                            color: model.isRead === true ? root.theme.accent : root.theme.text_dim
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            visible: root.replyToText.length > 0
            implicitHeight: 32
            color: root.theme.header_bg

            RowLayout {
                anchors.fill: parent
                anchors.margins: 4
                Label {
                    Layout.fillWidth: true
                    text: "↩ " + root.replyToText
                    elide: Text.ElideRight
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    color: root.theme.text
                }
                ToolButton {
                    icon.name: "dialog-close"
                    onClicked: root.replyToText = ""
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: root.theme.border
        }

        // Input bar — themed background/border/placeholder (was left at
        // the default Controls style, which is why it ignored theme
        // switches before).
        Rectangle {
            Layout.fillWidth: true
            color: root.theme.bg2
            implicitHeight: inputRow.implicitHeight + Kirigami.Units.smallSpacing * 2

            RowLayout {
                id: inputRow
                anchors.fill: parent
                anchors.margins: Kirigami.Units.smallSpacing

                ToolButton {
                    icon.name: "mail-attachment"
                    onClicked: fileDialog.open()
                }

                TextField {
                    id: inputField
                    Layout.fillWidth: true
                    placeholderText: Translations.t("chat.placeholder")
                    color: root.theme.text
                    placeholderTextColor: root.theme.text_dim
                    selectionColor: root.theme.accent
                    leftPadding: 10
                    rightPadding: 10
                    onAccepted: sendButton.clicked()

                    background: Rectangle {
                        radius: 8
                        color: root.theme.bg3
                        border.width: 1
                        border.color: inputField.activeFocus ? root.theme.accent : root.theme.border
                    }
                }

                Button {
                    id: sendButton
                    text: Translations.t("chat.send")
                    enabled: inputField.text.length > 0
                    onClicked: {
                        root.sendRequested(inputField.text)
                        inputField.text = ""
                        root.replyToText = ""
                    }

                    contentItem: Text {
                        text: sendButton.text
                        color: sendButton.enabled ? "white" : root.theme.text_dim
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 8
                        color: sendButton.enabled
                            ? (sendButton.down ? root.theme.accent2 : root.theme.accent)
                            : root.theme.bg3
                        border.width: sendButton.enabled ? 0 : 1
                        border.color: root.theme.border
                    }
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: Translations.t("chat.attach_title")
        onAccepted: root.attachRequested(selectedFile.toString().replace("file://", ""))
    }
}
