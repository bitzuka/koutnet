// SPDX-FileCopyrightText: 2022 Tobias Fella <tobias.fella@kde.org>
// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Adapted from NeoChat's src/chatbar/EmojiDelegate.qml.
//
// Two things are gone: the Image half, which drew Matrix custom emoji and
// stickers from an mxc:// URI, and Kirigami.Units.cornerRadius, which this
// project cannot use because it declares a Kirigami 6.0 floor and that property
// arrived later. smallSpacing is the radius instead.
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

QQC2.ItemDelegate {
    id: root

    property string name
    property string emoji
    property bool showTones: false

    QQC2.ToolTip.text: root.name
    QQC2.ToolTip.visible: hovered && root.name !== ""
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

    topPadding: Kirigami.Units.smallSpacing
    bottomPadding: Kirigami.Units.smallSpacing
    leftPadding: Kirigami.Units.smallSpacing
    rightPadding: Kirigami.Units.smallSpacing

    contentItem: Item {
        QQC2.Label {
            anchors.fill: parent
            text: root.emoji
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.family: "emoji"
            font.pixelSize: height - Kirigami.Units.largeSpacing
            elide: Text.ElideNone
            wrapMode: Text.NoWrap
            textFormat: Text.PlainText

            Kirigami.Icon {
                width: Kirigami.Units.gridUnit * 0.5
                height: Kirigami.Units.gridUnit * 0.5
                source: "arrow-down-symbolic"
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                visible: root.showTones
            }
        }
    }

    background: Rectangle {
        Kirigami.Theme.colorSet: Kirigami.Theme.ColorSet.View
        color: root.checked ? Kirigami.Theme.highlightColor : Kirigami.Theme.backgroundColor
        radius: Kirigami.Units.smallSpacing

        Rectangle {
            radius: Kirigami.Units.smallSpacing
            anchors.fill: parent
            color: Kirigami.Theme.highlightColor
            opacity: root.hovered && !root.pressed ? 0.2 : 0
        }
    }
}
