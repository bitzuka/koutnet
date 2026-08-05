// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import koutnet.app

// The first column of the window: the connection rail, then the conversations,
// then who you are.
//
// A plain Page with a RowLayout in it rather than a ScrollablePage, because the
// rail runs the full height of the column beside a list that scrolls on its own,
// and a ScrollablePage would scroll the rail with it.
//
// Sections are one ListView with a header rather than a stack of views: one
// scrollbar covers everything, the keyboard walks the whole list, and rows still
// animate when the model reorders them. The cost is that the fold state cannot
// live in the headings - a ListView header is an implicit Component, and the
// delegates cannot see an id inside it - so it lives here.
Kirigami.Page {
    id: root

    // What ChatListModel produces. Kept as a property so the page does not have
    // to know where the model was declared.
    property alias model: chatsView.model
    property string selectedChatId: ""
    // The local "chat with yourself" row, which is pinned and is not a peer.
    property string favoritesChatId: ""
    // AppSettings.connectionMode, mirrored so the rail has something to draw.
    property int connectionMode: 0

    // Mirrored from the window so the account row can draw them; the window is
    // what owns the call these actually act on.
    property bool micMuted: false
    property bool deafened: false

    property bool favoritesExpanded: true
    property bool directExpanded: true

    signal chatActivated(string chatId)
    signal newChatRequested()
    signal forgetRequested(string chatId)
    signal profileRequested()
    signal settingsRequested()
    signal connectionModeRequested(int mode)
    signal micToggled()
    signal deafenToggled()
    // The row that was clicked travels with the request: the card is anchored to
    // it, and only this page knows which delegate it was.
    signal peerCardRequested(string chatId, Item anchorItem)

    readonly property string favoritesName: i18nc("@item conversation list, chat with yourself", "Favorites")

    title: i18nc("@title sidebar section, the list of conversations", "Chats")
    padding: 0

    // See the note on Kirigami.Theme in Main.qml: the selected row is filled with
    // the highlight, so the accent is restated wherever the theme chain restarts.
    Kirigami.Theme.highlightColor: Brand.accent

    // Search and "new chat" go in the toolbar rather than a strip under it. The
    // column is seventeen grid units wide; a row each is two rows the
    // conversations do not get.
    titleDelegate: RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Kirigami.SearchField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: i18nc("@info:placeholder filter the conversation list", "Search")
            // Folding a section hides rows, so typing has to unfold them again or
            // the search appears to find nothing.
            onTextChanged: if (text.length > 0) {
                root.favoritesExpanded = true
                root.directExpanded = true
            }
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

    // Matching is on the name and on the address: an unnamed peer only has the
    // second one. Here rather than in the delegate so the pinned row and the
    // conversations are filtered by the same rule.
    function matchesSearch(displayName, chatId) {
        if (searchField.text.length === 0)
            return true
        const needle = searchField.text.toLowerCase()
        return displayName.toLowerCase().indexOf(needle) !== -1
            || chatId.toLowerCase().indexOf(needle) !== -1
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ConnectionRail {
            Layout.fillHeight: true
            currentMode: root.connectionMode
            onModeSelected: (mode) => root.connectionModeRequested(mode)
        }

        Kirigami.Separator {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
        }

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: chatsView

                currentIndex: -1
                clip: true

                // The model reorders rows as messages arrive, so the list has
                // somewhere to animate them to.
                move: Transition {
                    NumberAnimation { properties: "y"; duration: Kirigami.Units.shortDuration }
                }
                displaced: Transition {
                    NumberAnimation { properties: "y"; duration: Kirigami.Units.shortDuration }
                }

                // Right-click and long-press on a row land here. One menu for the
                // whole list rather than one per delegate, so scrolling does not
                // build a hundred of them.
                Components.ConvergentContextMenu {
                    id: rowMenu

                    property string chatId: ""
                    property string chatName: ""

                    Kirigami.Action {
                        text: i18nc("@action:inmenu open this conversation", "Open")
                        icon.name: "document-open"
                        onTriggered: root.chatActivated(rowMenu.chatId)
                    }
                    Kirigami.Action {
                        text: i18nc("@action:inmenu remove the conversation from the list, keeping the history", "Forget this chat")
                        icon.name: "list-remove"
                        onTriggered: root.forgetRequested(rowMenu.chatId)
                    }
                }

                // Favorites is one pinned local row and not a chat with anybody,
                // so it is in the view header rather than in the model. Groups and
                // Channels are the same shape when they land: another heading and
                // another block of rows above the model's.
                header: ColumnLayout {
                    width: chatsView.width
                    spacing: 0

                    ChatSection {
                        Layout.fillWidth: true
                        text: root.favoritesName
                        itemCount: 1
                        visible: itemCount > 0
                        expanded: root.favoritesExpanded
                        onToggleRequested: root.favoritesExpanded = !root.favoritesExpanded
                    }

                    ContactDelegate {
                        Layout.fillWidth: true
                        visible: root.favoritesExpanded
                            && root.matchesSearch(root.favoritesName, root.favoritesChatId)
                        // A hidden item still takes its implicit height in a
                        // layout unless it is told not to.
                        Layout.preferredHeight: visible ? -1 : 0

                        displayName: root.favoritesName
                        iconName: "bookmarks"
                        showPresence: false
                        selected: root.selectedChatId === root.favoritesChatId
                        onClicked: root.chatActivated(root.favoritesChatId)
                    }

                    ChatSection {
                        Layout.fillWidth: true
                        text: i18nc("@title:group conversation list section, one-to-one chats", "Direct messages")
                        itemCount: chatsView.count
                        visible: itemCount > 0
                        expanded: root.directExpanded
                        onToggleRequested: root.directExpanded = !root.directExpanded
                    }
                }

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.largeSpacing * 4
                    visible: chatsView.count === 0
                    icon.name: "dialog-messages"
                    text: i18nc("@info there are no conversations yet", "No chats yet")
                    explanation: i18nc("@info", "Start one from the button in the toolbar, or wait for someone to write to you.")
                    helpfulAction: Kirigami.Action {
                        text: i18nc("@action:button start a conversation", "New chat")
                        icon.name: "list-add"
                        onTriggered: root.newChatRequested()
                    }
                }

                delegate: ContactDelegate {
                    id: chatRow

                    // model.* rather than required properties: every role below
                    // shares its name with a property this delegate already has,
                    // and a required property of the same name would bind to
                    // itself.
                    visible: root.directExpanded
                        && root.matchesSearch(model.displayName, model.chatId)
                    height: visible ? implicitHeight : 0

                    displayName: model.displayName
                    preview: model.preview
                    stampSecs: model.stampSecs
                    lastSeenSecs: model.lastSeenSecs
                    unreadCount: model.unreadCount
                    online: model.online
                    selected: model.chatId === root.selectedChatId

                    function openRowMenu() {
                        rowMenu.chatId = model.chatId
                        rowMenu.chatName = model.displayName
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
    }

    footer: AccountRow {
        micMuted: root.micMuted
        deafened: root.deafened

        onProfileRequested: root.profileRequested()
        onSettingsRequested: root.settingsRequested()
        onMicToggled: root.micToggled()
        onDeafenToggled: root.deafenToggled()
    }
}
