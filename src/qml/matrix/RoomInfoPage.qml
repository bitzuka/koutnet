// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import org.kde.kirigamiaddons.formcard as FormCard
import koutnet.app

// PeerInfoPage's opposite number. That one answers "who is on the other end of
// this" - a fingerprint, an operating system, the addresses their packets came
// from. A room has no other end and none of those questions apply to it, so this
// answers the ones that do: what the room is for, what it is called on the
// network, whether it is encrypted, and who is in it.
//
// The two pages share a shape on purpose - the same banner, the same folding
// ChatSection headings, the same FormCard bodies - so that switching between a
// room and a LAN chat does not feel like switching applications. They share no
// content, because they are not describing the same kind of thing.
Kirigami.ScrollablePage {
    id: root

    // The "mx:" chat id, not the bare room id. Everything is read back off this
    // rather than passed in, so switching conversation with the column open
    // re-points it.
    property string chatId: ""

    property bool topicExpanded: true
    property bool encryptionExpanded: true
    // Folded away to begin with: the id and the aliases are the one thing here
    // that would be pasted somewhere, and that should take a deliberate click.
    property bool addressExpanded: false
    property bool membersExpanded: true

    signal memberActivated(string userId, Item anchorItem)
    signal leaveRequested(string chatId)
    signal notifyRequested(string text)

    // Filled by refresh(). Function calls rather than properties on the bridge,
    // because a member list nobody is looking at should cost nothing - see the
    // note at the top of MatrixRoomBridge.h.
    property var info: null
    property var members: []

    readonly property string roomName: root.info ? (root.info.displayName || "") : ""
    readonly property string topic: root.info ? (root.info.topic || "") : ""
    readonly property string canonicalAlias: root.info ? (root.info.canonicalAlias || "") : ""
    readonly property var altAliases: root.info ? (root.info.altAliases || []) : []
    readonly property string roomId: root.info ? (root.info.roomId || "") : ""
    readonly property bool encrypted: root.info ? root.info.encrypted === true : false
    readonly property int joinedCount: root.info ? (root.info.joinedCount || 0) : 0
    readonly property int invitedCount: root.info ? (root.info.invitedCount || 0) : 0

    title: i18nc("@title:window the topic, address and members of a Matrix room", "Room information")

    // See Main.qml: a ScrollablePage starts a theme chain of its own.
    Kirigami.Theme.highlightColor: Brand.accent

    function refresh() {
        if (root.chatId.length === 0) {
            root.info = null
            root.members = []
            return
        }
        root.info = matrixRooms.roomInfo(root.chatId)
        root.members = matrixRooms.roomMembers(root.chatId)
    }

    onChatIdChanged: root.refresh()
    Component.onCompleted: root.refresh()

    Connections {
        target: matrixRooms
        function onRoomInfoChanged(chatId) {
            if (chatId === root.chatId)
                root.refresh()
        }
    }

    // Same trick as ChatPage: there is no clipboard object in QML without a C++
    // helper for it, and one off-screen editor is cheaper than the helper.
    TextEdit {
        id: clipboardHelper
        visible: false
        function copyText(str) {
            text = str
            selectAll()
            copy()
        }
    }

    actions: [
        Kirigami.Action {
            text: i18nc("@action:button copy the room's address so it can be pasted elsewhere", "Copy address")
            icon.name: "edit-copy"
            visible: root.canonicalAlias.length > 0 || root.roomId.length > 0
            onTriggered: {
                clipboardHelper.copyText(root.canonicalAlias.length > 0 ? root.canonicalAlias : root.roomId)
                root.notifyRequested(i18nc("@info:status", "Room address copied to the clipboard"))
            }
        },
        Kirigami.Action {
            text: i18nc("@action:button leave a Matrix room", "Leave room")
            icon.name: "window-close"
            visible: root.roomId.length > 0
            onTriggered: leaveConfirm.open()
        }
    ]

    // Asked about rather than done: rejoining an invite-only room is not this
    // window's to arrange.
    Kirigami.PromptDialog {
        id: leaveConfirm

        // See the note on Kirigami.Theme in Main.qml.
        Kirigami.Theme.inherit: false
        Kirigami.Theme.highlightColor: Brand.accent

        title: i18nc("@title:window", "Leave this room?")
        subtitle: i18nc("@info", "You will stop receiving messages from it. Getting back in may need a new invitation.")
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: i18nc("@action:button confirm leaving a Matrix room", "Leave")
                icon.name: "window-close"
                onTriggered: {
                    root.leaveRequested(root.chatId)
                    leaveConfirm.close()
                }
            }
        ]
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
                name: root.roomName
                source: root.info ? (root.info.avatarUrl || "") : ""
            }

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 3
                text: root.roomName
                textFormat: Text.PlainText
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
            }

            QQC2.Label {
                Layout.fillWidth: true
                visible: root.canonicalAlias.length > 0
                text: root.canonicalAlias
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                horizontalAlignment: Text.AlignHCenter
                color: Kirigami.Theme.disabledTextColor
            }

            QQC2.Label {
                Layout.fillWidth: true
                // One string rather than two joined with a space: the order of
                // the two counts is a translator's to decide.
                text: root.invitedCount > 0
                    ? i18ncp("@info:status %1 is how many people are in the room, %2 how many have been invited and not joined",
                             "%1 member, %2 invited", "%1 members, %2 invited", root.joinedCount, root.invitedCount)
                    : i18ncp("@info:status %1 is how many people are in the room",
                             "%1 member", "%1 members", root.joinedCount)
                textFormat: Text.PlainText
                horizontalAlignment: Text.AlignHCenter
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
            }
        }

        ChatSection {
            Layout.fillWidth: true
            text: i18nc("@title:group what a Matrix room is for", "Topic")
            expanded: root.topicExpanded
            onToggleRequested: root.topicExpanded = !root.topicExpanded
        }

        FormCard.FormCard {
            Layout.fillWidth: true
            visible: root.topicExpanded

            FormCard.AbstractFormDelegate {
                id: topicDelegate
                Layout.fillWidth: true
                background: null
                focusPolicy: Qt.NoFocus

                readonly property bool hasTopic: root.topic.length > 0

                contentItem: Kirigami.SelectableLabel {
                    text: topicDelegate.hasTopic
                        ? root.topic
                        : i18nc("@info the room has set no topic", "No topic set")
                    // A topic is plain text in the protocol; rendering it as
                    // anything else would let a room write markup into this column.
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    color: topicDelegate.hasTopic ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                }
            }
        }

        ChatSection {
            Layout.fillWidth: true
            text: i18nc("@title:group whether this room is encrypted", "Encryption")
            expanded: root.encryptionExpanded
            onToggleRequested: root.encryptionExpanded = !root.encryptionExpanded
        }

        FormCard.FormCard {
            Layout.fillWidth: true
            visible: root.encryptionExpanded

            FormCard.FormTextDelegate {
                icon.name: root.encrypted ? "security-high" : "security-low"
                text: root.encrypted
                    ? i18nc("@info:status the room is end to end encrypted", "End to end encrypted")
                    : i18nc("@info:status the room is not encrypted", "Not encrypted")
                description: root.encrypted
                    ? i18nc("@info:whatsthis",
                            "Messages here are encrypted for the devices in the room, so the homeserver stores them but cannot read them. Messages sent before this device joined stay unreadable, because their keys were never sent to it.")
                    : i18nc("@info:whatsthis",
                            "Messages here are readable by the homeserver and by anybody it federates with.")
            }
        }

        ChatSection {
            Layout.fillWidth: true
            text: i18nc("@title:group what this room is called on the network", "Address")
            expanded: root.addressExpanded
            onToggleRequested: root.addressExpanded = !root.addressExpanded
        }

        FormCard.FormCard {
            Layout.fillWidth: true
            visible: root.addressExpanded

            FormCard.FormTextDelegate {
                id: aliasDelegate
                visible: root.canonicalAlias.length > 0
                icon.name: "globe"
                text: i18nc("@label the address a room is published under", "Main address")
                description: root.canonicalAlias
            }

            Repeater {
                model: root.altAliases

                delegate: FormCard.FormTextDelegate {
                    required property string modelData

                    icon.name: "globe"
                    text: i18nc("@label another address the same room answers to", "Also reachable as")
                    description: modelData
                }
            }

            FormCard.FormTextDelegate {
                // The internal id is last and is never the thing that is
                // offered first: it is what the protocol routes on, not what
                // anybody would type.
                icon.name: "code-context"
                text: i18nc("@label the room's internal identifier", "Room identifier")
                description: root.roomId
            }
        }

        ChatSection {
            Layout.fillWidth: true
            text: i18nc("@title:group the people in a Matrix room", "Members")
            itemCount: root.members.length
            expanded: root.membersExpanded
            onToggleRequested: root.membersExpanded = !root.membersExpanded
        }

        FormCard.FormCard {
            Layout.fillWidth: true
            visible: root.membersExpanded

            FormCard.FormTextDelegate {
                visible: root.members.length === 0
                icon.name: "user-group-new"
                text: i18nc("@info the member list has not arrived yet", "No members loaded")
                description: i18nc("@info:whatsthis",
                                   "A homeserver sends the member list lazily, so it may fill in a moment after the room opens.")
            }

            Repeater {
                model: root.members

                delegate: FormCard.AbstractFormDelegate {
                    id: memberDelegate

                    required property var modelData

                    readonly property string powerLabel: RoomRoles.label(memberDelegate.modelData.powerLevel || 0)

                    Layout.fillWidth: true
                    // The card is hung off this row, so this row is what reports
                    // the click - the same contract ContactDelegate has.
                    onClicked: root.memberActivated(memberDelegate.modelData.userId, memberDelegate)

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.largeSpacing

                        Components.Avatar {
                            Layout.alignment: Qt.AlignVCenter
                            implicitWidth: Kirigami.Units.iconSizes.medium
                            implicitHeight: Kirigami.Units.iconSizes.medium
                            name: memberDelegate.modelData.displayName || ""
                            source: memberDelegate.modelData.avatarUrl || ""
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            QQC2.Label {
                                Layout.fillWidth: true
                                text: memberDelegate.modelData.displayName || ""
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                            }

                            // The identifier under the name, because two people
                            // in one room are allowed to call themselves the
                            // same thing and only this tells them apart.
                            QQC2.Label {
                                Layout.fillWidth: true
                                text: memberDelegate.modelData.userId || ""
                                textFormat: Text.PlainText
                                elide: Text.ElideMiddle
                                font: Kirigami.Theme.smallFont
                                color: Kirigami.Theme.disabledTextColor
                            }
                        }

                        QQC2.Label {
                            Layout.alignment: Qt.AlignVCenter
                            visible: memberDelegate.powerLabel.length > 0
                            text: memberDelegate.powerLabel
                            textFormat: Text.PlainText
                            font: Kirigami.Theme.smallFont
                            color: Kirigami.Theme.highlightColor
                        }
                    }
                }
            }
        }
    }
}
