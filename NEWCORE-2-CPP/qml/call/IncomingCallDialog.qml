// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import koutnet.app

// Slide-in-from-bottom incoming call card with accept/reject.
// Port of legacy IncomingCallDialog.
Window {
    id: root
    width: 340
    height: 200
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"

    property string callerName: ""
    property string callerIp: ""

    readonly property var theme: ThemeManager.colors
    readonly property int endY: Screen.height - 220
    readonly property int startY: Screen.height + 10

    signal accepted()
    signal rejected()

    Component.onCompleted: {
        x = Screen.width / 2 - width / 2
        y = startY
        slideIn.start()
    }

    NumberAnimation {
        id: slideIn
        target: root
        property: "y"
        to: root.endY
        duration: 350
        easing.type: Easing.OutCubic
    }

    function slideOutThen(callback) {
        const anim = slideOutComponent.createObject(root, { target: root })
        anim.finished.connect(function() {
            callback()
            root.close()
        })
        anim.start()
    }

    Component {
        id: slideOutComponent
        NumberAnimation {
            property: "y"
            to: root.startY
            duration: 250
            easing.type: Easing.InCubic
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 20
        color: root.theme.bg2
        border.color: root.theme.border
        border.width: 1

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

        RowLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 16

            Rectangle {
                Layout.preferredWidth: 64
                Layout.preferredHeight: 64
                radius: 32
                color: root.theme.item_sel
                Label {
                    anchors.centerIn: parent
                    text: root.callerName.length > 0 ? root.callerName.charAt(0).toUpperCase() : "?"
                    font.pixelSize: 26
                    font.bold: true
                    color: "white"
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Label {
                    text: i18n("Incoming call")
                    font.pixelSize: 10
                    font.bold: true
                    color: root.theme.text_dim
                }
                Label {
                    text: root.callerName
                    font.pixelSize: 16
                    font.bold: true
                    color: root.theme.text
                }
                Label {
                    text: root.callerIp
                    font.pixelSize: 10
                    color: root.theme.text_dim
                }
                Item { Layout.fillHeight: true }
            }

            ColumnLayout {
                spacing: 8

                Rectangle {
                    width: 52; height: 52; radius: 26
                    color: acceptMouse.pressed ? "#1B5E20" : (acceptMouse.containsMouse ? "#388E3C" : "#2E7D32")
                    Label { anchors.centerIn: parent; icon.name: "dialog-ok"; width: 22; height: 22 }
                    MouseArea {
                        id: acceptMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.slideOutThen(function() { root.accepted() })
                    }
                }

                Rectangle {
                    width: 52; height: 52; radius: 26
                    color: rejectMouse.pressed ? "#B71C1C" : (rejectMouse.containsMouse ? "#E53935" : "#C62828")
                    Label { anchors.centerIn: parent; icon.name: "dialog-cancel"; width: 22; height: 22 }
                    MouseArea {
                        id: rejectMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.slideOutThen(function() { root.rejected() })
                    }
                }
            }
        }
    }

    function callRejected() {
        slideOutThen(function() {})
    }
}
