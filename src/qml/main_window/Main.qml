// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - application window
//
// The shell is up to three columns of a Kirigami.PageRow: the conversation list
// with the connection rail down its edge, the conversation, and - when it is
// asked for - who is on the other end of it. That is the whole of the layout.
// PageRow folds to one column on a narrow window and hands out its own back
// button, which is what the animated Layout.preferredWidth and the hand-drawn
// hamburger that used to be here were reaching for.
//
// The third column is pushed and popped rather than always present. An
// information panel that cannot be put away is a panel that is in the way, and
// pushing it is also what gets it the folding for free.
//
// Everything that is not a conversation - settings, profiles, notes, the call
// log, the player - goes on pageStack.layers, so it covers every column and
// comes back with the back button rather than as a modal sheet.
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
    // Narrow enough that the page row folds to a single column, which is the
    // layout being tested when it does.
    minimumWidth: Kirigami.Units.gridUnit * 20
    minimumHeight: Kirigami.Units.gridUnit * 18

    // The one thing this application says about colour. Everything else comes
    // from the Plasma colour scheme through Kirigami.Theme.
    //
    // It has to be repeated on a few other items rather than set once here:
    // Kirigami resolves a theme by walking up the parent chain, and anything
    // that sets Kirigami.Theme.inherit false - every ScrollablePage, every
    // FormCard, everything reparented into the window overlay - starts a chain
    // of its own. Each of those is marked with a pointer back to this comment.
    Kirigami.Theme.inherit: false
    Kirigami.Theme.highlightColor: Brand.accent

    // Compact mode: the conversation list and the conversation, tightened, and
    // no peer column. A mode the user picks from the drawer and keeps, as opposed
    // to what the page row already does by itself when the window is dragged
    // narrow.
    //
    // It is the same layout tree, not a second one. Kirigami.Settings.isMobile,
    // PageRow.wideMode and ApplicationWindow.wideScreen are all read-only - they
    // are conclusions Kirigami draws, not switches - so what this actually does is
    // change the two things that are writable: the width the list column asks for,
    // and the size of the window. PageRow's own folding then follows, which is the
    // point of not hand-rolling it.
    readonly property bool compact: appSettings.compactMode

    // The size to go back to when compact mode is switched off. Captured on the
    // way in rather than guessed, so a maximised window does not come back as
    // whatever looked like a sensible default.
    property int roomyWidth: 0
    property int roomyHeight: 0

    function toggleCompact() {
        if (!root.compact) {
            root.roomyWidth = root.width
            root.roomyHeight = root.height
            appSettings.compactMode = true
            // The peer column is the third one, and compact mode does not have a
            // third one.
            while (root.pageStack.depth > 2)
                root.pageStack.pop()
            root.width = Kirigami.Units.gridUnit * 26
            root.height = Kirigami.Units.gridUnit * 30
            return
        }
        appSettings.compactMode = false
        if (root.roomyWidth > 0)
            root.width = root.roomyWidth
        if (root.roomyHeight > 0)
            root.height = root.roomyHeight
    }

    // Empty means no conversation is open, which the chat page draws as a
    // placeholder rather than an empty bubble list.
    property string currentPeerIp: ""
    readonly property string kSelfChatId: "__self__"
    property bool micMuted: false
    // Deafen silences what comes in as well as what goes out, so it implies
    // mute. The engine is told about both separately - see
    // VoiceCallManager::setDeafen - because un-deafening has to put the
    // microphone back the way the user left it, not simply unmute it.
    property bool deafened: false

    function toggleMic() {
        root.micMuted = !root.micMuted
        voiceCallManager.setMute(root.micMuted)
    }

    function toggleDeafen() {
        root.deafened = !root.deafened
        voiceCallManager.setDeafen(root.deafened)
    }

    // Whether the open conversation is scrolled to its newest message. A read
    // receipt is a claim that somebody read something, so it is only sent for a
    // message that was actually on the screen.
    property bool chatAtBottom: true

    // Who is typing, and until when. The peer sends a notice every few seconds
    // while it writes and nothing at all when it stops, so the timer is what
    // ends the state - waiting for a "stopped typing" that a lost datagram can
    // swallow leaves the dots up forever.
    property string typingChatId: ""

    Timer {
        id: typingTimeout
        // Comfortably longer than the composer's own notice interval, so a
        // steady writer never flickers.
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
            // Every message that lands in any chat, in either direction, from one
            // place. Wiring the conversation list at the four call sites that send
            // and receive instead would leave it wrong the first time a fifth one
            // was added.
            m.messageAdded.connect(root.onChatActivity)
            chatModels[ip] = m
        }
        return chatModels[ip]
    }

    function onChatActivity(chatId, preview, isOwn, ts) {
        chatList.noteMessage(chatId, preview, isOwn, ts)
    }

    // Everything the user has read in this chat is read, and the peer is told so.
    // Called both when a chat is opened and when a message arrives in the one
    // already open - the second of those is what was missing, and it is why an
    // outgoing message kept its "sent, not confirmed" arrow forever while the
    // peer sat reading it with the window open.
    function markChatRead(ip) {
        if (ip.length === 0 || ip === root.kSelfChatId)
            return
        // Read here means read, so the popup about it has served its purpose.
        notificationManager.clearChat(ip)
        root.modelForPeer(ip).markAllRead()
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

    // Who is reachable right now, straight off the presence broadcasts. This is a
    // scanner and is not what the sidebar draws - it is the source for
    // reachability, for the profile page, and for the peer picker in New chat.
    ListModel { id: peersModel }

    // What the sidebar draws: the conversations the user actually has. Survives
    // restarts and peers going offline, neither of which peersModel does.
    ChatListModel {
        id: chatList
        historyManager: HistoryManager
        unreadManager: UnreadManager
    }

    // Describes the peer at the top of ChatPage: username, os, e2e,
    // avatarLetter, isFavorites, online, lastSeen. peersModel.count is read only
    // to register a dependency, so this re-evaluates as peers come and go. Same
    // idea as above, on a ListModel instead of a Q_PROPERTY.
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
                    // What the peer says about itself, as opposed to whether it
                    // is reachable. Rides in the presence packet - see
                    // NetworkManager::setStatus.
                    statusEmoji: p.status_emoji || "",
                    presence: p.presence || 0,
                    // Being in peersModel is what reachable means: userOffline
                    // takes a peer out of it as soon as NetworkManager stops
                    // hearing presence.
                    online: true,
                    // Stamped by handlePresence() on arrival, not sent by the
                    // peer - see kFieldLastSeen in network/Protocol.h. While the
                    // peer is up it is always seconds old, so it only says
                    // anything once "online" above has gone false.
                    lastSeen: p.last_seen || 0
                }
            }
        }
        // Not on the network. The conversation list is what remembers a peer that
        // is switched off, which is the only thing that can still say when it was
        // last around.
        const known = chatList.chatInfo(ip)
        return {
            ip: ip,
            username: known.displayName || root.unknownPeerName,
            os: "",
            e2e: false,
            avatarLetter: (known.displayName || root.unknownPeerName).charAt(0).toUpperCase(),
            isFavorites: false,
            // A peer that is switched off is not saying anything about itself,
            // and the last thing it said is not worth showing as though it were
            // current.
            statusEmoji: "",
            presence: 0,
            online: false,
            lastSeen: known.lastSeenSecs || 0
        }
    }

    // Call windows. The incoming one is declared below instead: answering a call
    // is a question with two answers, which is a dialog rather than a window.
    property var outgoingCallWindow: null
    property var activeCallWindow: null

    // The friendly name if the peer publishes one, then the handle, and then
    // nothing. It used to fall through to the bare address, which put an IP in
    // the window title, in every notification and in the conversation list.
    // Same order the conversation list files a chat under, so opening a chat and
    // hearing presence from it do not disagree about what to call it.
    function peerDisplayName(ip) {
        for (let i = 0; i < peersModel.count; ++i) {
            const p = peersModel.get(i)
            if (p.ip === ip)
                return p.display_name || p.username || ""
        }
        return ""
    }

    // What to actually draw for a peer that has published no name. The address
    // is what this client routes on, but it is not what it should say out loud.
    readonly property string unknownPeerName: i18nc("@info a peer that has published no name of its own", "Unknown peer")

    function peerLabel(ip) {
        return root.peerDisplayName(ip) || root.unknownPeerName
    }

    function startOutgoingCall(ip) {
        if (root.outgoingCallWindow) return
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

    // Referenced by type rather than by resource path. The old
    // Qt.createComponent("qrc:/koutnet/app/qml/call/...") had the module's
    // internal resource layout written into three string literals.
    Component { id: outgoingCallComponent; OutgoingCallWindow {} }
    Component { id: activeCallComponent; ActiveCallWindow {} }

    // One card, moved to whatever was clicked, rather than one per row. It is
    // reparented by openAt(), so it must not be declared inside anything that
    // gets destroyed under it - a delegate, for instance.
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

    function showPeerCard(chatId, anchorItem) {
        if (chatId.length === 0 || chatId === root.kSelfChatId)
            return
        peerCard.openAt(anchorItem, root.peerInfoFor(chatId))
    }

    // Your own card, which is the peer card with your identity in it. Declared
    // beside that one and for the same reason: openAt() reparents it, so it must
    // not live inside anything that can be destroyed under it.
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
            // Opening a chat is starting one, as far as the list is concerned.
            chatList.openChat(currentPeerIp, root.peerDisplayName(currentPeerIp))
            root.markChatRead(currentPeerIp)
        }
    }

    // Opens a conversation and makes sure it is the column being looked at, which
    // on a narrow window means the list slides out of the way.
    function openChat(chatId) {
        root.currentPeerIp = chatId
        while (pageStack.layers.depth > 1)
            pageStack.layers.pop()
        pageStack.currentIndex = 1
    }

    // The peer column, which is the third one when it is there at all. Its
    // contents are bound to currentPeerIp rather than passed in, so switching
    // conversation with it open re-points it instead of leaving the last peer up.
    readonly property bool peerInfoOpen: pageStack.depth > 2

    function togglePeerInfo() {
        if (root.peerInfoOpen) {
            pageStack.pop()
            return
        }
        // Compact mode is two columns by definition. The actions that ask for
        // this are hidden there, so this is the belt to that pair of braces -
        // a keyboard shortcut or a peer card could still get here.
        if (root.compact)
            return
        pageStack.push(peerInfoComponent)
        pageStack.currentIndex = pageStack.depth - 1
    }

    // Everything that used to open a modal "not wired up yet" sheet says it here
    // instead. An InlineMessage in the window footer reports the same thing
    // without taking the window away from whoever was using it.
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

    // The wallpaper, behind everything. Local decoration: it is never put on the
    // wire, no peer is told it exists, and nothing outside this window reads the
    // setting.
    //
    // It shows through the conversation column, whose background is bound to the
    // same setting - see ChatPage. Every other surface stays opaque on purpose:
    // a form card or a toolbar over a photograph is a legibility problem, and the
    // conversation is both the largest surface and the one a wallpaper is for.
    //
    // Kirigami.Page already draws an opaque background, so that holds by itself
    // for every page and every layer. It stops holding the moment one of them
    // replaces the background with something of its own, which is what once put
    // this picture behind the whole of the profile page.
    //
    // z is negative so this sits under the page row, which is the window's other
    // child and has no z of its own.
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

        // The scrim. Its strength is the complement of the opacity setting, so
        // turning the wallpaper up turns the veil over it down - but it is floored
        // rather than allowed to reach zero. At zero the text in the conversation
        // sits directly on an arbitrary photograph, and no colour scheme can be
        // legible against every one of those.
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
        titleIcon: "io.github.bitzuka.KOutNet"
        // A drawer that stays put would eat a third of a narrow window, and the
        // two columns behind it are the content.
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
            // own checked property, which drops the binding to micMuted, and the
            // footer button would stop agreeing with the drawer. The label carries
            // the state instead.
            Kirigami.Action {
                text: root.micMuted
                    ? i18nc("@action:inmenu let your microphone be heard again", "Unmute microphone")
                    : i18nc("@action:inmenu silence your own microphone", "Mute microphone")
                icon.name: (root.micMuted || root.deafened) ? "microphone-sensitivity-muted" : "audio-input-microphone"
                // Deafened already holds the microphone down, so offering to
                // unmute it while it cannot be heard either way would be a
                // control that does nothing.
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
            // The profile is the first section of the settings page rather than
            // a page of its own - see SettingsPage - so this and the entry below
            // it open the same layer. Both are kept: "My profile" is what
            // somebody looking for their own name reaches for.
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
            // Not checkable, for the same reason as the microphone entry above:
            // triggering a checkable action writes its own checked property,
            // which drops the binding to the setting. The label carries it.
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

    // Status strip plus whatever the network last had to say. Two items in one
    // footer because the window has one footer slot; the message hides itself
    // when there is nothing to report, so the strip is normally all that shows.
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

        // A peer count is the first thing to go when the window is deliberately
        // small: it is the least of what is on the screen and it costs a whole
        // row.
        //
        // It also goes when there is nobody to count, which is the strip of
        // empty grey that used to sit under every page in the application. A
        // toolbar with an invisible label in it is not an empty toolbar: the
        // desktop style gives its background an implicit height of forty pixels
        // whatever is inside it, so hiding the only label left forty pixels of
        // nothing being held at the bottom of the window. Hiding the bar itself
        // takes the footer layout to zero, and a zero-height footer is no footer.
        QQC2.ToolBar {
            Layout.fillWidth: true
            visible: !root.compact && peersModel.count > 0
            position: QQC2.ToolBar.Footer

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                // The empty case used to say it was searching, which is true of
                // every second the application is running and so worth saying in
                // none of them.
                QQC2.Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    text: i18ncp("@info:status %1 is a number of peers",
                                 "%1 peer on the network", "%1 peers on the network", peersModel.count)
                }

                // No address here. This used to print the host's own IP in the
                // corner of every screenshot, and the microphone button that
                // sat beside it now lives next to the account row, which is
                // where somebody reaches for it.
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
            // The stamp is deliberately not touched here: the last presence that
            // arrived is when the peer was last seen, and "now" would be a lie
            // that grows into "last seen just now" for a peer that has gone.
            chatList.setPresence(ip, false, 0, "")
        }
        // The peer is writing. from_ip is what files it under a conversation;
        // the username in the packet is a string the peer chose for itself and
        // two of them can say the same thing.
        function onTyping(username, chatId, fromIp) {
            if (fromIp.length === 0)
                return
            root.typingChatId = fromIp
            typingTimeout.restart()
        }
        function onMessage(msg) {
            if (msg.type === "private") {
                root.modelForPeer(msg.from_ip).receiveMessage(msg.text, msg.from_ip)
                // Always told; the manager decides what that turns into. A
                // popup when the window is behind something, a sound when it
                // is in front, both when nobody is at the machine - whether
                // the application is active is a question about the process
                // rather than about this window, which is why it is asked
                // there and not here.
                notificationManager.notifyMessage(msg.from_ip,
                                                  root.peerLabel(msg.from_ip),
                                                  msg.text)
                // The message that was being typed has arrived.
                if (root.typingChatId === msg.from_ip)
                    root.typingChatId = ""
                // Reading it is what the peer is waiting to hear about - but
                // only if it was really read. A message that landed below the
                // fold of a conversation somebody scrolled up out of has not
                // been, and the receipt for it would be this client's own lie.
                if (msg.from_ip === root.currentPeerIp && root.chatAtBottom)
                    root.markChatRead(msg.from_ip)
            } else if (msg.type === "edit") {
                // The peer changed something it had already sent. msg_ts is the
                // stamp of the original, which is the only identifier the two
                // ends share - see NetworkManager::sendMessageEdit.
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
                // from_ip has been rewritten by dispatch() to the address this
                // peer is filed under, which is the same string the chat is keyed
                // on - see the note above the rewrite in NetworkManager.
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
            // The caller giving up has to take the question away as well. The old
            // dialog had a callRejected() for this that nothing ever called, so an
            // abandoned call left the prompt sitting there.
            incomingCall.callRejected()
            voiceCallManager.hangup(ip)
        }
        // These used to go nowhere, so a bind failure or a dropped tunnel
        // looked exactly like an idle network.
        function onErrorOccurred(message) {
            root.reportError(message)
        }
    }

    // The notification is the only part of this application that can be clicked
    // while the window is behind something else, so everything it offers has to
    // bring the window back first.
    Connections {
        target: notificationManager

        // The activation token the compositor wants is dealt with on the C++
        // side before these arrive - see NotificationManager::adoptActivationToken
        // - so requestActivate() here is allowed to do something.
        function onChatRequested(chatId) {
            root.show()
            root.raise()
            root.requestActivate()
            root.openChat(chatId)
        }
        function onReplyRequested(chatId, text) {
            if (text.trim().length === 0)
                return
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

    // The two places that guess at a file being a picture used to carry the same
    // six extensions each. They still guess, but only once.
    function looksLikeImage(path) {
        return /\.(png|jpg|jpeg|gif|bmp|webp)$/i.test(path)
    }

    // Two columns, always both present. defaultColumnWidth is what every column
    // gets unless it says otherwise, and "otherwise" is
    // Kirigami.ColumnView.fillWidth on the conversation - set on ChatPage below.
    // Being last is not enough on its own, which is what left the conversation
    // seventeen grid units wide with the rest of a full-screen window empty
    // beside it. PageRow folds to one column by itself once the window is
    // narrower than two of them.
    //
    // Handed the two page objects rather than two Components on purpose.
    // PageRow.initPage() instantiates a Component with
    // pageComp.createObject(pagesLogic, ...), and pagesLogic is a QtObject - so
    // the page is briefly a graphical item whose parent is not in the scene, and
    // QQmlComponent says so:
    //   Main.qml:...: QML ChatListPage: Created graphical object was not placed
    //   in the graphics scene.
    // It is adopted a line later by columnView.insertItem() and works fine, but
    // the warning is real and printed at every start. Given an Item instead,
    // getPageComponent() returns nothing, the createObject call never happens,
    // and the same insertItem picks the page up from the window's contentData.
    pageStack.initialPage: [chatListPage, chatPage]
    // The one number compact mode actually changes. Narrower than two of these
    // and PageRow folds to a single column on its own, which is why the compact
    // window size above is set relative to this rather than to nothing.
    pageStack.defaultColumnWidth: root.compact ? Kirigami.Units.gridUnit * 11
                                               : Kirigami.Units.gridUnit * 17
    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.ToolBar
    pageStack.globalToolBar.showNavigationButtons: Kirigami.ApplicationHeaderStyle.ShowBackButton

    // The tray, when there is one. trayIcon is null if the setting was off at
    // start - see the note in main.cpp about why it is not built later - so
    // everything that touches it is guarded.
    readonly property bool hasTray: typeof trayIcon !== "undefined" && trayIcon !== null

    // Bindings into the item rather than a Connections block: these are all
    // "what the window already knows, drawn on the icon", and a signal handler
    // per property would be four places for them to fall out of step.
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

    // Close means "put it away" while there is a tray to put it into, and means
    // close otherwise. main.cpp turns off quitOnLastWindowClosed, so the second
    // branch has to say Qt.quit() out loud - see the note there.
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
        onForgetRequested: (chatId) => {
            chatList.removeChat(chatId)
            if (root.currentPeerIp === chatId)
                root.currentPeerIp = ""
        }
        // The rail picks a mode; applying it is the same two calls the
        // settings page makes, because switching mode raises or drops the
        // relay tunnel and half of that is not a state to be in.
        onConnectionModeRequested: (mode) => {
            if (!networkManager.modeAvailable(mode))
                return
            appSettings.connectionMode = mode
            networkManager.setRelayServer(appSettings.relayHost, appSettings.relayPort, 0)
            networkManager.setConnectionMode(mode)
        }
    }

    ChatPage {
        id: chatPage

        readonly property bool isSelfChat: peerIp === root.kSelfChatId

        // The column that grows. Without this the conversation is handed
        // defaultColumnWidth like the list beside it - see the note above
        // pageStack.initialPage.
        Kirigami.ColumnView.fillWidth: true

        compact: root.compact
        peerIp: root.currentPeerIp
        peerInfo: root.currentPeerIp.length > 0 ? root.peerInfoFor(root.currentPeerIp) : null
        messagesModel: root.currentPeerIp.length > 0 ? root.modelForPeer(root.currentPeerIp) : null
        peerTyping: root.typingChatId.length > 0 && root.typingChatId === root.currentPeerIp
        // What the timeline highlights as a mention of the reader, and what it
        // signs the reader's own messages with.
        selfDisplayName: appSettings.displayName || appSettings.username
        selfAvatarSource: appSettings.avatarPath

        onAtBottomChanged: root.chatAtBottom = atBottom
        Component.onCompleted: root.chatAtBottom = atBottom

        onCallRequested: {
            if (!isSelfChat)
                root.startOutgoingCall(peerIp)
        }
        // The row goes in first and the datagram second, which is the order
        // the hourglass needs: appending after the write would leave nothing
        // on screen to be in flight. markSent() is what turns it into a tick.
        onSendRequested: function(text, replyExcerpt, replyAuthor, replyId) {
            // The quote is stored with the message but not put on the wire:
            // the protocol has no reply field, so sending one would only
            // teach the peer to ignore it.
            const stamp = messagesModel.sendMessage(text, replyExcerpt, replyAuthor, replyId)
            if (stamp === 0)
                return
            if (!isSelfChat)
                networkManager.sendPrivate(text, peerIp)
            messagesModel.markSent(stamp)
        }
        onAttachRequested: function(localFilePath) {
            const stamp = messagesModel.sendFile(localFilePath, root.looksLikeImage(localFilePath))
            if (!isSelfChat)
                networkManager.sendFile(peerIp, localFilePath)
            messagesModel.markSent(stamp)
        }
        onTypingNotice: {
            if (!isSelfChat)
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
        // The page has already changed its own copy by the time these arrive -
        // it is the one holding the model. What is left is telling the peer,
        // which needs the address, and only the window has that. The stamp is
        // the identifier both ends agree on; see NetworkManager::sendMessageEdit.
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
        id: settingsPageComponent

        SettingsPage {
            onSaved: root.notify(i18nc("@info:status", "Settings saved"), Kirigami.MessageType.Positive)
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

    // The welcome screen is a layer rather than an item pinned over the window
    // overlay. The old arrangement needed a negative z to stay out of the way of
    // every popup in the application, and got it wrong twice.
    Component {
        id: welcomeComponent

        WelcomeScreen {
            onContinueRequested: root.pageStack.layers.pop()
            onAboutRequested: root.pageStack.layers.push(aboutPageComponent)
        }
    }
}
