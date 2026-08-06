// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import koutnet.app

// Same story as OutgoingCallWindow: it was a frameless always-on-top window dragged
// by a MouseArea, with the speaker and screen buttons drawn as Labels carrying an
// icon.name that Label does not have, so those two showed nothing at all. Speaker
// and screen sharing are still not wired to anything, so they say so.
Kirigami.ApplicationWindow {
    id: root

    property string peerName: ""
    property string peerIp: ""
    property bool muted: false
    property int elapsedSeconds: 0

    signal hangup()
    signal muteToggled(bool muted)

    title: i18nc("@title:window %1 is the name of the other person on the call", "Call with %1", root.peerName)
    width: Kirigami.Units.gridUnit * 20
    height: Kirigami.Units.gridUnit * 28
    minimumWidth: Kirigami.Units.gridUnit * 16
    minimumHeight: Kirigami.Units.gridUnit * 22
    visible: true

    Kirigami.Theme.inherit: false
    Kirigami.Theme.highlightColor: Brand.accent

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: root.elapsedSeconds += 1
    }

    pageStack.initialPage: Kirigami.Page {
        globalToolBarStyle: Kirigami.ApplicationHeaderStyle.None

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width, Kirigami.Units.gridUnit * 18)
            spacing: Kirigami.Units.largeSpacing

            Components.Avatar {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: Kirigami.Units.gridUnit * 8
                implicitHeight: Kirigami.Units.gridUnit * 8
                name: root.peerName
            }

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 1
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: root.peerName
            }

            QQC2.Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: {
                    const h = Math.floor(root.elapsedSeconds / 3600)
                    const m = Math.floor((root.elapsedSeconds % 3600) / 60)
                    const s = root.elapsedSeconds % 60
                    const mm = (h > 0 && m < 10 ? "0" : "") + m
                    const ss = s < 10 ? "0" + s : s
                    return h > 0 ? (h + ":" + mm + ":" + ss) : (m + ":" + ss)
                }
                font: Kirigami.Theme.fixedWidthFont
                color: Kirigami.Theme.highlightColor
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Kirigami.Units.gridUnit
                spacing: Kirigami.Units.largeSpacing

                QQC2.ToolButton {
                    display: QQC2.AbstractButton.TextUnderIcon
                    checkable: true
                    checked: root.muted
                    icon.name: root.muted ? "microphone-sensitivity-muted" : "audio-input-microphone"
                    text: i18nc("@action:button silence your own microphone", "Mute")
                    onToggled: {
                        root.muted = checked
                        root.muteToggled(root.muted)
                    }
                }

                QQC2.ToolButton {
                    display: QQC2.AbstractButton.TextUnderIcon
                    enabled: false
                    icon.name: "audio-volume-high"
                    text: i18nc("@action:button", "Speaker")
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: i18nc("@info:tooltip this control does nothing yet", "Not implemented yet")
                }

                QQC2.ToolButton {
                    display: QQC2.AbstractButton.TextUnderIcon
                    enabled: false
                    icon.name: "video-display"
                    text: i18nc("@action:button share your screen", "Screen")
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: i18nc("@info:tooltip this control does nothing yet", "Not implemented yet")
                }
            }

            QQC2.Button {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Kirigami.Units.gridUnit
                text: i18nc("@action:button hang up the call", "End call")
                // Breeze already draws call-stop in red.
                icon.name: "call-stop"
                onClicked: {
                    root.hangup()
                    root.close()
                }
            }
        }
    }
}
