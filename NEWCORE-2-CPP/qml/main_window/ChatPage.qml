// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import koutnet.app

Kirigami.Page {
    id: root

    property string peerIp: ""
    property var peerInfo: null
    property var messagesModel: null
    property bool showBackButton: false

    signal returnToListRequested()
    signal sendRequested(string text)
    signal attachRequested(string localFilePath)
    signal callRequested()
    signal profileRequested()
    signal forwardRequested(int index)
    signal deleteRequested(int index)

    property string replyToText: ""
    readonly property var theme: ThemeManager.colors
    // Ctrl+wheel zoom target, multiplies message text font size.
    property real chatFontScale: 1.0

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

    title: root.peerInfo ? root.peerInfo.username : root.peerIp
    padding: 0

    background: Rectangle { color: root.theme.bg }

    actions: [
        Kirigami.Action {
            text: i18n("Back")
            icon.name: "go-previous"
            visible: root.showBackButton
            onTriggered: root.returnToListRequested()
        },
        Kirigami.Action {
            text: i18n("Profile")
            icon.name: "user-identity"
            onTriggered: root.profileRequested()
        },
        Kirigami.Action {
            text: i18n("Call")
            icon.name: "call-start"
            onTriggered: root.callRequested()
        }
    ]

    // Small transient toast, shows the copied_notice string.
    Rectangle {
        id: toast
        z: 100
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 76
        radius: 8
        color: root.theme.item_sel
        opacity: 0
        implicitWidth: toastLabel.implicitWidth + 24
        implicitHeight: toastLabel.implicitHeight + 14

        Label {
            id: toastLabel
            anchors.centerIn: parent
            color: "white"
            text: ""
        }

        Behavior on opacity { NumberAnimation { duration: 150 } }

        function show(msg) {
            toastLabel.text = msg
            toast.opacity = 1
            toastTimer.restart()
        }
        Timer {
            id: toastTimer
            interval: 1400
            onTriggered: toast.opacity = 0
        }
    }

    TextEdit {
        id: clipboardHelper
        visible: false
        function copyText(str) {
            text = str
            selectAll()
            copy()
        }
    }

    // Inline image viewer. An overlay on this page rather than a separate
    // top-level Window.
    Rectangle {
        id: imageViewer
        anchors.fill: parent
        z: 90
        color: Qt.rgba(0, 0, 0, 0.92)
        opacity: 0
        scale: 0.92
        visible: opacity > 0.01
        property string source: ""

        Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on scale   { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

        Image {
            anchors.fill: parent
            anchors.margins: 24
            source: imageViewer.source
            fillMode: Image.PreserveAspectFit
        }

        ToolButton {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 12
            text: "✕"
            onClicked: imageViewer.hide()
        }

        MouseArea {
            anchors.fill: parent
            onClicked: imageViewer.hide()
        }

        Shortcut {
            sequence: "Escape"
            enabled: imageViewer.visible
            onActivated: imageViewer.hide()
        }

        function show(src) {
            imageViewer.source = src
            imageViewer.opacity = 1
            imageViewer.scale = 1
        }
        function hide() {
            imageViewer.opacity = 0
            imageViewer.scale = 0.92
        }
    }

    // Reaction picker: nicer grid popup instead of a plain submenu.
    Popup {
        id: reactionPopup
        property int msgIndex: -1
        modal: true
        focus: true
        width: 260
        padding: 12
        background: Rectangle { color: root.theme.bg2; radius: 12; border.color: root.theme.border; border.width: 1 }

        contentItem: GridLayout {
            columns: 6
            columnSpacing: 6
            rowSpacing: 6

            Repeater {
                model: root.quickEmojis.concat(root.customEmojis)
                delegate: Rectangle {
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 36
                    radius: 8
                    color: emojiMouse.containsMouse ? root.theme.btn_hover : "transparent"
                    scale: emojiMouse.containsMouse ? 1.15 : 1.0
                    Behavior on scale { NumberAnimation { duration: 90 } }

                    Loader {
                        anchors.centerIn: parent
                        sourceComponent: (typeof modelData === "string" && modelData.indexOf("img:") === 0)
                            ? customEmojiImg : plainEmojiText
                    }
                    Component {
                        id: plainEmojiText
                        Text { text: modelData; font.pixelSize: 20 }
                    }
                    Component {
                        id: customEmojiImg
                        Image {
                            width: 24; height: 24
                            source: modelData.substring(4)
                            fillMode: Image.PreserveAspectFit
                        }
                    }

                    MouseArea {
                        id: emojiMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            root.messagesModel.toggleReaction(reactionPopup.msgIndex, modelData, "me")
                            reactionPopup.close()
                        }
                    }
                }
            }

            // "+" adds a custom emoji from a local image (scaled to a
            // 128x128 display box like a normal sticker/emoji).
            Rectangle {
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                radius: 8
                color: addEmojiMouse.containsMouse ? root.theme.btn_hover : root.theme.bg3
                border.color: root.theme.border
                border.width: 1
                Kirigami.Icon { anchors.centerIn: parent; source: "list-add"; width: 18; height: 18; color: root.theme.text_dim }
                MouseArea {
                    id: addEmojiMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: customEmojiDialog.open()
                }
            }
        }
    }

    FileDialog {
        id: customEmojiDialog
        title: i18n("Choose an image for the emoji")
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.gif)"]
        onAccepted: {
            // Stored/rendered at a fixed 128x128 box everywhere it's used
            // (picker + reaction badge), matching a normal emoji's visual
            // footprint regardless of source image resolution.
            root.customEmojis.push("img:" + selectedFile.toString())
            root.customEmojis = root.customEmojis.slice()
        }
    }

    function openReactionPicker(index) {
        reactionPopup.msgIndex = index
        reactionPopup.open()
    }

    // Per-message context menu
    Menu {
        id: messageMenu
        property int msgIndex: -1
        property string msgText: ""

        MenuItem {
            text: i18n("Reply")
            onTriggered: {
                root.replyToText = messageMenu.msgText
                inputField.forceActiveFocus()
            }
        }
        MenuItem {
            text: i18n("Copy")
            onTriggered: {
                if (messageMenu.msgText.length > 0) {
                    clipboardHelper.copyText(messageMenu.msgText)
                    toast.show(i18n("Copied!"))
                }
            }
        }
        MenuItem {
            text: i18n("Forward")
            onTriggered: root.forwardRequested(messageMenu.msgIndex)
        }
        MenuItem {
            text: i18n("React")
            onTriggered: root.openReactionPicker(messageMenu.msgIndex)
        }
        MenuSeparator {}
        MenuItem {
            text: i18n("Delete")
            onTriggered: root.deleteRequested(messageMenu.msgIndex)
        }
    }

    function openMessageMenu(index, text) {
        messageMenu.msgIndex = index
        messageMenu.msgText = text
        messageMenu.popup()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Peer header: avatar + name + last-seen (favorites excluded)
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 52
            color: root.theme.header_bg
            visible: root.peerInfo !== null

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Kirigami.Units.largeSpacing
                anchors.rightMargin: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.smallSpacing

                Rectangle {
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 36
                    radius: 18
                    color: root.theme.accent

                    // Same bookmarks icon the sidebar row uses. These two
                    // used to disagree, one glyph and one icon.
                    Kirigami.Icon {
                        anchors.centerIn: parent
                        width: 22
                        height: 22
                        visible: root.peerInfo && root.peerInfo.isFavorites
                        source: "bookmarks"
                        color: "white"
                        isMask: true
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: !(root.peerInfo && root.peerInfo.isFavorites)
                        text: root.peerInfo ? root.peerInfo.avatarLetter : "?"
                        color: "white"
                        font.bold: true
                    }
                }

                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    Label {
                        text: root.peerInfo ? root.peerInfo.username : root.peerIp
                        color: root.theme.text
                        font.bold: true
                    }
                    Label {
                        visible: root.peerInfo && !root.peerInfo.isFavorites
                        text: root.peerInfo && root.peerInfo.lastSeen > 0
                            ? i18n("last seen") + " " + new Date(root.peerInfo.lastSeen * 1000).toLocaleString()
                            : i18n("online")
                        color: root.theme.text_dim
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }
                }
            }
        }

        ListView {
            id: messagesList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.messagesModel

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
                width: messagesList.width
                readonly property bool isEmojiOnly: root.isEmojiOnlyText(model.text) && !model.isFile && !model.isSystem
                height: model.isSystem ? sysLabel.implicitHeight + 12 : contentColumn.height + Kirigami.Units.smallSpacing

                Label {
                    id: sysLabel
                    visible: model.isSystem
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: model.text
                    font.italic: true
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    color: root.theme.text_dim
                }

                Column {
                    id: contentColumn
                    visible: model.isSystem !== true
                    anchors.right: model.isOwn ? parent.right : undefined
                    anchors.left: model.isOwn ? undefined : parent.left
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: 2
                    width: Math.min(bubble.implicitWidth + 16, messagesList.width * 0.7)

                    Label {
                        visible: !model.isOwn && model.sender && model.sender.length > 0
                        text: model.sender
                        font.bold: true
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        color: model.color || root.theme.accent
                    }

                    Rectangle {
                        id: bubble
                        // Text and file bubbles cap at 70% width. Image
                        // bubbles take their size from the picture's own
                        // aspect ratio, see imageLoader below.
                        width: (model.isFile && model.isImage)
                            ? imageLoader.item ? imageLoader.item.width : 220
                            : Math.min(implicitWidth, messagesList.width * 0.7)
                        implicitWidth: bubbleColumn.implicitWidth + Kirigami.Units.largeSpacing * 2
                        height: (model.isFile && model.isImage && !(model.replyToText && model.replyToText.length > 0))
                            ? bubbleColumn.implicitHeight
                            : bubbleColumn.implicitHeight + Kirigami.Units.smallSpacing * 2
                        radius: isEmojiOnly ? 0 : 10
                        color: isEmojiOnly ? "transparent" : (model.isOwn ? root.theme.msg_own : root.theme.msg_other)
                        border.color: root.theme.border
                        border.width: isEmojiOnly ? 0 : 1

                        ColumnLayout {
                            id: bubbleColumn
                            anchors.fill: parent
                            // No margin on image bubbles. The image sizes
                            // itself to the full bubble width, so an inset
                            // here would push it past the edge.
                            anchors.margins: (model.isFile && model.isImage) ? 0 : Kirigami.Units.smallSpacing
                            spacing: 4

                            Rectangle {
                                Layout.fillWidth: true
                                visible: model.replyToText && model.replyToText.length > 0
                                implicitHeight: replyLabel.implicitHeight + 8
                                color: Qt.rgba(1, 1, 1, 0.08)
                                radius: 4
                                border.color: root.theme.accent
                                border.width: 1
                                Label {
                                    id: replyLabel
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    text: model.replyToText
                                    elide: Text.ElideRight
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                                    color: root.theme.text_dim
                                }
                            }

                            Loader {
                                id: imageLoader
                                active: model.isFile && model.isImage
                                sourceComponent: Component {
                                    Item {
                                        // Full-bleed: width is capped, height
                                        // follows the image's real aspect
                                        // ratio once it reports sourceSize,
                                        // instead of an arbitrary *0.7 crop.
                                        readonly property real maxW: Math.min(260, messagesList.width * 0.65)
                                        width: maxW
                                        height: bubbleImage.sourceSize.height > 0
                                            ? maxW * (bubbleImage.sourceSize.height / bubbleImage.sourceSize.width)
                                            : maxW * 0.7

                                        Image {
                                            id: bubbleImage
                                            anchors.fill: parent
                                            source: "file://" + model.filePath
                                            fillMode: Image.PreserveAspectFit
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: function(mouse) {
                                                if (mouse.button === Qt.RightButton) {
                                                    root.openMessageMenu(index, model.text || "")
                                                } else {
                                                    imageViewer.show("file://" + model.filePath)
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                visible: !(model.isFile && model.isImage)
                                implicitWidth: model.isFile ? fileRow.implicitWidth + 20 : bubbleText.implicitWidth
                                implicitHeight: model.isFile ? fileRow.implicitHeight + 16 : bubbleText.implicitHeight + 8
                                radius: model.isFile ? 8 : 0
                                color: model.isFile ? root.theme.bg3 : "transparent"
                                border.color: model.isFile ? root.theme.border : "transparent"
                                border.width: model.isFile ? 1 : 0

                                RowLayout {
                                    id: fileRow
                                    anchors.centerIn: parent
                                    visible: model.isFile
                                    spacing: 8
                                    Kirigami.Icon {
                                        source: "document-open"
                                        width: 24
                                        height: 24
                                        color: root.theme.accent
                                    }
                                    Label {
                                        text: model.text
                                        color: root.theme.text
                                        elide: Text.ElideMiddle
                                        Layout.maximumWidth: 200
                                    }
                                }

                                Label {
                                    id: bubbleText
                                    anchors.fill: parent
                                    anchors.margins: model.isFile ? 0 : 4
                                    visible: !model.isFile
                                    text: model.text
                                    wrapMode: Text.WordWrap
                                    color: root.theme.text
                                    font.pixelSize: (isEmojiOnly ? 36 : Kirigami.Theme.defaultFont.pixelSize) * root.chatFontScale
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: model.isFile ? (Qt.LeftButton | Qt.RightButton) : Qt.RightButton
                                    cursorShape: model.isFile ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onClicked: function(mouse) {
                                        if (mouse.button === Qt.RightButton) {
                                            root.openMessageMenu(index, model.text || "")
                                        } else if (model.isFile) {
                                            Qt.openUrlExternally("file://" + model.filePath)
                                        }
                                    }
                                }
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: 4
                                // Captured here rather than inline below. Repeater declares
                                // its own "model", so an unqualified reference inside its
                                // model: binding resolves to itself and loops.
                                readonly property var reactionsList: (model && model.reactions) ? model.reactions : []
                                visible: reactionsList.length > 0
                                Repeater {
                                    model: parent.reactionsList
                                    delegate: Rectangle {
                                        radius: 10
                                        color: Qt.rgba(0, 0, 0, 0.25)
                                        implicitWidth: reactLabel.implicitWidth + 12
                                        implicitHeight: 22
                                        Label {
                                            id: reactLabel
                                            anchors.centerIn: parent
                                            text: modelData.emoji + " " + modelData.count
                                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                                            color: "white"
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: root.messagesModel.toggleReaction(index, modelData.emoji, "me")
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Row {
                        anchors.right: model.isOwn ? parent.right : undefined
                        anchors.left: model.isOwn ? undefined : parent.left
                        spacing: 3

                        Label {
                            visible: model.isEdited
                            text: i18n("edited")
                            font.italic: true
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            color: root.theme.text_dim
                        }

                        Text {
                            text: model.timeString || ""
                            color: root.theme.text_dim
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                        }

                        Text {
                            visible: model.isOwn
                            text: model.isRead ? "✓✓" : "✓"
                            color: model.isRead ? root.theme.accent : root.theme.text_dim
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            visible: root.replyToText.length > 0
            implicitHeight: 32
            color: root.theme.header_bg

            RowLayout {
                anchors.fill: parent
                anchors.margins: 4
                Label {
                    Layout.fillWidth: true
                    text: "↩ " + root.replyToText
                    elide: Text.ElideRight
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    color: root.theme.text
                }
                ToolButton {
                    icon.name: "dialog-close"
                    onClicked: root.replyToText = ""
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: root.theme.border
        }

        Rectangle {
            id: inputBar
            Layout.fillWidth: true
            color: root.theme.bg2
            implicitHeight: inputRow.implicitHeight + Kirigami.Units.smallSpacing * 2

            RowLayout {
                id: inputRow
                anchors.fill: parent
                anchors.margins: Kirigami.Units.smallSpacing

                ToolButton {
                    icon.name: "mail-attachment"
                    onClicked: fileDialog.open()
                }

                ToolButton {
                    icon.name: "face-smile"
                    onClicked: emojiPicker.open()
                }

                TextField {
                    id: inputField
                    Layout.fillWidth: true
                    placeholderText: i18n("Message...")
                    color: root.theme.text
                    placeholderTextColor: root.theme.text_dim
                    selectionColor: root.theme.accent
                    leftPadding: 10
                    rightPadding: 10
                    onAccepted: sendButton.clicked()

                    background: Rectangle {
                        radius: 8
                        color: root.theme.bg3
                        border.width: 1
                        border.color: inputField.activeFocus ? root.theme.accent : root.theme.border
                    }
                }

                Button {
                    id: sendButton
                    text: i18n("Send")
                    enabled: inputField.text.length > 0
                    onClicked: {
                        root.sendRequested(inputField.text)
                        inputField.text = ""
                        root.replyToText = ""
                    }

                    contentItem: Text {
                        text: sendButton.text
                        color: sendButton.enabled ? "white" : root.theme.text_dim
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 8
                        color: sendButton.enabled
                            ? (sendButton.down ? root.theme.accent2 : root.theme.accent)
                            : root.theme.bg3
                        border.width: sendButton.enabled ? 0 : 1
                        border.color: root.theme.border
                    }
                }
            }
        }
    }

    // Rich emoji picker (categorised, not exhaustive but broad)
    Popup {
        id: emojiPicker
        parent: root
        modal: true
        focus: true
        width: 340
        height: 320
        padding: 10
        x: (parent.width - width) / 2
        y: parent.height - height - inputBar.height - 4
        background: Rectangle { color: root.theme.bg2; radius: 12; border.color: root.theme.border; border.width: 1 }

        readonly property var emojiCategories: ({
            "😀": ["😀","😁","😂","🤣","😃","😄","😅","😆","😉","😊","😋","😎","😍","😘","🥰","😗","😙","😚","☺","🙂","🤗","🤩","🤔","🤨","😐","😑","😶","🙄","😏","😣","😥","😮","🤐","😯","😪","😫","🥱","😴","😌","😛","😜","😝","🤤","😒","😓","😔","😕","🙃","🤑","😲","☹","🙁","😖","😞","😟","😤","😢","😭","😦","😧","😨","😩","🤯","😬","😰","😱","🥵","🥶","😳","🤪","😵","🥴","😠","😡","🤬","😷","🤒","🤕","🤢","🤮","🤧","😇","🥳","🥺","🤠","🤡","🤥","🤫","🤭","🧐","🤓","😈","👿","👹","👺","💀","👻","👽","🤖","💩","😺","😸","😹","😻","😼","😽","🙀","😿","😾"],
            "👍": ["👍","👎","👏","🙌","👐","🤲","🤝","🙏","✍","💅","🤳","💪","🦾","🦿","🦵","🦶","👂","🦻","👃","🧠","🦷","🦴","👀","👁","👅","👄","💋","🩸","👶","🧒","👦","👧","🧑","👱","👨","🧔","👩","🧓","👴","👵","🙍","🙎","🙅","🙆","💁","🙋","🧏","🙇","🤦","🤷","👮","🕵","💂","🥷","👷","🤴","👸","👳","👲","🧕","🤵","👰","🤰","🤱","👼","🎅","🤶","🦸","🦹","🧙","🧚","🧛","🧜","🧝","🧞","🧟","💆","💇","🚶","🧍","🧎","🏃","💃","🕺","🕴","👯","🧖","🧗","🤺","🏇","⛷","🏂","🏌","🏄","🚣","🏊","⛹","🏋","🚴","🚵","🤸","🤼","🤽","🤾","🤹","🧘","🛀","🛌"],
            "🐶": ["🐶","🐱","🐭","🐹","🐰","🦊","🐻","🐼","🐨","🐯","🦁","🐮","🐷","🐽","🐸","🐵","🙈","🙉","🙊","🐒","🐔","🐧","🐦","🐤","🐣","🐥","🦆","🦅","🦉","🦇","🐺","🐗","🐴","🦄","🐝","🐛","🦋","🐌","🐞","🐜","🦟","🦗","🕷","🕸","🦂","🐢","🐍","🦎","🦖","🦕","🐙","🦑","🦐","🦞","🦀","🐡","🐠","🐟","🐬","🐳","🐋","🦈","🐊","🐅","🐆","🦓","🦍","🦧","🐘","🦛","🦏","🐪","🐫","🦒","🦘","🐃","🐂","🐄","🐎","🐖","🐏","🐑","🦙","🐐","🦌","🐕","🐩","🦮","🐕‍🦺","🐈","🐈‍⬛","🐓","🦃","🦚","🦜","🦢","🦩","🕊","🐇","🦝","🦨","🦡","🦦","🦥","🐁","🐀","🐿","🦔","🐾","🐉","🐲","🌵","🎄","🌲","🌳","🌴","🌱","🌿","☘","🍀","🎍","🎋","🍃","🍂","🍁","🍄","🌾","💐","🌷","🌹","🥀","🌺","🌸","🌼","🌻","🌞","🌝","🌛","🌜","🌚","🌕","🌖","🌗","🌘","🌑","🌒","🌓","🌔","🌙","🌎","🌍","🌏","🪐","💫","⭐","🌟","✨","⚡","🔥","💥","☄","☀","🌤","⛅","🌥","☁","🌦","🌧","⛈","🌩","🌨","❄","☃","⛄","🌬","💨","🌪","🌫","🌈","☂","☔","💧","💦","🌊"],
            "🍎": ["🍏","🍎","🍐","🍊","🍋","🍌","🍉","🍇","🍓","🫐","🍈","🍒","🍑","🥭","🍍","🥥","🥝","🍅","🍆","🥑","🥦","🥬","🥒","🌶","🫑","🌽","🥕","🫒","🧄","🧅","🥔","🍠","🥐","🥯","🍞","🥖","🥨","🧀","🥚","🍳","🧈","🥞","🧇","🥓","🥩","🍗","🍖","🦴","🌭","🍔","🍟","🍕","🫓","🥪","🥙","🧆","🌮","🌯","🫔","🥗","🥘","🫕","🥫","🍝","🍜","🍲","🍛","🍣","🍱","🥟","🦪","🍤","🍙","🍚","🍘","🍥","🥠","🥮","🍢","🍡","🍧","🍨","🍦","🥧","🧁","🍰","🎂","🍮","🍭","🍬","🍫","🍿","🍩","🍪","🌰","🥜","🍯","🥛","🍼","🫖","☕","🍵","🧃","🥤","🧋","🍶","🍺","🍻","🥂","🍷","🥃","🍸","🍹","🧉","🍾","🧊","🥄","🍴","🍽","🥣","🥡","🥢","🧂"],
            "⚽": ["⚽","🏀","🏈","⚾","🥎","🎾","🏐","🏉","🥏","🎱","🪀","🏓","🏸","🏒","🏑","🥍","🏏","🥅","⛳","🪁","🏹","🎣","🤿","🥊","🥋","🎽","🛹","🛷","⛸","🥌","🎿","⛷","🏂","🪂","🏋","🤼","🤸","⛹","🤺","🤾","🏌","🏇","⛷","🏂","🏄","🏊","🤽","🚣","🧗","🚴","🚵","🏎","🏍","🤹","🎖","🏆","🏅","🥇","🥈","🥉","🎗","🏵","🎫","🎟","🎪","🤹","🎭","🩰","🎨","🎬","🎤","🎧","🎼","🎹","🥁","🎷","🎺","🎸","🪕","🎻","🎲","♟","🎯","🎳","🎮","🎰","🧩"],
            "❤": ["❤","🧡","💛","💚","💙","💜","🖤","🤍","🤎","💔","❣","💕","💞","💓","💗","💖","💘","💝","💟","☮","✝","☪","🕉","☸","✡","🔯","🕎","☯","☦","🛐","⛎","♈","♉","♊","♋","♌","♍","♎","♏","♐","♑","♒","♓","🆔","⚛","🉑","☢","☣","📴","📳","🈶","🈚","🈸","🈺","🈷","✴","🆚","💮","🉐","㊙","㊗","🈴","🈵","🈹","🈲","🅰","🅱","🆎","🆑","🅾","🆘","❌","⭕","🛑","⛔","📛","🚫","💯","💢","♨","🚷","🚯","🚳","🚱","🔞","📵","🚭","❗","❕","❓","❔","‼","⁉","🔅","🔆","〽","⚠","🚸","🔱","⚜","🔰","♻","✅","🈯","💹","❇","✳","❎","🌐","💠","Ⓜ","🌀","💤","🏧","🚾","♿","🅿","🈳","🈂","🛂","🛃","🛄","🛅","🛗","🧭","🧱","🧳","⌚","⏰","⏱","⏲","🕰","🕛","🕧","🕐","🕜","🕑","🕝","🕒","🕞","🕓","🕟","🕔","🕠","🕕","🕡","🕖","🕢","🕗","🕣","🕘","🕤","🕙","🕥","🕚","🕦","🌑","🌒","🌓","🌔","🌕","🌖","🌗","🌘","🌙","🌚","🌛","🌜","🌡","☀","🌝","🌞","🪐","⭐","🌟","🌠","🌌","☁","⛅","⛈","🌤","🌥","🌦","🌧","🌨","❄","🌬","💨","🌪","🌫","🌈","☂","☔","⚡","❄","☃","⛄","☄","🔥","💧","🌊"],
        })

        ColumnLayout {
            anchors.fill: parent
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: Object.keys(emojiPicker.emojiCategories)
                    delegate: ToolButton {
                        text: modelData
                        onClicked: emojiGrid.model = emojiPicker.emojiCategories[modelData]
                    }
                }
            }

            GridView {
                id: emojiGrid
                Layout.fillWidth: true
                Layout.preferredHeight: 240
                cellWidth: 40
                cellHeight: 40
                model: emojiPicker.emojiCategories["😀"]
                clip: true

                delegate: Rectangle {
                    width: 36
                    height: 36
                    radius: 8
                    color: pickMouse.containsMouse ? root.theme.btn_hover : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        font.pixelSize: 20
                    }
                    MouseArea {
                        id: pickMouse
                        anchors.fill: parent
                        hoverEnabled: true
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

    FileDialog {
        id: fileDialog
        title: i18n("Attach a file")
        onAccepted: {
            const path = selectedFile.toString().replace("file://", "")
            console.log("FileDialog selected:", path)
            root.attachRequested(path)
        }
    }
}
