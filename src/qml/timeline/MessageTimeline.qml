// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import koutnet.app

// Bottom to top, over a model turned round - see core/chat/ReversedChatModel.h
// for why the reversal is a proxy and not a change to ChatModel. Three attempts
// to keep the list the natural way up and call positionViewAtEnd() failed alike:
// rows above the fold are never built, so QQuickListView guesses where they are
// from an average row height, and one-word replies mixed with pictures have no
// average worth having. That guess is what originY and contentHeight are made
// of, it is corrected as each real row appears, and what is anchored to it moves
// too - the flick bounds, and the scrollbar's own idea of where it is (the drag
// that scrolled to a blank page). Bottom to top the guess sits off the top where
// nothing looks at it; the end the view rests against is the newest message,
// built and measured, so the reader's position is exact. New messages insert at
// row 0, the end that does not move.
Item {
    id: root

    property var messagesModel: null
    property real fontScale: 1.0
    property string selfName: ""
    property string peerName: ""
    property string selfDisplayName: ""
    property string selfAvatarSource: ""
    property bool canEditMessages: true
    // The chat the timeline shows, threaded to each row so a poll vote can name
    // its room. Empty for the saved-messages chat, which has no polls.
    property string chatId: ""

    // Rows crossing this boundary go through toSourceRow()/fromSourceRow(); every
    // signal this item raises carries a ChatModel row, so nothing above it has to
    // know the list is upside down.
    ReversedChatModel {
        id: reversed
        sourceModel: root.messagesModel
    }

    // originY and contentHeight are each estimated, but their sum is the position
    // of row 0 and row 0 is a real built item, so the two errors cancel and the
    // distance comes out exact.
    //
    // Written out rather than taken from atYEnd, which compares floats: contentY
    // settles on -643.2 against a height of 643 and the view never admits to
    // being at the end, which leaves the jump button up for good and stops every
    // read receipt this window would have sent.
    readonly property real tailDistance: messagesList.originY + messagesList.contentHeight
        + messagesList.bottomMargin - messagesList.height - messagesList.contentY

    // The window reads this to decide whether an arriving message counts as read:
    // a receipt for a message that scrolled past above the fold is a lie, so a
    // pixel of slack and not a screenful.
    readonly property bool atBottom: Math.round(root.tailDistance) <= 1

    // Whether the view was following the newest message when the row arriving now
    // was handed over. Sampled before the insert: by the time count has changed
    // the new row is already in contentHeight, atBottom reads false, and a test
    // made there would drop out of a conversation the reader never left.
    property bool followTail: true

    onMessagesModelChanged: root.followTail = true

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
    signal pinRequested(int row, string msgId)
    signal editRequested(int row, string body)
    signal reactRequested(int row)
    signal menuRequested(int row, string author, string body, string msgId)
    signal reactionToggled(int row, string emoji)
    // A URL rather than a path: the attachment may never have been on this disk.
    signal imageActivated(string source)
    signal fileActivated(string source)
    signal readReached()
    // the anchor item belongs to a delegate, so nothing may hold on to it.
    signal avatarActivated(bool own, Item anchorItem)

    // A delivery mark sits at the trailing edge of its message and the scrollbar
    // at the trailing edge of the view, so the collision is horizontal. wip19
    // answered it on the other axis by shortening the list from the bottom, which
    // moved no mark and left a dead band across the foot of the conversation.
    // Unconditional rather than held only while the scrollbar is up: the
    // shortened version decided on contentHeight, which is re-derived on every
    // resize, and QML reported the binding loop.
    readonly property real scrollBarRoom: Math.max(Kirigami.Units.smallSpacing,
                                                   verticalScrollBar.implicitWidth)

    // A cap of 46 grid units here, on the reading-length argument, read as the
    // conversation floating in the middle of the screen once the column filled the
    // window; the column is the measure now, less the scrollbar's own track.
    readonly property real messageWidth: messagesList.width
        - Kirigami.Units.largeSpacing * 2 - root.scrollBarRoom

    // Row 0 is the newest, so the way back to it is the beginning of the view.
    function scrollToEnd() {
        messagesList.positionViewAtBeginning()
    }

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
        // By index and not by contentY: an index is the one way of asking for a
        // position that does not go through the estimate.
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

    // Held here rather than on the delegate because a delegate that scrolls out of
    // view is recycled and would forget. By id and not by row: reversed, every row
    // number shifts the moment a message arrives and the flash would walk down to
    // the message underneath.
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
            // A conversation shorter than the window sits against the bottom with
            // empty space over it, and the middle of the view is in that space.
            if (messagesList.contentHeight <= messagesList.height)
                return
            if (messagesList.indexAt(messagesList.width / 2,
                                     messagesList.contentY + messagesList.height / 2) >= 0)
                return
            // Flickable's own clamp, because it asks the view where the ends are.
            messagesList.returnToBounds()
        }
    }

    ListView {
        id: messagesList

        anchors.fill: parent
        clip: true
        model: reversed
        verticalLayoutDirection: ListView.BottomToTop
        spacing: 0
        topMargin: Kirigami.Units.smallSpacing
        bottomMargin: Kirigami.Units.smallSpacing
        // On, and it is the fix for the stall rather than a saving. A destroyed
        // delegate is rebuilt from nothing, and it enters the view short - the
        // Loaders are inactive and an Image has no sourceSize yet - then grows once
        // it has measured itself. Every one of those rebuilds moves the average row
        // height QQuickListView extrapolates the rows it has never built from, and
        // the whole coordinate system slides with it: measured at 1200px of drift in
        // originY + contentHeight over one scroll up and back, against 3px with
        // reuse on. Sliding away from the reader is what the stall was - scrolling
        // down moved the newest message down by nearly as much as it moved the view.
        // Recycled delegates keep the heights they already resolved, so the estimate
        // holds still. See onReused in MessageDelegate for the state that has to be
        // put back by hand.
        reuseItems: true

        QQC2.ScrollBar.vertical: QQC2.ScrollBar {
            id: verticalScrollBar
        }

        boundsBehavior: Flickable.StopAtBounds

        // Delegates are expensive to build - rich text, reactions, an attachment -
        // so a screenful either side is kept alive; without it every wheel notch
        // pays for a fresh toRichText() and text layout on each row coming in.
        cacheBuffer: Math.round(messagesList.height * 2)

        // The wheel taken off the Flickable and given to Kirigami's handler, which
        // clamps against originY and both margins and whose blockTargetWheel keeps
        // the flick path out of it, so there is no momentum left to overshoot with.
        Kirigami.WheelHandler {
            target: messagesList

            // Accepting the event is what stops the handler from scrolling on
            // the same notch.
            onWheel: (wheel) => {
                if (!(wheel.modifiers & Qt.ControlModifier))
                    return
                root.fontScale = Math.max(0.7, Math.min(2.0,
                    root.fontScale + (wheel.angleDelta.y > 0 ? 0.05 : -0.05)))
                wheel.accepted = true
            }
        }

        // Following the conversation only while the reader is already at the end of
        // it, rather than yanking them out of the message they are reading.
        // Deferred by a frame so the inserted row has been built and measured
        // before the view is asked to sit against it.
        onCountChanged: if (root.followTail)
            Qt.callLater(messagesList.positionViewAtBeginning)

        // Nothing on completion. A bottom-to-top view already opens on row 0, and
        // the positionViewAtEnd() that used to be here was the largest source of
        // the drift it was meant to hide: it releases every built row and
        // re-derives the origin from whatever is visible afterwards.

        // A scrollbar drag writes contentY straight from originY and contentHeight,
        // still guesses about rows never built, and a long one can leave the viewport
        // where the list has no items at all - a blank page with no way back but the
        // jump button. Only once the view has stopped, because an empty middle
        // during a refill is ordinary.
        onContentYChanged: emptyViewGuard.restart()

        delegate: MessageDelegate {
            id: messageRow

            contentWidth: root.messageWidth
            fontScale: root.fontScale
            selfName: root.selfName
            peerName: root.peerName
            selfDisplayName: root.selfDisplayName
            selfAvatarSource: root.selfAvatarSource
            canEditMessages: root.canEditMessages
            chatId: root.chatId
            flashing: flashTarget.msgId.length > 0 && flashTarget.msgId === messageRow.msgId

            onReplyRequested: (row, author, excerpt, msgId) =>
                root.replyRequested(reversed.toSourceRow(row), author, excerpt, msgId)
            onPinRequested: (row, msgId) => root.pinRequested(reversed.toSourceRow(row), msgId)
            onEditRequested: (row, body) => root.editRequested(reversed.toSourceRow(row), body)
            onReactRequested: (row) => root.reactRequested(reversed.toSourceRow(row))
            onMenuRequested: (row, author, body, msgId) =>
                root.menuRequested(reversed.toSourceRow(row), author, body, msgId)
            onReactionToggled: (row, emoji) => root.reactionToggled(reversed.toSourceRow(row), emoji)
            onJumpRequested: (msgId) => root.jumpToMessage(msgId)
            onImageActivated: (source) => root.imageActivated(source)
            onFileActivated: (source) => root.fileActivated(source)
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

    // Top corner, because it sends the view upwards and the other button down.
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

    // Only up while there is somewhere to go back from: an always-there button is
    // one nobody reads, and it keeps its corner clear of the newest message's
    // delivery mark, since it shows exactly when that message is off screen. No
    // room is reserved for it; reserving some is what wip19 did.
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
