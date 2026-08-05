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

    signal sendRequested(string text, string replyExcerpt, string replyAuthor, string replyId)
    signal attachRequested(string localFilePath)
    signal callRequested()
    signal profileRequested()
    signal infoRequested()
    signal newChatRequested()
    signal forwardRequested(int row)
    signal deleteRequested(int row)
    signal editRequested(int row)
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

    title: root.peerInfo ? (root.peerInfo.username || root.peerIp) : i18nc("@title", "Chat")
    padding: 0

    // See the note on Kirigami.Theme in Main.qml.
    Kirigami.Theme.highlightColor: Brand.accent

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
                Layout.fillWidth: true
                level: 4
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: root.title
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
            visible: root.hasChat && !root.isFavorites
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
            text: i18nc("@action:inmenu delete this message", "Delete")
            icon.name: "edit-delete"
            onTriggered: root.deleteRequested(messageMenu.row)
        }
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

    EmojiPicker {
        id: emojiPicker
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

        onReplyRequested: (row, author, excerpt, msgId) => root.startReply(author, excerpt, msgId)
        onEditRequested: (row, body) => root.editRequested(row)
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
    // reply to a message the reader has forgotten about.
    onPeerIpChanged: root.clearReply()

    footer: Composer {
        id: composer

        visible: root.hasChat
        peerTyping: root.peerTyping && !root.isFavorites
        peerName: root.title

        onSendRequested: (text, replyExcerpt, replyAuthor, replyId) =>
            root.sendRequested(text, replyExcerpt, replyAuthor, replyId)
        onAttachRequested: fileDialog.open()
        onEmojiRequested: emojiPicker.open()
        onReplyCancelled: root.clearReply()
        onTypingNotice: root.typingNotice()
    }
}
