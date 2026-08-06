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

    property alias model: chatsView.model
    property string selectedChatId: ""
    property string favoritesChatId: ""
    property int connectionMode: 0

    property bool micMuted: false
    property bool deafened: false

    // In compact mode the rail goes - its modes are all on the settings page and
    // the buttons are wider than the list - and so do the headings.
    property bool compact: false

    property bool favoritesExpanded: true
    property bool directExpanded: true

    // Held open wherever the headings are not drawn: a fold with no chevron left
    // to undo it is a row that has gone for good.
    readonly property bool favoritesShown: root.compact || root.favoritesExpanded
    readonly property bool directShown: root.compact || root.directExpanded

    signal chatActivated(string chatId)
    signal newChatRequested()
    signal forgetRequested(string chatId)
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
        root.directExpanded = true
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

            ChatSection {
                Layout.fillWidth: true
                text: i18nc("@title:group conversation list section, one-to-one chats", "Direct messages")
                itemCount: chatsView.count
                visible: !root.compact && itemCount > 0
                expanded: root.directExpanded
                onToggleRequested: root.directExpanded = !root.directExpanded
            }

            // The placeholder is a sibling of the view: a child of a Flickable is
            // parented to its content item and would scroll away with the rows.
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

                        move: Transition {
                            NumberAnimation { properties: "y"; duration: Kirigami.Units.shortDuration }
                        }
                        displaced: Transition {
                            NumberAnimation { properties: "y"; duration: Kirigami.Units.shortDuration }
                        }

                        // One menu for the whole list, so scrolling does not build
                        // a hundred of them.
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

                            // model.* rather than required properties: every role
                            // below shares its name with a property this delegate
                            // already has, which would bind to itself.
                            // The fold, named once and read twice: reading it back
                            // off visible would read effective visibility, which is
                            // the latch the pinned row above ran into.
                            readonly property bool shown: root.directShown
                                && root.matchesSearch(model.displayName, model.chatId)
                            visible: chatRow.shown
                            height: chatRow.shown ? implicitHeight : 0

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
