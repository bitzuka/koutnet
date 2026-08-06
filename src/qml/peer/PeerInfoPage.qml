// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import org.kde.kirigamiaddons.formcard as FormCard
import koutnet.app

// A group messenger would put a member list here. A direct-connection messenger has
// one member and something more interesting to say about them: which key they proved
// they hold, whether the session is really encrypted, and which addresses their
// packets came from - all three verified here rather than claimed by the peer.
Kirigami.ScrollablePage {
    id: root

    // The map Main.qml builds: { ip, username, displayName, bio, os, e2e,
    // avatarLetter, isFavorites, online, lastSeen }.
    property var peer: null

    property bool encryptionExpanded: true
    property bool systemExpanded: true
    // Folded away to begin with: the addresses are the one thing here that says
    // where somebody is, and that should take a deliberate click.
    property bool addressExpanded: false
    property bool aboutExpanded: true

    signal profileRequested()

    readonly property string peerIp: root.peer ? (root.peer.ip || "") : ""
    readonly property string shownName: root.peer
        ? (root.peer.displayName && root.peer.displayName.length > 0
            ? root.peer.displayName
            : (root.peer.username
                || i18nc("@info a peer that has published no name of its own", "Unknown peer")))
        : ""

    // Function calls into the crypto engine rather than properties, so nothing
    // notifies on its own and refresh() stands in for that.
    property string fingerprint: ""
    property bool encrypted: false
    property var addresses: []

    title: i18nc("@title:window information about the peer on the other end", "Peer details")

    // See Main.qml: a ScrollablePage starts a theme chain of its own.
    Kirigami.Theme.highlightColor: Brand.accent

    function refresh() {
        if (root.peerIp.length === 0) {
            root.fingerprint = ""
            root.encrypted = false
            root.addresses = []
            return
        }
        root.fingerprint = cryptoManager.peerFingerprint(root.peerIp)
        root.encrypted = cryptoManager.hasSession(root.peerIp)
        const identity = cryptoManager.identityForAddress(root.peerIp)
        root.addresses = identity.length > 0 ? cryptoManager.addressesFor(identity) : []
    }

    onPeerChanged: root.refresh()
    Component.onCompleted: root.refresh()

    Connections {
        target: networkManager
        function onUserOnline(peerInfo) {
            if (peerInfo.ip === root.peerIp)
                root.refresh()
        }
    }

    Connections {
        target: cryptoManager
        function onPeerIdentityChanged(address, oldFingerprint, newFingerprint) {
            if (address === root.peerIp)
                root.refresh()
        }
    }

    ColumnLayout {
        spacing: 0

        // Not a banner: this column is fifteen grid units wide at its narrowest.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            Components.Avatar {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: Kirigami.Units.gridUnit * 5
                implicitHeight: Kirigami.Units.gridUnit * 5
                name: root.shownName
            }

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 3
                text: root.shownName
                textFormat: Text.PlainText
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
            }

            QQC2.Label {
                Layout.fillWidth: true
                visible: root.peer && root.peer.username && root.peer.username.length > 0
                text: root.peer ? i18nc("@info a peer's handle, %1 is the user name", "@%1", root.peer.username || "") : ""
                textFormat: Text.PlainText
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                color: Kirigami.Theme.disabledTextColor
            }

            // RelativeTime.now is read so this ages with the column open.
            QQC2.Label {
                Layout.fillWidth: true
                text: root.peer
                    ? RelativeTime.presenceLabel(root.peer.online === true, root.peer.lastSeen || 0, RelativeTime.now)
                    : ""
                textFormat: Text.PlainText
                horizontalAlignment: Text.AlignHCenter
                font: Kirigami.Theme.smallFont
                color: (root.peer && root.peer.online === true)
                    ? Kirigami.Theme.positiveTextColor
                    : Kirigami.Theme.disabledTextColor
            }

            QQC2.Button {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Kirigami.Units.smallSpacing
                text: i18nc("@action:button open the peer's full profile page", "Open profile")
                icon.name: "user-identity"
                onClicked: root.profileRequested()
            }
        }

        ChatSection {
            Layout.fillWidth: true
            text: i18nc("@title:group whether this conversation is encrypted", "Encryption")
            expanded: root.encryptionExpanded
            onToggleRequested: root.encryptionExpanded = !root.encryptionExpanded
        }

        FormCard.FormCard {
            Layout.fillWidth: true
            visible: root.encryptionExpanded

            FormCard.FormTextDelegate {
                id: sessionDelegate
                icon.name: root.encrypted ? "security-high" : "security-low"
                text: root.encrypted
                    ? i18nc("@info:status the session with this peer is end to end encrypted", "End to end encrypted")
                    : i18nc("@info:status no key exchange has happened with this peer", "Not encrypted")
                description: root.encrypted
                    ? i18nc("@info:whatsthis", "A key exchange with this peer succeeded. Messages and calls are encrypted with X25519 and AES-256-GCM.")
                    : i18nc("@info:whatsthis", "No key exchange has happened with this peer yet, so nothing sent to it is protected.")
            }

            FormCard.FormDelegateSeparator { above: sessionDelegate; below: fingerprintDelegate }

            // Reduced to something two people can read to each other: comparing it
            // out of band is the only thing that turns trust-on-first-use into trust.
            FormCard.AbstractFormDelegate {
                id: fingerprintDelegate
                Layout.fillWidth: true
                background: null
                focusPolicy: Qt.NoFocus

                contentItem: ColumnLayout {
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18nc("@label the peer's identity key, shortened for reading aloud", "Identity fingerprint")
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                    }

                    Kirigami.SelectableLabel {
                        Layout.fillWidth: true
                        text: root.fingerprint.length > 0 && root.fingerprint !== "?"
                            ? root.fingerprint
                            : i18nc("@info the peer has never proved which key it holds", "Not established")
                        // Fixed pitch so the groups line up against the other copy.
                        font.family: "monospace"
                        wrapMode: Text.WrapAnywhere
                        color: root.fingerprint.length > 0 && root.fingerprint !== "?"
                            ? Kirigami.Theme.textColor
                            : Kirigami.Theme.disabledTextColor
                    }
                }
            }
        }

        ChatSection {
            Layout.fillWidth: true
            text: i18nc("@title:group what the peer is running", "System")
            expanded: root.systemExpanded
            onToggleRequested: root.systemExpanded = !root.systemExpanded
        }

        FormCard.FormCard {
            Layout.fillWidth: true
            visible: root.systemExpanded

            FormCard.FormTextDelegate {
                icon.name: "computer"
                text: i18nc("@label the peer's operating system", "Operating system")
                description: (root.peer && root.peer.os && root.peer.os.length > 0)
                    ? root.peer.os
                    : i18nc("@info the peer did not say what it is running", "Not advertised")
            }
        }

        ChatSection {
            Layout.fillWidth: true
            text: i18nc("@title:group the network addresses this peer has answered on", "Addresses")
            itemCount: root.addresses.length
            expanded: root.addressExpanded
            onToggleRequested: root.addressExpanded = !root.addressExpanded
        }

        FormCard.FormCard {
            Layout.fillWidth: true
            visible: root.addressExpanded

            FormCard.FormTextDelegate {
                visible: root.addresses.length === 0
                icon.name: "network-disconnect"
                text: i18nc("@info no packet from this peer has been verified yet", "No verified address")
                description: i18nc("@info:whatsthis",
                                   "Addresses appear here once a signed packet has actually arrived from one. What a peer advertises about itself is not listed.")
            }

            Repeater {
                model: root.addresses

                delegate: FormCard.FormTextDelegate {
                    required property int index
                    required property string modelData

                    icon.name: "network-wired"
                    text: modelData
                    // Newest first, so the top one is where a message will be sent.
                    description: index === 0
                        ? i18nc("@info:whatsthis the address most recently heard from", "Last heard from here")
                        : i18nc("@info:whatsthis an address this peer has used before", "Also seen here")
                }
            }
        }

        ChatSection {
            Layout.fillWidth: true
            text: i18nc("@title:group free-form text the peer wrote about itself", "About")
            expanded: root.aboutExpanded
            onToggleRequested: root.aboutExpanded = !root.aboutExpanded
        }

        FormCard.FormCard {
            Layout.fillWidth: true
            visible: root.aboutExpanded

            FormCard.AbstractFormDelegate {
                id: bioDelegate
                Layout.fillWidth: true
                background: null
                focusPolicy: Qt.NoFocus

                readonly property bool hasBio: root.peer && root.peer.bio && root.peer.bio.length > 0

                contentItem: Kirigami.SelectableLabel {
                    text: bioDelegate.hasBio
                        ? root.peer.bio
                        : i18nc("@info the peer wrote nothing about itself", "No description")
                    textFormat: bioDelegate.hasBio ? Text.MarkdownText : Text.PlainText
                    wrapMode: Text.WordWrap
                    color: bioDelegate.hasBio ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                }
            }
        }
    }
}
