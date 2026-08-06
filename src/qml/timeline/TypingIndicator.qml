// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// The animation only runs while the row is visible: a repeating animation on a
// hidden item is a wakeup every 400ms for a conversation nobody is having.
RowLayout {
    id: root

    property string peerName: ""

    spacing: Kirigami.Units.smallSpacing

    // Off the font and not a pixel count, so the dots follow the desktop font.
    FontMetrics {
        id: metrics
        font: Kirigami.Theme.smallFont
    }

    Row {
        Layout.alignment: Qt.AlignVCenter
        spacing: Math.round(metrics.xHeight / 2)

        Repeater {
            model: 3

            delegate: Rectangle {
                id: dot

                required property int index

                width: Math.round(metrics.xHeight * 0.6)
                height: width
                radius: width / 2
                color: Kirigami.Theme.disabledTextColor

                SequentialAnimation on opacity {
                    running: root.visible
                    loops: Animation.Infinite
                    // A third of a cycle apart, so the row reads as a wave.
                    PauseAnimation { duration: dot.index * 160 }
                    NumberAnimation { to: 1.0; duration: 320; easing.type: Easing.InOutQuad }
                    NumberAnimation { to: 0.3; duration: 320; easing.type: Easing.InOutQuad }
                    PauseAnimation { duration: (2 - dot.index) * 160 }
                }
            }
        }
    }

    QQC2.Label {
        Layout.fillWidth: true
        text: root.peerName.length > 0
            ? i18nc("@info:status the peer is writing a message, %1 is their name", "%1 is typing...", root.peerName)
            : i18nc("@info:status the peer is writing a message", "Typing...")
        textFormat: Text.PlainText
        elide: Text.ElideRight
        font: Kirigami.Theme.smallFont
        color: Kirigami.Theme.disabledTextColor
    }
}
