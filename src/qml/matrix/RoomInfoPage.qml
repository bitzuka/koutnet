// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import org.kde.kirigamiaddons.formcard as FormCard
import QtQuick.Dialogs as Dialogs
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
    property bool settingsExpanded: false
    property bool pinnedExpanded: false
    property bool searchExpanded: false

    signal memberActivated(string userId, Item anchorItem)
    signal leaveRequested(string chatId)
    signal notifyRequested(string text)
    // The dialog lives in the window, not in this column: a verification
    // outlives the column being shut, and it is about the account rather than
    // about this room.
    signal verifySessionsRequested()

    // Filled by refresh(). Function calls rather than properties on the bridge,
    // because a member list nobody is looking at should cost nothing - see the
    // note at the top of MatrixRoomBridge.h.
    property var info: null
    property var members: []
    // Pinned messages (from roomPinnedChanged) and search results, kept here so
    // the two sections below can list them without another bridge round trip.
    property var pinned: []
    property var searchResults: []

    readonly property string roomName: root.info ? (root.info.displayName || "") : ""
    readonly property string topic: root.info ? (root.info.topic || "") : ""
    readonly property string canonicalAlias: root.info ? (root.info.canonicalAlias || "") : ""
    readonly property var altAliases: root.info ? (root.info.altAliases || []) : []
    readonly property string roomId: root.info ? (root.info.roomId || "") : ""
    readonly property bool encrypted: root.info ? root.info.encrypted === true : false
    // Whether the session can do encryption, as opposed to whether the room
    // wants it. An encrypted room in a session with no key store is a room
    // nothing can be read from or written to, and it must not wear a padlock.
    readonly property bool encryptionActive: root.info ? root.info.encryptionActive === true : false
    // False when libQuotient's trust tables could not be asked. Kept apart from
    // the answers themselves, because "nobody is verified" and "nobody was
    // asked" are different statements and only one of them is ever true here.
    readonly property bool trustKnown: root.info ? root.info.trustKnown === true : false
    readonly property bool ownSessionsVerified: root.info ? root.info.ownSessionsVerified === true : false
    readonly property bool keyBackupAvailable: root.info ? root.info.keyBackupAvailable === true : false
    readonly property bool keyBackupUnlocked: root.info ? root.info.keyBackupUnlocked === true : false
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
        root.info = chatTransport.roomInfo(root.chatId)
        root.members = chatTransport.roomMembers(root.chatId)
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

    // Unlocking the backup re-arms the restore button and flips its label, and
    // that state lives on the account rather than on this room.
    Connections {
        target: matrixManager
        function onKeyBackupUnlocked() {
            root.refresh()
        }
    }

    // The pinned set and search results are emitted by the bridge with the chat
    // id; re-point them when they are about this room.
    Connections {
        target: matrixRooms
        function onRoomPinnedChanged(chatId, list) {
            if (chatId === root.chatId)
                root.pinned = list
        }
        function onRoomSearchResults(chatId, list) {
            if (chatId === root.chatId)
                root.searchResults = list
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
            text: i18nc("@action:button back to the conversation from the room information page", "Back")
            icon.name: "go-previous"
            // The global toolbar draws no back arrow for a third column that
            // outlived the wide layout: it only knows about the first two.
            visible: !applicationWindow().pageStack.wideMode
            onTriggered: applicationWindow().pageStack.pop()
        },
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

                // The room's picture is one tap from a new one; the picker hands
                // the path to the bridge, which uploads and points the state at it.
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: avatarDialog.open()
                }
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
                icon.name: !root.encrypted ? "security-low" : (root.encryptionActive ? "security-high" : "dialog-warning")
                text: !root.encrypted
                    ? i18nc("@info:status the room is not encrypted", "Not encrypted")
                    : (root.encryptionActive
                        ? i18nc("@info:status the room is end to end encrypted", "End to end encrypted")
                        : i18nc("@info:status the room is encrypted but this session has no keys", "Encrypted, and unreadable here"))
                description: !root.encrypted
                    ? i18nc("@info:whatsthis",
                            "Messages here are readable by the homeserver and by anybody it federates with.")
                    : (root.encryptionActive
                        ? i18nc("@info:whatsthis",
                                "Messages here are encrypted for the devices in the room, so the homeserver stores them but cannot read them. Messages sent before this device joined stay unreadable, because their keys were never sent to it.")
                        : i18nc("@info:whatsthis",
                                "This session could not open its encryption keys, so nothing in this room can be read and nothing can be sent to it."))
            }

            // A second line, and never folded into the first. Encryption says
            // the homeserver cannot read the room; verification says whether the
            // devices it is encrypted for are the ones they claim to be. They
            // are different promises and one padlock cannot make both.
            FormCard.FormTextDelegate {
                visible: root.encrypted && root.encryptionActive
                icon.name: root.trustKnown && root.ownSessionsVerified ? "security-medium" : "security-low"
                text: !root.trustKnown
                    ? i18nc("@info:status device verification could not be checked", "Verification unknown")
                    : (root.ownSessionsVerified
                        ? i18nc("@info:status all of this account's own sessions are verified", "Your other sessions are verified")
                        : i18nc("@info:status some of this account's own sessions are unverified", "Some of your sessions are unverified"))
                description: !root.trustKnown
                    ? i18nc("@info:whatsthis",
                            "The trust table could not be read, so nothing here has been checked either way.")
                    : (root.ownSessionsVerified
                        ? i18nc("@info:whatsthis",
                                "Every other session on this account has been verified from here, so they will share their room keys with this one.")
                        : i18nc("@info:whatsthis",
                                "An unverified session is refused keys by some clients, which is why parts of an encrypted room can stay unreadable. Verify this one against another session of yours to fix that."))
            }

            // Separate from the line above on purpose: that line reports, and
            // it has to go on saying the same thing whether or not there is
            // anything to be done about it.
            FormCard.FormButtonDelegate {
                visible: root.encrypted && root.encryptionActive && root.trustKnown
                icon.name: "security-medium"
                text: i18nc("@action:button open the device verification dialog", "Verify sessions...")
                description: i18nc("@info:whatsthis",
                                   "Compare emoji with another Matrix session of yours to prove this one is the same person.")
                onClicked: root.verifySessionsRequested()
            }

            // The room key backup is the answer to "messages sent before this
            // device joined stay unreadable": an encrypted copy of the keys
            // waits on the homeserver, and typing the recovery key or the
            // passphrase back in unlocks the messages from the past. Only
            // offered once this session can actually use keys at all.
            FormCard.FormButtonDelegate {
                visible: root.encrypted && root.encryptionActive
                    && root.keyBackupAvailable === true
                icon.name: "drive-removable-media"
                text: root.keyBackupUnlocked
                    ? i18nc("@action:button the room key backup is already unlocked", "Room keys restored from backup")
                    : i18nc("@action:button open the dialog that unlocks the room key backup", "Restore room keys from backup...")
                description: root.keyBackupUnlocked
                    ? i18nc("@info:whatsthis",
                            "The backup on the homeserver was already unlocked in this session, so messages from the past appear as their keys arrive.")
                    : i18nc("@info:whatsthis",
                            "Ask the homeserver for the encrypted copy of the room keys. A recovery key or the backup passphrase unlocks it; messages sent before this device joined become readable after this.")
                enabled: !root.keyBackupUnlocked
                onClicked: keyBackupDialog.open()
            }
        }

        KeyBackupDialog {
            id: keyBackupDialog
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
            text: i18nc("@title:group edit the room's name and topic", "Room settings")
            expanded: false
            onToggleRequested: root.settingsExpanded = !root.settingsExpanded
        }

        FormCard.FormCard {
            Layout.fillWidth: true
            visible: root.settingsExpanded

            FormCard.FormTextFieldDelegate {
                label: i18nc("@label:textbox the room's display name", "Name")
                text: root.roomName
                onAccepted: matrixRooms.setRoomName(root.chatId, text)
            }

            FormCard.FormTextFieldDelegate {
                label: i18nc("@label:textbox the room's topic", "Topic")
                text: root.topic
                onAccepted: matrixRooms.setRoomTopic(root.chatId, text)
            }
        }

        ChatSection {
            Layout.fillWidth: true
            text: i18nc("@title:group messages pinned in this room", "Pinned messages")
            itemCount: root.pinned.length
            expanded: false
            onToggleRequested: root.pinnedExpanded = !root.pinnedExpanded
        }

        FormCard.FormCard {
            Layout.fillWidth: true
            visible: root.pinnedExpanded

            FormCard.FormTextDelegate {
                visible: root.pinned.length === 0
                text: i18nc("@info no messages are pinned in this room", "Nothing pinned yet")
            }

            Repeater {
                model: root.pinned

                delegate: FormCard.AbstractFormDelegate {
                    id: pinDelegate
                    required property var modelData
                    Layout.fillWidth: true

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: pinDelegate.modelData.text || pinDelegate.modelData.eventId
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            wrapMode: Text.WordWrap
                        }
                        QQC2.Button {
                            icon.name: "pin-remove"
                            text: i18nc("@action:button unpin a message", "Unpin")
                            onClicked: matrixRooms.unpinMessage(root.chatId, pinDelegate.modelData.ts)
                        }
                    }
                }
            }
        }

        ChatSection {
            Layout.fillWidth: true
            text: i18nc("@title:group search the room history", "Search")
            expanded: false
            onToggleRequested: root.searchExpanded = !root.searchExpanded
        }

        FormCard.FormCard {
            Layout.fillWidth: true
            visible: root.searchExpanded

            FormCard.FormTextFieldDelegate {
                id: searchField
                label: i18nc("@label:textbox search the room history", "Search")
                placeholderText: i18nc("@info:placeholder type to search messages", "Type to search messages")
                onAccepted: matrixRooms.searchMessages(root.chatId, text)
            }

            FormCard.FormTextDelegate {
                visible: root.searchResults.length === 0 && searchField.text.length > 0
                text: i18nc("@info no messages matched the search", "No matches")
            }

            Repeater {
                model: root.searchResults

                delegate: FormCard.AbstractFormDelegate {
                    id: resultDelegate
                    required property var modelData
                    Layout.fillWidth: true

                    contentItem: ColumnLayout {
                        spacing: 0
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: resultDelegate.modelData.text || resultDelegate.modelData.eventId
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            wrapMode: Text.WordWrap
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: resultDelegate.modelData.sender || ""
                            textFormat: Text.PlainText
                            font: Kirigami.Theme.smallFont
                            color: Kirigami.Theme.disabledTextColor
                        }
                    }
                }
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

        Dialogs.FileDialog {
            id: avatarDialog
            title: i18nc("@title:window", "Set room avatar")
            nameFilters: [i18nc("@item file type filter", "Images (*.png *.jpg *.jpeg *.gif *.webp)"), i18nc("@item file type filter", "All files (*)")]
            onAccepted: matrixRooms.setRoomAvatar(root.chatId, selectedFile.toString().replace("file://", ""))
        }
    }
}
