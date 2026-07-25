// KOutNet — Main application window
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import koutnet.app

Kirigami.ApplicationWindow {
    id: root

    title: "KOutNet"
    width: 1000
    height: 650
    minimumWidth: 480
    minimumHeight: 360
    visible: false

    readonly property var theme: ThemeManager.colors

    // Q_INVOKABLE calls (Translations.t) don't register as QML binding
    // dependencies — only reading a NOTIFYable Q_PROPERTY does. Reading
    // Translations.current here (via the comma operator) forces every
    // binding that calls tr() to also depend on "current", so switching
    // language actually re-evaluates the UI instead of silently doing
    // nothing everywhere t() was called directly.
    function tr(key) {
        return (Translations.current, Translations.t(key))
    }

    readonly property var languageLabels: ({
        ru: "Русский", en: "English", ja: "日本語", ar: "العربية",
        de: "Deutsch", es: "Español", fr: "Français", hi: "हिन्दी",
        it: "Italiano", pl: "Polski", pt: "Português", tr: "Türkçe",
        uk: "Українська", zh: "中文"
    })
    function languageLabel(code) {
        return root.languageLabels[code] || code.toUpperCase()
    }

    property string currentPeerIp: ""
    readonly property bool compactMode: width < 480
    readonly property string kSelfChatId: "__self__"

    // Overlay sidebar (Telegram-style): fixed width, slides over content
    // on the X axis instead of resizing a Layout column. The old
    // push-layout approach animated Layout.preferredWidth, which forced
    // the whole content column (and any text inside, e.g. the "no
    // contacts" placeholder) to reflow every animation frame — that was
    // the source of the jumping/shifting reported when picking a chat.
    property bool sidebarOpen: true
    property string contactSearchText: ""
    property bool micMuted: false

    property var chatModels: ({})

    function modelForPeer(ip) {
        if (!chatModels[ip]) {
            const m = Qt.createQmlObject(
                'import koutnet.app; ChatModel {}', root, "dynamicChatModel")
            m.historyManager = HistoryManager
            m.reactionStore = ReactionStore
            m.unreadManager = UnreadManager
            m.chatId = ip
            chatModels[ip] = m
        }
        return chatModels[ip]
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

    // Returns a plain object describing the peer shown at the top of
    // ChatPage: { username, os, e2e, avatarLetter, isFavorites, lastSeen }.
    // peersModel.count is read here purely to register a dependency so
    // this re-evaluates when peers come/go while a chat is open — the
    // same trick as tr() above, applied to a ListModel instead of a
    // Q_PROPERTY.
    function peerInfoFor(ip) {
        /* eslint-disable no-unused-expressions */
        peersModel.count
        if (ip === root.kSelfChatId) {
            return {
                username: root.tr("sidebar.favorites"),
                os: "",
                e2e: false,
                avatarLetter: "★",
                isFavorites: true,
                lastSeen: 0
            }
        }
        for (let i = 0; i < peersModel.count; ++i) {
            const p = peersModel.get(i)
            if (p.ip === ip) {
                return {
                    username: p.username || ip,
                    os: p.os || "",
                    e2e: p.e2e === true,
                    avatarLetter: (p.username || ip).charAt(0).toUpperCase(),
                    isFavorites: false,
                    // NOTE: field name "last_seen" is assumed from the
                    // legacy Python payload shape; confirm against
                    // NetworkManager.h if this doesn't populate.
                    lastSeen: p.last_seen || 0
                }
            }
        }
        return { username: ip, os: "", e2e: false, avatarLetter: "?", isFavorites: false, lastSeen: 0 }
    }

    // ── Call windows (Outgoing / Incoming / Active) ──
    property var outgoingCallWindow: null
    property var incomingCallDialog: null
    property var activeCallWindow: null

    function peerDisplayName(ip) {
        for (let i = 0; i < peersModel.count; ++i) {
            if (peersModel.get(i).ip === ip)
                return peersModel.get(i).username || ip
        }
        return ip
    }

    function startOutgoingCall(ip) {
        if (root.outgoingCallWindow) return
        networkManager.sendCallRequest(ip)
        const comp = Qt.createComponent("qrc:/koutnet/app/qml/call/OutgoingCallWindow.qml")
        const win = comp.createObject(root, { peerName: root.peerDisplayName(ip), peerIp: ip })
        win.cancelled.connect(function() {
            networkManager.sendCallEnd(ip)
            root.outgoingCallWindow = null
        })
        root.outgoingCallWindow = win
    }

    function showIncomingCall(username, ip) {
        if (root.incomingCallDialog) return
        const comp = Qt.createComponent("qrc:/koutnet/app/qml/call/IncomingCallDialog.qml")
        const dlg = comp.createObject(root, { callerName: username, callerIp: ip })
        dlg.accepted.connect(function() {
            networkManager.sendCallAccept(ip)
            voiceCallManager.call(ip)
            root.openActiveCall(username, ip)
            root.incomingCallDialog = null
        })
        dlg.rejected.connect(function() {
            networkManager.sendCallReject(ip)
            root.incomingCallDialog = null
        })
        root.incomingCallDialog = dlg
    }

    function openActiveCall(username, ip) {
        if (root.outgoingCallWindow) {
            root.outgoingCallWindow.close()
            root.outgoingCallWindow = null
        }
        if (root.activeCallWindow) return
        const comp = Qt.createComponent("qrc:/koutnet/app/qml/call/ActiveCallWindow.qml")
        const win = comp.createObject(root, { peerName: username, peerIp: ip })
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

    onCurrentPeerIpChanged: {
        if (currentPeerIp.length > 0 && currentPeerIp !== kSelfChatId)
            networkManager.sendReadReceipt(currentPeerIp, "dm")
        // Telegram-style: picking a chat auto-hides the overlay sidebar.
        if (currentPeerIp.length > 0)
            root.sidebarOpen = false
    }

    Shortcut {
        sequence: "F11"
        onActivated: root.visibility = (root.visibility === Window.FullScreen)
            ? Window.Windowed : Window.FullScreen
    }

    Shortcut {
        sequence: "Tab"
        onActivated: root.sidebarOpen = !root.sidebarOpen
    }

    // ── Generic "not wired up yet" info sheet ──
    function showStub(titleText, bodyText) {
        stubSheet.title = titleText
        stubBody.text = bodyText
        stubSheet.open()
    }

    Kirigami.OverlaySheet {
        id: stubSheet
        Label {
            id: stubBody
            width: parent.width
            wrapMode: Text.WordWrap
            color: root.theme.text
        }
    }

    // ── Menu bar ──
    menuBar: MenuBar {
        background: Rectangle { color: root.theme.header_bg }

        Menu {
            title: root.tr("menu.file")
            MenuItem {
                text: root.tr("menu.my_profile")
                onTriggered: root.showStub(root.tr("menu.my_profile"), root.tr("profile_not_ported"))
            }
            MenuItem { text: root.tr("menu.settings"); onTriggered: settingsSheet.open() }
            MenuSeparator {}
            MenuItem { text: root.tr("menu.quit"); onTriggered: Qt.quit() }
        }

        Menu {
            title: root.tr("menu.view")

            Menu {
                title: root.tr("menu.themes")
                Repeater {
                    model: ThemeManager.availableThemes
                    MenuItem {
                        text: ThemeManager.themeLabel(modelData)
                        onTriggered: ThemeManager.currentTheme = modelData
                    }
                }
            }

            MenuSeparator {}

            MenuItem {
                text: root.tr("tab_player_violla")
                onTriggered: tabStrip.currentIndex = 3
            }

            MenuSeparator {}

            MenuItem {
                text: root.tr("menu.fullscreen") + "  (F11)"
                onTriggered: root.visibility = (root.visibility === Window.FullScreen)
                    ? Window.Windowed : Window.FullScreen
            }

            MenuSeparator {}

            Menu {
                id: langMenu
                title: root.tr("lang_choose")
                Instantiator {
                    model: Translations.availableLanguages
                    delegate: MenuItem {
                        text: root.languageLabel(modelData)
                        checkable: true
                        checked: Translations.current === modelData
                        onTriggered: {
                        Translations.current = modelData
                        appSettings.language = modelData
                    }
                    }
                    onObjectAdded: (index, object) => langMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => langMenu.removeItem(object)
                }
            }
        }

        Menu {
            title: root.tr("menu.calls")
            MenuItem {
                text: root.tr("menu.mute_toggle")
                checkable: true
                checked: root.micMuted
                onTriggered: {
                    root.micMuted = !root.micMuted
                    voiceCallManager.setMute(root.micMuted)
                }
            }
            MenuItem {
                text: root.tr("menu.hangup_all")
                onTriggered: {
                    voiceCallManager.hangupAll()
                    if (root.activeCallWindow) { root.activeCallWindow.close(); root.activeCallWindow = null }
                    if (root.outgoingCallWindow) { root.outgoingCallWindow.close(); root.outgoingCallWindow = null }
                }
            }
        }

        Menu {
            title: root.tr("menu.help")
            MenuItem { text: root.tr("menu.about"); onTriggered: aboutSheet.open() }
            MenuItem {
                text: root.tr("menu.tutorial")
                onTriggered: root.showStub(root.tr("menu.tutorial"), root.tr("tutorial_not_ported"))
            }
        }
    }

    // ── Status bar ──
    footer: Rectangle {
        implicitHeight: 26
        color: root.theme.header_bg

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Kirigami.Units.smallSpacing
            anchors.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.largeSpacing

            Label {
                text: root.tr("status.searching")
                color: root.theme.text_dim
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                Layout.fillWidth: true
            }

            Label {
                text: "IP: " + (networkManager.localIp || "—")
                color: root.theme.text_dim
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }

            Label {
                text: root.micMuted ? root.tr("mic.off") : root.tr("mic.on")
                color: root.micMuted ? "#FF8080" : "#80FF80"
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        root.micMuted = !root.micMuted
                        voiceCallManager.setMute(root.micMuted)
                    }
                }
            }

            Label {
                text: root.tr("status.no_calls")
                color: root.theme.text_dim
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }
        }
    }

    Window {
        id: splash
        width: 616
        height: 338
        visible: true
        flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
        color: "transparent"
        title: "KOutNet"

        Component.onCompleted: {
            x = Screen.width / 2 - width / 2
            y = Screen.height / 2 - height / 2
        }

        SplashScreen { anchors.fill: parent }

        Timer {
            interval: 2200
            running: true
            onTriggered: {
                splash.visible = false
                root.visible = true
            }
        }
    }

    Connections {
        target: networkManager
        function onUserOnline(peerInfo) { root.upsertPeer(peerInfo) }
        function onUserOffline(ip) { root.removePeer(ip) }
        function onMessage(msg) {
            if (msg.type === "private")
                root.modelForPeer(msg.from_ip).receiveMessage(msg.text, msg.from_ip)
            else if (msg.type === "read")
                root.modelForPeer(msg.from_ip).markOwnMessagesRead()
        }
        function onCallRequest(username, ip) { root.showIncomingCall(username, ip) }
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
            if (root.outgoingCallWindow) { root.outgoingCallWindow.close(); root.outgoingCallWindow = null }
            if (root.activeCallWindow) { root.activeCallWindow.close(); root.activeCallWindow = null }
            voiceCallManager.hangup(ip)
        }
    }

    Connections {
        target: fileTransferHandler
        function onFileSaved(meta, localPath) {
            const fromIp = meta.from_ip
            if (!fromIp) return
            const lower = localPath.toLowerCase()
            const isImage = lower.endsWith(".png") || lower.endsWith(".jpg")
                            || lower.endsWith(".jpeg") || lower.endsWith(".gif")
                            || lower.endsWith(".bmp") || lower.endsWith(".webp")
            root.modelForPeer(fromIp).receiveFile(localPath, isImage, fromIp)
        }
    }

    pageStack.initialPage: mainPage

    Kirigami.Page {
        id: mainPage
        globalToolBarStyle: Kirigami.ApplicationHeaderStyle.None
        title: "KOutNet"
        padding: 0
        background: Rectangle { color: root.theme.bg }

        RowLayout {
            anchors.fill: parent
            spacing: 0

            // ── Sidebar (pushing) ──
            ColumnLayout {
                Layout.preferredWidth: root.sidebarOpen ? 280 : 0
                Layout.minimumWidth: 0
                Layout.maximumWidth: 320
                Layout.fillHeight: true
                clip: true
                spacing: 0

                Behavior on Layout.preferredWidth {
                    NumberAnimation { duration: 150; easing.type: Easing.InOutQuad }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.theme.bg2

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.margins: Kirigami.Units.smallSpacing
                            Layout.leftMargin: Kirigami.Units.smallSpacing + 36

                            Kirigami.Heading {
                                text: root.tr("contacts_header")
                                level: 1
                                font.bold: true
                                font.weight: Font.Black
                                color: root.theme.text
                            }
                            Item { Layout.fillWidth: true }
                        }

                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: root.theme.border }

                        ContactDelegate {
                            Layout.fillWidth: true
                            peerIp: root.tr("sidebar.favorites")
                            iconName: "bookmarks"
                            showOnlineIndicator: false
                            showSecurityLabel: false
                            selected: root.currentPeerIp === root.kSelfChatId
                            onClicked: root.currentPeerIp = root.kSelfChatId
                        }

                        TextField {
                            id: searchField
                            Layout.fillWidth: true
                            Layout.margins: Kirigami.Units.smallSpacing
                            placeholderText: root.tr("sidebar.search_placeholder")
                            text: root.contactSearchText
                            color: root.theme.text
                            placeholderTextColor: root.theme.text_dim
                            selectionColor: root.theme.accent
                            leftPadding: 10
                            rightPadding: 10
                            onTextChanged: root.contactSearchText = text

                            background: Rectangle {
                                radius: 6
                                color: root.theme.bg3
                                border.width: 1
                                border.color: searchField.activeFocus ? root.theme.accent : root.theme.border
                            }
                        }

                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: root.theme.border }

                        ListView {
                            id: peersList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            model: peersModel
                            clip: true

                            Kirigami.PlaceholderMessage {
                                anchors.centerIn: parent
                                width: parent.width - Kirigami.Units.largeSpacing * 2
                                visible: peersList.count === 0
                                text: root.tr("no_contacts_title")
                                explanation: root.tr("no_contacts_explanation")
                            }

                            delegate: ContactDelegate {
                                width: peersList.width
                                visible: root.contactSearchText.length === 0
                                         || model.ip.toLowerCase().indexOf(root.contactSearchText.toLowerCase()) !== -1
                                height: visible ? 60 : 0
                                peerIp: model.ip
                                peerOs: model.os || ""
                                e2e: model.e2e === true
                                selected: model.ip === root.currentPeerIp
                                onClicked: root.currentPeerIp = model.ip
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillHeight: true
                implicitWidth: 1
                color: root.theme.border
                visible: root.sidebarOpen
            }

            // ── Right: content + tabs ──
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    id: tabStrip
                    Layout.fillWidth: true
                    implicitHeight: 32
                    color: root.theme.header_bg

                    property int currentIndex: 0
                    readonly property var tabLabels: [
                        root.tr("tab_main_chat"),
                        root.tr("tab_main_notes"),
                        root.tr("tab_main_calls"),
                        root.tr("tab_player_violla"),
                        root.tr("tab_wns_keenly"),
                    ]

                    RowLayout {
                        anchors.fill: parent
                        spacing: 0

                        Item { Layout.preferredWidth: root.sidebarOpen ? 4 : 40; Layout.fillHeight: true }

                        Repeater {
                            model: tabStrip.tabLabels

                            delegate: Rectangle {
                                Layout.fillHeight: true
                                Layout.preferredWidth: tabLabel.implicitWidth + 24
                                color: tabStrip.currentIndex === index
                                    ? root.theme.bg
                                    : (tabMouse.containsMouse ? root.theme.btn_hover : root.theme.header_bg)

                                Text {
                                    id: tabLabel
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: tabStrip.currentIndex === index ? root.theme.accent : root.theme.text_dim
                                    font.bold: tabStrip.currentIndex === index
                                }

                                Rectangle {
                                    visible: tabStrip.currentIndex === index
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 2
                                    color: root.theme.accent
                                }

                                MouseArea {
                                    id: tabMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: tabStrip.currentIndex = index
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: tabStrip.currentIndex

                    Loader {
                        visible: !root.compactMode || root.currentPeerIp.length > 0
                        sourceComponent: root.currentPeerIp.length > 0 ? chatComponent : placeholderComponent
                    }
                    NotesTab {}
                    CallsTab {}
                    PlayerTab {}
                    WnsTab {}
                }
            }
        }

        // Hamburger toggle — top-left, inside sidebar when open, over content when closed
        Rectangle {
            id: collapseButton
            width: 32
            height: 32
            radius: 4
            color: hamburgerMouse.containsMouse ? root.theme.btn_hover : "transparent"
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: 4
            anchors.leftMargin: root.sidebarOpen ? 4 : 4
            z: 30

            Column {
                anchors.centerIn: parent
                spacing: 3
                Repeater {
                    model: 3
                    Rectangle { width: 16; height: 2; radius: 1; color: root.theme.text }
                }
            }

            MouseArea {
                id: hamburgerMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.sidebarOpen = !root.sidebarOpen
            }
        }
    }

    Kirigami.OverlaySheet {
        id: settingsSheet
        title: root.tr("sidebar.settings")

        ColumnLayout {
            width: parent.width

            Label { text: root.tr("settings.username"); color: root.theme.text }
            TextField {
                Layout.fillWidth: true
                text: appSettings.username
                onEditingFinished: appSettings.username = text
            }

            Label { text: root.tr("settings.theme"); color: root.theme.text }
            ComboBox {
                id: themeCombo
                Layout.fillWidth: true
                model: ThemeManager.availableThemes
                displayText: ThemeManager.themeLabel(ThemeManager.currentTheme)
                currentIndex: model.indexOf(ThemeManager.currentTheme)
                delegate: ItemDelegate {
                    width: themeCombo.width
                    text: ThemeManager.themeLabel(modelData)
                }
                onActivated: ThemeManager.currentTheme = model[currentIndex]
            }
        }
    }

    Kirigami.OverlaySheet {
        id: aboutSheet
        title: root.tr("menu.about")
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: "KOutNet — P2P encrypted messenger"
            color: root.theme.text
        }
    }

    Component {
        id: chatComponent
        ChatPage {
            readonly property bool isSelfChat: peerIp === root.kSelfChatId

            peerIp: root.currentPeerIp
            peerInfo: root.peerInfoFor(root.currentPeerIp)
            showBackButton: root.compactMode
            messagesModel: root.modelForPeer(root.currentPeerIp)

            onReturnToListRequested: root.currentPeerIp = ""
            onCallRequested: {
                if (!isSelfChat)
                    root.startOutgoingCall(peerIp)
            }
            onSendRequested: function(text) {
                if (!isSelfChat)
                    networkManager.sendPrivate(text, peerIp)
                messagesModel.sendMessage(text)
            }
            onAttachRequested: function(localFilePath) {
                const lower = localFilePath.toLowerCase()
                const isImage = lower.endsWith(".png") || lower.endsWith(".jpg")
                                || lower.endsWith(".jpeg") || lower.endsWith(".gif")
                                || lower.endsWith(".bmp") || lower.endsWith(".webp")
                if (!isSelfChat)
                    networkManager.sendFile(peerIp, localFilePath)
                messagesModel.sendFile(localFilePath, isImage)
            }
            onForwardRequested: root.showStub(root.tr("msg_forward"), root.tr("forward_not_ported"))
            onDeleteRequested: root.showStub(root.tr("msg_delete"), root.tr("delete_not_ported"))
        }
    }

    Component {
        id: placeholderComponent
        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            text: root.tr("select_contact_placeholder")
        }
    }
}
