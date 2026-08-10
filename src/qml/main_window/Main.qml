// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// Up to three columns of a Kirigami.PageRow. PageRow folds to one column on a
// narrow window and hands out its own back button, which is what the animated
// Layout.preferredWidth and hand-drawn hamburger that used to be here reached for.
//
// The third column is pushed and popped rather than always present: a panel that
// cannot be put away is a panel in the way, and pushing it gets the folding free.
//
// Everything that is not a conversation goes on pageStack.layers, so it covers
// every column and comes back with the back button rather than as a modal sheet.
import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import koutnet.app

Kirigami.ApplicationWindow {
    id: root

    title: i18nc("@title:window", "KOutNet")
    width: Kirigami.Units.gridUnit * 55
    height: Kirigami.Units.gridUnit * 36
    // Narrow enough that the page row folds to a single column.
    minimumWidth: Kirigami.Units.gridUnit * 20
    minimumHeight: Kirigami.Units.gridUnit * 18

    // The one thing this application says about colour. It has to be repeated
    // rather than set once here: Kirigami resolves a theme by walking up the
    // parent chain, and anything that sets Kirigami.Theme.inherit false starts a
    // chain of its own. Each of those points back to this comment.
    Kirigami.Theme.inherit: false
    Kirigami.Theme.highlightColor: Brand.accent

    // A mode the user picks and keeps, as opposed to what the page row already
    // does by itself when the window is dragged narrow. Same layout tree, but not
    // the same layout - see ChatListPage, ContactDelegate and AccountRow, which
    // each hold their own half of it.
    readonly property bool compact: appSettings.compactMode

    // One conversation and no furniture. A narrower copy of the three-column
    // desktop window is still a desktop window; what somebody who wants the
    // messenger out of the way needs on screen is the messages and a way to answer
    // them, so compact mode shows the conversation alone and leaves the list a back
    // button away. PageRow.wideMode is readonly and derived from this, so the fold
    // is asked for by making one column as wide as the window rather than set.
    readonly property real kRoomyColumnWidth: root.compact
        ? root.width
        : Math.min(Math.max(Kirigami.Units.gridUnit * 17, Math.round(root.width * 0.2)),
                   Kirigami.Units.gridUnit * 26)

    readonly property int kCompactWidth: Kirigami.Units.gridUnit * 22
    readonly property int kCompactHeight: Kirigami.Units.gridUnit * 28

    function toggleCompact() {
        if (!root.compact) {
            // Persisted rather than kept in memory: leaving compact mode after a
            // restart used to come back to whatever the default width was.
            appSettings.roomyWidth = root.width
            appSettings.roomyHeight = root.height
            appSettings.compactMode = true
            // Compact mode has no third column.
            while (root.pageStack.depth > 2)
                root.pageStack.pop()
            root.applyCompactSize()
            return
        }
        appSettings.compactMode = false
        if (appSettings.roomyWidth > 0)
            root.width = appSettings.roomyWidth
        if (appSettings.roomyHeight > 0)
            root.height = appSettings.roomyHeight
    }

    function applyCompactSize() {
        root.width = root.kCompactWidth
        root.height = root.kCompactHeight
    }

    property string currentPeerIp: ""
    readonly property string kSelfChatId: "__self__"
    property bool micMuted: false
    // Deafen implies mute, but the engine is told about both separately, because
    // un-deafening has to put the microphone back the way the user left it.
    property bool deafened: false

    // The only thing in the interface that knows there is more than one
    // transport. Everything downstream of it - the models, the timeline, the
    // conversation list, the history on disk - is handed the same chat id
    // whichever side it came from. core/chat/ChatAddress.h owns the prefix.
    function isMatrixChat(chatId) {
        return chatId.startsWith("mx:")
    }

    // What the open conversation is, when it is a room: MatrixRoomBridge's map,
    // or null for a LAN chat. Pulled rather than pushed - see the note at the
    // top of MatrixRoomBridge.h - so it has to be refreshed by hand whenever the
    // conversation changes or the room does.
    property var currentRoomInfo: null
    readonly property bool currentIsRoom: root.currentRoomInfo !== null

    function refreshRoomInfo() {
        if (!root.isMatrixChat(root.currentPeerIp)) {
            root.currentRoomInfo = null
            return
        }
        const info = matrixRooms.roomInfo(root.currentPeerIp)
        // An empty map means the session has not synced this room yet. Kept as
        // a room all the same: the header must not fall back to peer furniture
        // for the second between opening the row and the state arriving.
        root.currentRoomInfo = info && info.roomId ? info : {}
    }

    function toggleMic() {
        root.micMuted = !root.micMuted
        voiceCallManager.setMute(root.micMuted)
    }

    function toggleDeafen() {
        root.deafened = !root.deafened
        voiceCallManager.setDeafen(root.deafened)
    }

    // A read receipt is a claim that somebody read something, so it is only sent
    // for a message that was actually on the screen.
    property bool chatAtBottom: true

    // The peer sends nothing at all when it stops, so the timer is what ends the
    // state: waiting for a "stopped typing" a lost datagram can swallow leaves the
    // dots up forever.
    property string typingChatId: ""

    Timer {
        id: typingTimeout
        interval: 6000
        onTriggered: root.typingChatId = ""
    }

    property var chatModels: ({})

    function modelForPeer(ip) {
        if (!chatModels[ip]) {
            const m = Qt.createQmlObject(
                'import koutnet.app; ChatModel {}', root, "dynamicChatModel")
            m.historyManager = HistoryManager
            m.reactionStore = ReactionStore
            m.unreadManager = UnreadManager
            m.chatId = ip
            // Every message in any chat, in either direction, from one place;
            // wiring the four send and receive sites instead breaks at the fifth.
            m.messageAdded.connect(root.onChatActivity)
            chatModels[ip] = m
        }
        return chatModels[ip]
    }

    function onChatActivity(chatId, preview, isOwn, ts) {
        chatList.noteMessage(chatId, preview, isOwn, ts)
    }

    // Called when a chat is opened and when a message arrives in the one already
    // open; the second was missing, which kept an outgoing message's "sent, not
    // confirmed" arrow up while the peer sat reading it.
    function markChatRead(ip) {
        if (ip.length === 0 || ip === root.kSelfChatId)
            return
        notificationManager.clearChat(ip)
        root.modelForPeer(ip).markAllRead()
        if (root.isMatrixChat(ip)) {
            // The receipt goes to the homeserver, not into a datagram addressed
            // to a string that is not an address.
            matrixRooms.markRead(ip)
            return
        }
        networkManager.sendReadReceipt(ip, "dm")
    }

    function upsertPeer(info) {
        const ip = info.ip
        if (!ip) return
        for (let i = 0; i < peersModel.count; ++i) {
            if (peersModel.get(i).ip === ip) {
                peersModel.set(i, info)
                return
            }
        }
        peersModel.append(info)
    }

    function removePeer(ip) {
        for (let i = 0; i < peersModel.count; ++i) {
            if (peersModel.get(i).ip === ip) {
                peersModel.remove(i)
                return
            }
        }
    }

    ListModel { id: peersModel }

    // What the sidebar draws: survives restarts and peers going offline, neither
    // of which peersModel does.
    ChatListModel {
        id: chatList
        historyManager: HistoryManager
        unreadManager: UnreadManager
    }

    // The Matrix side reaches the same two models as everything else, through
    // the same two calls a LAN peer's traffic makes. MatrixRoomBridge holds all
    // the libQuotient there is; nothing below this block knows the difference.
    Connections {
        target: matrixRooms

        function onRoomListed(chatId, displayName) {
            // openChat() is idempotent and never reorders, so a sync that lists
            // forty rooms does not shuffle the conversation the user is reading.
            chatList.openChat(chatId, displayName)
        }
        function onRoomMessage(chatId, eventId, text, sender, isOwn, ts, isSystem) {
            const model = root.modelForPeer(chatId)
            // False means the model already had this event id, which is the
            // normal case for the backlog replayed after every reconnect. The
            // conversation list is fed by messageAdded from inside the model, so
            // there is nothing to do on either branch here.
            if (!model.ingestRemoteMessage(eventId, text, sender, isOwn, ts, isSystem))
                return
            if (isSystem || isOwn)
                return
            notificationManager.notifyMessage(chatId, root.peerLabel(chatId), text)
            if (chatId === root.currentPeerIp && root.chatAtBottom)
                root.markChatRead(chatId)
        }
        function onRoomAttachment(chatId, eventId, media, sender, isOwn, ts) {
            const model = root.modelForPeer(chatId)
            if (!model.ingestRemoteAttachment(eventId, media, sender, isOwn, ts))
                return
            if (isOwn)
                return
            notificationManager.notifyMessage(chatId, root.peerLabel(chatId), media.name || "")
            if (chatId === root.currentPeerIp && root.chatAtBottom)
                root.markChatRead(chatId)
        }
        // Never a new row: the corrected text replaces what is already on the
        // screen, and false means the original is older than the loaded backlog.
        function onRoomMessageEdited(chatId, eventId, newText) {
            root.modelForPeer(chatId).applyRemoteEdit(eventId, newText)
        }
        // The same replacement without the "edited" mark: the key for an
        // encrypted message turned up and the placeholder row can finally say
        // what it always said. Nobody edited it.
        function onRoomMessageRevealed(chatId, eventId, text) {
            root.modelForPeer(chatId).applyRemoteEdit(eventId, text, false)
        }
        function onRoomInfoChanged(chatId) {
            if (chatId === root.currentPeerIp)
                root.refreshRoomInfo()
        }
        function onRoomLeft(chatId) {
            // The row stays: a room that was left still has a log, exactly as a
            // peer that was switched off does.
            root.modelForPeer(chatId).appendSystemMessage(
                i18nc("@info in-timeline notice", "You are no longer in this room."))
        }
        function onSendFailed(chatId, reason) {
            root.reportError(reason)
        }
    }

    Connections {
        target: matrixManager

        function onSessionNotPersisted(reason) {
            root.notify(i18nc("@info:status %1 is the reason the wallet gave",
                              "Signed in, but the session could not be saved: %1", reason),
                        Kirigami.MessageType.Warning)
        }
        // The sign-in page is where a session is started and almost never where
        // the user is when one dies, so every failure is reported here too. The
        // alternative was the window saying "Syncing..." for the rest of the day.
        function onSessionError(message) {
            root.reportError(message)
        }
    }

    // peersModel.count is read only to register a dependency, so this
    // re-evaluates as peers come and go.
    function peerInfoFor(ip) {
        /* eslint-disable no-unused-expressions */
        peersModel.count
        if (ip === root.kSelfChatId) {
            return {
                username: i18nc("@item contact list, chat with yourself", "Favorites"),
                os: "",
                e2e: false,
                avatarLetter: "\u2605",
                isFavorites: true,
                online: false,
                lastSeen: 0
            }
        }
        for (let i = 0; i < peersModel.count; ++i) {
            const p = peersModel.get(i)
            if (p.ip === ip) {
                return {
                    ip: ip,
                    username: p.username || root.unknownPeerName,
                    displayName: p.display_name || "",
                    bio: p.bio || "",
                    os: p.os || "",
                    e2e: p.e2e === true,
                    avatarLetter: (p.username || root.unknownPeerName).charAt(0).toUpperCase(),
                    isFavorites: false,
                    statusEmoji: p.status_emoji || "",
                    presence: p.presence || 0,
                    // Being in peersModel is what reachable means: userOffline
                    // takes a peer out as soon as presence stops arriving.
                    online: true,
                    // Stamped by handlePresence() on arrival, not sent by the peer,
                    // so it only says anything once "online" above has gone false.
                    lastSeen: p.last_seen || 0
                }
            }
        }
        // Not on the network: the conversation list is the only thing that still
        // remembers when a peer that is switched off was last around.
        const known = chatList.chatInfo(ip)
        return {
            ip: ip,
            username: known.displayName || root.unknownPeerName,
            os: "",
            e2e: false,
            avatarLetter: (known.displayName || root.unknownPeerName).charAt(0).toUpperCase(),
            isFavorites: false,
            statusEmoji: "",
            presence: 0,
            online: false,
            lastSeen: known.lastSeenSecs || 0
        }
    }

    // The incoming call is declared below as a dialog instead: answering is a
    // question with two answers.
    property var outgoingCallWindow: null
    property var activeCallWindow: null

    // Nothing rather than the bare address at the end, which used to put an IP in
    // the window title, in every notification and in the conversation list.
    function peerDisplayName(ip) {
        for (let i = 0; i < peersModel.count; ++i) {
            const p = peersModel.get(i)
            if (p.ip === ip)
                return p.display_name || p.username || ""
        }
        return ""
    }

    readonly property string unknownPeerName: i18nc("@info a peer that has published no name of its own", "Unknown peer")

    function peerLabel(ip) {
        const named = root.peerDisplayName(ip)
        if (named.length > 0)
            return named
        // The conversation list remembers a name for a peer that is switched
        // off, and it is the only place a Matrix room's name lives at all.
        const known = chatList.chatInfo(ip)
        return known.displayName || root.unknownPeerName
    }

    function startOutgoingCall(ip) {
        if (root.outgoingCallWindow) return
        if (root.isMatrixChat(ip)) {
            // Voice is the LAN protocol's own, peer to peer over TCP. There is
            // no Matrix call here and pretending otherwise would ring nothing.
            root.reportError(i18nc("@info:status", "Calls are not available in Matrix rooms."))
            return
        }
        networkManager.sendCallRequest(ip)
        const win = outgoingCallComponent.createObject(
            root, { peerName: root.peerLabel(ip), peerIp: ip })
        win.cancelled.connect(function() {
            networkManager.sendCallEnd(ip)
            root.outgoingCallWindow = null
        })
        root.outgoingCallWindow = win
    }

    function openActiveCall(username, ip) {
        if (root.outgoingCallWindow) {
            root.outgoingCallWindow.close()
            root.outgoingCallWindow = null
        }
        if (root.activeCallWindow) return
        const win = activeCallComponent.createObject(root, { peerName: username, peerIp: ip })
        win.hangup.connect(function() {
            networkManager.sendCallEnd(ip)
            voiceCallManager.hangup(ip)
            root.activeCallWindow = null
        })
        win.muteToggled.connect(function(muted) {
            voiceCallManager.setMute(muted)
        })
        root.activeCallWindow = win
    }

    function endAllCalls() {
        voiceCallManager.hangupAll()
        if (root.activeCallWindow) { root.activeCallWindow.close(); root.activeCallWindow = null }
        if (root.outgoingCallWindow) { root.outgoingCallWindow.close(); root.outgoingCallWindow = null }
    }

    // By type rather than by resource path: the old Qt.createComponent() calls had
    // the module's internal resource layout in three string literals.
    Component { id: outgoingCallComponent; OutgoingCallWindow {} }
    Component { id: activeCallComponent; ActiveCallWindow {} }

    // One card, moved to whatever was clicked. openAt() reparents it, so it must
    // not be declared inside anything that gets destroyed under it - a delegate.
    PeerCard {
        id: peerCard

        onMessageRequested: (chatId) => root.openChat(chatId)
        onCallRequested: (chatId) => root.startOutgoingCall(chatId)
        onDetailsRequested: (chatId) => {
            root.openChat(chatId)
            if (!root.peerInfoOpen)
                root.togglePeerInfo()
        }
    }

    // A room member is not a peer, so clicking one does not open the peer card.
    // The two cards are the same shape on purpose and hold different things,
    // because the questions they answer are different ones.
    RoomMemberCard {
        id: roomMemberCard

        onNotifyRequested: (text) => root.notify(text, Kirigami.MessageType.Information)
    }

    // In the window rather than in whichever page asked for it: a verification
    // takes a minute of somebody's attention and must survive the room column
    // being shut or the sign-in page being popped. It also opens itself when
    // another session asks, which can happen with no page of ours on screen.
    DeviceVerificationDialog {
        id: deviceVerificationDialog
    }

    Connections {
        target: matrixVerification

        function onSessionFinished(ok, message) {
            root.notify(message, ok ? Kirigami.MessageType.Positive : Kirigami.MessageType.Warning)
        }
    }

    function showPeerCard(chatId, anchorItem) {
        if (chatId.length === 0 || chatId === root.kSelfChatId)
            return
        if (root.isMatrixChat(chatId)) {
            // The row in the conversation list is the room, not a person; the
            // room's own column is what has anybody in it.
            root.openChat(chatId)
            if (!root.peerInfoOpen)
                root.togglePeerInfo()
            return
        }
        peerCard.openAt(anchorItem, root.peerInfoFor(chatId))
    }

    function showRoomMemberCard(userId, anchorItem) {
        if (userId.length === 0 || !root.isMatrixChat(root.currentPeerIp))
            return
        const info = matrixRooms.memberInfo(root.currentPeerIp, userId)
        if (!info || !info.userId)
            return
        roomMemberCard.openAt(anchorItem, info)
    }

    AccountCard {
        id: accountCard

        onEditProfileRequested: root.showLayer(settingsPageComponent)
        onNotifyRequested: (text) => root.notify(text, Kirigami.MessageType.Information)
    }

    function showAccountCard(anchorItem) {
        accountCard.openAt(anchorItem)
    }

    IncomingCallDialog {
        id: incomingCall

        onAnswered: {
            networkManager.sendCallAccept(incomingCall.callerIp)
            voiceCallManager.call(incomingCall.callerIp)
            root.openActiveCall(incomingCall.callerName, incomingCall.callerIp)
        }
        onDeclined: networkManager.sendCallReject(incomingCall.callerIp)
    }

    onCurrentPeerIpChanged: {
        if (currentPeerIp.length > 0 && currentPeerIp !== kSelfChatId) {
            chatList.openChat(currentPeerIp, root.peerDisplayName(currentPeerIp))
            root.markChatRead(currentPeerIp)
        }
        root.refreshRoomInfo()
        // The third column is a different page for a room than for a peer, and
        // it is bound to the conversation rather than pushed with one, so
        // moving between the two kinds has to replace it rather than re-point it.
        if (root.peerInfoOpen) {
            pageStack.pop()
            root.togglePeerInfo()
        }
    }

    function openChat(chatId) {
        root.currentPeerIp = chatId
        while (pageStack.layers.depth > 1)
            pageStack.layers.pop()
        pageStack.currentIndex = 1
    }

    // The peer column's contents are bound to currentPeerIp rather than passed in,
    // so switching conversation with it open re-points it.
    readonly property bool peerInfoOpen: pageStack.depth > 2

    function togglePeerInfo() {
        if (root.peerInfoOpen) {
            pageStack.pop()
            return
        }
        // The actions that ask for this are hidden in compact mode, but a keyboard
        // shortcut or a peer card can still get here.
        if (root.compact)
            return
        pageStack.push(root.currentIsRoom ? roomInfoComponent : peerInfoComponent)
        pageStack.currentIndex = pageStack.depth - 1
    }

    // An InlineMessage in the footer rather than the modal sheet this used to be,
    // which took the window away from whoever was using it.
    function notify(text, type) {
        notification.text = text
        notification.type = type
        notification.visible = true
    }

    function reportError(text) {
        root.notify(text, Kirigami.MessageType.Error)
    }

    // Layers are one deep by design: a drawer entry replaces whatever layer is
    // showing instead of stacking a second copy of it behind the first.
    function showLayer(page, properties) {
        while (pageStack.layers.depth > 1)
            pageStack.layers.pop()
        pageStack.layers.push(page, properties)
    }

    // Local decoration: never put on the wire, and nothing outside this window
    // reads the setting.
    //
    // Kirigami.Page draws an opaque background, so every page and layer covers
    // this by itself - until one of them replaces that background with something
    // of its own, which is what once put the picture behind the profile page.
    //
    // z is negative so this sits under the page row, which has no z of its own.
    Item {
        anchors.fill: parent
        z: -1
        visible: appSettings.wallpaperPath.length > 0

        Image {
            anchors.fill: parent
            source: appSettings.wallpaperPath
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            // The window is large and the file is whatever the user picked, so
            // it is scaled once on load rather than on every frame.
            sourceSize.width: parent.width
            sourceSize.height: parent.height
        }

        // The scrim is floored rather than allowed to reach zero: at zero the
        // conversation's text sits on an arbitrary photograph, and no colour
        // scheme is legible against every one of those.
        Rectangle {
            anchors.fill: parent
            color: Kirigami.Theme.backgroundColor
            opacity: Math.max(0.2, 1 - appSettings.wallpaperOpacity / 100)
        }
    }

    Shortcut {
        sequence: StandardKey.FullScreen
        onActivated: root.toggleFullScreen()
    }

    function toggleFullScreen() {
        root.visibility = (root.visibility === Window.FullScreen) ? Window.Windowed : Window.FullScreen
    }

    globalDrawer: Kirigami.GlobalDrawer {
        title: i18nc("@title:window", "KOutNet")
        titleIcon: "org.kde.koutnet"
        // A drawer that stays put would eat a third of a narrow window.
        modal: true

        actions: [
            Kirigami.Action {
                text: i18nc("@action:inmenu the list of conversations", "Chats")
                icon.name: "dialog-messages"
                onTriggered: {
                    while (root.pageStack.layers.depth > 1)
                        root.pageStack.layers.pop()
                    root.pageStack.currentIndex = 0
                }
            },
            Kirigami.Action {
                text: i18nc("@action:inmenu", "Notes")
                icon.name: "note"
                onTriggered: root.showLayer(notesPageComponent)
            },
            Kirigami.Action {
                text: i18nc("@action:inmenu the log of past calls", "Call log")
                icon.name: "call-start"
                onTriggered: root.showLayer(callLogPageComponent)
            },
            Kirigami.Action {
                // Violla is the name of the player, not a word to translate.
                text: "Violla"
                icon.name: "multimedia-player"
                onTriggered: root.showLayer(playerPageComponent)
            },
            Kirigami.Action { separator: true },
            // Not checkable on purpose: triggering a checkable action writes its
            // own checked property, which drops the binding to micMuted and lets
            // the footer button stop agreeing with the drawer.
            Kirigami.Action {
                text: root.micMuted
                    ? i18nc("@action:inmenu let your microphone be heard again", "Unmute microphone")
                    : i18nc("@action:inmenu silence your own microphone", "Mute microphone")
                icon.name: (root.micMuted || root.deafened) ? "microphone-sensitivity-muted" : "audio-input-microphone"
                enabled: !root.deafened
                onTriggered: root.toggleMic()
            },
            Kirigami.Action {
                text: root.deafened
                    ? i18nc("@action:inmenu start hearing calls again", "Undeafen")
                    : i18nc("@action:inmenu stop hearing calls, and stop being heard", "Deafen")
                icon.name: root.deafened ? "audio-volume-muted" : "audio-volume-high"
                onTriggered: root.toggleDeafen()
            },
            Kirigami.Action {
                text: i18nc("@action:inmenu hang up every call in progress", "End all calls")
                icon.name: "call-stop"
                enabled: root.activeCallWindow !== null || root.outgoingCallWindow !== null
                onTriggered: root.endAllCalls()
            },
            Kirigami.Action { separator: true },
            // The profile is the first section of the settings page, so this and
            // the entry below it open the same layer on purpose.
            Kirigami.Action {
                text: i18nc("@action:inmenu", "My profile")
                icon.name: "user-identity"
                onTriggered: root.showLayer(settingsPageComponent)
            },
            Kirigami.Action {
                text: i18nc("@action:inmenu", "Settings")
                icon.name: "settings-configure"
                shortcut: StandardKey.Preferences
                onTriggered: root.showLayer(settingsPageComponent)
            },
            Kirigami.Action {
                text: i18nc("@action:inmenu", "About KOutNet")
                icon.name: "help-about"
                onTriggered: root.showLayer(aboutPageComponent)
            },
            // Not checkable, for the same reason as the microphone entry above.
            Kirigami.Action {
                text: root.compact
                    ? i18nc("@action:inmenu go back to the full three-column layout", "Leave compact mode")
                    : i18nc("@action:inmenu switch to a reduced layout for a narrow window", "Compact mode")
                icon.name: root.compact ? "sidebar-expand" : "sidebar-collapse"
                onTriggered: root.toggleCompact()
            },
            Kirigami.Action {
                text: root.visibility === Window.FullScreen
                    ? i18nc("@action:inmenu leave full screen", "Exit full screen")
                    : i18nc("@action:inmenu", "Full screen")
                icon.name: root.visibility === Window.FullScreen ? "view-restore" : "view-fullscreen"
                // Full screen and compact are opposite requests, and compact
                // resizes the window, which a full-screen window cannot honour.
                enabled: !root.compact
                onTriggered: root.toggleFullScreen()
            },
            Kirigami.Action {
                text: i18nc("@action:inmenu", "Quit")
                icon.name: "application-exit"
                shortcut: StandardKey.Quit
                onTriggered: Qt.quit()
            }
        ]
    }

    footer: ColumnLayout {
        spacing: 0

        Kirigami.InlineMessage {
            id: notification
            Layout.fillWidth: true
            position: Kirigami.InlineMessage.Position.Footer
            showCloseButton: true
            visible: false
            type: Kirigami.MessageType.Information
        }

        // The bar itself is hidden, not just its label: the desktop style gives a
        // toolbar background an implicit forty pixels whatever is inside it, which
        // is the strip of empty grey that used to sit under every page.
        QQC2.ToolBar {
            Layout.fillWidth: true
            visible: !root.compact && peersModel.count > 0
            position: QQC2.ToolBar.Footer

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                // The empty case used to say it was searching, which is true every
                // second the application runs and so worth saying in none of them.
                QQC2.Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    text: i18ncp("@info:status %1 is a number of peers",
                                 "%1 peer on the network", "%1 peers on the network", peersModel.count)
                }

                // No address: this used to print the host's own IP in every shot.
            }
        }
    }

    Connections {
        target: networkManager
        function onUserOnline(peerInfo) {
            root.upsertPeer(peerInfo)
            chatList.setPresence(peerInfo.ip, true, peerInfo.last_seen || 0,
                                 peerInfo.display_name || peerInfo.username || "")
        }
        function onPeerRefreshed(ip, lastSeen) {
            chatList.setPresence(ip, true, lastSeen, "")
        }
        function onUserOffline(ip) {
            root.removePeer(ip)
            // The stamp is deliberately not touched: the last presence that arrived
            // is when the peer was last seen, and "now" grows into "just now".
            chatList.setPresence(ip, false, 0, "")
        }
        // Filed under from_ip, not the username in the packet: the peer chose that
        // for itself and two of them can say the same thing.
        function onTyping(username, chatId, fromIp) {
            if (fromIp.length === 0)
                return
            root.typingChatId = fromIp
            typingTimeout.restart()
        }
        function onMessage(msg) {
            if (msg.type === "private") {
                root.modelForPeer(msg.from_ip).receiveMessage(msg.text, msg.from_ip)
                // Always told; the manager decides what that turns into, because
                // whether the application is active is a question about the
                // process rather than about this window.
                notificationManager.notifyMessage(msg.from_ip,
                                                  root.peerLabel(msg.from_ip),
                                                  msg.text)
                if (root.typingChatId === msg.from_ip)
                    root.typingChatId = ""
                // A message that landed below the fold of a conversation somebody
                // scrolled up out of has not been read, whatever the receipt says.
                if (msg.from_ip === root.currentPeerIp && root.chatAtBottom)
                    root.markChatRead(msg.from_ip)
            } else if (msg.type === "edit") {
                // msg_ts is the stamp of the original, which is the only identifier
                // the two ends share - see NetworkManager::sendMessageEdit.
                const editModel = root.modelForPeer(msg.from_ip)
                const editRow = editModel.rowForStamp(msg.msg_ts)
                if (editRow >= 0)
                    editModel.editMessage(editRow, msg.new_text)
            } else if (msg.type === "delete") {
                const delModel = root.modelForPeer(msg.from_ip)
                const delRow = delModel.rowForStamp(msg.msg_ts)
                if (delRow >= 0)
                    delModel.deleteMessage(delRow)
            } else if (msg.type === "read") {
                // dispatch() has rewritten from_ip to the address this peer is
                // filed under, which is the string the chat is keyed on.
                root.modelForPeer(msg.from_ip).markOwnMessagesRead()
            }
        }
        function onCallRequest(username, ip) {
            incomingCall.callerName = username
            incomingCall.callerIp = ip
            incomingCall.open()
            // Unconditionally, unlike a message: a dialog behind whatever the
            // user is actually looking at is not a ringing telephone.
            notificationManager.notifyCall(username, ip)
        }
        function onCallAccepted(username, ip) {
            voiceCallManager.call(ip)
            root.openActiveCall(username, ip)
        }
        function onCallRejected(ip) {
            if (root.outgoingCallWindow) {
                root.outgoingCallWindow.close()
                root.outgoingCallWindow = null
            }
        }
        function onCallEnded(ip) {
            notificationManager.clearCall(ip)
            if (root.outgoingCallWindow) { root.outgoingCallWindow.close(); root.outgoingCallWindow = null }
            if (root.activeCallWindow) { root.activeCallWindow.close(); root.activeCallWindow = null }
            // The old dialog had a callRejected() nothing ever called, so a caller
            // giving up left the prompt sitting there.
            incomingCall.callRejected()
            voiceCallManager.hangup(ip)
        }
        // These used to go nowhere, so a bind failure looked like an idle network.
        function onErrorOccurred(message) {
            root.reportError(message)
        }
    }

    // A notification is the only thing here that can be clicked while the window
    // is behind something else, so everything it offers raises the window first.
    Connections {
        target: notificationManager

        // The compositor's activation token is adopted on the C++ side before these
        // arrive, so requestActivate() is allowed to do something.
        function onChatRequested(chatId) {
            root.show()
            root.raise()
            root.requestActivate()
            root.openChat(chatId)
        }
        function onReplyRequested(chatId, text) {
            if (text.trim().length === 0)
                return
            if (root.isMatrixChat(chatId)) {
                matrixRooms.sendText(chatId, text)
                return
            }
            const replyModel = root.modelForPeer(chatId)
            const replyStamp = replyModel.sendMessage(text)
            networkManager.sendPrivate(text, chatId)
            replyModel.markSent(replyStamp)
        }
        function onCallAnswerRequested(ip) {
            root.show()
            root.raise()
            root.requestActivate()
            networkManager.sendCallAccept(ip)
            voiceCallManager.call(ip)
            root.openActiveCall(root.peerLabel(ip), ip)
            incomingCall.callRejected()
        }
        function onCallRejectRequested(ip) {
            networkManager.sendCallReject(ip)
            incomingCall.callRejected()
        }
        function onCallShowRequested(ip) {
            root.show()
            root.raise()
            root.requestActivate()
            if (incomingCall.callerIp !== ip) {
                incomingCall.callerName = root.peerLabel(ip)
                incomingCall.callerIp = ip
            }
            incomingCall.open()
        }
    }

    Connections {
        target: audioDevices
        function onError(message) {
            root.reportError(message)
        }
    }

    Connections {
        target: fileTransferHandler
        function onTransferRejected(tid, reason) {
            root.reportError(reason)
        }
        function onFileSaved(meta, localPath) {
            const fromIp = meta.from_ip
            if (!fromIp) return
            root.modelForPeer(fromIp).receiveFile(localPath, root.looksLikeImage(localPath), fromIp)
            if (fromIp === root.currentPeerIp && root.chatAtBottom)
                root.markChatRead(fromIp)
        }
    }

    function looksLikeImage(path) {
        return /\.(png|jpg|jpeg|gif|bmp|webp)$/i.test(path)
    }

    // defaultColumnWidth is what every column gets unless it says otherwise, and
    // "otherwise" is Kirigami.ColumnView.fillWidth on the conversation below.
    // Being last is not enough on its own, which left the conversation seventeen
    // grid units wide with the rest of a full-screen window empty beside it.
    //
    // Handed the two page objects rather than two Components on purpose:
    // PageRow.initPage() instantiates a Component under pagesLogic, a QtObject, so
    // the page is briefly a graphical item outside the scene and QQmlComponent
    // warns about it at every start. Given an Item, getPageComponent() returns
    // nothing, and insertItem picks the page up from the window's contentData.
    pageStack.initialPage: [chatListPage, chatPage]
    pageStack.defaultColumnWidth: root.kRoomyColumnWidth
    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.ToolBar
    pageStack.globalToolBar.showNavigationButtons: Kirigami.ApplicationHeaderStyle.ShowBackButton

    // trayIcon is null if the setting was off at start - see main.cpp on why it is
    // not built later - so everything that touches it is guarded.
    readonly property bool hasTray: typeof trayIcon !== "undefined" && trayIcon !== null

    // Bindings rather than a Connections block: a signal handler per property
    // would be four places for these to fall out of step.
    Binding {
        target: root.hasTray ? trayIcon : null
        property: "unreadCount"
        value: UnreadManager.total
        restoreMode: Binding.RestoreNone
    }
    Binding {
        target: root.hasTray ? trayIcon : null
        property: "micMuted"
        value: root.micMuted
        restoreMode: Binding.RestoreNone
    }
    Binding {
        target: root.hasTray ? trayIcon : null
        property: "deafened"
        value: root.deafened
        restoreMode: Binding.RestoreNone
    }
    Binding {
        target: root.hasTray ? trayIcon : null
        property: "presence"
        value: appSettings.presence
        restoreMode: Binding.RestoreNone
    }
    Binding {
        target: root.hasTray ? trayIcon : null
        property: "windowVisible"
        value: root.visible
        restoreMode: Binding.RestoreNone
    }

    Connections {
        target: root.hasTray ? trayIcon : null

        function onShowHideRequested() {
            if (root.visible) {
                root.hide()
                return
            }
            root.show()
            root.raise()
            root.requestActivate()
        }
        function onMuteToggleRequested() {
            root.toggleMic()
        }
        function onDeafenToggleRequested() {
            root.toggleDeafen()
        }
        function onPresenceRequested(presence) {
            appSettings.presence = presence
        }
        function onQuitRequested() {
            Qt.quit()
        }
    }

    // main.cpp turns off quitOnLastWindowClosed, so the branch that really closes
    // has to say Qt.quit() out loud - see the note there.
    onClosing: (close) => {
        if (root.hasTray && appSettings.minimizeToTray) {
            close.accepted = false
            root.hide()
            return
        }
        close.accepted = true
        Qt.quit()
    }

    Component.onCompleted: {
        if (root.hasTray)
            trayIcon.attachWindow(root)
        // The setting survives a restart on its own, the size does not, so the
        // application used to reopen as a compact layout in a full-width window.
        if (root.compact)
            root.applyCompactSize()
        if (appSettings.showWelcome)
            pageStack.layers.push(welcomeComponent)
    }

    ChatListPage {
        id: chatListPage

        selectedChatId: root.currentPeerIp
        favoritesChatId: root.kSelfChatId
        connectionMode: appSettings.connectionMode
        model: chatList
        micMuted: root.micMuted
        deafened: root.deafened
        compact: root.compact

        onChatActivated: (chatId) => root.openChat(chatId)
        onMicToggled: root.toggleMic()
        onDeafenToggled: root.toggleDeafen()
        onPeerCardRequested: (chatId, anchorItem) => root.showPeerCard(chatId, anchorItem)
        onNewChatRequested: root.showLayer(newChatPageComponent)
        onProfileRequested: (anchorItem) => root.showAccountCard(anchorItem)
        onSettingsRequested: root.showLayer(settingsPageComponent)
        onLeaveRoomRequested: (chatId) => matrixRooms.leaveRoom(chatId)
        selfChatId: root.kSelfChatId
        onForgetRequested: (chatId) => {
            chatList.removeChat(chatId)
            if (root.currentPeerIp === chatId)
                root.currentPeerIp = ""
        }
        onClearRequested: (chatId) => {
            clearChatPrompt.chatId = chatId
            clearChatPrompt.open()
        }
        // The same two calls the settings page makes, because switching mode
        // raises or drops the relay tunnel and half of that is not a state to be in.
        onConnectionModeRequested: (mode) => {
            if (!networkManager.modeAvailable(mode))
                return
            appSettings.connectionMode = mode
            networkManager.setRelayServer(appSettings.relayHost, appSettings.relayPort, 0)
            networkManager.setConnectionMode(mode)
        }
    }

    // Emptying a chat cannot be undone, and the saved messages one is the
    // place people keep things on purpose.
    Kirigami.PromptDialog {
        id: clearChatPrompt

        property string chatId: ""

        title: i18nc("@title:window", "Clear this chat?")
        subtitle: i18nc("@info", "Every message in it is deleted from this device. There is no way back.")
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: i18nc("@action:button", "Clear")
                icon.name: "edit-clear-all"
                onTriggered: {
                    const model = root.chatModels[clearChatPrompt.chatId]
                    if (model)
                        model.clearMessages()
                    clearChatPrompt.close()
                }
            }
        ]
    }

    ChatPage {
        id: chatPage

        readonly property bool isSelfChat: peerIp === root.kSelfChatId

        // Without this the conversation is handed defaultColumnWidth like the list.
        Kirigami.ColumnView.fillWidth: true

        compact: root.compact
        isRoom: root.currentIsRoom
        roomInfo: root.currentRoomInfo
        peerIp: root.currentPeerIp
        peerInfo: root.currentPeerIp.length > 0 ? root.peerInfoFor(root.currentPeerIp) : null
        messagesModel: root.currentPeerIp.length > 0 ? root.modelForPeer(root.currentPeerIp) : null
        peerTyping: root.typingChatId.length > 0 && root.typingChatId === root.currentPeerIp
        selfDisplayName: appSettings.displayName || appSettings.username
        selfAvatarSource: appSettings.avatarPath

        onAtBottomChanged: root.chatAtBottom = atBottom
        Component.onCompleted: root.chatAtBottom = atBottom

        onCallRequested: {
            if (!isSelfChat)
                root.startOutgoingCall(peerIp)
        }
        // The row goes in first and the datagram second, or there is nothing on
        // screen for the hourglass to sit on; markSent() turns it into a tick.
        onSendRequested: function(text, replyExcerpt, replyAuthor, replyId) {
            if (root.isMatrixChat(peerIp)) {
                // No local row first, unlike the LAN path below. The homeserver
                // echoes the message back through sync carrying the event id the
                // whole room sees, and that id is what the timeline is keyed on;
                // a row invented here would have to be reconciled with it, and
                // the reconciliation is what duplicates messages in every client
                // that has tried it. sendFailed() reports whatever went wrong.
                matrixRooms.sendText(peerIp, text)
                return
            }
            // The quote is stored with the message but not put on the wire: the
            // protocol has no reply field.
            const stamp = messagesModel.sendMessage(text, replyExcerpt, replyAuthor, replyId)
            if (stamp === 0)
                return
            if (!isSelfChat)
                networkManager.sendPrivate(text, peerIp)
            messagesModel.markSent(stamp)
        }
        onAttachRequested: function(localFilePath) {
            if (root.isMatrixChat(peerIp)) {
                // No local row first, for the same reason as a text message
                // above: the homeserver echoes the event back through sync
                // carrying the id the whole room sees, and that id is what the
                // timeline is keyed on.
                matrixRooms.sendFile(peerIp, localFilePath)
                return
            }
            const stamp = messagesModel.sendFile(localFilePath, root.looksLikeImage(localFilePath))
            if (!isSelfChat)
                networkManager.sendFile(peerIp, localFilePath)
            messagesModel.markSent(stamp)
        }
        onTypingNotice: {
            if (!isSelfChat && !root.isMatrixChat(peerIp))
                networkManager.sendTyping(peerIp, peerIp)
        }
        onReadReached: root.markChatRead(peerIp)
        onProfileRequested: root.showLayer(otherProfileComponent, { peer: root.peerInfoFor(peerIp) })
        onInfoRequested: root.togglePeerInfo()
        onPeerCardRequested: (anchorItem) => root.showPeerCard(peerIp, anchorItem)
        onOwnProfileRequested: (anchorItem) => root.showAccountCard(anchorItem)
        onNewChatRequested: root.showLayer(newChatPageComponent)
        onNotifyRequested: (text) => root.notify(text, Kirigami.MessageType.Information)
        onForwardRequested: root.notify(i18nc("@info", "Forwarding messages is not implemented yet."),
                                       Kirigami.MessageType.Information)
        // The page has already changed its own copy by the time these arrive; what
        // is left is telling the peer, which needs the address only the window has.
        onEditCommitted: (stamp, newText) => {
            if (!isSelfChat)
                networkManager.sendMessageEdit(peerIp, peerIp, stamp, newText)
        }
        onDeleteCommitted: (stamp) => {
            if (!isSelfChat)
                networkManager.sendMessageDelete(peerIp, peerIp, stamp)
        }
    }

    Component {
        id: peerInfoComponent

        PeerInfoPage {
            peer: root.currentPeerIp.length > 0 ? root.peerInfoFor(root.currentPeerIp) : null
            onProfileRequested: root.showLayer(otherProfileComponent,
                                               { peer: root.peerInfoFor(root.currentPeerIp) })
        }
    }

    Component {
        id: roomInfoComponent

        RoomInfoPage {
            chatId: root.currentPeerIp
            onMemberActivated: (userId, anchorItem) => root.showRoomMemberCard(userId, anchorItem)
            onVerifySessionsRequested: deviceVerificationDialog.openForSession()
            onLeaveRequested: (chatId) => matrixRooms.leaveRoom(chatId)
            onNotifyRequested: (text) => root.notify(text, Kirigami.MessageType.Information)
        }
    }

    Component {
        id: settingsPageComponent

        SettingsPage {
            onSaved: root.notify(i18nc("@info:status", "Settings saved"), Kirigami.MessageType.Positive)
            onMatrixAccountRequested: root.pageStack.layers.push(matrixLoginComponent)
        }
    }

    Component {
        id: matrixLoginComponent

        MatrixLoginPage {
            onVerifySessionsRequested: deviceVerificationDialog.openForSession()
        }
    }

    Component { id: aboutPageComponent; AboutPage {} }
    Component { id: notesPageComponent; NotesPage {} }
    Component { id: callLogPageComponent; CallLogPage {} }
    Component { id: playerPageComponent; PlayerPage {} }
    Component { id: otherProfileComponent; OtherProfile {} }

    Component {
        id: newChatPageComponent

        NewChatPage {
            peers: peersModel
            onChatRequested: (ip) => {
                root.pageStack.layers.pop()
                root.openChat(ip)
            }
        }
    }

    // A layer rather than an item pinned over the window overlay: that needed a
    // negative z to stay out of the way of every popup, and got it wrong twice.
    Component {
        id: welcomeComponent

        WelcomeScreen {
            onContinueRequested: root.pageStack.layers.pop()
            onAboutRequested: root.pageStack.layers.push(aboutPageComponent)
        }
    }
}
