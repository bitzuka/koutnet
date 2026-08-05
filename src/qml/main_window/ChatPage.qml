// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
// Deliberately not aliased: the image viewer's model has to be declared as
// list<AlbumModelItem>, and a property type in a declaration cannot carry an
// import namespace.
import org.kde.kirigamiaddons.labs.components
import koutnet.app

// The middle column: one conversation.
//
// The page itself is now not much more than plumbing. The timeline draws the
// messages, the composer writes them, and the pickers and the viewer are windows
// of their own; what is left here is the header, the wiring between those four,
// and the one context menu the whole list shares.
Kirigami.Page {
    id: root

    property string peerIp: ""
    property var peerInfo: null
    property var messagesModel: null
    property bool peerTyping: false
    // Passed down to the timeline so a message that names the reader says so,
    // and so the reader's own messages are signed the way the peer's are.
    property string selfDisplayName: ""
    property string selfAvatarSource: ""

    signal sendRequested(string text, string replyExcerpt, string replyAuthor, string replyId)
    signal attachRequested(string localFilePath)
    signal callRequested()
    signal profileRequested()
    signal infoRequested()
    // The header identity, clicked. The window owns the card; this only says
    // which item to hang it off.
    signal peerCardRequested(Item anchorItem)
    // Your own face in the timeline, clicked. The window holds the account card,
    // and the item it is hung off belongs to a delegate, so nothing may keep it.
    signal ownProfileRequested(Item anchorItem)
    signal newChatRequested()
    signal forwardRequested(int row)
    // Raised after the change has already been made to this page's own model.
    // The window sends the peer its half, because it is the one that knows the
    // address. The stamp rather than the row: a row number means nothing on the
    // other side of a socket.
    signal editCommitted(double stamp, string newText)
    signal deleteCommitted(double stamp)
    signal typingNotice()
    // The window owns the one message strip, this page only asks for it.
    signal notifyRequested(string text)
    // The newest message is on screen, so the backlog has been seen.
    signal readReached()

    readonly property bool hasChat: root.peerIp.length > 0 && root.messagesModel !== null
    readonly property bool isFavorites: root.peerInfo !== null && root.peerInfo.isFavorites === true
    readonly property alias atBottom: timeline.atBottom

    // The name this client files its own reactions under. One constant rather
    // than the string "me" written at each of the three call sites that used it.
    readonly property string selfReactionName: "me"

    // What the image viewer is currently showing. The viewer needs a model, and
    // a model of one item is still a model.
    property string viewerSource: ""
    property string viewerCaption: ""

    // Never the address. peerInfoFor() already substitutes a name for a peer
    // that has published none, so the fallback here is only for having no peer.
    title: root.peerInfo
        ? (root.peerInfo.username || i18nc("@title a peer that has published no name of its own", "Unknown peer"))
        : i18nc("@title", "Chat")
    padding: 0

    // Compact mode. The peer column does not exist there, so the action that asks
    // for it goes - see Main.qml, which refuses the request itself as well.
    property bool compact: false

    // See the note on Kirigami.Theme in Main.qml.
    Kirigami.Theme.highlightColor: Brand.accent

    // The one surface the wallpaper shows through. Every other page keeps its
    // opaque background, because a form card or a toolbar over a photograph is a
    // legibility problem and the conversation is both the largest surface and the
    // one a wallpaper is for. The scrim that keeps the text readable sits between
    // the picture and this - see Main.qml.
    //
    // A transparent Rectangle rather than no background at all: Kirigami.Page
    // draws whatever is here, and taking it away also takes the surface the
    // timeline's own bubbles are read against.
    background: Rectangle {
        color: Kirigami.Theme.backgroundColor
        opacity: appSettings.wallpaperPath.length > 0 ? 0 : 1
    }

    // Avatar, name and presence in the toolbar itself, which is where Kirigami
    // puts a page's identity.
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

                TapHandler {
                    enabled: !root.isFavorites
                    acceptedButtons: Qt.LeftButton
                    onTapped: root.peerCardRequested(headerName)
                }
            }

            // Reachability is the flag; the stamp is only what it falls back to.
            // RelativeTime.now is read so this ages on its own: the label has to
            // walk from "just now" to "2 minutes ago" with the window sitting
            // open and nothing arriving.
            QQC2.Label {
                Layout.fillWidth: true
                visible: root.peerInfo && !root.isFavorites
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: root.peerInfo
                    ? RelativeTime.presenceLabel(root.peerInfo.online === true,
                                                 root.peerInfo.lastSeen || 0,
                                                 RelativeTime.now)
                    : ""
                font: Kirigami.Theme.smallFont
                color: (root.peerInfo && root.peerInfo.online === true)
                    ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.disabledTextColor
            }
        }
    }

    actions: [
        Kirigami.Action {
            text: i18nc("@action:button show who is on the other end of this conversation", "Details")
            icon.name: "documentinfo"
            // The peer card and the full profile page are both still reachable in
            // compact mode; it is only the third column that is not.
            visible: root.hasChat && !root.isFavorites && !root.compact
            onTriggered: root.infoRequested()
        },
        Kirigami.Action {
            text: i18nc("@action:button start a voice call", "Call")
            icon.name: "call-start"
            visible: root.hasChat && !root.isFavorites
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

    // Click to dim and centre, with zoom, rotate and keyboard dismissal, none of
    // it written here. One item rather than an album because a message carries
    // one picture; the component takes a model either way, and the list has to
    // be typed or the roles do not resolve.
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

        // The picture is already a file on this machine - the transfer put it
        // there - so there is nothing to download and "save as" only has to hand
        // it to whatever the desktop opens pictures with.
        onSaveItem: Qt.openUrlExternally(root.viewerSource)
    }

    function showImage(localPath) {
        root.viewerSource = "file://" + localPath
        root.viewerCaption = localPath.substring(localPath.lastIndexOf("/") + 1)
        imageViewer.open()
    }

    // One menu for the whole list rather than one per row, so scrolling a long
    // conversation does not build a menu per message.
    Components.ConvergentContextMenu {
        id: messageMenu

        property int row: -1
        property string body: ""
        property string author: ""
        property string msgId: ""
        // Whether this message has anything fenced in it, which is what decides
        // if the "copy the code" entry is worth offering.
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
    function canEditRow(row) {
        if (!root.messagesModel || row < 0)
            return false
        const idx = root.messagesModel.index(row, 0)
        return root.messagesModel.data(idx, ChatModel.IsOwnRole) === true
            && root.messagesModel.data(idx, ChatModel.IsFileRole) !== true
    }

    // Editing puts the old text back in the composer and remembers which row it
    // came out of. Committing is the model's job first and the peer's second;
    // nothing goes on the wire for a message the model refused to change.
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
        onImageActivated: (path) => root.showImage(path)
        onFileActivated: (path) => Qt.openUrlExternally("file://" + path)
        onReadReached: root.readReached()
    }

    // A conversation that is switched away from should not come back with half a
    // reply to a message the reader has forgotten about - nor with an edit of a
    // message that is no longer on the screen, which would land on whatever row
    // now has that number in a different chat.
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
