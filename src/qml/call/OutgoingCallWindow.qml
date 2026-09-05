// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import koutnet.app

// The pulsing rings are kept: they are the one piece of decoration here that says
// something, namely that it is still trying. It used to be a FramelessWindowHint
// window moved by a MouseArea, which is the window manager's job and got no
// snapping, no keyboard move, no taskbar entry and no compositor shadow.
Kirigami.ApplicationWindow {
    id: root

    property string peerName: ""
    property string peerIp: ""
    property int elapsedSeconds: 0

    signal cancelled()

    title: i18nc("@title:window %1 is the name of the person being called", "Calling %1", root.peerName)
    width: Kirigami.Units.gridUnit * 20
    height: Kirigami.Units.gridUnit * 26
    minimumWidth: Kirigami.Units.gridUnit * 16
    minimumHeight: Kirigami.Units.gridUnit * 20
    visible: true

    // A separate window is a separate theme chain, so the accent is named again.
    Kirigami.Theme.inherit: false
    Kirigami.Theme.highlightColor: Brand.accent

    // The dots are animated, so they are a placeholder and not a concatenation.
    function callingLabel(dots) {
        return i18nc("@info:status waiting for the peer to pick up, %1 is a run of dots",
                     "Calling%1", ".".repeat(dots))
    }

    // Twice a second, because the dots are what it is really driving.
    Timer {
        interval: 500
        running: true
        repeat: true
        onTriggered: {
            statusLabel.dotCount = (statusLabel.dotCount + 1) % 4
            root.elapsedSeconds += 1
        }
    }

    pageStack.initialPage: Kirigami.Page {
        // Nothing to put in a toolbar: the title says who, the button says stop.
        globalToolBarStyle: Kirigami.ApplicationHeaderStyle.None

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width, Kirigami.Units.gridUnit * 18)
            spacing: Kirigami.Units.largeSpacing

            Item {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: Kirigami.Units.gridUnit * 12
                implicitHeight: implicitWidth

                // Much cheaper than the paintEvent this was ported from.
                Repeater {
                    model: 3

                    delegate: Rectangle {
                        id: ring

                        required property int index

                        anchors.centerIn: parent
                        width: avatar.width
                        height: width
                        radius: width / 2
                        color: "transparent"
                        border.width: 2
                        border.color: Kirigami.Theme.highlightColor
                        opacity: 0

                        SequentialAnimation {
                            running: true
                            loops: Animation.Infinite

                            PauseAnimation { duration: ring.index * 400 }

                            ParallelAnimation {
                                NumberAnimation {
                                    target: ring
                                    property: "width"
                                    from: avatar.width
                                    to: avatar.width * 1.8
                                    duration: 1400
                                    easing.type: Easing.OutQuad
                                }
                                NumberAnimation {
                                    target: ring
                                    property: "opacity"
                                    from: 0.5
                                    to: 0
                                    duration: 1400
                                }
                            }
                        }
                    }
                }

                Components.Avatar {
                    id: avatar
                    anchors.centerIn: parent
                    width: Kirigami.Units.gridUnit * 8
                    height: width
                    name: root.peerName
                }
            }

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 1
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: root.peerName
            }

            QQC2.Label {
                id: statusLabel
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                color: Kirigami.Theme.disabledTextColor

                property int dotCount: 0

                text: root.callingLabel(statusLabel.dotCount)
            }

            QQC2.Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: {
                    const total = Math.floor(root.elapsedSeconds / 2)
                    const m = Math.floor(total / 60)
                    const s = total % 60
                    return m + ":" + (s < 10 ? "0" + s : s)
                }
                font: Kirigami.Theme.fixedWidthFont
                color: Kirigami.Theme.highlightColor
            }

            QQC2.Button {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Kirigami.Units.gridUnit
                text: i18nc("@action:button abandon the outgoing call", "Cancel")
                // Breeze already draws call-stop in red.
                icon.name: "call-stop"
                onClicked: {
                    root.cancelled()
                    root.close()
                }
            }
        }
    }
}
