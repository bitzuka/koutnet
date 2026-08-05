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

Kirigami.Page {
    id: root

    property string peerIp: ""
    property var peerInfo: null
    property var messagesModel: null

    signal sendRequested(string text)
    signal attachRequested(string localFilePath)
    signal callRequested()
    signal profileRequested()
    signal newChatRequested()
    signal forwardRequested(int index)
    signal deleteRequested(int index)
    // The window owns the one message strip, this page only asks for it.
    signal notifyRequested(string text)

    readonly property bool hasChat: root.peerIp.length > 0 && root.messagesModel !== null

    property string replyToText: ""
    // Ctrl+wheel zoom target, multiplies message text font size.
    property real chatFontScale: 1.0

    // What the image viewer is currently showing. The viewer needs a model, and a
    // model of one item is still a model.
    property string viewerSource: ""
    property string viewerCaption: ""

    function isEmojiOnlyText(text) {
        if (!text || text.trim().length === 0 || text.length > 16 || text.indexOf(' ') >= 0 || text.indexOf('\n') >= 0) return false
        let count = 0
        for (const ch of text) {
            count++
            const cp = ch.codePointAt(0)
            if (!root.isEmojiCodePoint(cp)) return false
        }
        return count > 0 && count <= 6
    }

    function isEmojiCodePoint(cp) {
        return (cp >= 0x1F600 && cp <= 0x1F64F) ||
               (cp >= 0x1F300 && cp <= 0x1F5FF) ||
               (cp >= 0x1F680 && cp <= 0x1F6FF) ||
               (cp >= 0x1F1E0 && cp <= 0x1F1FF) ||
               (cp >= 0x2600 && cp <= 0x26FF) ||
               (cp >= 0x2700 && cp <= 0x27BF) ||
               (cp >= 0x1F900 && cp <= 0x1F9FF) ||
               (cp >= 0x1FA00 && cp <= 0x1FA6F) ||
               (cp >= 0x1F7E0 && cp <= 0x1F7FF) ||
               cp === 0x2764 || cp === 0x2665 || cp === 0x2728 ||
               cp === 0x2B50 || cp === 0x2B55 || cp === 0xA9 || cp === 0xAE ||
               cp === 0x2122 || cp === 0x3030 || cp === 0x303D ||
               (cp >= 0x2000 && cp <= 0x200F) || (cp >= 0xFE00 && cp <= 0xFE0F)
    }

    readonly property var quickEmojis: ["👍", "❤️", "😂", "😮", "😢", "🔥"]
    // Client-side only for now - not persisted, not synced to peers.
    // Making custom emoji durable/shared needs a backend store (e.g. a
    // ReactionStore/EmojiStore extension) which isn't wired up yet.
    property var customEmojis: []

    title: root.peerInfo ? root.peerInfo.username : i18nc("@title", "Chat")
    padding: 0

    // See the note on Kirigami.Theme in Main.qml. Restated here rather than
    // inherited because the own-message bubble is mixed out of it, and a bubble
    // that quietly turns Breeze blue is the whole problem being fixed.
    Kirigami.Theme.highlightColor: Brand.accent

    // Avatar, name and presence in the toolbar itself, which is where Kirigami
    // puts a page's identity. The old version drew a second 52px header strip
    // under the toolbar and repeated the name in it.
    titleDelegate: RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.largeSpacing
        visible: root.peerInfo !== null

        Components.Avatar {
            implicitWidth: Kirigami.Units.iconSizes.medium
            implicitHeight: Kirigami.Units.iconSizes.medium
            Layout.alignment: Qt.AlignVCenter
            name: (root.peerInfo && root.peerInfo.isFavorites) ? "" : root.title
            iconSource: (root.peerInfo && root.peerInfo.isFavorites) ? "bookmarks" : ""
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 4
                elide: Text.ElideRight
                text: root.title
            }

            // This used to read "last seen <full date>" whenever lastSeen was
            // non-zero and "online" only when it was zero, which is backwards:
            // handlePresence() stamps last_seen on every packet that arrives, so a
            // peer that was up had a fresh stamp and therefore never once said
            // "online". Reachability is the flag, the stamp is only what it fell
            // back to.
            //
            // RelativeTime.now is read so this ages on its own: the label has to
            // walk from "just now" to "2 minutes ago" with the window sitting open
            // and nothing arriving.
            QQC2.Label {
                Layout.fillWidth: true
                visible: root.peerInfo && !root.peerInfo.isFavorites
                elide: Text.ElideRight
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
            text: i18nc("@action:button open the peer's profile", "Profile")
            icon.name: "user-identity"
            visible: root.hasChat && !(root.peerInfo && root.peerInfo.isFavorites)
            onTriggered: root.profileRequested()
        },
        Kirigami.Action {
            text: i18nc("@action:button start a voice call", "Call")
            icon.name: "call-start"
            visible: root.hasChat && !(root.peerInfo && root.peerInfo.isFavorites)
            onTriggered: root.callRequested()
        }
    ]

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
    // it written here. This used to be a Popup wrapping an Image whose fitted size
    // was worked out by hand from sourceSize.
    //
    // One item rather than an album because a chat bubble is one picture; the
    // component takes a model either way, and the roles below are the ones it
    // reads. The list has to be typed rather than a JS array or the roles do not
    // resolve.
    property list<AlbumModelItem> viewerItems: [
        AlbumModelItem {
            type: AlbumModelItem.Image
            source: root.viewerSource
            caption: root.viewerCaption
        }
    ]

    AlbumMaximizeComponent {
        id: imageViewer

        // See the note on Kirigami.Theme in Main.qml: this is reparented into the
        // window overlay, which is a theme chain of its own.
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

    // One menu for the whole list rather than one per bubble, so scrolling a long
    // conversation does not build a menu per message.
    Components.ConvergentContextMenu {
        id: messageMenu

        property int msgIndex: -1
        property string msgText: ""

        Kirigami.Action {
            text: i18nc("@action:inmenu", "Reply")
            icon.name: "mail-replied-symbolic"
            onTriggered: {
                root.replyToText = messageMenu.msgText
                inputField.forceActiveFocus()
            }
        }
        Kirigami.Action {
            text: i18nc("@action:inmenu", "Copy")
            icon.name: "edit-copy"
            enabled: messageMenu.msgText.length > 0
            onTriggered: {
                clipboardHelper.copyText(messageMenu.msgText)
                root.notifyRequested(i18nc("@info:status", "Message copied to the clipboard"))
            }
        }
        Kirigami.Action {
            text: i18nc("@action:inmenu forward this message", "Forward")
            icon.name: "mail-forward"
            onTriggered: root.forwardRequested(messageMenu.msgIndex)
        }
        Kirigami.Action {
            text: i18nc("@action:inmenu add a reaction", "React")
            icon.name: "smiley"
            onTriggered: root.openReactionPicker(messageMenu.msgIndex)
        }
        Kirigami.Action { separator: true }
        Kirigami.Action {
            text: i18nc("@action:inmenu delete this message", "Delete")
            icon.name: "edit-delete"
            onTriggered: root.deleteRequested(messageMenu.msgIndex)
        }
    }

    function openMessageMenu(index, text) {
        messageMenu.msgIndex = index
        messageMenu.msgText = text
        messageMenu.popup()
    }

    function openReactionPicker(index) {
        reactionPopup.msgIndex = index
        reactionPopup.open()
    }

    // Reaction picker. No background of its own any more: the style already knows
    // what a popup frame looks like, and the hand-drawn one only knew what the
    // palette table said.
    QQC2.Popup {
        id: reactionPopup

        property int msgIndex: -1

        // See the note on Kirigami.Theme in Main.qml.
        Kirigami.Theme.inherit: false
        Kirigami.Theme.highlightColor: Brand.accent

        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        padding: Kirigami.Units.largeSpacing

        contentItem: GridLayout {
            columns: 6
            columnSpacing: Kirigami.Units.smallSpacing
            rowSpacing: Kirigami.Units.smallSpacing

            Repeater {
                model: root.quickEmojis.concat(root.customEmojis)

                delegate: QQC2.ToolButton {
                    required property var modelData

                    implicitWidth: Kirigami.Units.gridUnit * 2
                    implicitHeight: Kirigami.Units.gridUnit * 2
                    // A picture emoji is a file path behind an "img:" marker, a
                    // plain one is the character itself.
                    readonly property bool isImage: typeof modelData === "string" && modelData.indexOf("img:") === 0
                    text: isImage ? "" : modelData
                    icon.source: isImage ? modelData.substring(4) : ""
                    display: isImage ? QQC2.AbstractButton.IconOnly : QQC2.AbstractButton.TextOnly

                    onClicked: {
                        root.messagesModel.toggleReaction(reactionPopup.msgIndex, modelData, "me")
                        reactionPopup.close()
                    }
                }
            }

            // Adds a custom emoji from a local image.
            QQC2.ToolButton {
                implicitWidth: Kirigami.Units.gridUnit * 2
                implicitHeight: Kirigami.Units.gridUnit * 2
                display: QQC2.AbstractButton.IconOnly
                icon.name: "list-add"
                text: i18nc("@action:button add a picture to use as a reaction", "Add an emoji")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: text
                onClicked: customEmojiDialog.open()
            }
        }
    }

    FileDialog {
        id: customEmojiDialog
        title: i18nc("@title:window", "Choose an image for the emoji")
        nameFilters: [i18nc("@item:inlistbox file dialog filter, keep the glob patterns",
                            "Images (*.png *.jpg *.jpeg *.webp *.gif)")]
        onAccepted: {
            root.customEmojis.push("img:" + selectedFile.toString())
            root.customEmojis = root.customEmojis.slice()
        }
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

    ListView {
        id: messagesList

        anchors.fill: parent
        visible: root.hasChat
        clip: true
        model: root.messagesModel
        spacing: Kirigami.Units.smallSpacing
        topMargin: Kirigami.Units.smallSpacing
        bottomMargin: Kirigami.Units.smallSpacing

        // On Wayland libinput reports wheel notches with a non-null
        // pixelDelta, and Flickable's built-in handling reads that as
        // trackpad-style direct movement: no flick, no deceleration.
        // Accepting the event here and driving flick() ourselves keeps
        // the wheel inertial whatever the compositor reports.
        flickDeceleration: 400
        maximumFlickVelocity: 10000
        boundsBehavior: Flickable.StopAtBounds
        pixelAligned: false

        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            // A burst of notches should build more speed than one
            // isolated click, so track the gap since the last wheel
            // event and how many have arrived in the streak.
            property real lastWheelTime: 0
            property int streakCount: 0

            onWheel: (event) => {
                const now = Date.now()
                if (now - lastWheelTime > 250)
                    streakCount = 0
                streakCount = Math.min(streakCount + 1, 8)
                lastWheelTime = now

                // Ctrl+wheel: zoom the chat text size instead of
                // scrolling - mirrors the familiar browser/editor
                // convention.
                if (event.modifiers & Qt.ControlModifier) {
                    root.chatFontScale = Math.max(0.7, Math.min(2.0,
                        root.chatFontScale + (event.angleDelta.y > 0 ? 0.05 : -0.05)))
                    event.accepted = true
                    return
                }

                // Shift+wheel: fast scroll, several messages per notch.
                const shiftBoost = (event.modifiers & Qt.ShiftModifier) ? 3 : 1
                const streakBoost = 1 + streakCount * 0.15

                if (event.pixelDelta.y !== 0) {
                    // Trackpad panning on Wayland reports pixelDelta
                    // with angleDelta at zero. Moving 1:1 feels right
                    // without inertia; the streak boost still applies.
                    const maxY = Math.max(0, messagesList.contentHeight - messagesList.height)
                    const delta = event.pixelDelta.y * shiftBoost * streakBoost
                    messagesList.contentY = Math.max(0, Math.min(maxY, messagesList.contentY - delta))
                } else {
                    // Toned down from the original tuning - the
                    // previous multiplier made every single notch
                    // glide too far past where the wheel actually
                    // stopped.
                    messagesList.flick(0, event.angleDelta.y * 5 * shiftBoost * streakBoost)
                }
                event.accepted = true
            }
        }

        onCountChanged: Qt.callLater(positionViewAtEnd)
        Component.onCompleted: positionViewAtEnd()

        delegate: Item {
            id: messageRow

            width: messagesList.width
            implicitHeight: model.isSystem ? systemLabel.implicitHeight : bubbleColumn.implicitHeight
            height: implicitHeight

            // Captured because the reaction Repeater below declares an index of
            // its own, and the unqualified name resolves to the innermost one -
            // which is how reacting to a message used to toggle the reaction on
            // whichever row happened to share the badge's number.
            readonly property int messageIndex: index
            readonly property bool isEmojiOnly: root.isEmojiOnlyText(model.text) && !model.isFile && !model.isSystem
            readonly property bool isPicture: model.isFile === true && model.isImage === true
            readonly property bool hasReply: model.replyToText && model.replyToText.length > 0
            // A bubble is at most this wide, whichever side it is on.
            readonly property real maxBubbleWidth: Math.round(messagesList.width * 0.7)

            QQC2.Label {
                id: systemLabel
                visible: model.isSystem === true
                anchors.horizontalCenter: parent.horizontalCenter
                topPadding: Kirigami.Units.smallSpacing
                bottomPadding: Kirigami.Units.smallSpacing
                text: model.text
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                font.italic: true
                color: Kirigami.Theme.disabledTextColor
            }

            ColumnLayout {
                id: bubbleColumn

                visible: model.isSystem !== true
                spacing: 0
                anchors.right: model.isOwn ? parent.right : undefined
                anchors.left: model.isOwn ? undefined : parent.left
                anchors.rightMargin: Kirigami.Units.largeSpacing
                anchors.leftMargin: Kirigami.Units.largeSpacing
                width: Math.min(implicitWidth, messageRow.maxBubbleWidth)

                QQC2.Label {
                    Layout.fillWidth: true
                    visible: !model.isOwn && model.sender && model.sender.length > 0
                    text: model.sender || ""
                    elide: Text.ElideRight
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    font.bold: true
                    color: model.color || Kirigami.Theme.highlightColor
                }

                // ShadowedRectangle rather than a Rectangle with a radius: the
                // corner rounding, the border and the drop shadow are one item
                // and one repaint, and the radius comes from Kirigami.
                Kirigami.ShadowedRectangle {
                    id: bubble

                    // A picture fills its bubble edge to edge, so an inset here
                    // would push it past the corner rounding.
                    readonly property real contentMargin: messageRow.isPicture && !messageRow.hasReply
                        ? 0 : Kirigami.Units.smallSpacing

                    Layout.fillWidth: true
                    implicitWidth: bubbleContent.implicitWidth + bubble.contentMargin * 2
                    implicitHeight: bubbleContent.implicitHeight + bubble.contentMargin * 2
                    radius: Kirigami.Units.cornerRadius
                    // An emoji on its own is the message; a frame round it only
                    // makes it look smaller.
                    color: messageRow.isEmojiOnly
                        ? "transparent"
                        : (model.isOwn
                            // The same recipe the Kirigami list delegates use for
                            // a selected row, so an own bubble is the accent
                            // without being a block of it.
                            ? Kirigami.ColorUtils.tintWithAlpha(Kirigami.Theme.backgroundColor, Kirigami.Theme.highlightColor, 0.3)
                            : Kirigami.Theme.alternateBackgroundColor)
                    border.width: messageRow.isEmojiOnly ? 0 : 1
                    border.color: Kirigami.ColorUtils.linearInterpolation(bubble.color, Kirigami.Theme.textColor, 0.15)
                    shadow.size: messageRow.isEmojiOnly ? 0 : Kirigami.Units.smallSpacing
                    shadow.color: Qt.rgba(0, 0, 0, 0.10)
                    shadow.yOffset: 1

                    ColumnLayout {
                        id: bubbleContent

                        anchors.fill: parent
                        anchors.margins: bubble.contentMargin
                        spacing: Kirigami.Units.smallSpacing

                        // Quoted message this one answers.
                        QQC2.Label {
                            Layout.fillWidth: true
                            visible: messageRow.hasReply
                            leftPadding: Kirigami.Units.smallSpacing
                            text: model.replyToText || ""
                            elide: Text.ElideRight
                            font: Kirigami.Theme.smallFont
                            color: Kirigami.Theme.disabledTextColor

                            Kirigami.Separator {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: Math.round(Kirigami.Units.smallSpacing / 2)
                                color: Kirigami.Theme.highlightColor
                            }
                        }

                        // Picture. Sized from the file's own aspect ratio, capped
                        // to the bubble, never blown up past what the file has.
                        Image {
                            id: bubbleImage

                            // Sized off a constant rather than off its own width:
                            // a height that reads back the layout's width is the
                            // binding loop this is written around.
                            readonly property real pictureWidth:
                                Math.min(messageRow.maxBubbleWidth, Kirigami.Units.gridUnit * 16)

                            Layout.preferredWidth: pictureWidth
                            Layout.preferredHeight: sourceSize.width > 0
                                ? Math.round(pictureWidth * (sourceSize.height / sourceSize.width))
                                : Kirigami.Units.gridUnit * 8
                            visible: messageRow.isPicture
                            source: messageRow.isPicture ? "file://" + model.filePath : ""
                            fillMode: Image.PreserveAspectFit
                            mipmap: true
                            asynchronous: true

                            HoverHandler {
                                cursorShape: Qt.PointingHandCursor
                            }
                            TapHandler {
                                onTapped: root.showImage(model.filePath)
                            }
                        }

                        // A file that is not a picture: name, icon, opens on click.
                        QQC2.ItemDelegate {
                            Layout.fillWidth: true
                            visible: model.isFile === true && !messageRow.isPicture
                            text: model.text || ""
                            icon.name: "document-open"
                            onClicked: Qt.openUrlExternally("file://" + model.filePath)
                        }

                        // Plain Label rather than SelectableLabel: that one takes
                        // the right button for its own copy menu, and the message
                        // menu is what belongs on a bubble.
                        QQC2.Label {
                            Layout.fillWidth: true
                            visible: model.isFile !== true
                            text: model.text || ""
                            textFormat: Text.PlainText
                            wrapMode: Text.WordWrap
                            // pointSize, not pixelSize: a desktop font is
                            // configured in points and reports pixelSize -1, which
                            // is what the Ctrl+wheel zoom used to multiply.
                            font.pointSize: (messageRow.isEmojiOnly ? 2.5 : 1.0)
                                * Kirigami.Theme.defaultFont.pointSize * root.chatFontScale
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            // Captured here rather than inline below. Repeater declares
                            // its own "model", so an unqualified reference inside its
                            // model: binding resolves to itself and loops.
                            readonly property var reactionsList: (model && model.reactions) ? model.reactions : []
                            visible: reactionsList.length > 0

                            Repeater {
                                model: parent.reactionsList

                                delegate: QQC2.Label {
                                    required property var modelData

                                    padding: Kirigami.Units.smallSpacing
                                    text: i18ncp("@item reaction badge, %2 is the emoji",
                                                 "%2 %1", "%2 %1",
                                                 modelData.count, modelData.emoji)
                                    font: Kirigami.Theme.smallFont

                                    background: Rectangle {
                                        radius: Kirigami.Units.cornerRadius
                                        color: Kirigami.ColorUtils.tintWithAlpha(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.10)
                                    }

                                    TapHandler {
                                        onTapped: root.messagesModel.toggleReaction(messageRow.messageIndex, modelData.emoji, "me")
                                    }
                                }
                            }
                        }
                    }

                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: root.openMessageMenu(messageRow.messageIndex, model.text || "")
                    }
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onLongPressed: root.openMessageMenu(messageRow.messageIndex, model.text || "")
                    }
                }

                RowLayout {
                    Layout.alignment: model.isOwn ? Qt.AlignRight : Qt.AlignLeft
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        visible: model.isEdited === true
                        text: i18nc("@info:status the message was edited", "edited")
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        font.italic: true
                        color: Kirigami.Theme.disabledTextColor
                    }

                    QQC2.Label {
                        text: model.timeString || ""
                        font: Kirigami.Theme.smallFont
                        color: Kirigami.Theme.disabledTextColor
                    }

                    // A tick used to go up the moment the message was handed
                    // to the socket, which over UDP says nothing at all -
                    // the receiver may have dropped it, and that is exactly
                    // what it looked like when it did. Ticks are now the read
                    // receipt the peer really sends; on its own an outgoing
                    // message is sent and nothing more is known about it.
                    QQC2.Label {
                        id: deliveryMark
                        visible: model.isOwn === true
                        text: model.isRead ? "\u2713\u2713" : "\u2191"
                        font: Kirigami.Theme.smallFont
                        color: model.isRead ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor

                        HoverHandler {
                            id: deliveryMarkHover
                        }
                        QQC2.ToolTip.visible: deliveryMarkHover.hovered
                        QQC2.ToolTip.text: model.isRead
                            ? i18nc("@info:tooltip state of an outgoing message", "Read by the recipient")
                            : i18nc("@info:tooltip state of an outgoing message", "Sent - delivery not confirmed")
                    }
                }
            }
        }
    }

    footer: ColumnLayout {
        spacing: 0
        visible: root.hasChat

        // What this message is answering, with a way out of it.
        QQC2.ToolBar {
            Layout.fillWidth: true
            visible: root.replyToText.length > 0

            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                    source: "mail-replied-symbolic"
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: root.replyToText
                    elide: Text.ElideRight
                    font: Kirigami.Theme.smallFont
                }
                QQC2.ToolButton {
                    display: QQC2.AbstractButton.IconOnly
                    icon.name: "dialog-close"
                    text: i18nc("@action:button stop replying to the quoted message", "Cancel the reply")
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: text
                    onClicked: root.replyToText = ""
                }
            }
        }

        QQC2.ToolBar {
            Layout.fillWidth: true
            position: QQC2.ToolBar.Footer

            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.ToolButton {
                    display: QQC2.AbstractButton.IconOnly
                    icon.name: "mail-attachment"
                    text: i18nc("@action:button attach a file to the message", "Attach a file")
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: text
                    onClicked: fileDialog.open()
                }

                QQC2.ToolButton {
                    display: QQC2.AbstractButton.IconOnly
                    icon.name: "smiley"
                    text: i18nc("@action:button open the emoji picker", "Insert an emoji")
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: text
                    onClicked: emojiPicker.open()
                }

                QQC2.TextField {
                    id: inputField
                    Layout.fillWidth: true
                    placeholderText: i18nc("@info:placeholder", "Message...")
                    onAccepted: sendButton.clicked()
                }

                QQC2.Button {
                    id: sendButton
                    text: i18nc("@action:button", "Send")
                    icon.name: "document-send"
                    // Highlighted is what carries the accent here, so the button
                    // does not need a background of its own.
                    highlighted: true
                    enabled: inputField.text.length > 0
                    onClicked: {
                        root.sendRequested(inputField.text)
                        inputField.text = ""
                        root.replyToText = ""
                    }
                }
            }
        }
    }

    // Emoji picker, categorised, not exhaustive but broad.
    QQC2.Popup {
        id: emojiPicker

        // See the note on Kirigami.Theme in Main.qml.
        Kirigami.Theme.inherit: false
        Kirigami.Theme.highlightColor: Brand.accent

        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        width: Kirigami.Units.gridUnit * 20
        height: Kirigami.Units.gridUnit * 18
        padding: Kirigami.Units.smallSpacing

        readonly property var categoryKeys: Object.keys(emojiPicker.emojiCategories)

        readonly property var emojiCategories: ({
            "😀": ["😀","😁","😂","🤣","😃","😄","😅","😆","😉","😊","😋","😎","😍","😘","🥰","😗","😙","😚","☺","🙂","🤗","🤩","🤔","🤨","😐","😑","😶","🙄","😏","😣","😥","😮","🤐","😯","😪","😫","🥱","😴","😌","😛","😜","😝","🤤","😒","😓","😔","😕","🙃","🤑","😲","☹","🙁","😖","😞","😟","😤","😢","😭","😦","😧","😨","😩","🤯","😬","😰","😱","🥵","🥶","😳","🤪","😵","🥴","😠","😡","🤬","😷","🤒","🤕","🤢","🤮","🤧","😇","🥳","🥺","🤠","🤡","🤥","🤫","🤭","🧐","🤓","😈","👿","👹","👺","💀","👻","👽","🤖","💩","😺","😸","😹","😻","😼","😽","🙀","😿","😾"],
            "👍": ["👍","👎","👏","🙌","👐","🤲","🤝","🙏","✍","💅","🤳","💪","🦾","🦿","🦵","🦶","👂","🦻","👃","🧠","🦷","🦴","👀","👁","👅","👄","💋","🩸","👶","🧒","👦","👧","🧑","👱","👨","🧔","👩","🧓","👴","👵","🙍","🙎","🙅","🙆","💁","🙋","🧏","🙇","🤦","🤷","👮","🕵","💂","🥷","👷","🤴","👸","👳","👲","🧕","🤵","👰","🤰","🤱","👼","🎅","🤶","🦸","🦹","🧙","🧚","🧛","🧜","🧝","🧞","🧟","💆","💇","🚶","🧍","🧎","🏃","💃","🕺","🕴","👯","🧖","🧗","🤺","🏇","⛷","🏂","🏌","🏄","🚣","🏊","⛹","🏋","🚴","🚵","🤸","🤼","🤽","🤾","🤹","🧘","🛀","🛌"],
            "🐶": ["🐶","🐱","🐭","🐹","🐰","🦊","🐻","🐼","🐨","🐯","🦁","🐮","🐷","🐽","🐸","🐵","🙈","🙉","🙊","🐒","🐔","🐧","🐦","🐤","🐣","🐥","🦆","🦅","🦉","🦇","🐺","🐗","🐴","🦄","🐝","🐛","🦋","🐌","🐞","🐜","🦟","🦗","🕷","🕸","🦂","🐢","🐍","🦎","🦖","🦕","🐙","🦑","🦐","🦞","🦀","🐡","🐠","🐟","🐬","🐳","🐋","🦈","🐊","🐅","🐆","🦓","🦍","🦧","🐘","🦛","🦏","🐪","🐫","🦒","🦘","🐃","🐂","🐄","🐎","🐖","🐏","🐑","🦙","🐐","🦌","🐕","🐩","🦮","🐕‍🦺","🐈","🐈‍⬛","🐓","🦃","🦚","🦜","🦢","🦩","🕊","🐇","🦝","🦨","🦡","🦦","🦥","🐁","🐀","🐿","🦔","🐾","🐉","🐲","🌵","🎄","🌲","🌳","🌴","🌱","🌿","☘","🍀","🎍","🎋","🍃","🍂","🍁","🍄","🌾","💐","🌷","🌹","🥀","🌺","🌸","🌼","🌻","🌞","🌝","🌛","🌜","🌚","🌕","🌖","🌗","🌘","🌑","🌒","🌓","🌔","🌙","🌎","🌍","🌏","🪐","💫","⭐","🌟","✨","⚡","🔥","💥","☄","☀","🌤","⛅","🌥","☁","🌦","🌧","⛈","🌩","🌨","❄","☃","⛄","🌬","💨","🌪","🌫","🌈","☂","☔","💧","💦","🌊"],
            "🍎": ["🍏","🍎","🍐","🍊","🍋","🍌","🍉","🍇","🍓","🫐","🍈","🍒","🍑","🥭","🍍","🥥","🥝","🍅","🍆","🥑","🥦","🥬","🥒","🌶","🫑","🌽","🥕","🫒","🧄","🧅","🥔","🍠","🥐","🥯","🍞","🥖","🥨","🧀","🥚","🍳","🧈","🥞","🧇","🥓","🥩","🍗","🍖","🦴","🌭","🍔","🍟","🍕","🫓","🥪","🥙","🧆","🌮","🌯","🫔","🥗","🥘","🫕","🥫","🍝","🍜","🍲","🍛","🍣","🍱","🥟","🦪","🍤","🍙","🍚","🍘","🍥","🥠","🥮","🍢","🍡","🍧","🍨","🍦","🥧","🧁","🍰","🎂","🍮","🍭","🍬","🍫","🍿","🍩","🍪","🌰","🥜","🍯","🥛","🍼","🫖","☕","🍵","🧃","🥤","🧋","🍶","🍺","🍻","🥂","🍷","🥃","🍸","🍹","🧉","🍾","🧊","🥄","🍴","🍽","🥣","🥡","🥢","🧂"],
            "⚽": ["⚽","🏀","🏈","⚾","🥎","🎾","🏐","🏉","🥏","🎱","🪀","🏓","🏸","🏒","🏑","🥍","🏏","🥅","⛳","🪁","🏹","🎣","🤿","🥊","🥋","🎽","🛹","🛷","⛸","🥌","🎿","⛷","🏂","🪂","🏋","🤼","🤸","⛹","🤺","🤾","🏌","🏇","⛷","🏂","🏄","🏊","🤽","🚣","🧗","🚴","🚵","🏎","🏍","🤹","🎖","🏆","🏅","🥇","🥈","🥉","🎗","🏵","🎫","🎟","🎪","🤹","🎭","🩰","🎨","🎬","🎤","🎧","🎼","🎹","🥁","🎷","🎺","🎸","🪕","🎻","🎲","♟","🎯","🎳","🎮","🎰","🧩"],
            "❤": ["❤","🧡","💛","💚","💙","💜","🖤","🤍","🤎","💔","❣","💕","💞","💓","💗","💖","💘","💝","💟","☮","✝","☪","🕉","☸","✡","🔯","🕎","☯","☦","🛐","⛎","♈","♉","♊","♋","♌","♍","♎","♏","♐","♑","♒","♓","🆔","⚛","🉑","☢","☣","📴","📳","🈶","🈚","🈸","🈺","🈷","✴","🆚","💮","🉐","㊙","㊗","🈴","🈵","🈹","🈲","🅰","🅱","🆎","🆑","🅾","🆘","❌","⭕","🛑","⛔","📛","🚫","💯","💢","♨","🚷","🚯","🚳","🚱","🔞","📵","🚭","❗","❕","❓","❔","‼","⁉","🔅","🔆","〽","⚠","🚸","🔱","⚜","🔰","♻","✅","🈯","💹","❇","✳","❎","🌐","💠","Ⓜ","🌀","💤","🏧","🚾","♿","🅿","🈳","🈂","🛂","🛃","🛄","🛅","🛗","🧭","🧱","🧳","⌚","⏰","⏱","⏲","🕰","🕛","🕧","🕐","🕜","🕑","🕝","🕒","🕞","🕓","🕟","🕔","🕠","🕕","🕡","🕖","🕢","🕗","🕣","🕘","🕤","🕙","🕥","🕚","🕦","🌑","🌒","🌓","🌔","🌕","🌖","🌗","🌘","🌙","🌚","🌛","🌜","🌡","☀","🌝","🌞","🪐","⭐","🌟","🌠","🌌","☁","⛅","⛈","🌤","🌥","🌦","🌧","🌨","❄","🌬","💨","🌪","🌫","🌈","☂","☔","⚡","❄","☃","⛄","☄","🔥","💧","🌊"],
        })

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.TabBar {
                id: categoryTabs
                Layout.fillWidth: true

                Repeater {
                    model: emojiPicker.categoryKeys

                    delegate: QQC2.TabButton {
                        required property var modelData
                        text: modelData
                    }
                }
            }

            GridView {
                id: emojiGrid
                Layout.fillWidth: true
                Layout.fillHeight: true
                cellWidth: Kirigami.Units.gridUnit * 2.2
                cellHeight: Kirigami.Units.gridUnit * 2.2
                clip: true
                model: emojiPicker.emojiCategories[emojiPicker.categoryKeys[categoryTabs.currentIndex]]

                delegate: QQC2.ToolButton {
                    required property var modelData

                    width: emojiGrid.cellWidth
                    height: emojiGrid.cellHeight
                    text: modelData
                    onClicked: {
                        inputField.text += modelData
                        emojiPicker.close()
                        inputField.forceActiveFocus()
                    }
                }
            }
        }
    }
}
