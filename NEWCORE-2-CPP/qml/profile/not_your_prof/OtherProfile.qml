// SPDX-FileCopyrightText: 2026 bitzuka <matveypotyzhno@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import koutnet.app

// A peer's profile. Same shape as YourProfile so the two read as one screen
// in two states, minus every control that writes: no pickers, no edit mode,
// no account switch.
//
// Everything here arrives in the presence packet, which carries the handle,
// display name and a capped bio. Avatar, banner and background are files and
// stay out of a broadcast that repeats on a timer, so those slots fall back
// to the initial and the theme until a fetch keyed on profile_rev exists.
Item {
    id: root
    readonly property var theme: ThemeManager.colors

    // { ip, username, displayName, bio, os, e2e, avatarLetter }
    property var peer: null

    implicitWidth: Kirigami.Units.gridUnit * 46
    implicitHeight: Kirigami.Units.gridUnit * 30

    function tr(key) {
        return (Translations.current, Translations.t(key))
    }

    readonly property string shownName: peer
        ? (peer.displayName && peer.displayName.length > 0 ? peer.displayName
                                                           : (peer.username || peer.ip))
        : ""

    Rectangle {
        anchors.fill: parent
        color: root.theme.bg
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 9

            Rectangle {
                anchors.fill: parent
                color: root.theme.bg3
            }

            Rectangle {
                id: avatarFrame
                width: Kirigami.Units.gridUnit * 6.5
                height: width
                radius: width / 2
                color: root.theme.bg
                anchors.left: parent.left
                anchors.leftMargin: Kirigami.Units.largeSpacing
                anchors.bottom: parent.bottom
                anchors.bottomMargin: -height * 0.35

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 3
                    radius: width / 2
                    color: root.theme.item_sel

                    Label {
                        anchors.centerIn: parent
                        text: root.shownName.length > 0 ? root.shownName.charAt(0).toUpperCase() : "?"
                        font.pixelSize: Kirigami.Units.gridUnit * 2.2
                        font.bold: true
                        color: "white"
                    }
                }

                Rectangle {
                    width: Kirigami.Units.gridUnit * 1.1
                    height: width
                    radius: width / 2
                    color: root.theme.online
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    border.color: root.theme.bg
                    border.width: 2
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.gridUnit * 2.4
            spacing: Kirigami.Units.smallSpacing

            ColumnLayout {
                spacing: 2
                Kirigami.Heading {
                    level: 2
                    text: root.shownName
                    color: root.theme.text
                }
                Label {
                    text: root.peer ? "@" + (root.peer.username || root.peer.ip) : ""
                    color: root.theme.text_dim
                }
            }

            Item { Layout.fillWidth: true }

            // Reachability and session state belong to a peer and have no
            // equivalent on your own page, so this row exists only here.
            RowLayout {
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Icon {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    source: root.peer && root.peer.e2e ? "security-high" : "security-low"
                    color: root.peer && root.peer.e2e ? root.theme.online : root.theme.text_dim
                }
                Label {
                    text: root.peer && root.peer.e2e ? "E2E" : root.tr("profile.no_session")
                    color: root.theme.text_dim
                }
            }
        }

        Label {
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            text: root.tr("profile.status_online")
                + (root.peer && root.peer.os.length > 0 ? "  -  " + root.peer.os : "")
            color: root.theme.text_dim
            font.pixelSize: 13
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.largeSpacing

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: root.width * 0.65
                Layout.alignment: Qt.AlignTop
                spacing: Kirigami.Units.smallSpacing

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(60, aboutText.implicitHeight + 24)
                    radius: 8
                    color: root.theme.bg3
                    border.color: root.theme.border

                    Text {
                        id: aboutText
                        anchors.fill: parent
                        anchors.margins: 12
                        wrapMode: Text.Wrap
                        textFormat: Text.MarkdownText
                        color: root.peer && root.peer.bio && root.peer.bio.length > 0
                            ? root.theme.text : root.theme.text_dim
                        text: root.peer && root.peer.bio && root.peer.bio.length > 0
                            ? root.peer.bio : root.tr("profile.no_bio")
                    }
                }

                Kirigami.PlaceholderMessage {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: root.tr("profile.tab_content_placeholder")
                    icon.name: "folder-symbolic"
                }
            }

            ColumnLayout {
                Layout.preferredWidth: root.width * 0.32
                Layout.fillHeight: true
                Layout.alignment: Qt.AlignTop
                spacing: Kirigami.Units.largeSpacing

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: friendsCol.implicitHeight + 24
                    radius: 8
                    color: root.theme.bg3
                    border.color: root.theme.border

                    ColumnLayout {
                        id: friendsCol
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8
                        Kirigami.Heading {
                            level: 5
                            text: root.tr("profile.friends")
                            color: root.theme.text
                        }
                        Label {
                            text: root.tr("profile.no_friends_yet")
                            color: root.theme.text_dim
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
