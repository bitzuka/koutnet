// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import koutnet.app

// A plain Page with a RowLayout in it rather than a ScrollablePage, because the
// rail runs the full height of the column beside a list that scrolls on its own,
// and a ScrollablePage would scroll the rail with it.
//
// The section headings sit above the view rather than inside it, and the pinned
// Favorites row with them. As the ListView's header, folding one changed the
// header's height, and QQuickItemView answers a header resize with
// updateHeader() and fixupPosition() and nothing else: shrinking clamps contentY
// back into bounds, growing does not, so the height the header just got back
// appears above the viewport and the section that was unfolded is off the top.
Kirigami.Page {
    id: root

    // Handed to both views below rather than to one, so it cannot be an alias.
    property var model: null
    property string selectedChatId: ""
    property string favoritesChatId: ""
    property int connectionMode: 0

    property bool micMuted: false
    property bool deafened: false

    // In compact mode the rail goes - its modes are all on the settings page and
    // the buttons are wider than the list - and so do the headings.
    property bool compact: false

    property bool favoritesExpanded: true
    property bool lanExpanded: true
    property bool matrixExpanded: true
    property bool telegramExpanded: true
    property bool rocketChatExpanded: true
    property bool invitesExpanded: true

    // Held open wherever the headings are not drawn: a fold with no chevron left
    // to undo it is a row that has gone for good.
    readonly property bool favoritesShown: root.compact || root.favoritesExpanded
    readonly property bool lanShown: root.compact || root.lanExpanded
    readonly property bool matrixShown: root.compact || root.matrixExpanded
    readonly property bool telegramShown: root.compact || root.telegramExpanded
    readonly property bool rocketChatShown: root.compact || root.rocketChatExpanded

    readonly property int lanCount: root.model ? root.model.lanCount : 0
    readonly property int matrixCount: root.model ? root.model.matrixCount : 0
    readonly property int telegramCount: root.model ? root.model.telegramCount : 0
    readonly property int rocketChatCount: root.model ? root.model.rocketChatCount : 0

    // The rail switches the list between backends: pick Matrix and the list is
    // Matrix alone, pick LAN/VPN and it is the local chats alone. Groups with
    // no rail button yet - Telegram, Rocket.Chat - show in either mode.
    function modeShowsGroup(group) {
        if (group === 0)
            return root.connectionMode === 0
        if (group === 1)
            return root.connectionMode === 1
        return true
    }

    // How many rows the current mode leaves on screen; the placeholder watches
    // this, not the totals, so a mode with no chats does not look broken.
    readonly property int shownChatCount: (modeShowsGroup(0) ? root.lanCount : 0)
        + (modeShowsGroup(1) ? root.matrixCount : 0) + root.telegramCount + root.rocketChatCount

    // One row per Matrix room this account has been asked into. Not part of the
    // conversation model: an invitation is not a conversation, and the row
    // carries buttons rather than a preview.
    property var invites: null

    signal chatActivated(string chatId)
    signal newChatRequested()
    signal forgetRequested(string chatId)
    signal clearRequested(string chatId)
    // Remove the row and the saved history, this device only. A joined room
    // comes back on the next login - the server still has the account in it -
    // which is exactly the difference from Leave this one is named for.
    signal deleteRequested(string chatId)
    signal inviteAccepted(string chatId)
    signal inviteDeclined(string chatId)

    // The pinned saved-messages row, which is the only one that can be
    // emptied: forgetting it is not on offer, so clearing it is.
    property string selfChatId: ""
    signal leaveRoomRequested(string chatId)
    signal profileRequested(Item anchorItem)
    signal settingsRequested()
    signal connectionModeRequested(int mode)
    signal micToggled()
    signal deafenToggled()
    // The row that was clicked travels with the request: the card is anchored to
    // it, and only this page knows which delegate it was.
    signal peerCardRequested(string chatId, Item anchorItem)

    readonly property string favoritesName: i18nc("@item conversation list, chat with yourself", "Favorites")

    // The needle lives here and not on the field. titleDelegate is a Component, so
    // the field inside it is in a scope of its own and an id reaching in from out
    // here does not resolve - matchesSearch() threw ReferenceError on every call.
    // A throw leaves the binding at whatever it last held, which is why folding a
    // section was final: collapsing short-circuits the && and hides the rows
    // honestly, then expanding calls in here, throws, and visible stays false.
    property string searchText: ""

    // Folding hides rows, so typing has to unfold them again or the search appears
    // to find nothing.
    onSearchTextChanged: if (root.searchText.length > 0) {
        root.favoritesExpanded = true
        root.lanExpanded = true
        root.matrixExpanded = true
        root.telegramExpanded = true
        root.rocketChatExpanded = true
    }

    title: i18nc("@title sidebar section, the list of conversations", "Chats")
    padding: 0

    // See the note on Kirigami.Theme in Main.qml.
    Kirigami.Theme.highlightColor: Brand.accent

    // Search and "new chat" go in the toolbar rather than a strip under it: in a
    // seventeen unit column, a row each is two rows the conversations do not get.
    titleDelegate: RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Kirigami.SearchField {
            Layout.fillWidth: true
            placeholderText: root.compact
                ? ""
                : i18nc("@info:placeholder filter the conversation list", "Search")
            // Two ways round on purpose: the write keeps the page in step with what
            // was typed, and the binding puts the text back if the page header is
            // ever reloaded under it, which is what a Component in a Loader does.
            text: root.searchText
            onTextChanged: root.searchText = text
        }

        QQC2.ToolButton {
            display: QQC2.AbstractButton.IconOnly
            icon.name: "mail-message-new"
            text: i18nc("@action:button start a conversation with a peer that is not in the list yet", "New chat")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            QQC2.ToolTip.text: text
            onClicked: root.newChatRequested()
        }
    }

    // Matches the address as well as the name, since an unnamed peer only has the
    // former; here so the pinned row and the conversations use the same rule.
    function matchesSearch(displayName, chatId) {
        if (root.searchText.length === 0)
            return true
        const needle = root.searchText.toLowerCase()
        return displayName.toLowerCase().indexOf(needle) !== -1
            || chatId.toLowerCase().indexOf(needle) !== -1
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ConnectionRail {
            Layout.fillHeight: true
            visible: !root.compact
            currentMode: root.connectionMode
            onModeSelected: (mode) => root.connectionModeRequested(mode)
        }

        Kirigami.Separator {
            Layout.fillHeight: true
            visible: !root.compact
            Layout.preferredWidth: 1
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ChatSection {
                Layout.fillWidth: true
                visible: !root.compact
                text: root.favoritesName
                itemCount: 1
                expanded: root.favoritesExpanded
                onToggleRequested: root.favoritesExpanded = !root.favoritesExpanded
            }

            // Wrapped in an Item because RoundedItemDelegate binds its own width
            // to the view it is the delegate of, this one is the delegate of
            // nothing, and a binding on width beats whatever a layout assigns.
            //
            // The fold is tested on the wrapper and nowhere else. Binding this
            // item to favoritesRow.visible latched the section shut for the rest
            // of the session: visible is effective visibility, so once the wrapper
            // was down it held the row's reading of it at false however the row's
            // own flag was set, and the binding never fired again.
            Item {
                Layout.fillWidth: true
                implicitHeight: favoritesRow.implicitHeight
                visible: root.favoritesShown
                    && root.matchesSearch(root.favoritesName, root.favoritesChatId)

                ContactDelegate {
                    id: favoritesRow

                    width: parent.width

                    displayName: root.favoritesName
                    iconName: "bookmarks"
                    compact: root.compact
                    showPresence: false
                    selected: root.selectedChatId === root.favoritesChatId
                    onClicked: root.chatActivated(root.favoritesChatId)
                }
            }

            // The placeholder is a sibling of the scroll area: a child of a
            // Flickable is parented to its content item and would scroll away
            // with the rows.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.largeSpacing * 4
                    visible: root.shownChatCount === 0 && (root.invites === null || root.invites.count === 0)
                    icon.name: "dialog-messages"
                    text: i18nc("@info there are no conversations yet", "No chats yet")
                    explanation: i18nc("@info", "Start one from the button in the toolbar, or wait for someone to write to you.")
                    helpfulAction: Kirigami.Action {
                        text: i18nc("@action:button start a conversation", "New chat")
                        icon.name: "list-add"
                        onTriggered: root.newChatRequested()
                    }
                }

                // Two sections, so two groups over the same model, each
                // collapsing the rows that are not its own. One view could not
                // do it: the model is ordered by activity, so rooms and direct
                // chats are interleaved in it and a section heading drawn from
                // that order would repeat down the column.
                //
                // Repeaters rather than two ListViews. A ListView sized to its
                // own contentHeight inside a layout starts at zero height,
                // creates no delegates because nothing is visible, and so stays
                // at zero. The rows here are a conversation list rather than a
                // timeline - tens of items, not thousands - so building all of
                // them is cheaper than the arithmetic that would avoid it.
                QQC2.ScrollView {
                    id: chatsScroll
                    anchors.fill: parent
                    contentWidth: availableWidth

                    ColumnLayout {
                        width: chatsScroll.availableWidth
                        spacing: 0

                        ChatSection {
                            Layout.fillWidth: true
                            text: i18nc("@title:group conversation list section, Matrix room invitations", "Invitations")
                            itemCount: root.invites ? root.invites.count : 0
                            visible: !root.compact && root.invites !== null && root.invites.count > 0
                            expanded: root.invitesExpanded
                            onToggleRequested: root.invitesExpanded = !root.invitesExpanded
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            visible: root.invitesExpanded

                            Repeater {
                                model: root.invites
                                delegate: inviteRowComponent
                            }
                        }

                        ChatSection {
                            Layout.fillWidth: true
                            text: i18nc("@title:group conversation list section, chats on the local network", "LAN / VPN")
                            itemCount: root.lanCount
                            visible: !root.compact && root.lanCount > 0 && root.modeShowsGroup(0)
                            expanded: root.lanExpanded
                            onToggleRequested: root.lanExpanded = !root.lanExpanded
                        }

                        // The group carries the two facts that tell one section
                        // from the other, and the shared delegate reads them off
                        // its parent - which is this, because a Repeater parents
                        // what it builds to its own parent rather than to itself.
                        ColumnLayout {
                            id: lanGroup

                            readonly property int transportGroup: 0
                            readonly property bool sectionShown: root.lanShown && root.modeShowsGroup(0)

                            Layout.fillWidth: true
                            spacing: 0

                            Repeater {
                                model: root.model
                                delegate: chatRowComponent
                            }
                        }

                        ChatSection {
                            Layout.fillWidth: true
                            text: i18nc("@title:group conversation list section, Matrix rooms and one-on-ones", "Matrix")
                            itemCount: root.matrixCount
                            visible: !root.compact && root.matrixCount > 0 && root.modeShowsGroup(1)
                            expanded: root.matrixExpanded
                            onToggleRequested: root.matrixExpanded = !root.matrixExpanded
                        }

                        ColumnLayout {
                            id: matrixGroup

                            readonly property int transportGroup: 1
                            readonly property bool sectionShown: root.matrixShown && root.modeShowsGroup(1)

                            Layout.fillWidth: true
                            spacing: 0

                            Repeater {
                                model: root.model
                                delegate: chatRowComponent
                            }
                        }

                        ChatSection {
                            Layout.fillWidth: true
                            text: i18nc("@title:group conversation list section, Telegram chats", "Telegram")
                            itemCount: root.telegramCount
                            visible: !root.compact && root.telegramCount > 0
                            expanded: root.telegramExpanded
                            onToggleRequested: root.telegramExpanded = !root.telegramExpanded
                        }

                        ColumnLayout {
                            id: telegramGroup

                            readonly property int transportGroup: 2
                            readonly property bool sectionShown: root.telegramShown

                            Layout.fillWidth: true
                            spacing: 0

                            Repeater {
                                model: root.model
                                delegate: chatRowComponent
                            }
                        }

                        ChatSection {
                            Layout.fillWidth: true
                            text: i18nc("@title:group conversation list section, Rocket.Chat channels", "Rocket.Chat")
                            itemCount: root.rocketChatCount
                            visible: !root.compact && root.rocketChatCount > 0
                            expanded: root.rocketChatExpanded
                            onToggleRequested: root.rocketChatExpanded = !root.rocketChatExpanded
                        }

                        ColumnLayout {
                            id: rocketChatGroup

                            readonly property int transportGroup: 3
                            readonly property bool sectionShown: root.rocketChatShown

                            Layout.fillWidth: true
                            spacing: 0

                            Repeater {
                                model: root.model
                                delegate: chatRowComponent
                            }
                        }
                    }
                }
            }
        }
    }

    // One menu for both groups, so a long list does not build a hundred of them.
    Components.ConvergentContextMenu {
        id: rowMenu

        property string chatId: ""
        property string chatName: ""
        property bool isRoom: false

        Kirigami.Action {
            text: i18nc("@action:inmenu open this conversation", "Open")
            icon.name: "document-open"
            onTriggered: root.chatActivated(rowMenu.chatId)
        }
        Kirigami.Action {
            text: i18nc("@action:inmenu remove the conversation from the list, keeping the history", "Forget this chat")
            icon.name: "list-remove"
            // A room is left rather than forgotten: dropping the row would put
            // it straight back on the next sync, because the homeserver still
            // has this account in it.
            visible: !rowMenu.isRoom
            onTriggered: root.forgetRequested(rowMenu.chatId)
        }
        Kirigami.Action {
            text: i18nc("@action:inmenu empty the saved messages chat", "Clear this chat")
            icon.name: "edit-clear-all"
            visible: rowMenu.chatId.length > 0 && rowMenu.chatId === root.selfChatId
            onTriggered: root.clearRequested(rowMenu.chatId)
        }
        Kirigami.Action {
            text: i18nc("@action:inmenu leave a Matrix room", "Leave room")
            icon.name: "window-close"
            visible: rowMenu.isRoom
            onTriggered: root.leaveRoomRequested(rowMenu.chatId)
        }
        Kirigami.Action {
            text: i18nc("@action:inmenu delete the conversation and its history on this device only", "Delete this chat here")
            icon.name: "edit-delete"
            onTriggered: root.deleteRequested(rowMenu.chatId)
        }
    }

    // An invitation is not a conversation: it has no preview and no unread
    // count, and its job is to be accepted or declined, so the row is a name
    // and two buttons rather than the usual delegate. The name can lag behind
    // the room - the invitation arrives before the room's own summary has -
    // and the row falls back to the room address until it turns up.
    Component {
        id: inviteRowComponent

        Item {
            id: inviteWrapper

            Layout.fillWidth: true
            implicitHeight: inviteColumn.implicitHeight

            ColumnLayout {
                id: inviteColumn

                width: parent.width
                spacing: 0

                ContactDelegate {
                    Layout.fillWidth: true

                    compact: root.compact
                    displayName: model.displayName.length > 0 ? model.displayName : model.chatId
                    preview: model.displayName.length > 0 ? model.chatId : ""
                    showPresence: false
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    Layout.bottomMargin: Kirigami.Units.smallSpacing
                    visible: !root.compact
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Button {
                        Layout.fillWidth: true
                        text: i18nc("@action:button accept a room invitation", "Accept")
                        icon.name: "dialog-ok"
                        highlighted: true
                        onClicked: root.inviteAccepted(model.chatId)
                    }

                    QQC2.Button {
                        Layout.fillWidth: true
                        text: i18nc("@action:button decline a room invitation", "Decline")
                        icon.name: "dialog-cancel"
                        onClicked: root.inviteDeclined(model.chatId)
                    }
                }
            }
        }
    }

    // One delegate for both groups. Which group it is in decides which rows it
    // draws; everything else about a row is the same either way, because a room
    // and a LAN chat are the same thing in this list - a conversation with
    // something new in it - and only differ once one is opened.
    //
    // Wrapped in an Item for the reason the pinned row above is: a
    // RoundedItemDelegate binds its own width to the view it is the delegate
    // of, this one is the delegate of no view, and a binding on width beats
    // whatever a layout assigns.
    Component {
        id: chatRowComponent

        Item {
            id: rowWrapper

            // model.* rather than required properties: every role below shares
            // its name with a property ContactDelegate already has, which would
            // bind to itself.
            readonly property bool isRoom: model.transport === "matrix"
            // The group this was built into - see the note beside lanGroup.
            readonly property bool inThisSection: rowWrapper.parent !== null
                && model.transportGroup === rowWrapper.parent.transportGroup
            readonly property bool shown: rowWrapper.inThisSection
                && rowWrapper.parent.sectionShown
                && root.matchesSearch(model.displayName, model.chatId)

            Layout.fillWidth: true
            implicitHeight: rowWrapper.shown ? chatRow.implicitHeight : 0
            visible: rowWrapper.shown

            ContactDelegate {
                id: chatRow

                width: parent.width

                compact: root.compact
                displayName: model.displayName
                preview: model.preview
                stampSecs: model.stampSecs
                lastSeenSecs: model.lastSeenSecs
                unreadCount: model.unreadCount
                online: model.online
                transport: model.transport
                avatarSource: model.avatarSource
                // A Matrix room has no presence to report, and a permanently
                // grey dot reads as a peer that is switched off rather than as
                // one that is not asked.
                showPresence: !rowWrapper.isRoom
                selected: model.chatId === root.selectedChatId

                function openRowMenu() {
                    rowMenu.chatId = model.chatId
                    rowMenu.chatName = model.displayName
                    rowMenu.isRoom = rowWrapper.isRoom
                    rowMenu.popup()
                }

                onClicked: root.chatActivated(model.chatId)
                onPressAndHold: chatRow.openRowMenu()
                onAvatarClicked: (anchorItem) => root.peerCardRequested(model.chatId, anchorItem)

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: chatRow.openRowMenu()
                }
            }
        }
    }

    // Gone in compact mode rather than shrunk: the microphone, the deafen toggle
    // and the settings entry are all in the global drawer and on the tray icon, so
    // a strip of them under the list is the furniture compact mode is meant to drop.
    footer: AccountRow {
        visible: !root.compact
        compact: root.compact
        micMuted: root.micMuted
        deafened: root.deafened

        onProfileRequested: (anchorItem) => root.profileRequested(anchorItem)
        onSettingsRequested: root.settingsRequested()
        onMicToggled: root.micToggled()
        onDeafenToggled: root.deafenToggled()
    }
}
