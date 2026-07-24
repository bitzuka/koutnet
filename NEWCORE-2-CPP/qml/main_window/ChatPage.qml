import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: root

    property string peerIp: ""
    property string displayTitle: peerIp
    property var messagesModel: null
    property bool showBackButton: false

    signal returnToListRequested()
    signal sendRequested(string text)
    signal attachRequested(string localFilePath)

    property string replyToText: ""

    title: root.displayTitle
    padding: 0

    actions: [
        Kirigami.Action {
            text: "Назад"
            icon.name: "go-previous"
            visible: root.showBackButton
            onTriggered: root.returnToListRequested()
        }
    ]

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
                    color: Kirigami.Theme.disabledTextColor
                }

                Column {
                    id: contentColumn
                    visible: model.isSystem !== true
                    anchors.right: model.isOwn ? parent.right : undefined
                    anchors.left: model.isOwn ? undefined : parent.left
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: 2
                    width: Math.min(bubble.implicitWidth + 16, messagesList.width * 0.7)

                    // Sender name (only for others, in group-style chats)
                    Label {
                        visible: !model.isOwn && model.sender && model.sender.length > 0
                        text: model.sender
                        font.bold: true
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        color: model.color || Kirigami.Theme.linkColor
                    }

                    Rectangle {
                        id: bubble
                        width: model.isFile === true
                            ? Math.min(240, messagesList.width * 0.7)
                            : Math.min(implicitWidth, messagesList.width * 0.7)
                        implicitWidth: bubbleColumn.implicitWidth + Kirigami.Units.largeSpacing * 2
                        height: bubbleColumn.implicitHeight + Kirigami.Units.smallSpacing * 2
                        radius: 10
                        color: model.isOwn ? Kirigami.Theme.highlightColor : Kirigami.Theme.alternateBackgroundColor

                        ColumnLayout {
                            id: bubbleColumn
                            anchors.fill: parent
                            anchors.margins: Kirigami.Units.smallSpacing
                            spacing: 4

                            // Reply quote
                            Rectangle {
                                Layout.fillWidth: true
                                visible: model.replyToText && model.replyToText.length > 0
                                implicitHeight: replyLabel.implicitHeight + 8
                                color: Qt.rgba(1, 1, 1, 0.08)
                                radius: 4
                                Label {
                                    id: replyLabel
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    text: model.replyToText
                                    elide: Text.ElideRight
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                                    color: model.isOwn ? "white" : Kirigami.Theme.disabledTextColor
                                }
                            }

                            Loader {
                                Layout.fillWidth: true
                                active: model.isFile === true && model.isImage === true
                                sourceComponent: Image {
                                    source: "file://" + model.filePath
                                    fillMode: Image.PreserveAspectFit
                                    width: Math.min(220, messagesList.width * 0.6)
                                    height: width * 0.7
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: !(model.isFile === true && model.isImage === true)
                                text: model.isFile === true ? ("📎 " + model.text) : model.text
                                wrapMode: Text.WordWrap
                                color: model.isOwn ? "white" : Kirigami.Theme.textColor

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton
                                    onClicked: {
                                        root.replyToText = model.text
                                        inputField.forceActiveFocus()
                                    }
                                }
                            }

                            // Reactions row
                            Row {
                                Layout.fillWidth: true
                                spacing: 4
                                visible: model.reactions && model.reactions.length > 0
                                Repeater {
                                    model: root.messagesModel ? messagesList.model : null
                                    delegate: Item { visible: false } // placeholder, replaced below
                                }
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: 4
                                visible: model.reactions && model.reactions.length > 0
                                Repeater {
                                    model: model.reactions || []
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
                            text: "изменено"
                            font.italic: true
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            color: Kirigami.Theme.disabledTextColor
                        }

                        Text {
                            text: model.timeString || ""
                            color: Kirigami.Theme.disabledTextColor
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                        }

                        Text {
                            visible: model.isOwn === true
                            text: model.isRead === true ? "✓✓" : "✓"
                            color: model.isRead === true ? Kirigami.Theme.linkColor : Kirigami.Theme.disabledTextColor
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                        }
                    }
                }
            }
        }

        // Reply preview bar above the input field
        Rectangle {
            Layout.fillWidth: true
            visible: root.replyToText.length > 0
            implicitHeight: 32
            color: Kirigami.Theme.alternateBackgroundColor

            RowLayout {
                anchors.fill: parent
                anchors.margins: 4
                Label {
                    Layout.fillWidth: true
                    text: "↩ " + root.replyToText
                    elide: Text.ElideRight
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                }
                ToolButton {
                    icon.name: "dialog-close"
                    onClicked: root.replyToText = ""
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing

            ToolButton {
                icon.name: "mail-attachment"
                onClicked: fileDialog.open()
            }

            TextField {
                id: inputField
                Layout.fillWidth: true
                placeholderText: "Сообщение..."
                onAccepted: sendButton.clicked()
            }

            Button {
                id: sendButton
                text: "Отправить"
                enabled: inputField.text.length > 0
                onClicked: {
                    root.sendRequested(inputField.text)
                    inputField.text = ""
                    root.replyToText = ""
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: "Прикрепить файл"
        onAccepted: root.attachRequested(selectedFile.toString().replace("file://", ""))
    }
}
