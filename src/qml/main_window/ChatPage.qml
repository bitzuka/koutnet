// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
// Deliberately not aliased: the viewer's model is declared list<AlbumModelItem>,
// and a property type in a declaration cannot carry an import namespace.
import org.kde.kirigamiaddons.labs.components
import koutnet.app

Kirigami.Page {
    id: root

    property string peerIp: ""
    property var peerInfo: null
    property var messagesModel: null
    property bool peerTyping: false
    property string selfDisplayName: ""
    property string selfAvatarSource: ""

    // A room chat (Matrix, and later Rocket.Chat or Telegram groups), as
    // opposed to a peer on the local network. The two are deliberately not
    // made to look alike: a room has a name, a topic, an address and members,
    // and a LAN peer has a person at the other end of it. Everything below
    // that branches on this says which of the two it is drawing.
    property bool isRoom: false
    // MatrixRoomBridge::roomInfo()'s map, or null. Keys used here: displayName,
    // topic, joinedCount, avatarUrl, encrypted.
    property var roomInfo: null

    signal sendRequested(string text, string replyExcerpt, string replyAuthor, string replyId)
    signal attachRequested(string localFilePath)
    signal callRequested()
    signal profileRequested()
    signal infoRequested()
    signal peerCardRequested(Item anchorItem)
    signal ownProfileRequested(Item anchorItem)
    signal newChatRequested()
    signal forwardRequested(int row)
    // Raised after this page's own model has already been changed. The stamp
    // rather than the row: a row number means nothing on the other end of a socket.
    signal editCommitted(double stamp, string newText)
    signal deleteCommitted(double stamp)
    signal typingNotice()
    signal notifyRequested(string text)
    signal readReached()

    readonly property bool hasChat: root.peerIp.length > 0 && root.messagesModel !== null
    readonly property bool isFavorites: root.peerInfo !== null && root.peerInfo.isFavorites === true
    readonly property alias atBottom: timeline.atBottom

    readonly property string selfReactionName: "me"

    property string viewerSource: ""
    property string viewerCaption: ""

    readonly property string roomName: root.roomInfo ? (root.roomInfo.displayName || "") : ""
    readonly property string roomTopic: root.roomInfo ? (root.roomInfo.topic || "") : ""
    readonly property int roomMemberCount: root.roomInfo ? (root.roomInfo.joinedCount || 0) : 0

    // Never the address. peerInfoFor() already substitutes a name for a peer
    // that has published none, so the fallback here is only for having no peer.
    title: root.isRoom
        ? (root.roomName.length > 0
            ? root.roomName
            : i18nc("@title a room whose name has not been synced yet", "Room"))
        : (root.peerInfo
            ? (root.peerInfo.username || i18nc("@title a peer that has published no name of its own", "Unknown peer"))
            : i18nc("@title", "Chat"))
    padding: 0

    // No peer column in compact mode, so the action that asks for one goes too.
    property bool compact: false

    // See the note on Kirigami.Theme in Main.qml.
    Kirigami.Theme.highlightColor: Brand.accent

    // The one surface the wallpaper shows through; every other page stays opaque,
    // because a form card over a photograph is a legibility problem.
    //
    // A transparent Rectangle rather than no background at all: taking it away
    // also takes the surface the timeline's bubbles are read against.
    background: Rectangle {
        color: Kirigami.Theme.backgroundColor
        opacity: appSettings.wallpaperPath.length > 0 ? 0 : 1
    }

    titleDelegate: RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.largeSpacing
        visible: root.peerInfo !== null

        Components.Avatar {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: Kirigami.Units.iconSizes.medium
            implicitHeight: Kirigami.Units.iconSizes.medium
            name: root.isFavorites ? "" : root.title
            iconSource: root.isFavorites ? "bookmarks" : ""
            // A room usually has a picture and a LAN peer never does - peers
            // publish a name and a bio and nothing else.
            source: root.isRoom && root.roomInfo ? (root.roomInfo.avatarUrl || "") : ""
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            Kirigami.Heading {
                id: headerName

                Layout.fillWidth: true
                level: 4
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: root.title

                HoverHandler {
                    enabled: !root.isFavorites
                    cursorShape: Qt.PointingHandCursor
                }

                // A room's name opens the room, a peer's name opens the peer.
                // The same gesture, two different things behind it, because the
                // two headers are describing two different kinds of thing.
                TapHandler {
                    enabled: !root.isFavorites
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        if (root.isRoom)
                            root.infoRequested()
                        else
                            root.peerCardRequested(headerName)
                    }
                }
            }

            // A room's second line is what the room is for; a peer's is whether
            // the person is there. RelativeTime.now is read in the second case
            // so the label ages on its own: it has to walk to "2 minutes ago"
            // with the window open and nothing arriving.
            QQC2.Label {
                Layout.fillWidth: true
                // The second line of the header goes in compact mode: when the
                // window is this short every line spent on furniture is a message.
                visible: !root.isFavorites && !root.compact && text.length > 0
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: {
                    if (root.isRoom) {
                        if (root.roomTopic.length > 0)
                            return root.roomTopic.replace(/\s+/g, " ")
                        return root.roomMemberCount > 0
                            ? i18ncp("@info:status %1 is how many people are in a room",
                                     "%1 member", "%1 members", root.roomMemberCount)
                            : ""
                    }
                    return root.peerInfo
                        ? RelativeTime.presenceLabel(root.peerInfo.online === true,
                                                     root.peerInfo.lastSeen || 0,
                                                     RelativeTime.now)
                        : ""
                }
                font: Kirigami.Theme.smallFont
                color: (!root.isRoom && root.peerInfo && root.peerInfo.online === true)
                    ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.disabledTextColor
            }
        }
    }

    actions: [
        Kirigami.Action {
            text: root.isRoom
                ? i18nc("@action:button show the room's topic, address and members", "Room information")
                : i18nc("@action:button show who is on the other end of this conversation", "Details")
            icon.name: "documentinfo"
            // The peer card is still reachable in compact mode, the column is not.
            visible: root.hasChat && !root.isFavorites && !root.compact
            onTriggered: root.infoRequested()
        },
        Kirigami.Action {
            text: i18nc("@action:button start a voice call", "Call")
            icon.name: "call-start"
            // Compact mode keeps what is needed to read a message and answer it and
            // nothing else; a call still starts from the peer card or the drawer.
            // Shown only where the backend behind this chat can actually ring:
            // voice is the LAN protocol's own, and a transport that has no call
            // must not draw a button that answers nothing.
            visible: root.hasChat && !root.isFavorites && !root.compact && chatTransport.supportsCalls(root.peerIp)
            onTriggered: root.callRequested()
        }
    ]

    // Copying goes through an off-screen editor because there is no clipboard
    // object in QML without pulling in a C++ helper for it.
    TextEdit {
        id: clipboardHelper
        visible: false
        function copyText(str) {
            text = str
            selectAll()
            copy()
        }
    }

    // A message carries one picture, but the component takes a model either way,
    // and the list has to be typed or the roles do not resolve.
    property list<AlbumModelItem> viewerItems: [
        AlbumModelItem {
            type: AlbumModelItem.Image
            source: root.viewerSource
            caption: root.viewerCaption
        }
    ]

    AlbumMaximizeComponent {
        id: imageViewer

        // See the note on Kirigami.Theme in Main.qml: this is reparented into
        // the window overlay, which is a theme chain of its own.
        Kirigami.Theme.inherit: false
        Kirigami.Theme.highlightColor: Brand.accent

        model: root.viewerItems
        initialIndex: 0
        showCaption: root.viewerCaption.length > 0
        title: root.title
        subtitle: root.viewerCaption

        // The transfer already put the picture on this machine, so there is
        // nothing to download and "save as" just hands it to the desktop.
        onSaveItem: Qt.openUrlExternally(root.viewerSource)
    }

    // Already a URL when it arrives: a LAN attachment was turned into a file://
    // one by AttachmentBlock, and a room's picture never had a path to begin
    // with. The caption is the last path segment with any query string cut off,
    // because an authenticated mxc URL carries one.
    function showImage(source) {
        root.viewerSource = source
        const withoutQuery = source.split("?")[0]
        root.viewerCaption = decodeURIComponent(withoutQuery.substring(withoutQuery.lastIndexOf("/") + 1))
        imageViewer.open()
    }

    // One menu for the whole list, so scrolling does not build one per message.
    Components.ConvergentContextMenu {
        id: messageMenu

        property int row: -1
        property string body: ""
        property string author: ""
        property string msgId: ""
        readonly property var codeBlocks: TextHandler.codeBlocks(messageMenu.body)

        Kirigami.Action {
            text: i18nc("@action:inmenu", "Reply")
            icon.name: "mail-replied-symbolic"
            onTriggered: root.startReply(messageMenu.author, messageMenu.body, messageMenu.msgId)
        }
        Kirigami.Action {
            text: i18nc("@action:inmenu", "Copy")
            icon.name: "edit-copy"
            enabled: messageMenu.body.length > 0
            onTriggered: {
                clipboardHelper.copyText(messageMenu.body)
                root.notifyRequested(i18nc("@info:status", "Message copied to the clipboard"))
            }
        }
        Kirigami.Action {
            text: i18ncp("@action:inmenu copy the code out of a message; the count is how many fenced blocks it has",
                         "Copy the code", "Copy the code blocks", messageMenu.codeBlocks.length)
            icon.name: "edit-copy"
            visible: messageMenu.codeBlocks.length > 0
            onTriggered: {
                clipboardHelper.copyText(messageMenu.codeBlocks.join("\n\n"))
                root.notifyRequested(i18nc("@info:status", "Code copied to the clipboard"))
            }
        }
        Kirigami.Action {
            text: i18nc("@action:inmenu add a reaction", "React")
            icon.name: "smiley-add"
            onTriggered: reactionPicker.openFor(messageMenu.row)
        }
        Kirigami.Action {
            text: i18nc("@action:inmenu forward this message", "Forward")
            icon.name: "mail-forward"
            onTriggered: root.forwardRequested(messageMenu.row)
        }
        Kirigami.Action { separator: true }
        Kirigami.Action {
            text: i18nc("@action:inmenu change what this message says", "Edit")
            icon.name: "document-edit"
            visible: root.canEditRow(messageMenu.row)
            onTriggered: root.startEdit(messageMenu.row, messageMenu.body)
        }
        Kirigami.Action {
            text: i18nc("@action:inmenu delete this message", "Delete")
            icon.name: "edit-delete"
            // Not offered in a room. Deleting here only removes the local copy,
            // and a message that is gone from this window and still in
            // everybody else's is worse than one that is still in both.
            visible: !root.isRoom
            onTriggered: deleteConfirm.open()
        }
    }

    // Asked about rather than done, because there is no undo behind it: the
    // entry is gone from the log as well as from the list.
    Kirigami.PromptDialog {
        id: deleteConfirm

        // See the note on Kirigami.Theme in Main.qml.
        Kirigami.Theme.inherit: false
        Kirigami.Theme.highlightColor: Brand.accent

        title: i18nc("@title:window", "Delete this message?")
        subtitle: i18nc("@info", "It will be removed from this conversation and from the other end of it. This cannot be undone.")
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: i18nc("@action:button confirm deleting a message", "Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    root.commitDelete(messageMenu.row)
                    deleteConfirm.close()
                }
            }
        ]
    }

    // Only your own text can be changed, and a file has no text to change.
    // Never in a room, for the reason on the Delete action above: an edit that
    // does not reach the homeserver is a private disagreement with the record.
    function canEditRow(row) {
        if (!root.messagesModel || row < 0 || root.isRoom)
            return false
        const idx = root.messagesModel.index(row, 0)
        return root.messagesModel.data(idx, ChatModel.IsOwnRole) === true
            && root.messagesModel.data(idx, ChatModel.IsFileRole) !== true
    }

    // Committing is the model's job first and the peer's second; nothing goes on
    // the wire for a message the model refused to change.
    function startEdit(row, body) {
        composer.startEdit(row, body)
    }

    function commitEdit(row, newText) {
        if (!root.messagesModel)
            return
        const stamp = root.messagesModel.stampForRow(row)
        if (root.messagesModel.editMessage(row, newText))
            root.editCommitted(stamp, newText)
    }

    function commitDelete(row) {
        if (!root.messagesModel)
            return
        // Read before the row goes, or there is nothing left to read it from.
        const stamp = root.messagesModel.stampForRow(row)
        if (root.messagesModel.deleteMessage(row))
            root.deleteCommitted(stamp)
    }

    function startReply(author, excerpt, msgId) {
        composer.replyAuthor = author
        composer.replyExcerpt = excerpt
        composer.replyId = msgId
        composer.focusInput()
    }

    function clearReply() {
        composer.replyAuthor = ""
        composer.replyExcerpt = ""
        composer.replyId = ""
    }

    ReactionPicker {
        id: reactionPicker
        onChosen: (row, emoji) => {
            if (root.messagesModel)
                root.messagesModel.toggleReaction(row, emoji, root.selfReactionName)
        }
    }

    EmojiPopup {
        id: emojiPopup
        onPicked: (emoji) => composer.insert(emoji)
    }

    FileDialog {
        id: fileDialog
        title: i18nc("@title:window", "Attach a file")
        onAccepted: root.attachRequested(selectedFile.toString().replace("file://", ""))
    }

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        visible: !root.hasChat
        icon.name: "dialog-messages"
        text: i18nc("@info", "No conversation open")
        explanation: i18nc("@info", "Pick a chat from the list, or start a new one.")
        helpfulAction: Kirigami.Action {
            text: i18nc("@action:button start a conversation", "New chat")
            icon.name: "list-add"
            onTriggered: root.newChatRequested()
        }
    }

    MessageTimeline {
        id: timeline

        anchors.fill: parent
        visible: root.hasChat

        messagesModel: root.messagesModel
        selfName: root.selfReactionName
        peerName: root.title
        selfDisplayName: root.selfDisplayName
        selfAvatarSource: root.selfAvatarSource
        canEditMessages: chatTransport.supportsEdits(root.peerIp)

        onAvatarActivated: (own, anchorItem) => {
            if (own)
                root.ownProfileRequested(anchorItem)
            else
                root.peerCardRequested(anchorItem)
        }

        onReplyRequested: (row, author, excerpt, msgId) => root.startReply(author, excerpt, msgId)
        onEditRequested: (row, body) => root.startEdit(row, body)
        onReactRequested: (row) => reactionPicker.openFor(row)
        onMenuRequested: (row, author, body, msgId) => {
            messageMenu.row = row
            messageMenu.author = author
            messageMenu.body = body
            messageMenu.msgId = msgId
            messageMenu.popup()
        }
        onReactionToggled: (row, emoji) => {
            if (root.messagesModel)
                root.messagesModel.toggleReaction(row, emoji, root.selfReactionName)
        }
        onImageActivated: (source) => root.showImage(source)
        onFileActivated: (source) => Qt.openUrlExternally(source)
        onReadReached: root.readReached()
    }

    // A pending edit left over from another conversation would land on whatever
    // row now has that number in this one.
    onPeerIpChanged: {
        root.clearReply()
        composer.cancelEdit()
    }

    footer: Composer {
        id: composer

        visible: root.hasChat
        peerTyping: root.peerTyping && !root.isFavorites
        peerName: root.title

        onSendRequested: (text, replyExcerpt, replyAuthor, replyId) =>
            root.sendRequested(text, replyExcerpt, replyAuthor, replyId)
        onEditSubmitted: (row, text) => root.commitEdit(row, text)
        onAttachRequested: fileDialog.open()
        onEmojiRequested: emojiPopup.open()
        onReplyCancelled: root.clearReply()
        onTypingNotice: root.typingNotice()
    }
}
