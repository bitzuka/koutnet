// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import koutnet.app

// One row in the conversation list: avatar, name, a line of the last message,
// when that was, and how many are unread. ItemDelegate rather than MouseArea
// plus Item, so click, hover and keyboard handling come from Qt Quick Controls.
//
// This used to be a row in a list of everyone broadcasting on the network, which
// is why reachability was the loudest thing on it. It is a conversation now, so
// the peer being up or down is one small dot on the avatar and nothing more -
// see the note at the top of ChatListModel.
//
// Colours come from ThemeManager, not Kirigami.Theme. The two are separate
// palettes and only the former follows the theme picker in Settings.
ItemDelegate {
    id: root

    readonly property var theme: ThemeManager.colors

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
    // real conversation gets. Set it to a Kirigami icon name for the pinned rows
    // such as favorites.
    property string iconName: ""
    // The pinned local rows are not conversations with anybody, so they have no
    // reachability and no last-message line to show.
    property bool showPresence: true

    // implicitHeight rather than height, so a list that hides a row while a
    // search is running can collapse it to zero and get the size back after.
    implicitHeight: Math.round(Kirigami.Units.gridUnit * 3.5)
    hoverEnabled: true

    background: Rectangle {
        color: root.selected ? root.theme.item_sel
             : (root.hovered ? root.theme.btn_hover : root.theme.item_bg)
        radius: 4
    }

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Item {
            implicitWidth: Kirigami.Units.gridUnit * 2
            implicitHeight: Kirigami.Units.gridUnit * 2
            Layout.alignment: Qt.AlignVCenter

            // Kirigami.Avatar already does "picture, or initials, or a fallback",
            // which is the whole of what this needs - it only has to be told the
            // name and kept on the theme's accent instead of its own generated
            // colour.
            Kirigami.Avatar {
                anchors.fill: parent
                visible: root.iconName.length === 0
                name: root.displayName
                color: root.theme.accent
            }

            Rectangle {
                anchors.fill: parent
                visible: root.iconName.length > 0
                radius: width / 2
                color: root.theme.accent

                Kirigami.Icon {
                    anchors.centerIn: parent
                    width: Math.round(parent.width * 0.6)
                    height: width
                    source: root.iconName
                    color: "white"
                    isMask: true
                }
            }

            // Presence, reduced to what it is worth: a dot.
            Rectangle {
                width: Math.round(Kirigami.Units.smallSpacing * 2.5)
                height: width
                radius: width / 2
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                visible: root.showPresence
                color: root.online ? root.theme.online : root.theme.offline
                border.color: root.theme.bg2
                border.width: 2
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            Kirigami.Heading {
                text: root.displayName
                level: 5
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: root.selected ? "white" : root.theme.text
            }

            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                elide: Text.ElideRight
                maximumLineCount: 1
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                // The message if there is one, and the peer's status while there
                // is not, so a chat the user has only just opened still says
                // something about who is on the other end.
                text: root.preview.length > 0
                    ? root.preview
                    : (root.showPresence
                        ? RelativeTime.presenceLabel(root.online, root.lastSeenSecs, RelativeTime.now)
                        : "")
                color: root.unreadCount > 0 ? root.theme.text : root.theme.text_dim
            }
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: Math.round(Kirigami.Units.smallSpacing / 2)

            Label {
                Layout.alignment: Qt.AlignRight
                visible: text.length > 0
                text: root.stampSecs > 0 ? RelativeTime.chatStamp(root.stampSecs, RelativeTime.now) : ""
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                color: root.theme.text_dim
            }

            // Kirigami has no unread-badge component, so this is a rounded
            // rectangle sized to its own label rather than a fixed box that a
            // three-digit count would spill out of.
            Rectangle {
                Layout.alignment: Qt.AlignRight
                visible: root.unreadCount > 0
                implicitWidth: Math.max(height, unreadLabel.implicitWidth + Kirigami.Units.smallSpacing * 2)
                implicitHeight: Math.round(Kirigami.Units.gridUnit * 1.1)
                radius: height / 2
                color: root.theme.accent

                Accessible.role: Accessible.StaticText
                Accessible.name: i18ncp("@info:whatsthis unread messages in this conversation, %1 is a number",
                                        "%1 unread message", "%1 unread messages", root.unreadCount)

                Label {
                    id: unreadLabel
                    anchors.centerIn: parent
                    // Past two digits the number stops being information and
                    // starts being a layout problem.
                    text: root.unreadCount > 99
                        ? i18nc("@info unread count too large to show exactly", "99+")
                        : String(root.unreadCount)
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    font.bold: true
                    color: "white"
                }
            }
        }
    }
}
