// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import koutnet.app

// Discovery by handle is the next piece of work and deliberately not here yet;
// until it lands a chat is started from a presence broadcast or a typed address.
// When it arrives it replaces the contents of this page and nothing else:
// everything downstream only ever sees chatRequested being emitted.
//
// The Matrix half of this page is also discovery, in its own sense: a room is
// opened by its address, or made from scratch, and both are answered by the
// homeserver rather than by a presence broadcast. There is no room list to
// browse here - that needs the public rooms directory, which this build does
// not poll - so the page offers exactly the two entries into a room there are.
Kirigami.ScrollablePage {
    id: root

    property alias peers: peerView.model

    // The Matrix rows below are only offered while an account is signed in -
    // without one there is no homeserver to join or create a room on.
    readonly property bool matrixAvailable: matrixManager.loggedIn

    signal chatRequested(string ip)
    signal roomJoinRequested(string aliasOrId)
    signal roomCreateRequested(string name, string topic, string alias, string invites, bool isPrivate)
    signal directChatRequested(string userId)

    title: i18nc("@title:window", "New chat")

    // See the note on Kirigami.Theme in Main.qml.
    Kirigami.Theme.highlightColor: Brand.accent

    function startChatWith(ip) {
        const trimmed = ip.trim()
        if (trimmed.length > 0)
            root.chatRequested(trimmed)
    }

    header: QQC2.Control {
        padding: Kirigami.Units.largeSpacing

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.largeSpacing

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

            // Both entries are Matrix-only: the LAN transport has no rooms to
            // join and none to make, so both rows stay question marks until a
            // signed-in account is present, which the window knows as the
            // matrix transport being able to name one.
            Kirigami.Separator {
                Layout.fillWidth: true
                visible: root.matrixAvailable
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
                    onAccepted: root.directChatRequested(text.trim())
                }

                QQC2.Button {
                    text: i18nc("@action:button open a private chat with the user typed above", "Open private chat")
                    icon.name: "mail-message-new"
                    highlighted: true
                    enabled: directUserField.text.trim().length > 0
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
                    enabled: roomNameField.text.trim().length > 0
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

    ListView {
        id: peerView

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
