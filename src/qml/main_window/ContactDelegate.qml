// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.delegates as Delegates
import org.kde.kirigamiaddons.components as Components
import koutnet.app

// One row in the conversation list: avatar, name, a line of the last message,
// when that was, and how many are unread.
//
// RoundedItemDelegate rather than a Rectangle with a hand-written radius. The
// rounding, the hover tint, the pressed state, the selection fill and the focus
// ring all come with it and all follow the colour scheme; the old version painted
// four of those itself out of a palette table and had no focus ring at all.
//
// It has to be the direct delegate of a view, because that is where it takes its
// width from. The pinned row uses it as a ListView header, which is still a child
// of the view, so that holds there too.
Delegates.RoundedItemDelegate {
    id: root

    property string displayName: ""
    // One line of the newest message. Empty for a chat with nothing in it yet,
    // where the row falls back to the peer's status instead.
    property string preview: ""
    property double stampSecs: 0
    property double lastSeenSecs: 0
    property int unreadCount: 0
    property bool online: false
    property bool selected: false
    // Empty means the avatar draws initials from displayName, which is what a
    // real conversation gets. Set it to a Breeze icon name for a pinned row such
    // as favorites: a blank name is what makes Avatar fall through to its icon.
    property string iconName: ""
    // The pinned local rows are not conversations with anybody, so they have no
    // reachability and no last-message line to show.
    property bool showPresence: true

    // Compact mode. The message preview and the time stamp go, which is what
    // takes the row from two lines to one, and the avatar comes down a size. The
    // name, the presence dot and the unread count stay: those are the three
    // things the row exists to say.
    property bool compact: false

    // What the row actually shows under the name. Nothing in compact mode, which
    // is also what makes SubtitleContentItem lay itself out on one line.
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

    // The avatar answers separately from the row: clicking a face asks who
    // this is, clicking the row opens the conversation.
    signal avatarClicked(Item anchorItem)

    text: root.displayName
    // The base class ties this to the view's current item. Selection here means
    // which conversation is open, which the view knows nothing about.
    highlighted: root.selected

    // No padding override. The row is tighter in compact mode because there is
    // less in it - one line of text instead of two, and a smaller avatar - and
    // RoundedItemDelegate sizes itself off its content. Writing a smaller padding
    // here as well would mean naming the base class's own value in the other
    // branch, which is a number this file would then own a stale copy of.
    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Item {
            id: avatarSlot

            implicitWidth: root.avatarSize
            implicitHeight: root.avatarSize
            Layout.alignment: Qt.AlignVCenter

            // Taken before the delegate's own tap handling, so the card opens
            // instead of the conversation. The card is hung off this item.
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
                // Left on its generated per-name colour on purpose. Every avatar
                // used to be the one accent colour, which turned a list of them
                // into a column of identical circles.
                name: root.iconName.length > 0 ? "" : root.displayName
                iconSource: root.iconName
            }

            // Presence, reduced to what it is worth: a dot.
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
        }

        Delegates.SubtitleContentItem {
            itemDelegate: root
            Layout.fillWidth: true
            // The message if there is one, and the peer's status while there is
            // not, so a chat the user has only just opened still says something
            // about who is on the other end. Empty in compact mode.
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

            // Kirigami has no unread badge in the version this targets, so this
            // is still a rounded rectangle - but one sized to its own label
            // rather than a fixed box a three-digit count would spill out of.
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
                    // Past two digits the number stops being information and
                    // starts being a layout problem.
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
