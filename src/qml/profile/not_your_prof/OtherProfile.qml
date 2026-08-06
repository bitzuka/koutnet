// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import koutnet.app

// A peer's profile at full size: what the settings page shows about you, minus every
// control that writes. Everything here arrives in the presence packet, which carries
// the handle, display name, status emoji and a capped bio. Avatar, banner and badge
// are files and stay out of a broadcast that repeats on a timer, so those slots fall
// back to initials and the brand colour until a fetch keyed on profile_rev exists.
Kirigami.ScrollablePage {
    id: root

    // { ip, username, displayName, bio, os, e2e, avatarLetter, online, lastSeen,
    // statusEmoji }
    property var peer: null

    readonly property string shownName: peer
        ? (peer.displayName && peer.displayName.length > 0 ? peer.displayName
                                                           : (peer.username || root.unknownPeerName))
        : ""

    readonly property string unknownPeerName: i18nc("@info a peer that has published no name of its own", "Unknown peer")

    readonly property string statusEmoji: root.peer ? (root.peer.statusEmoji || "") : ""
    readonly property string bio: root.peer ? (root.peer.bio || "") : ""

    // A FormCard fills its row and then draws the card centred at its own
    // maximumWidth, so anything here that is not a FormCard needs the same width and
    // alignment or it starts at the window edge.
    readonly property real kContentWidth: Kirigami.Units.gridUnit * 30
    // What a form delegate puts round its text, so the blocks share a left edge.
    readonly property real kContentPadding: Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing

    // The address is behind this rather than on the page: it is the one thing here
    // that says where somebody physically is, and a screen is read over shoulders.
    property bool technicalShown: false

    title: root.shownName

    Kirigami.Theme.highlightColor: Brand.accent

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        ProfileHeader {
            Layout.fillWidth: true
            Layout.maximumWidth: root.kContentWidth
            Layout.alignment: Qt.AlignHCenter

            displayName: root.shownName
            handle: root.peer ? (root.peer.username || "") : ""
            online: root.peer ? root.peer.online === true : false
            lastSeenSecs: root.peer ? (root.peer.lastSeen || 0) : 0
            statusEmoji: root.statusEmoji
        }

        // Only the emoji: the text half of a status is not in the presence packet.
        ProfileBlock {
            Layout.fillWidth: true
            Layout.maximumWidth: root.kContentWidth
            Layout.alignment: Qt.AlignHCenter
            Layout.leftMargin: root.kContentPadding
            Layout.rightMargin: root.kContentPadding
            visible: root.statusEmoji.length > 0

            label: i18nc("@label:textbox caption over what somebody says they are up to", "Custom status")

            QQC2.Label {
                text: root.statusEmoji
                textFormat: Text.PlainText
                font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * 1.6)
            }
        }

        ProfileBlock {
            Layout.fillWidth: true
            Layout.maximumWidth: root.kContentWidth
            Layout.alignment: Qt.AlignHCenter
            Layout.leftMargin: root.kContentPadding
            Layout.rightMargin: root.kContentPadding

            label: i18nc("@title:group free-form text about the peer", "About")

            // Markdown, so the same text renders the same way on either side.
            Kirigami.SelectableLabel {
                Layout.fillWidth: true
                text: root.bio.length > 0 ? root.bio : i18nc("@info", "No description")
                textFormat: Text.MarkdownText
                wrapMode: Text.WordWrap
                color: root.bio.length > 0 ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
            }
        }

        FormCard.FormHeader {
            Layout.fillWidth: true
            title: i18nc("@title:group", "Session")
        }

        // Reachability and session state have no equivalent on your own profile.
        FormCard.FormCard {
            Layout.fillWidth: true

            FormCard.FormTextDelegate {
                id: e2eDelegate
                icon.name: (root.peer && root.peer.e2e) ? "security-high" : "security-low"
                text: (root.peer && root.peer.e2e)
                    ? i18nc("@info:status the session is end to end encrypted", "End to end encrypted")
                    : i18nc("@info:status", "No session")
                description: (root.peer && root.peer.e2e)
                    ? i18nc("@info:whatsthis", "Messages and calls with this peer are encrypted with X25519 and AES-256-GCM.")
                    : i18nc("@info:whatsthis", "No key exchange has happened with this peer yet.")
            }

            FormCard.FormDelegateSeparator { above: e2eDelegate; below: systemDelegate }

            FormCard.FormTextDelegate {
                id: systemDelegate
                visible: root.peer && root.peer.os && root.peer.os.length > 0
                icon.name: "computer"
                text: i18nc("@label the peer's operating system", "System")
                description: root.peer ? root.peer.os : ""
            }

            FormCard.FormDelegateSeparator { above: systemDelegate; below: addressDelegate }

            FormCard.FormSwitchDelegate {
                id: addressDelegate
                icon.name: "documentinfo"
                text: i18nc("@option:check reveal the peer's network address", "Show technical details")
                checked: root.technicalShown
                onToggled: root.technicalShown = checked
            }

            FormCard.FormTextDelegate {
                visible: root.technicalShown
                icon.name: "network-wired"
                text: i18nc("@label network address of the peer", "Address")
                description: root.peer ? (root.peer.ip || "") : ""
            }
        }
    }
}
