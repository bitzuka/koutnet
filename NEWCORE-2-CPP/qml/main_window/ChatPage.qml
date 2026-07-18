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
            verticalLayoutDirection: ListView.BottomToTop
            clip: true
            model: root.messagesModel

            delegate: Item {
                width: messagesList.width
                height: bubble.height + Kirigami.Units.smallSpacing

                Rectangle {
                    id: bubble
                    width: model.isFile === true
                        ? Math.min(240, messagesList.width * 0.7)
                        : Math.min(label.implicitWidth + Kirigami.Units.largeSpacing * 2, messagesList.width * 0.7)
                    height: model.isFile === true && model.isImage === true
                        ? 160
                        : label.implicitHeight + Kirigami.Units.smallSpacing * 2
                    radius: 10
                    color: model.fromMe ? Kirigami.Theme.highlightColor : Kirigami.Theme.alternateBackgroundColor
                    anchors.right: model.fromMe ? parent.right : undefined
                    anchors.left: model.fromMe ? undefined : parent.left
                    anchors.margins: Kirigami.Units.smallSpacing

                    Loader {
                        anchors.centerIn: parent
                        active: model.isFile === true && model.isImage === true
                        sourceComponent: Image {
                            source: "file://" + model.filePath
                            fillMode: Image.PreserveAspectFit
                            width: Math.min(220, messagesList.width * 0.6)
                            height: width * 0.7
                        }
                    }

                    Text {
                        id: label
                        anchors.centerIn: parent
                        visible: !(model.isFile === true && model.isImage === true)
                        text: model.isFile === true ? ("📎 " + model.text) : model.text
                        wrapMode: Text.WordWrap
                        color: model.fromMe ? "white" : Kirigami.Theme.textColor
                    }
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
