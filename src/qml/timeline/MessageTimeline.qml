// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import koutnet.app

// The conversation itself: the list, the two ways back to where you were, and
// the line that says the other side is writing.
//
// Bottom to top, over a model turned round - see core/chat/ReversedChatModel.h
// for why the reversal is a proxy and not a change to ChatModel. Three attempts
// were made to keep this list the natural way up and put the view at the end of
// it with positionViewAtEnd(), and all three failed the same way: the rows above
// the fold have never been built, so QQuickListView works out where they are
// from an average row height, and a conversation of one-word replies and
// pictures has no average worth having. That estimate is what originY and
// contentHeight are made of, it is corrected every time a real row comes into
// existence, and everything anchored to it moves when it is - the flick bounds
// (the tug, and the yank on a fast scroll) and the scrollbar's own idea of where
// it is (the drag that scrolled to a blank page).
//
// Laid out bottom to top the guess sits at the far end instead. The end the view
// rests against is the newest message, which is built and measured, so the
// position the reader is at is exact and stays exact; the error accumulates off
// the top where nothing is looking at it. A new message is an insert at row 0,
// which is the end that does not move.
Item {
    id: root

    property var messagesModel: null
    property real fontScale: 1.0
    property string selfName: ""
    property string peerName: ""
    property string selfDisplayName: ""
    property string selfAvatarSource: ""

    // Row 0 is the newest message. Rows crossing this boundary in either
    // direction go through toSourceRow()/fromSourceRow(); every signal this item
    // raises carries a ChatModel row, so nothing above it has to know the list
    // is upside down.
    ReversedChatModel {
        id: reversed
        sourceModel: root.messagesModel
    }

    // How far the view still has to travel before the newest message is on
    // screen. Negative once the whole conversation fits.
    //
    // The same arithmetic as before the reversal, and deliberately so: the
    // newest message is still at the far end of contentY, it is only a different
    // row that lives there now. What did change is that this is no longer a
    // guess. originY and contentHeight are each estimated, but their sum is the
    // position of row 0 and row 0 is a real built item, so the two errors cancel
    // and the distance comes out exact.
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

    // Every row in these is a ChatModel row and not a view row.
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

    // The strip along the bottom the jump-to-bottom button is allowed to float
    // in. It sits in the corner an outgoing message draws its delivery mark in,
    // and it was sitting on top of the mark; this is the list shortened rather
    // than the button moved, because the corner is where a jump button belongs.
    //
    // The list's own bottomMargin would have been the obvious place for it, and
    // it is the wrong one: atBottom is measured against that margin, so growing
    // it moves the end of the list away from a reader who is already standing on
    // it and the view can never admit to being at the bottom again. Shortening
    // the list moves the end with it and the arithmetic comes out the same.
    //
    // Held whenever the conversation is long enough to scroll rather than
    // whenever the button is up - the button is up exactly when the view is not
    // at the end, and reserving on that would step the content by the gap every
    // time the reader reached it. Measured against this item's height and not
    // the list's, so the room made here cannot decide whether room is needed.
    readonly property real jumpRoom: messagesList.contentHeight > root.height
        ? jumpButton.height + Kirigami.Units.largeSpacing * 2
        : 0

    // How wide a message is. There used to be a cap of 46 grid units here, on
    // the reading-length argument, but with the column finally filling the
    // window it read as the conversation floating in the middle of the screen
    // with a margin either side rather than as a comfortable measure. The
    // column width is the measure now.
    readonly property real messageWidth: messagesList.width - Kirigami.Units.largeSpacing * 2

    // Row 0 is the newest, so the way back to it is the beginning of the view.
    function scrollToEnd() {
        messagesList.positionViewAtBeginning()
    }

    // Takes a ChatModel row.
    function jumpToRow(row) {
        if (row < 0 || !root.messagesModel)
            return
        const idx = root.messagesModel.index(row, 0)
        root.jumpTo(row, root.messagesModel.data(idx, ChatModel.MsgIdRole) || "")
    }

    function jumpTo(sourceRow, msgId) {
        const target = reversed.fromSourceRow(sourceRow)
        if (target < 0)
            return
        // By index and not by contentY. An index is the one way of asking for a
        // position that does not go through the estimate: the view builds the
        // row and then places itself against the real thing.
        messagesList.positionViewAtIndex(target, ListView.Center)
        flashTarget.msgId = msgId
        flashTimer.restart()
    }

    function jumpToMessage(msgId) {
        if (!root.messagesModel)
            return
        const row = root.messagesModel.rowForMsgId(msgId)
        if (row < 0)
            return
        root.jumpTo(row, msgId)
    }

    function jumpToFirstUnread() {
        if (!root.messagesModel)
            return
        root.jumpToRow(root.messagesModel.firstUnreadRow())
    }

    // Which message the last jump landed on, and for how long. Held here rather
    // than on the delegate because a delegate that scrolls out of view is
    // recycled and would forget.
    //
    // By id and not by row: reversed, every row number shifts by one the moment
    // a message arrives, and the flash would walk down to the message underneath.
    QtObject {
        id: flashTarget
        property string msgId: ""
    }

    Timer {
        id: flashTimer
        interval: Kirigami.Units.humanMoment
        onTriggered: flashTarget.msgId = ""
    }

    Timer {
        id: emptyViewGuard
        interval: 50
        onTriggered: {
            if (messagesList.count === 0)
                return
            // A conversation shorter than the window sits against the bottom
            // with empty space over it, and the middle of the view is in that
            // space. There is no estimate to get wrong here and nothing to fix.
            if (messagesList.contentHeight <= messagesList.height)
                return
            if (messagesList.indexAt(messagesList.width / 2,
                                     messagesList.contentY + messagesList.height / 2) >= 0)
                return
            // Flickable's own clamp, because it asks the view where the ends
            // are - which bottom to top is not where the arithmetic in this
            // file would put them.
            messagesList.returnToBounds()
        }
    }

    ListView {
        id: messagesList

        anchors.fill: parent
        // Held off the bottom edge so the jump button is beside the newest
        // message rather than on top of it - see the note on jumpRoom.
        anchors.bottomMargin: root.jumpRoom
        clip: true
        model: reversed
        // Row 0 at the bottom. See the note at the top of this file.
        verticalLayoutDirection: ListView.BottomToTop
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
        // scrollable thing in the application, so the timeline scrolls the way
        // the rest of the desktop does. scrollFlickable() clamps against
        // originY, both margins and contentHeight; blockTargetWheel keeps the
        // Flickable's flick path out of it, so there is no momentum left to
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

        // Following the conversation only while the reader is already at the end
        // of it. Yanking somebody down out of the message they are reading
        // because a new one arrived is the behaviour this replaces.
        //
        // Deferred by a frame so the row that has just been inserted has been
        // built and measured before the view is asked to sit against it.
        onCountChanged: if (root.followTail)
            Qt.callLater(messagesList.positionViewAtBeginning)

        // Nothing on completion. A bottom-to-top view already opens on row 0,
        // and the positionViewAtEnd() that used to be here was the largest
        // single source of the drift it was meant to hide: it releases every
        // built row and re-derives the origin from whatever is visible
        // afterwards - once at startup, and again on every message that arrived.

        // The net under the scrollbar.
        //
        // A drag on the scrollbar writes contentY straight from originY and
        // contentHeight, and those are still guesses about rows the reader has
        // never scrolled far enough to build. A long drag can put the viewport
        // where the list has no items at all, and there is no way back from a
        // blank page but the jump button. Bottom to top makes that rare rather
        // than impossible: the guess stops being corrected under the reader, but
        // it is still a guess.
        //
        // Tested by asking whether anything is under the middle of the view -
        // the complaint itself rather than a theory about it - and only once the
        // view has stopped, because a frame with nothing on it in the middle of
        // a refill is ordinary and must not be pounced on.
        onContentYChanged: emptyViewGuard.restart()

        delegate: MessageDelegate {
            id: messageRow

            contentWidth: root.messageWidth
            fontScale: root.fontScale
            selfName: root.selfName
            peerName: root.peerName
            selfDisplayName: root.selfDisplayName
            selfAvatarSource: root.selfAvatarSource
            flashing: flashTarget.msgId.length > 0 && flashTarget.msgId === messageRow.msgId

            // index on a delegate is a view row. Everything past this point
            // speaks ChatModel rows, so this is where the two are told apart.
            onReplyRequested: (row, author, excerpt, msgId) =>
                root.replyRequested(reversed.toSourceRow(row), author, excerpt, msgId)
            onEditRequested: (row, body) => root.editRequested(reversed.toSourceRow(row), body)
            onReactRequested: (row) => root.reactRequested(reversed.toSourceRow(row))
            onMenuRequested: (row, author, body, msgId) =>
                root.menuRequested(reversed.toSourceRow(row), author, body, msgId)
            onReactionToggled: (row, emoji) => root.reactionToggled(reversed.toSourceRow(row), emoji)
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
        id: jumpButton

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
