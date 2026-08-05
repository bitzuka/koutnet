// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import koutnet.app

// The seam for peer discovery by handle, which is the next piece of work and is
// deliberately not here. Until it lands, a conversation with somebody who is not
// in the list yet is started either by picking them out of the peers currently
// broadcasting presence, or by typing an address.
//
// When discovery by handle arrives it replaces the contents of this page and
// nothing else: everything downstream only ever sees chatRequested being emitted.
Kirigami.ScrollablePage {
    id: root

    // The live presence model, not the conversation list.
    property alias peers: peerView.model

    signal chatRequested(string ip)

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
