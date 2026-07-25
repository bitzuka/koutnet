import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import koutnet.app

// Telegram-style active call window: mute / speaker / screen / hangup.
// Port of legacy ActiveCallWindow.
Window {
    id: root
    width: 340
    height: 560
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"

    property string peerName: ""
    property string peerIp: ""
    property bool muted: false
    property int elapsedSeconds: 0

    readonly property var theme: ThemeManager.colors

    signal hangup()
    signal muteToggled(bool muted)

    Component.onCompleted: {
        x = Screen.width / 2 - width / 2
        y = Screen.height / 2 - height / 2
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: root.elapsedSeconds += 1
    }

    Rectangle {
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
            anchors.topMargin: 44
            anchors.bottomMargin: 36
            anchors.leftMargin: 28
            anchors.rightMargin: 28
            spacing: 0

            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 130
                Layout.preferredHeight: 130
                radius: 65
                color: root.theme.item_sel
                Label {
                    anchors.centerIn: parent
                    text: root.peerName.length > 0 ? root.peerName.charAt(0).toUpperCase() : "?"
                    font.pixelSize: 52
                    font.bold: true
                    color: "white"
                }
            }

            Item { Layout.preferredHeight: 20 }

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: root.peerName
                font.pixelSize: 22
                font.bold: true
                color: root.theme.text
            }

            Item { Layout.preferredHeight: 8 }

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: {
                    const h = Math.floor(root.elapsedSeconds / 3600)
                    const m = Math.floor((root.elapsedSeconds % 3600) / 60)
                    const s = root.elapsedSeconds % 60
                    const mm = (h > 0 && m < 10 ? "0" : "") + m
                    const ss = s < 10 ? "0" + s : s
                    return h > 0 ? (h + ":" + mm + ":" + ss) : (m + ":" + ss)
                }
                font.family: "monospace"
                font.pixelSize: 14
                color: root.theme.accent
            }

            Item { Layout.fillHeight: true }

            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 4

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 20

                    Rectangle {
                        width: 56; height: 56; radius: 28
                        color: root.muted ? "#444444" : (muteMouse.containsMouse ? "#3A3A60" : "#2A2A46")
                        Label { anchors.centerIn: parent; text: root.muted ? "🔇" : "🎤"; font.pixelSize: 24 }
                        MouseArea {
                            id: muteMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                root.muted = !root.muted
                                root.muteToggled(root.muted)
                            }
                        }
                    }

                    Rectangle {
                        width: 56; height: 56; radius: 28
                        color: spkMouse.containsMouse ? "#3A3A60" : "#2A2A46"
                        Label { anchors.centerIn: parent; text: "🔊"; font.pixelSize: 24 }
                        MouseArea { id: spkMouse; anchors.fill: parent; hoverEnabled: true }
                    }

                    Rectangle {
                        width: 56; height: 56; radius: 28
                        color: screenMouse.containsMouse ? "#3A3A60" : "#2A2A46"
                        Label { anchors.centerIn: parent; text: "🖥"; font.pixelSize: 24 }
                        MouseArea { id: screenMouse; anchors.fill: parent; hoverEnabled: true }
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 20
                    Label { Layout.preferredWidth: 56; horizontalAlignment: Text.AlignHCenter; text: "Мут"; font.pixelSize: 10; color: root.theme.text_dim }
                    Label { Layout.preferredWidth: 56; horizontalAlignment: Text.AlignHCenter; text: "Динамик"; font.pixelSize: 10; color: root.theme.text_dim }
                    Label { Layout.preferredWidth: 56; horizontalAlignment: Text.AlignHCenter; text: "Экран"; font.pixelSize: 10; color: root.theme.text_dim }
                }
            }

            Item { Layout.preferredHeight: 24 }

            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 4

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 72; height: 72; radius: 36
                    color: endMouse.pressed ? "#B71C1C" : (endMouse.containsMouse ? "#EF5350" : "#E53935")
                    Label { anchors.centerIn: parent; text: "📵"; font.pixelSize: 28 }
                    MouseArea {
                        id: endMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            root.hangup()
                            root.close()
                        }
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Завершить"
                    font.pixelSize: 10
                    color: root.theme.text_dim
                }
            }
        }
    }
}
