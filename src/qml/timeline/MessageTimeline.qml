// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components

// The conversation itself: the list, the two ways back to where you were, and
// the line that says the other side is writing.
//
// Top to bottom rather than a bottom-to-top view with an inverted model. The
// model is a plain append-only list, and it would have to be turned inside out
// to buy a first paint that is already at the newest message;
// positionViewAtEnd() buys the same thing for one call.
Item {
    id: root

    property var messagesModel: null
    property real fontScale: 1.0
    property string selfName: ""
    property string peerName: ""
    property string selfDisplayName: ""
    property string selfAvatarSource: ""

    // How far the view still has to travel before the newest message is on
    // screen. Negative once the whole conversation fits.
    //
    // Written out rather than taken from atYEnd, which compares floats: contentY
    // settles on -643.2 against a height of 643 and the view never admits to
    // being at the end. NeoChat carries a hand-rolled closeToYEnd for the same
    // reason. A view stuck at "not quite there" leaves the jump button up for
    // good and stops every read receipt this window would have sent.
    readonly property real tailDistance: messagesList.originY + messagesList.contentHeight
        + messagesList.bottomMargin - messagesList.height - messagesList.contentY

    // True while the newest message is on screen. The window reads this to
    // decide whether an arriving message counts as read: a read receipt for a
    // message that scrolled past somewhere above the fold is a lie, and it is
    // the one lie a messenger cannot take back. Hence a pixel of slack and not
    // a screenful.
    readonly property bool atBottom: Math.round(root.tailDistance) <= 1

    // Whether the view was following the newest message at the moment the row
    // that is arriving now was handed over.
    //
    // Sampled before the insert on purpose. By the time count has changed the
    // new row is already counted into contentHeight, atBottom reads false, and
    // a test made there would drop out of following a conversation the reader
    // never actually left.
    property bool followTail: true

    onMessagesModelChanged: root.followTail = true

    // Getting back to the bottom is what says the backlog has been seen.
    onAtBottomChanged: if (root.atBottom)
        root.readReached()

    Connections {
        target: root.messagesModel

        function onRowsAboutToBeInserted() {
            root.followTail = root.atBottom
        }
    }

    readonly property int unreadCount: root.messagesModel ? root.messagesModel.unreadCount : 0

    signal replyRequested(int row, string author, string excerpt, string msgId)
    signal editRequested(int row, string body)
    signal reactRequested(int row)
    signal menuRequested(int row, string author, string body, string msgId)
    signal reactionToggled(int row, string emoji)
    signal imageActivated(string path)
    signal fileActivated(string path)
    signal readReached()
    // own says which profile is being asked for; the item is what the card gets
    // hung off, and it belongs to a delegate, so nothing may hold on to it.
    signal avatarActivated(bool own, Item anchorItem)

    // How wide a message is. There used to be a cap of 46 grid units here, on
    // the reading-length argument, but with the column finally filling the
    // window it read as the conversation floating in the middle of the screen
    // with a margin either side rather than as a comfortable measure. The
    // column width is the measure now.
    readonly property real messageWidth: messagesList.width - Kirigami.Units.largeSpacing * 2

    function scrollToEnd() {
        messagesList.positionViewAtEnd()
    }

    function jumpToRow(row) {
        if (row < 0 || !root.messagesModel)
            return
        messagesList.positionViewAtIndex(row, ListView.Center)
        flashTarget.row = row
        flashTimer.restart()
    }

    function jumpToMessage(msgId) {
        if (!root.messagesModel)
            return
        root.jumpToRow(root.messagesModel.rowForMsgId(msgId))
    }

    function jumpToFirstUnread() {
        if (!root.messagesModel)
            return
        root.jumpToRow(root.messagesModel.firstUnreadRow())
    }

    // Which row the last jump landed on, and for how long. Held here rather
    // than on the delegate because a delegate that scrolls out of view is
    // recycled and would forget.
    QtObject {
        id: flashTarget
        property int row: -1
    }

    Timer {
        id: flashTimer
        interval: Kirigami.Units.humanMoment
        onTriggered: flashTarget.row = -1
    }

    ListView {
        id: messagesList

        anchors.fill: parent
        clip: true
        model: root.messagesModel
        spacing: 0
        topMargin: Kirigami.Units.smallSpacing
        bottomMargin: Kirigami.Units.smallSpacing
        // Delegates carry a hover strip and per-row state; recycling them makes
        // that state land on the wrong message.
        reuseItems: false

        QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

        boundsBehavior: Flickable.StopAtBounds

        // Delegates are expensive to build - rich text, reactions, an
        // attachment - so a screenful either side is kept alive rather than
        // destroyed at the edge and rebuilt on the way back. Without it every
        // wheel notch pays for a fresh TextHandler.toRichText() and a fresh
        // text layout on each row that comes into view.
        cacheBuffer: Math.round(messagesList.height * 2)

        // The wheel, taken off the Flickable and given to Kirigami's handler -
        // which is the same one qqc2-desktop-style hangs on every other
        // scrollable thing in the application, so the timeline now scrolls the
        // way the rest of the desktop does.
        //
        // Two attempts at this were spent on the wrong half of the problem. The
        // first drove the view by hand and clamped contentY against
        // Math.max(0, contentHeight - height): contentY is measured from
        // originY, the view has two margins the sum did not count, and
        // contentHeight is only an estimate from the average row height while
        // the rows above have never been built - which for a list holding both
        // one-word replies and images is nowhere near the real total. The second
        // deleted that clamp and handed the wheel back to the Flickable, which
        // turns a notch into a flick: momentum then carries contentY past the
        // end of the same estimate, the view has no items where it has landed,
        // and the reader is looking at nothing until the jump button calls
        // positionViewAtEnd() and recomputes the position from an index.
        //
        // WheelHandler::scrollFlickable does the clamp against originY, both
        // margins and contentHeight; blockTargetWheel keeps the Flickable's
        // flick path out of it entirely, so there is no momentum left to
        // overshoot with; and it rounds to device pixels. Dragging still flicks,
        // which is what a touchscreen wants.
        Kirigami.WheelHandler {
            target: messagesList

            // Ctrl+wheel zooms the chat text rather than scrolling, which is the
            // browser and editor convention. Accepting the event is what stops
            // the handler from scrolling on the same notch.
            onWheel: (wheel) => {
                if (!(wheel.modifiers & Qt.ControlModifier))
                    return
                root.fontScale = Math.max(0.7, Math.min(2.0,
                    root.fontScale + (wheel.angleDelta.y > 0 ? 0.05 : -0.05)))
                wheel.accepted = true
            }
        }

        // Following the conversation only while the reader is already at the
        // end of it. Yanking somebody down out of the message they are reading
        // because a new one arrived is the behaviour this replaces.
        onCountChanged: if (root.followTail)
            Qt.callLater(messagesList.positionViewAtEnd)
        Component.onCompleted: messagesList.positionViewAtEnd()

        delegate: MessageDelegate {
            id: messageRow

            contentWidth: root.messageWidth
            fontScale: root.fontScale
            selfName: root.selfName
            peerName: root.peerName
            selfDisplayName: root.selfDisplayName
            selfAvatarSource: root.selfAvatarSource
            flashing: flashTarget.row === messageRow.index

            onReplyRequested: (row, author, excerpt, msgId) => root.replyRequested(row, author, excerpt, msgId)
            onEditRequested: (row, body) => root.editRequested(row, body)
            onReactRequested: (row) => root.reactRequested(row)
            onMenuRequested: (row, author, body, msgId) => root.menuRequested(row, author, body, msgId)
            onReactionToggled: (row, emoji) => root.reactionToggled(row, emoji)
            onJumpRequested: (msgId) => root.jumpToMessage(msgId)
            onImageActivated: (path) => root.imageActivated(path)
            onFileActivated: (path) => root.fileActivated(path)
            onAvatarClicked: (own, anchorItem) => root.avatarActivated(own, anchorItem)
        }
    }

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        visible: messagesList.count === 0
        icon.name: "dialog-messages"
        text: i18nc("@info an open conversation with no messages in it", "Nothing here yet")
        explanation: i18nc("@info", "Write something below, or drop a file on this window to send it.")
    }

    // Back to the oldest message that has not been read. Top corner, because it
    // sends the view upwards and the other button sends it down.
    Components.FloatingButton {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Kirigami.Units.largeSpacing
        z: 2

        visible: root.unreadCount > 0 && !root.atBottom
        icon.name: "mail-unread-symbolic"
        text: i18ncp("@action:button go to the oldest message not yet read, %1 is a number",
                     "Jump to %1 unread message", "Jump to %1 unread messages", root.unreadCount)

        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC2.ToolTip.text: text

        onClicked: root.jumpToFirstUnread()
    }

    // Back to the newest message. Only while there is somewhere to go back
    // from: a button that is always there is a button nobody reads.
    Components.FloatingButton {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Kirigami.Units.largeSpacing
        z: 2

        visible: !root.atBottom
        icon.name: "go-bottom"
        text: i18nc("@action:button scroll the conversation back to the newest message", "Jump to the latest message")

        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC2.ToolTip.text: text

        onClicked: {
            root.scrollToEnd()
            root.readReached()
        }
    }
}
