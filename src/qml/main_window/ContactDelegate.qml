// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import koutnet.app

// Single row in the peer list: avatar (letter or icon), name, and optional
// online and security status. ItemDelegate rather than MouseArea plus Item,
// so click, hover and keyboard handling come from Qt Quick Controls.
//
// Colours come from ThemeManager, not Kirigami.Theme. The two are separate
// palettes and only the former follows the theme picker in Settings.
ItemDelegate {
    id: root

    readonly property var theme: ThemeManager.colors

    property string peerIp: ""
    property string peerOs: ""
    property bool e2e: false
    property bool selected: false
    // Empty means the avatar is the first letter of peerIp, which is what
    // real contacts get. Set it to a Kirigami icon name for special rows
    // such as favorites.
    property string iconName: ""
    // Real network peers have a reachability/encryption status; special
    // local-only rows (the self-chat) have neither concept, so both can be
    // turned off rather than showing meaningless "online"/"E2E" labels.
    property bool showOnlineIndicator: true
    property bool showSecurityLabel: true

    height: 60
    hoverEnabled: true

    background: Rectangle {
        color: root.selected ? root.theme.item_sel
             : (root.hovered ? root.theme.btn_hover : root.theme.item_bg)
        radius: 4
    }

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Item {
            width: 36
            height: 36
            Layout.alignment: Qt.AlignVCenter

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: root.theme.accent
            }

            Kirigami.Icon {
                anchors.centerIn: parent
                width: 26
                height: 26
                visible: root.iconName.length > 0
                source: root.iconName
                color: "white"
                isMask: true
            }

            Text {
                anchors.centerIn: parent
                visible: root.iconName.length === 0
                text: root.peerIp.length > 0 ? root.peerIp.charAt(0) : "?"
                color: "white"
                font.bold: true
            }

            Rectangle {
                width: 10
                height: 10
                radius: 5
                color: root.theme.online
                border.color: root.theme.bg2
                border.width: 2
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                visible: root.showOnlineIndicator
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Kirigami.Heading {
                text: root.peerIp
                level: 5
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: root.selected ? "white" : root.theme.text
            }

            Text {
                visible: root.showSecurityLabel || root.peerOs.length > 0
                // One whole sentence per case rather than three fragments
                // glued together, so the order is the translator's to choose.
                text: {
                    if (!root.showSecurityLabel)
                        return root.peerOs
                    const sec = root.e2e
                        ? i18nc("@info:status the session is end to end encrypted", "E2E")
                        : i18nc("@info:status there is no encrypted session", "Plain")
                    if (root.peerOs.length === 0)
                        return sec
                    return i18nc("@info:status %1 is the encryption state, %2 the peer's operating system",
                                 "%1 - %2", sec, root.peerOs)
                }
                color: root.e2e ? root.theme.online : root.theme.text_dim
                elide: Text.ElideRight
                Layout.fillWidth: true
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }
        }
    }
}
