// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import koutnet.app

// one tab per transport, since each opens a chat differently.
// LAN/VPN and Matrix are live; Rocket.Chat, Telegram and Tox are scaffolded
// and open a preview page instead of an action form, so the picker never
// offers a backend the app cannot show.
Kirigami.Page {
    id: root

    property alias peers: peerView.model

    // Matrix rows only show while an account is signed in
    readonly property bool matrixAvailable: matrixManager.loggedIn

    signal chatRequested(string ip)
    signal roomJoinRequested(string aliasOrId)
    signal roomCreateRequested(string name, string topic, string alias, string invites, bool isPrivate)
    signal directChatRequested(string userId)

    title: i18nc("@title:window", "New chat")
    padding: 0

    // See the note on Kirigami.Theme in Main.qml.
    Kirigami.Theme.highlightColor: Brand.accent

    function startChatWith(ip) {
        const trimmed = ip.trim()
        if (trimmed.length > 0)
            root.chatRequested(trimmed)
    }

    // a Matrix id is @localpart:domain; the server drops invites to a bad id
    // without error, so a typo looks like a sent invite that never arrived.
    // check the shape here. the localpart is case sensitive, so do not lower it,
    // just flag a capital letter since most servers use lowercase ids.
    function matrixIdError(id) {
        const trimmed = id.trim()
        if (trimmed.length === 0)
            return ""
        if (!trimmed.startsWith("@"))
            return i18nc("@info:status a Matrix id was typed without its leading @", "A Matrix id starts with @, as in @name:server.")
        const colon = trimmed.indexOf(":")
        if (colon < 0)
            return i18nc("@info:status a Matrix id was typed without a server part", "A Matrix id needs a server, as in @name:server.")
        const localpart = trimmed.substring(1, colon)
        const domain = trimmed.substring(colon + 1)
        if (localpart.length === 0)
            return i18nc("@info:status a Matrix id was typed with nothing before the colon", "There is no name before the colon in this Matrix id.")
        if (domain.length === 0)
            return i18nc("@info:status a Matrix id was typed with nothing after the colon", "There is no server after the colon in this Matrix id.")
        return ""
    }

    // an upper case localpart is usually a typo, but a few accounts have one, so only warn
    function matrixIdWarning(id) {
        const trimmed = id.trim()
        if (trimmed.length === 0 || root.matrixIdError(trimmed).length > 0)
            return ""
        const colon = trimmed.indexOf(":")
        const localpart = trimmed.substring(1, colon)
        if (localpart !== localpart.toLowerCase())
            return i18nc("@info:status a Matrix id has an upper case letter, which is usually a typo", "This id has a capital letter. Most servers register names in lower case, so check it is exactly right.")
        return ""
    }

    // check each id in the comma list, return the first error
    function inviteListError(list) {
        const parts = list.split(",").map((s) => s.trim()).filter((s) => s.length > 0)
        for (let i = 0; i < parts.length; i++) {
            const err = root.matrixIdError(parts[i])
            if (err.length > 0)
                return err
        }
        return ""
    }

    // The five unified transports. LAN and Matrix are live; Rocket.Chat,
    // Telegram and Tox are scaffolded and open a preview page instead of an
    // action form, so the picker never offers a backend the app cannot show.
    header: QQC2.TabBar {
        id: tabBar

        QQC2.TabButton { text: i18nc("@title:tab a transport", "Telegram") }
        QQC2.TabButton { text: i18nc("@title:tab a transport", "Matrix") }
        QQC2.TabButton { text: i18nc("@title:tab a transport", "Rocket.Chat") }
        QQC2.TabButton { text: i18nc("@title:tab a transport", "LAN / VPN") }
        QQC2.TabButton { text: i18nc("@title:tab a transport", "Tox") }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: tabBar.currentIndex

        // Telegram, not built yet
        Item {
            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.largeSpacing * 4
                icon.name: "send-to"
                text: i18nc("@title a transport", "Telegram")
                explanation: i18nc("@info a transport that is not implemented yet", "Not built yet.")
            }
        }

        // Matrix: join or create a room
        QQC2.ScrollView {
            id: matrixScroll

            contentWidth: availableWidth

            ColumnLayout {
                width: matrixScroll.availableWidth
                spacing: Kirigami.Units.largeSpacing

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: !root.matrixAvailable
                    type: Kirigami.MessageType.Information
                    text: i18nc("@info:status the Matrix tab needs a signed-in account", "Sign in to a Matrix account to join or create rooms.")
                }

                ColumnLayout {
                    visible: root.matrixAvailable
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18nc("@label:textbox", "Matrix room address")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.TextField {
                            id: roomAddressField
                            Layout.fillWidth: true
                            placeholderText: i18nc("@info:placeholder a Matrix room address or id", "#room:server")
                            onAccepted: root.roomJoinRequested(text.trim())
                        }

                        QQC2.Button {
                            text: i18nc("@action:button join the room whose address was typed", "Join")
                            icon.name: "go-jump"
                            highlighted: true
                            enabled: roomAddressField.text.trim().length > 0
                            onClicked: root.roomJoinRequested(roomAddressField.text.trim())
                        }
                    }
                }

                ColumnLayout {
                    visible: root.matrixAvailable
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18nc("@label:textbox", "Private chat with a Matrix user")
                    }

                    QQC2.TextField {
                        id: directUserField
                        Layout.fillWidth: true
                        placeholderText: i18nc("@info:placeholder a Matrix identity to talk to one-on-one", "@user:server")
                        onAccepted: if (directUserField.acceptable) root.directChatRequested(text.trim())

                        readonly property string idError: root.matrixIdError(directUserField.text)
                        readonly property string idWarning: root.matrixIdWarning(directUserField.text)
                        readonly property bool acceptable: directUserField.text.trim().length > 0 && directUserField.idError.length === 0
                    }

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        visible: directUserField.idError.length > 0 || directUserField.idWarning.length > 0
                        type: directUserField.idError.length > 0 ? Kirigami.MessageType.Error : Kirigami.MessageType.Warning
                        text: directUserField.idError.length > 0 ? directUserField.idError : directUserField.idWarning
                    }

                    QQC2.Button {
                        text: i18nc("@action:button open a private chat with the user typed above", "Open private chat")
                        icon.name: "mail-message-new"
                        highlighted: true
                        enabled: directUserField.acceptable
                        onClicked: root.directChatRequested(directUserField.text.trim())
                    }
                }

                ColumnLayout {
                    visible: root.matrixAvailable
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18nc("@label:textbox", "New room")
                    }

                    QQC2.TextField {
                        id: roomNameField
                        Layout.fillWidth: true
                        placeholderText: i18nc("@info:placeholder a name for the new room", "Room name")
                    }

                    QQC2.TextField {
                        id: roomTopicField
                        Layout.fillWidth: true
                        placeholderText: i18nc("@info:placeholder the topic of the new room", "Topic (optional)")
                    }

                    QQC2.TextField {
                        id: roomAliasField
                        Layout.fillWidth: true
                        placeholderText: i18nc("@info:placeholder a Matrix address to give the new room", "Address like #room:server (optional)")
                    }

                    QQC2.TextField {
                        id: roomInvitesField
                        Layout.fillWidth: true
                        placeholderText: i18nc("@info:placeholder Matrix addresses of people to invite, separated by commas", "Invite @user:server, ... (optional)")

                        readonly property string listError: root.inviteListError(roomInvitesField.text)
                    }

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        visible: roomInvitesField.listError.length > 0
                        type: Kirigami.MessageType.Error
                        text: roomInvitesField.listError
                    }

                    QQC2.CheckBox {
                        id: roomPrivateBox
                        text: i18nc("@option:check create a room that does not appear in the public directory", "Private")
                        checked: true
                    }

                    QQC2.Button {
                        Layout.fillWidth: true
                        text: i18nc("@action:button create a room with what was typed above", "Create room")
                        icon.name: "list-add"
                        highlighted: true
                        enabled: roomNameField.text.trim().length > 0 && roomInvitesField.listError.length === 0
                        onClicked: root.roomCreateRequested(
                            roomNameField.text.trim(),
                            roomTopicField.text.trim(),
                            roomAliasField.text.trim(),
                            roomInvitesField.text.trim(),
                            roomPrivateBox.checked)
                    }
                }
            }
        }

        // Rocket.Chat, not built yet
        Item {
            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.largeSpacing * 4
                icon.name: "chat-partner"
                text: i18nc("@title a transport", "Rocket.Chat")
                explanation: i18nc("@info a transport that is not implemented yet", "Not built yet.")
            }
        }

        // LAN / VPN: typed address or presence broadcast
        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.largeSpacing

            ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18nc("@label:textbox", "Address of the peer")
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.TextField {
                        id: manualPeerField
                        Layout.fillWidth: true
                        placeholderText: i18nc("@info:placeholder an IPv4 address", "192.168.1.42")
                        onAccepted: root.startChatWith(text)
                    }

                    QQC2.Button {
                        text: i18nc("@action:button open a chat with the address that was typed", "Open")
                        icon.name: "document-open"
                        highlighted: true
                        enabled: manualPeerField.text.trim().length > 0
                        onClicked: root.startChatWith(manualPeerField.text)
                    }
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            ListView {
                id: peerView

                Layout.fillWidth: true
                Layout.fillHeight: true

                header: Kirigami.ListSectionHeader {
                    width: peerView.width
                    text: i18nc("@title:group peers whose presence broadcasts are arriving", "On the network now")
                }

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.largeSpacing * 4
                    visible: peerView.count === 0
                    icon.name: "network-wireless"
                    text: i18nc("@info", "Nobody is broadcasting")
                    explanation: i18nc("@info", "KOutNet is listening, but it's quiet here for now...")
                }

                delegate: ContactDelegate {
                    displayName: model.display_name || model.username || model.ip
                    preview: model.ip
                    online: true
                    onClicked: root.startChatWith(model.ip)
                }
            }
        }

        // Tox, not built yet
        Item {
            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.largeSpacing * 4
                icon.name: "user-available"
                text: i18nc("@title a transport", "Tox")
                explanation: i18nc("@info a transport that is not implemented yet", "Not built yet.")
            }
        }
    }
}
