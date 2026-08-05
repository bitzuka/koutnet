// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import koutnet.app

// One row of the timeline.
//
// Flat: no bubble, no tail, no coloured block behind the text. A bubble is a
// frame drawn round every message to say where it starts and who sent it, and
// both of those are already said - by the run header above it and by the gutter
// beside it. The frames only cost width, and width is what a message wants.
//
// Consecutive messages from one sender share a header. The model works out where
// a run begins, because that is a statement about the row before this one and a
// delegate cannot see its neighbour.
//
// Your own messages are marked by a stripe down the leading edge rather than by
// alignment or a fill. Alternating sides halves the usable width of the column
// and makes every run start in a different place; a fill on your own messages
// puts the loudest colour on the screen on the half of the conversation you
// already wrote.
Item {
    id: root

    // Model roles, taken as required properties so a renamed role is a hard
    // error at load and not a blank row at runtime.
    required property int index
    required property string sender
    required property string text
    required property bool isOwn
    required property bool isSystem
    required property bool isEdited
    required property string replyToText
    required property string replyToSender
    required property string replyToId
    required property string msgId
    required property bool isRead
    required property bool isPending
    required property var reactions
    required property string timeString
    required property double stampSecs
    required property bool isFile
    required property string filePath
    required property bool isImage
    required property bool showAuthor
    required property bool showDay

    // Set by the timeline.
    property real contentWidth: root.width
    property real fontScale: 1.0
    property string selfName: ""
    property string peerName: ""
    // The reader's own name, highlighted wherever the message says it, and worn
    // by the header over their own messages.
    property string selfDisplayName: ""
    // The reader's own picture, for the same header.
    property string selfAvatarSource: ""
    // Per-message and not per-conversation: revealing one spoiler should not
    // reveal every other one in the backlog.
    property bool spoilerRevealed: false
    // Briefly true after a jump lands on this message, so the reader can see
    // which one they were sent to.
    property bool flashing: false

    // The quote travels with the request rather than being looked up again by
    // row: the page would have to reach into the model through a QModelIndex to
    // find what the delegate is already holding.
    signal replyRequested(int row, string author, string excerpt, string msgId)
    signal editRequested(int row, string body)
    signal reactRequested(int row)
    signal menuRequested(int row, string author, string body, string msgId)
    signal reactionToggled(int row, string emoji)
    signal jumpRequested(string msgId)
    signal imageActivated(string path)
    signal fileActivated(string path)
    signal avatarClicked(bool own, Item anchorItem)

    // Who wrote this. Incoming messages carry a sender only when the peer
    // published one, and the conversation already knows who it is with.
    //
    // Your own name and not the word "You", because this is also what a reply
    // quotes and what the context menu is titled with, and a quote attributed to
    // "You" reads as nobody once it is sitting under somebody else's message.
    readonly property string authorName: root.isOwn
        ? (root.selfDisplayName.length > 0
            ? root.selfDisplayName
            : i18nc("@info:placeholder the local user, as the author of their own message", "You"))
        : (root.sender.length > 0 ? root.sender : root.peerName)

    // The same name in the run header. Marked, so that a glance down the column
    // still tells your own messages from the peer's - the gutter stripe was
    // carrying that on its own, and a two-pixel line is not much of a signature.
    readonly property string headerName: root.isOwn
        ? i18nc("@info run header over your own messages, %1 is your own display name", "%1 (you)", root.authorName)
        : root.authorName

    // Links, code, emphasis and spoilers, worked out in C++ - see
    // core/chat/TextHandler.h, which is a port of NeoChat's. Colours are handed
    // over rather than read there, because this item is the one that knows which
    // theme chain it is drawing under.
    readonly property string renderedBody: TextHandler.toRichText(root.text, {
        "mentionName": root.selfDisplayName,
        "mentionColor": Kirigami.Theme.positiveTextColor.toString(),
        "codeBackground": Kirigami.Theme.alternateBackgroundColor.toString(),
        "spoilerBackground": Kirigami.Theme.textColor.toString(),
        "spoilerRevealed": root.spoilerRevealed,
        "elideLinksAt": 48
    })

    readonly property bool hasReply: root.replyToText.length > 0
    readonly property bool isPicture: root.isFile && root.isImage
    readonly property bool emojiOnly: !root.isFile && !root.isSystem && root.looksLikeEmojiRun(root.text)
    readonly property real gutterWidth: Kirigami.Units.iconSizes.medium

    // A message of nothing but a handful of emoji is the emoji, and gets drawn
    // at a size that says so. Six is the cut-off because past that it is a
    // sentence written in pictures and wants to read like one.
    function looksLikeEmojiRun(str) {
        if (!str || str.trim().length === 0 || str.length > 16)
            return false
        if (str.indexOf(" ") >= 0 || str.indexOf("\n") >= 0)
            return false
        let count = 0
        for (const ch of str) {
            count++
            if (!root.isEmojiCodePoint(ch.codePointAt(0)))
                return false
        }
        return count > 0 && count <= 6
    }

    function isEmojiCodePoint(cp) {
        return (cp >= 0x1F300 && cp <= 0x1FAFF)
            || (cp >= 0x2600 && cp <= 0x27BF)
            || (cp >= 0x1F1E0 && cp <= 0x1F1FF)
            || cp === 0x2764 || cp === 0x2665 || cp === 0x2B50 || cp === 0x2B55
            || cp === 0x00A9 || cp === 0x00AE || cp === 0x2122
            || cp === 0x3030 || cp === 0x303D
            // Zero-width joiner, variation selectors and the rest of the glue
            // that holds a composed emoji together.
            || (cp >= 0x200B && cp <= 0x200F)
            || (cp >= 0xFE00 && cp <= 0xFE0F)
    }

    // ListView sizes a delegate's height from it and leaves the width alone, so
    // the width has to be taken from the view by hand.
    width: root.ListView.view ? root.ListView.view.width : root.contentWidth
    implicitHeight: layout.implicitHeight
    height: implicitHeight

    ColumnLayout {
        id: layout

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        width: root.contentWidth
        spacing: 0

        DateSeparator {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            Layout.bottomMargin: Kirigami.Units.largeSpacing
            visible: root.showDay
            Layout.preferredHeight: visible ? -1 : 0
            whenSecs: root.stampSecs
        }

        SystemMessage {
            Layout.fillWidth: true
            visible: root.isSystem
            Layout.preferredHeight: visible ? -1 : 0
            text: root.text
        }

        Item {
            id: body

            Layout.fillWidth: true
            visible: !root.isSystem
            implicitHeight: bodyRow.implicitHeight
            Layout.preferredHeight: visible ? -1 : 0
            // A run that has started gets a little air above it; a message
            // continuing one does not, which is what makes the run read as one
            // block instead of five evenly spaced lines.
            Layout.topMargin: root.showAuthor && !root.showDay ? Kirigami.Units.largeSpacing : 0

            HoverHandler {
                id: bodyHover
            }

            // The jump target flash, and nothing else painted behind a message.
            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: -Kirigami.Units.smallSpacing
                anchors.rightMargin: -Kirigami.Units.smallSpacing
                radius: Kirigami.Units.cornerRadius
                visible: root.flashing
                color: Kirigami.ColorUtils.tintWithAlpha(Kirigami.Theme.backgroundColor, Kirigami.Theme.highlightColor, 0.25)
            }

            RowLayout {
                id: bodyRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                spacing: Kirigami.Units.smallSpacing

                // The gutter: your own stripe, then the avatar, then nothing at
                // all on the rows that continue a run. Reserved either way, so
                // every line of a run starts at the same x.
                Item {
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: root.gutterWidth
                    Layout.preferredHeight: root.gutterWidth

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: Math.max(2, Math.round(Kirigami.Units.smallSpacing / 2))
                        radius: width / 2
                        visible: root.isOwn
                        color: Kirigami.Theme.highlightColor
                    }

                    Components.Avatar {
                        id: authorAvatar

                        anchors.right: parent.right
                        anchors.top: parent.top
                        width: root.gutterWidth - Kirigami.Units.smallSpacing
                        height: width
                        visible: root.showAuthor
                        // The plain name and not the header's: the generated
                        // colour and the fallback initial have no business being
                        // taken off the "(you)" the header wears.
                        name: root.authorName
                        // Peers publish a name and a bio but no picture, so
                        // theirs stays an initial - see setProfile() in
                        // NetworkManager.
                        source: root.isOwn ? root.selfAvatarSource : ""
                        asynchronous: true

                        // A face is the shortest way to ask who somebody is. The
                        // card is hung off this item, so it has to be the one
                        // that reports the tap.
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            gesturePolicy: TapHandler.ReleaseWithinBounds
                            onTapped: root.avatarClicked(root.isOwn, authorAvatar)
                        }

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }
                    }

                    // The clock for a message inside a run, which has no header
                    // to carry one. On hover only: a column of times down the
                    // gutter is noise, and the one time anybody wants is the one
                    // next to the message they are looking at.
                    QQC2.Label {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        visible: !root.showAuthor && bodyHover.hovered
                        text: root.timeString
                        textFormat: Text.PlainText
                        font: Kirigami.Theme.smallFont
                        color: Kirigami.Theme.disabledTextColor
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                    // Run header: who, and when they started.
                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.showAuthor
                        Layout.preferredHeight: visible ? -1 : 0
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Label {
                            text: root.headerName
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            font.bold: true
                            // The accent, not a per-sender colour. A direct chat
                            // has two people in it and a palette of generated
                            // colours would be answering a question groups have.
                            color: Kirigami.Theme.highlightColor
                            // Off contentWidth rather than off the row this sits
                            // in: reading back a width the layout is still
                            // working out is how a header ends up in a loop.
                            Layout.maximumWidth: Math.round(root.contentWidth * 0.5)
                        }

                        QQC2.Label {
                            text: root.timeString
                            textFormat: Text.PlainText
                            font: Kirigami.Theme.smallFont
                            color: Kirigami.Theme.disabledTextColor

                            HoverHandler {
                                id: timeHover
                            }
                            QQC2.ToolTip.visible: timeHover.hovered
                            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                            QQC2.ToolTip.text: i18nc("@info:tooltip when a message was sent, %1 is a date and time",
                                                     "Sent %1",
                                                     new Date(root.stampSecs * 1000).toLocaleString(Qt.locale()))
                        }

                        Item {
                            Layout.fillWidth: true
                        }
                    }

                    // The quote, the attachment and the reactions are loaded
                    // rather than declared. Most messages are a line of text
                    // with none of the three, and building all three for every
                    // row is most of what a fast scroll was paying for.
                    Loader {
                        Layout.fillWidth: true
                        active: root.hasReply
                        visible: active

                        sourceComponent: ReplyQuote {
                            author: root.replyToSender
                            excerpt: root.replyToText
                            targetId: root.replyToId
                            onJumpRequested: (msgId) => root.jumpRequested(msgId)
                        }
                    }

                    Loader {
                        Layout.fillWidth: true
                        active: root.isFile
                        visible: active

                        sourceComponent: AttachmentBlock {
                            filePath: root.filePath
                            fileName: root.text
                            isImage: root.isImage
                            maxImageWidth: Math.min(Kirigami.Units.gridUnit * 18,
                                                    root.contentWidth - root.gutterWidth - Kirigami.Units.largeSpacing * 2)
                            onImageActivated: (path) => root.imageActivated(path)
                            onFileActivated: (path) => root.fileActivated(path)
                        }
                    }

                    // Plain Label rather than SelectableLabel: that one takes the
                    // right button for a copy menu of its own, and the right
                    // button on a message belongs to the message menu.
                    //
                    // Rich text everywhere except a run of emoji, which has no
                    // markup in it by definition and is cheaper drawn plain.
                    QQC2.Label {
                        id: bodyLabel

                        Layout.fillWidth: true
                        visible: !root.isFile
                        Layout.preferredHeight: visible ? -1 : 0
                        text: root.emojiOnly ? root.text : root.renderedBody
                        textFormat: root.emojiOnly ? Text.PlainText : Text.RichText
                        wrapMode: Text.Wrap
                        // pointSize and not pixelSize: a desktop font is
                        // configured in points and reports pixelSize -1, which is
                        // what the Ctrl+wheel zoom would otherwise multiply.
                        font.pointSize: (root.emojiOnly ? 2.5 : 1.0)
                            * Kirigami.Theme.defaultFont.pointSize * root.fontScale

                        // Opened outside the application rather than followed in
                        // place: there is nothing in here that can render a page,
                        // and a messenger that navigates is a browser.
                        onLinkActivated: (link) => Qt.openUrlExternally(link)

                        HoverHandler {
                            cursorShape: bodyLabel.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                        }

                        // A spoiler is uncovered by clicking it and stays
                        // uncovered. Only bound when the message has one, so an
                        // ordinary message is not swallowing left clicks.
                        TapHandler {
                            enabled: !root.spoilerRevealed && root.text.indexOf("||") >= 0
                            acceptedButtons: Qt.LeftButton
                            onTapped: root.spoilerRevealed = true
                        }
                    }

                    Loader {
                        Layout.fillWidth: true
                        active: root.reactions.length > 0
                        visible: active

                        sourceComponent: ReactionFlow {
                            reactions: root.reactions
                            selfName: root.selfName
                            onToggled: (emoji) => root.reactionToggled(root.index, emoji)
                            onAddRequested: root.reactRequested(root.index)
                        }
                    }
                }

                // Edited marker and delivery state, at the trailing edge of the
                // message rather than under it: a line of its own per message
                // would double the height of a run of one-word replies.
                RowLayout {
                    Layout.alignment: Qt.AlignTop
                    spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                    QQC2.Label {
                        visible: root.isEdited
                        text: i18nc("@info:status the message was changed after it was sent", "edited")
                        textFormat: Text.PlainText
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        font.italic: true
                        color: Kirigami.Theme.disabledTextColor
                    }

                    DeliveryMark {
                        visible: root.isOwn
                        read: root.isRead
                        pending: root.isPending
                    }
                }
            }

            // The hover strip, straddling the top edge at the trailing side.
            //
            // Loaded on hover and thrown away after. It is four buttons, four
            // tooltips and a shadowed rectangle, and it used to be built for
            // every message in the view whether or not anybody went near it.
            //
            // The strip hangs half above this message, so the pointer reaching
            // for it has already left the body: keeping it loaded on
            // bodyHover alone would pull it out from under the click. The strip
            // reports its own hover into stripHovered instead. Assigned rather
            // than bound, because a binding from active back through item to
            // active is a loop and QML says so at every frame.
            Loader {
                id: hoverActions

                property bool stripHovered: false

                anchors.right: parent.right
                anchors.verticalCenter: parent.top
                z: 1
                active: bodyHover.hovered || hoverActions.stripHovered

                sourceComponent: MessageActions {
                    hoverEnabled: true
                    canEdit: root.isOwn && !root.isFile

                    onHoveredChanged: hoverActions.stripHovered = hovered
                    // Or a strip unloaded while the pointer was still on it
                    // would leave the flag up and the next one stuck open.
                    Component.onDestruction: hoverActions.stripHovered = false

                    onReactRequested: root.reactRequested(root.index)
                    onReplyRequested: root.replyRequested(root.index, root.authorName, root.text, root.msgId)
                    onEditRequested: root.editRequested(root.index, root.text)
                    onMenuRequested: root.menuRequested(root.index, root.authorName, root.text, root.msgId)
                }
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: root.menuRequested(root.index, root.authorName, root.text, root.msgId)
            }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onLongPressed: root.menuRequested(root.index, root.authorName, root.text, root.msgId)
            }
        }
    }
}
