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
// The section headings sit above the view rather than inside it, and the pinned
// Favorites row with them. They used to be the ListView's header, and that is a
// header whose height changes: folding one shrinks it, unfolding grows it again.
// QQuickItemView answers a header resize with updateHeader() and fixupPosition()
// and nothing else - it re-places the header above the first row and clamps
// contentY into the new bounds. Shrinking puts contentY out of bounds and it is
// pulled back; growing does not, so the height the header just got back appears
// above the viewport and the section that was unfolded is off the top of the
// list. Kept out of the view, a fold is a layout change and nothing else.
//
// The fold state still lives here rather than in the headings: the rows a
// heading folds are somewhere else - inside a ListView delegate - and the one
// copy of the state has to be visible to both.
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

    // Compact mode. The rail goes: it is a column of mode buttons beside a list
    // that has been narrowed to eleven grid units, and every mode it offers is
    // also on the settings page. The rows tighten - see ContactDelegate.
    property bool compact: false

    property bool favoritesExpanded: true
    property bool directExpanded: true

    signal chatActivated(string chatId)
    signal newChatRequested()
    signal forgetRequested(string chatId)
    // The account row travels with the request: the account card is anchored to
    // it, the same way a peer's card is anchored to the row that asked for it.
    signal profileRequested(Item anchorItem)
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
                text: root.favoritesName
                itemCount: 1
                expanded: root.favoritesExpanded
                onToggleRequested: root.favoritesExpanded = !root.favoritesExpanded
            }

            // Favorites is one pinned local row and not a chat with anybody, so
            // it is not in the model. Wrapped in an Item because
            // RoundedItemDelegate binds its own width - to the view it is the
            // delegate of, and this one is the delegate of nothing - and a
            // binding on width beats whatever a layout assigns the next time it
            // re-evaluates. The wrapper is what the row takes its width from and
            // what carries the fold, so nothing here fights the column.
            Item {
                Layout.fillWidth: true
                implicitHeight: favoritesRow.implicitHeight
                visible: favoritesRow.visible

                ContactDelegate {
                    id: favoritesRow

                    width: parent.width
                    visible: root.favoritesExpanded
                        && root.matchesSearch(root.favoritesName, root.favoritesChatId)

                    displayName: root.favoritesName
                    iconName: "bookmarks"
                    compact: root.compact
                    showPresence: false
                    selected: root.selectedChatId === root.favoritesChatId
                    onClicked: root.chatActivated(root.favoritesChatId)
                }
            }

            ChatSection {
                Layout.fillWidth: true
                text: i18nc("@title:group conversation list section, one-to-one chats", "Direct messages")
                itemCount: chatsView.count
                visible: itemCount > 0
                expanded: root.directExpanded
                onToggleRequested: root.directExpanded = !root.directExpanded
            }

            // The placeholder is a sibling of the view rather than a child of it:
            // a child of a Flickable is parented to its content item and would
            // scroll away with the rows it is standing in for.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

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

                QQC2.ScrollView {
                    anchors.fill: parent

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

                        delegate: ContactDelegate {
                            id: chatRow

                            // model.* rather than required properties: every role below
                            // shares its name with a property this delegate already has,
                            // and a required property of the same name would bind to
                            // itself.
                            visible: root.directExpanded
                                && root.matchesSearch(model.displayName, model.chatId)
                            height: visible ? implicitHeight : 0

                            compact: root.compact
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
        }
    }

    footer: AccountRow {
        micMuted: root.micMuted
        deafened: root.deafened

        onProfileRequested: (anchorItem) => root.profileRequested(anchorItem)
        onSettingsRequested: root.settingsRequested()
        onMicToggled: root.micToggled()
        onDeafenToggled: root.deafenToggled()
    }
}
