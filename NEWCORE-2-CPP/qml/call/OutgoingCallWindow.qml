import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import koutnet.app

// Frameless "Calling..." overlay with pulsing rings around the avatar.
// Port of legacy OutgoingCallWindow (paintEvent rings -> QML animated
// Rectangles; much cheaper than manual repainting).
Window {
    id: root
    width: 340
    height: 520
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"

    property string peerName: ""
    property string peerIp: ""
    property int elapsedSeconds: 0

    readonly property var theme: ThemeManager.colors

    signal cancelled()

    Component.onCompleted: {
        x = Screen.width / 2 - width / 2
        y = Screen.height / 2 - height / 2
    }

    Rectangle {
        id: card
        anchors.fill: parent
        radius: 24
        border.color: root.theme.border
        border.width: 1
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: root.theme.bg3 }
            GradientStop { position: 1.0; color: root.theme.bg }
        }

        MouseArea {
            anchors.fill: parent
            property point dragOrigin
            onPressed: dragOrigin = Qt.point(mouseX, mouseY)
            onPositionChanged: {
                if (pressed) {
                    root.x += mouseX - dragOrigin.x
                    root.y += mouseY - dragOrigin.y
                }
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: 50
            anchors.bottomMargin: 40
            anchors.leftMargin: 30
            anchors.rightMargin: 30
            spacing: 0

            // Avatar with pulse rings
            Item {
                Layout.alignment: Qt.AlignHCenter
                width: 140
                height: 140

                Repeater {
                    model: [1.0, 1.4, 1.8]
                    delegate: Rectangle {
                        anchors.centerIn: parent
                        radius: width / 2
                        color: "transparent"
                        border.width: 2
                        border.color: Qt.rgba(
                            Qt.color(root.theme.accent).r,
                            Qt.color(root.theme.accent).g,
                            Qt.color(root.theme.accent).b,
                            0.0)

                        width: 140
                        height: 140

                        SequentialAnimation on width {
                            loop: Animation.Infinite
                            NumberAnimation { from: 140; to: 140 + modelData * 60; duration: 1400; easing.type: Easing.OutQuad }
                            PauseAnimation { duration: 0 }
                        }
                        SequentialAnimation on height {
                            loop: Animation.Infinite
                            NumberAnimation { from: 140; to: 140 + modelData * 60; duration: 1400; easing.type: Easing.OutQuad }
                        }
                        SequentialAnimation on border.color {
                            loop: Animation.Infinite
                            ColorAnimation {
                                from: Qt.rgba(Qt.color(root.theme.accent).r, Qt.color(root.theme.accent).g, Qt.color(root.theme.accent).b, 0.5)
                                to: Qt.rgba(Qt.color(root.theme.accent).r, Qt.color(root.theme.accent).g, Qt.color(root.theme.accent).b, 0.0)
                                duration: 1400
                            }
                        }
                    }
                }

                Rectangle {
                    id: avatarCircle
                    anchors.centerIn: parent
                    width: 120
                    height: 120
                    radius: 60
                    color: root.theme.item_sel
                    Label {
                        anchors.centerIn: parent
                        text: root.peerName.length > 0 ? root.peerName.charAt(0).toUpperCase() : "?"
                        font.pixelSize: 48
                        font.bold: true
                        color: "white"
                    }
                }
            }

            Item { Layout.preferredHeight: 24 }

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: root.peerName
                font.pixelSize: 22
                font.bold: true
                color: root.theme.text
            }

            Item { Layout.preferredHeight: 10 }

            Label {
                id: statusLabel
                Layout.alignment: Qt.AlignHCenter
                text: root.tr("call.calling")
                font.pixelSize: 14
                color: root.theme.text_dim

                property int dotCount: 0
                Timer {
                    interval: 500
                    running: true
                    repeat: true
                    onTriggered: {
                        statusLabel.dotCount = (statusLabel.dotCount + 1) % 4
                        statusLabel.text = "Звоним" + ".".repeat(statusLabel.dotCount)
                        root.elapsedSeconds += 1
                    }
                }
            }

            Item { Layout.preferredHeight: 6 }

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: {
                    const m = Math.floor(root.elapsedSeconds / 2 / 60)
                    const s = Math.floor(root.elapsedSeconds / 2 % 60)
                    return m + ":" + (s < 10 ? "0" + s : s)
                }
                font.family: "monospace"
                font.pixelSize: 12
                color: root.theme.accent
            }

            Item { Layout.fillHeight: true }

            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 4

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 72
                    height: 72
                    radius: 36
                    color: cancelMouse.pressed ? "#C62828" : (cancelMouse.containsMouse ? "#EF5350" : "#E53935")

                    Label {
                        anchors.centerIn: parent
                        icon.name: "call-stop"
                        font.pixelSize: 28
                    }

                    MouseArea {
                        id: cancelMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            root.cancelled()
                            root.close()
                        }
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.tr("call.cancel")
                    font.pixelSize: 10
                    color: root.theme.text_dim
                }
            }
        }
    }

    function callAccepted() {
        statusLabel.text = "Соединяем…"
    }
}
