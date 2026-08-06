// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.delegates as Delegates
import org.kde.kirigamiaddons.components as Components
import koutnet.app

// RoundedItemDelegate rather than a hand-written Rectangle: the rounding, hover
// tint, pressed state, selection fill and focus ring come with it and follow the
// colour scheme. The old version painted four of those out of a palette table.
//
// It binds its own width, taken from the view it is the direct delegate of, so
// anywhere else it has to be handed one - see the pinned Favorites row.
Delegates.RoundedItemDelegate {
    id: root

    property string displayName: ""
    property string preview: ""
    property double stampSecs: 0
    property double lastSeenSecs: 0
    property int unreadCount: 0
    property bool online: false
    property bool selected: false
    // A Breeze icon name for a pinned row: a blank Avatar name is what makes it
    // fall through to its icon instead of drawing initials.
    property string iconName: ""
    property bool showPresence: true

    // "lan", "matrix" or "reserved", straight off ChatListModel's transport
    // role. The only thing this row is told about where the conversation lives,
    // and it spends all of it on the small mark over the avatar.
    property string transport: "lan"

    // In compact mode the preview and the stamp go, which is what takes the row
    // from two lines to one, and the avatar comes down a size.
    property bool compact: false

    // Empty in compact mode, which is also what makes SubtitleContentItem lay
    // itself out on one line.
    readonly property string subtitleText: root.compact
        ? ""
        : (root.preview.length > 0
            ? root.preview
            : (root.showPresence
                ? RelativeTime.presenceLabel(root.online, root.lastSeenSecs, RelativeTime.now)
                : ""))

    readonly property real avatarSize: root.compact
        ? Kirigami.Units.iconSizes.smallMedium
        : Kirigami.Units.iconSizes.medium

    signal avatarClicked(Item anchorItem)

    text: root.displayName
    // The base class ties this to the view's current item. Selection here means
    // which conversation is open, which the view knows nothing about.
    highlighted: root.selected

    // At one line the base class's own padding is most of the row's height, so it
    // comes down too. Through a Binding rather than a conditional so the roomy
    // layout keeps whatever that value is instead of a stale copy of it.
    Binding {
        target: root
        property: "verticalPadding"
        value: Kirigami.Units.smallSpacing
        when: root.compact
        restoreMode: Binding.RestoreBindingOrValue
    }

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Item {
            id: avatarSlot

            implicitWidth: root.avatarSize
            implicitHeight: root.avatarSize
            Layout.alignment: Qt.AlignVCenter

            // Taken before the delegate's own tap handling, so the card opens
            // instead of the conversation.
            TapHandler {
                acceptedButtons: Qt.LeftButton
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: root.avatarClicked(avatarSlot)
            }

            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }

            Components.Avatar {
                anchors.fill: parent
                // Left on its generated per-name colour: every avatar used to be
                // the one accent colour, which made a column of identical circles.
                name: root.iconName.length > 0 ? "" : root.displayName
                iconSource: root.iconName
            }

            Rectangle {
                width: Math.round(Kirigami.Units.iconSizes.small * 0.6)
                height: width
                radius: width / 2
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                visible: root.showPresence
                color: root.online ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.disabledTextColor
                border.color: Kirigami.Theme.backgroundColor
                border.width: 1
            }

            // A conversation that is not on this network says so, because
            // everything else about the row - the name, the preview, the unread
            // count - is deliberately identical to a LAN peer's. Top corner
            // rather than bottom: the presence dot owns the bottom one, and a
            // Matrix room has no presence to show there.
            Kirigami.Icon {
                width: Math.round(Kirigami.Units.iconSizes.small * 0.7)
                height: width
                anchors.right: parent.right
                anchors.top: parent.top
                visible: root.transport === "matrix"
                source: "network-server"
                color: Kirigami.Theme.highlightColor

                QQC2.ToolTip.visible: hoverHandler.hovered
                QQC2.ToolTip.text: i18nc("@info:tooltip conversation list badge", "Matrix room on a K-Server")

                HoverHandler { id: hoverHandler }
            }
        }

        Delegates.SubtitleContentItem {
            itemDelegate: root
            Layout.fillWidth: true
            subtitle: root.subtitleText
            bold: root.unreadCount > 0
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: 0

            QQC2.Label {
                Layout.alignment: Qt.AlignRight
                visible: !root.compact && text.length > 0
                text: root.stampSecs > 0 ? RelativeTime.chatStamp(root.stampSecs, RelativeTime.now) : ""
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
            }

            // Kirigami has no unread badge in the version this targets; sized to
            // its label rather than a fixed box a three-digit count spills out of.
            Rectangle {
                Layout.alignment: Qt.AlignRight
                visible: root.unreadCount > 0
                implicitWidth: Math.max(height, unreadLabel.implicitWidth + Kirigami.Units.smallSpacing * 2)
                implicitHeight: Math.round(Kirigami.Units.gridUnit * 1.1)
                radius: height / 2
                color: Kirigami.Theme.highlightColor

                Accessible.role: Accessible.StaticText
                Accessible.name: i18ncp("@info:whatsthis unread messages in this conversation, %1 is a number",
                                        "%1 unread message", "%1 unread messages", root.unreadCount)

                QQC2.Label {
                    id: unreadLabel
                    anchors.centerIn: parent
                    // Past two digits the count is a layout problem, not news.
                    text: root.unreadCount > 99
                        ? i18nc("@info unread count too large to show exactly", "99+")
                        : String(root.unreadCount)
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    font.bold: true
                    color: Kirigami.Theme.highlightedTextColor
                }
            }
        }
    }
}
