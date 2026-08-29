// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import koutnet.app

// Flat: no bubble. A bubble frames a message to say where it starts and who sent
// it, and the run header above and the gutter beside already say both; the frame
// only costs width. Consecutive messages from one sender share a header, and the
// model works out where a run begins because that is a statement about the row
// before this one and a delegate cannot see its neighbour. Own messages get a
// stripe down the leading edge rather than alignment or a fill: alternating sides
// halves the usable width and moves every run start, and a fill puts the loudest
// colour on the half of the conversation you wrote.
Item {
    id: root

    // Required, so a renamed role is a hard error at load and not a blank row.
    required property int index
    required property string sender
    required property string senderAvatar
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
    required property string mediaKind
    required property string mediaUrl
    required property int mediaWidth
    required property int mediaHeight
    required property int mediaDuration
    required property bool showAuthor
    required property bool showDay

    // The room this row belongs to, fed from MessageTimeline so a poll vote can
    // name its room on the wire. poll comes from the model role of the same name;
    // do NOT declare it here or the local property shadows the role.
    property string chatId: ""

    property real contentWidth: root.width
    property real fontScale: 1.0
    property string selfName: ""
    property string peerName: ""
    property string selfDisplayName: ""
    property string selfAvatarSource: ""
    // False in a Matrix room: an edit that never reaches the homeserver is a
    // private disagreement with what everybody else can still read.
    property bool canEditMessages: true
    // Per-message: revealing one spoiler should not reveal the whole backlog.
    property bool spoilerRevealed: false
    property bool flashing: false

    // The quote travels with the request rather than being looked up again by row:
    // the page would have to reach into the model to find what this is holding.
    signal replyRequested(int row, string author, string excerpt, string msgId)
    signal pinRequested(int row, string msgId)
    signal editRequested(int row, string body)
    signal reactRequested(int row)
    signal menuRequested(int row, string author, string body, string msgId)
    signal reactionToggled(int row, string emoji)
    signal jumpRequested(string msgId)
    // A URL rather than a path: the attachment may never have been on this disk.
    signal imageActivated(string source)
    signal fileActivated(string source)
    signal avatarClicked(bool own, Item anchorItem)

    // Your own name and not the word "You", because this is also what a reply
    // quotes and what the context menu is titled with, and a quote attributed to
    // "You" reads as nobody once it is sitting under somebody else's message.
    readonly property string authorName: root.isOwn
        ? (root.selfDisplayName.length > 0
            ? root.selfDisplayName
            : i18nc("@info:placeholder the local user, as the author of their own message", "You"))
        : (root.sender.length > 0 ? root.sender : root.peerName)

    // Marked, so a glance down the column still tells your own messages from the
    // peer's: the gutter stripe was carrying that alone, and it is two pixels wide.
    readonly property string headerName: root.isOwn
        ? i18nc("@info run header over your own messages, %1 is your own display name", "%1 (you)", root.authorName)
        : root.authorName

    // Colours are handed over rather than read inside TextHandler, because this
    // item is the one that knows which theme chain it is drawing under.
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

    // Six is the cut-off: past that it is a sentence written in pictures.
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
            || (cp >= 0x200B && cp <= 0x200F)
            || (cp >= 0xFE00 && cp <= 0xFE0F)
    }

    // ListView sizes a delegate's height from it and leaves the width alone, so
    // the width has to be taken from the view by hand.
    width: root.ListView.view ? root.ListView.view.width : root.contentWidth
    implicitHeight: layout.implicitHeight
    height: implicitHeight

    // The view reassigns every required property on reuse, so anything bound to a
    // role puts itself right. These two were set by hand and would arrive on the
    // next message still holding the last one's answer - a revealed spoiler and a
    // hover strip stuck open.
    ListView.onReused: {
        root.spoilerRevealed = false
        hoverActions.stripHovered = false
    }

    ColumnLayout {
        id: layout

        // Leading edge rather than centred: centring split the timeline's
        // scrollBarRoom between both sides, so half of a reservation meant to clear
        // the trailing edge went to the leading one.
        anchors.left: parent.left
        anchors.leftMargin: Kirigami.Units.largeSpacing
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
            Layout.topMargin: root.showAuthor && !root.showDay ? Kirigami.Units.largeSpacing : 0

            HoverHandler {
                id: bodyHover
            }

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

                // The gutter holds nothing at all on rows that continue a run, but
                // is reserved either way so every line of a run starts at the same x.
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
                        // The plain name and not the header's: the generated colour
                        // and fallback initial must not come off the "(you)".
                        name: root.authorName
                        // Peers publish a name and a bio but no picture, so theirs
                        // stays an initial - see setProfile() in NetworkManager.
                        // The Matrix side is different: the avatar is on the
                        // homeserver, and this session's own is the local file.
                        source: root.isOwn ? root.selfAvatarSource : root.senderAvatar
                        asynchronous: true

                        // The card is hung off this item, so it has to be the one
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

                    // On hover only: a column of times down the gutter is noise, and
                    // the wanted one is beside the message being looked at.
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
                            // The accent, not a per-sender colour: generated colours
                            // answer a question only group chats have.
                            color: Kirigami.Theme.highlightColor
                            // Off contentWidth, not the row: reading back a width the
                            // layout is still working out puts the header in a loop.
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

                    // Quote, attachment and reactions are loaded rather than declared:
                    // most messages have none of the three, and building all three per
                    // row was most of what a fast scroll paid for.
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
                            mediaUrl: root.mediaUrl
                            mediaKind: root.mediaKind
                            mediaWidth: root.mediaWidth
                            mediaHeight: root.mediaHeight
                            mediaDurationMs: root.mediaDuration
                            fileName: root.text
                            isImage: root.isImage
                            maxImageWidth: Math.min(Kirigami.Units.gridUnit * 18,
                                                    root.contentWidth - root.gutterWidth - Kirigami.Units.largeSpacing * 2)
                            onImageActivated: (source) => root.imageActivated(source)
                            onFileActivated: (source) => root.fileActivated(source)
                        }
                    }

                    // A poll: the question and one button per option. Voting goes
                    // straight to the bridge with this row's room and event id; the
                    // bridge posts an m.poll.response the server fans back as votes.
                    Loader {
                        Layout.fillWidth: true
                        active: root.poll && root.poll.answers && root.poll.answers.length > 0
                        visible: active

                        sourceComponent: ColumnLayout {
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Label {
                                Layout.fillWidth: true
                                text: root.poll.question || ""
                                textFormat: Text.PlainText
                                font.bold: true
                                wrapMode: Text.Wrap
                            }

                            Repeater {
                                model: root.poll.answers
                                delegate: QQC2.Button {
                                    Layout.fillWidth: true
                                    text: modelData.body || ""
                                    onClicked: matrixRooms.sendPollVote(root.chatId, root.msgId, modelData.id || "")
                                }
                            }

                            QQC2.Label {
                                Layout.fillWidth: true
                                visible: root.poll.disclosed === false
                                text: i18nc("@info poll votes are hidden until closed", "Votes are private until the poll closes.")
                                textFormat: Text.PlainText
                                font: Kirigami.Theme.smallFont
                                color: Kirigami.Theme.disabledTextColor
                            }
                        }
                    }

                    // Plain Label rather than SelectableLabel: that one takes the right
                    // button for a copy menu of its own, and the right button on a
                    // message belongs to the message menu.
                    QQC2.Label {
                        id: bodyLabel

                        Layout.fillWidth: true
                        visible: !root.isFile && !(root.poll && root.poll.answers && root.poll.answers.length > 0)
                        Layout.preferredHeight: visible ? -1 : 0
                        text: root.emojiOnly ? root.text : root.renderedBody
                        textFormat: root.emojiOnly ? Text.PlainText : Text.RichText
                        wrapMode: Text.Wrap
                        // pointSize and not pixelSize: a desktop font is configured in
                        // points and reports pixelSize -1, which the zoom would multiply.
                        font.pointSize: (root.emojiOnly ? 2.5 : 1.0)
                            * Kirigami.Theme.defaultFont.pointSize * root.fontScale

                        // Opened outside rather than followed in place: nothing in here
                        // renders a page, and a messenger that navigates is a browser.
                        onLinkActivated: (link) => Qt.openUrlExternally(link)

                        HoverHandler {
                            cursorShape: bodyLabel.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                        }

                        // Only bound when the message has a spoiler, so an ordinary
                        // message is not swallowing left clicks.
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

                // At the trailing edge rather than under the message: a line of its
                // own would double the height of a run of one-word replies.
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

            // Loaded on hover and thrown away after: four buttons, four tooltips and a
            // shadowed rectangle, once built for every message in the view. The strip
            // hangs half above this message, so a pointer reaching for it has already
            // left the body and bodyHover alone would pull it from under the click.
            // Assigned, not bound: active -> item -> active is a loop QML reports.
            Loader {
                id: hoverActions

                property bool stripHovered: false

                anchors.right: parent.right
                anchors.verticalCenter: parent.top
                z: 1
                active: bodyHover.hovered || hoverActions.stripHovered

                sourceComponent: MessageActions {
                    hoverEnabled: true
                    canEdit: root.canEditMessages && root.isOwn && !root.isFile

                    onHoveredChanged: hoverActions.stripHovered = hovered
                    // Or a strip unloaded while the pointer was still on it
                    // would leave the flag up and the next one stuck open.
                    Component.onDestruction: hoverActions.stripHovered = false

                    onReactRequested: root.reactRequested(root.index)
                    onReplyRequested: root.replyRequested(root.index, root.authorName, root.text, root.msgId)
                    onPinRequested: root.pinRequested(root.index, root.msgId)
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
