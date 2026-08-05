// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import koutnet.app

// The conversation list, as the first column of the window's page row rather
// than a hand-animated overlay. Being a page is what gets it a title, a header
// and the New chat action in the global toolbar for free.
Kirigami.ScrollablePage {
    id: root

    // What ChatListModel produces. Kept as a property so the page does not have
    // to know where the model was declared.
    property alias model: chatsView.model
    property string selectedChatId: ""
    // The local "chat with yourself" row, which is pinned and is not a peer.
    property string favoritesChatId: ""

    signal chatActivated(string chatId)
    signal newChatRequested()
    signal forgetRequested(string chatId)

    title: i18nc("@title sidebar section, the list of conversations", "Chats")

    // See the note on Kirigami.Theme in Main.qml: ScrollablePage starts a theme
    // chain of its own, so the accent has to be restated here or the selected row
    // falls back to the system highlight.
    Kirigami.Theme.highlightColor: Brand.accent

    actions: [
        Kirigami.Action {
            text: i18nc("@action:button start a conversation with a peer that is not in the list yet", "New chat")
            icon.name: "list-add"
            onTriggered: root.newChatRequested()
        }
    ]

    // Wrapped in a Control only to get the padding: a header item is stretched to
    // the column width, and a search field flush against both edges looks like a
    // mistake.
    header: QQC2.Control {
        padding: Kirigami.Units.smallSpacing

        contentItem: Kirigami.SearchField {
            id: searchField
            placeholderText: i18nc("@info:placeholder filter the conversation list", "Search")
        }
    }

    ListView {
        id: chatsView

        currentIndex: -1

        // Right-click and long-press on a row land here. One menu for the whole list
        // rather than one per delegate, so scrolling does not build a hundred of them.
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
        // The model reorders rows as messages arrive, so the list has somewhere
        // to animate them to.
        move: Transition {
            NumberAnimation { properties: "y"; duration: Kirigami.Units.shortDuration }
        }
        displaced: Transition {
            NumberAnimation { properties: "y"; duration: Kirigami.Units.shortDuration }
        }

        // Pinned above the conversations. A view header rather than something in
        // the page header, because ContactDelegate takes its width from the view
        // it belongs to and a layout would fight that binding.
        header: ContactDelegate {
            width: chatsView.width
            displayName: i18nc("@item conversation list, chat with yourself", "Favorites")
            iconName: "bookmarks"
            showPresence: false
            selected: root.selectedChatId === root.favoritesChatId
            onClicked: root.chatActivated(root.favoritesChatId)
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
            // Searching matches the name and the address: an unnamed peer only
            // has the second one.
            readonly property bool matchesSearch:
                searchField.text.length === 0
                || model.displayName.toLowerCase().indexOf(searchField.text.toLowerCase()) !== -1
                || model.chatId.toLowerCase().indexOf(searchField.text.toLowerCase()) !== -1
            visible: matchesSearch
            height: visible ? implicitHeight : 0

            displayName: model.displayName
            preview: model.preview
            stampSecs: model.stampSecs
            lastSeenSecs: model.lastSeenSecs
            unreadCount: model.unreadCount
            online: model.online
            selected: model.chatId === root.selectedChatId

            onClicked: root.chatActivated(model.chatId)
            onPressAndHold: {
                rowMenu.chatId = model.chatId
                rowMenu.chatName = model.displayName
                rowMenu.popup()
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: {
                    rowMenu.chatId = model.chatId
                    rowMenu.chatName = model.displayName
                    rowMenu.popup()
                }
            }
        }
    }
}
