// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import koutnet.app

// A peer's profile. Literally the same header as YourProfile, so the two read as
// one screen in two states, minus every control that writes: no pickers, no edit
// mode, no account switch.
//
// Everything here arrives in the presence packet, which carries the handle,
// display name and a capped bio. Avatar, banner and background are files and stay
// out of a broadcast that repeats on a timer, so those slots fall back to the
// initials and the colour scheme until a fetch keyed on profile_rev exists.
Kirigami.ScrollablePage {
    id: root

    // { ip, username, displayName, bio, os, e2e, avatarLetter, online, lastSeen }
    property var peer: null

    readonly property string shownName: peer
        ? (peer.displayName && peer.displayName.length > 0 ? peer.displayName
                                                           : (peer.username || root.unknownPeerName))
        : ""

    readonly property string unknownPeerName: i18nc("@info a peer that has published no name of its own", "Unknown peer")

    // The address is behind this rather than on the page. It is the one thing
    // here that says where somebody physically is, and a profile is read with
    // other people looking at the screen.
    property bool technicalShown: false

    title: root.shownName

    // See the note on Kirigami.Theme in Main.qml.
    Kirigami.Theme.highlightColor: Brand.accent

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        // Banner, avatar, name, handle, presence and status, all of it shared
        // with the own-profile page - see qml/profile/ProfileHeader.qml. Nothing
        // is editable here: none of it belongs to this end.
        //
        // The avatar and banner sources stay empty for the reason at the top of
        // this file: they are files, and files are not in a broadcast that
        // repeats on a timer. The header falls back to the initials and the brand
        // colour, which is what those slots are for.
        ProfileHeader {
            Layout.fillWidth: true

            displayName: root.shownName
            handle: root.peer ? (root.peer.username || "") : ""
            online: root.peer ? root.peer.online === true : false
            lastSeenSecs: root.peer ? (root.peer.lastSeen || 0) : 0
            statusEmoji: root.peer ? (root.peer.statusEmoji || "") : ""
        }

        FormCard.FormHeader {
            Layout.fillWidth: true
            title: i18nc("@title:group", "Session")
        }

        // Reachability and session state belong to a peer and have no equivalent on
        // your own page, so this section exists only here.
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

        FormCard.FormHeader {
            Layout.fillWidth: true
            title: i18nc("@title:group free-form text about the peer", "About")
        }

        FormCard.FormCard {
            Layout.fillWidth: true

            // Markdown, like the own-profile bio, so the same text renders the same
            // way whichever side of the conversation it is read from.
            FormCard.AbstractFormDelegate {
                id: bioDelegate
                background: null
                focusPolicy: Qt.NoFocus

                readonly property bool hasBio: root.peer && root.peer.bio && root.peer.bio.length > 0

                contentItem: Kirigami.SelectableLabel {
                    text: bioDelegate.hasBio ? root.peer.bio : i18nc("@info", "No description")
                    textFormat: Text.MarkdownText
                    wrapMode: Text.WordWrap
                    color: bioDelegate.hasBio ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                }
            }
        }

        FormCard.FormHeader {
            Layout.fillWidth: true
            title: i18nc("@title profile section", "Friends")
        }

        FormCard.FormCard {
            Layout.fillWidth: true

            FormCard.FormTextDelegate {
                text: i18nc("@info the friend list is empty", "No one yet")
            }
        }

        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 8
            icon.name: "folder"
            text: i18nc("@info", "Will appear once connected to a K-Server")
        }
    }
}
