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

    property string currentPeerIp: ""
    readonly property bool compactMode: width < 480
    readonly property string kSelfChatId: "__self__"

    property bool sidebarCollapsed: false
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
    }

    // ── Generic "not wired up yet" info sheet — used instead of silent
    //    no-op buttons so clicking always gives feedback. Replace each
    //    call site with the real feature once its backend exists. ──
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

    // ── Menu bar — mirrors legacy _setup_menubar (File / View / Calls / Help) ──
    menuBar: MenuBar {
        background: Rectangle { color: root.theme.header_bg }

        Menu {
            title: Translations.t("menu.file")
            MenuItem {
                text: Translations.t("menu.my_profile")
                onTriggered: root.showStub(
                    Translations.t("menu.my_profile"),
                    Translations.t("profile_not_ported"))
            }
            MenuItem { text: Translations.t("menu.settings"); onTriggered: settingsSheet.open() }
            MenuSeparator {}
            MenuItem {
                text: Translations.t("menu.check_updates")
                onTriggered: root.showStub(
                    Translations.t("menu.check_updates"),
                    Translations.t("updates_not_ported"))
            }
            MenuSeparator {}
            MenuItem { text: Translations.t("menu.quit"); onTriggered: Qt.quit() }
        }

        Menu {
            title: Translations.t("menu.view")

            Menu {
                title: Translations.t("menu.themes")
                Instantiator {
                    model: ThemeManager.availableThemes
                    delegate: MenuItem {
                        text: ThemeManager.themeLabel(modelData)
                        onTriggered: ThemeManager.currentTheme = modelData
                    }
                    onObjectAdded: (index, object) => parent.insertItem(index, object)
                    onObjectRemoved: (index, object) => parent.removeItem(object)
                }
            }

            MenuSeparator {}
            MenuItem {
                text: Translations.t("menu.public_chat")
                onTriggered: root.currentPeerIp = "public"
            }
            MenuItem {
                text: Translations.t("tab_player_violla")
                onTriggered: tabBar.currentIndex = 3
            }
            MenuSeparator {}
            MenuItem {
                text: Translations.t("menu.fullscreen")
                onTriggered: root.visibility = (root.visibility === Window.FullScreen)
                    ? Window.Windowed : Window.FullScreen
            }
            MenuItem { text: Translations.t("menu.lang_ru"); onTriggered: Translations.current = "ru" }
            MenuItem { text: Translations.t("menu.lang_en"); onTriggered: Translations.current = "en" }
            MenuItem { text: Translations.t("menu.lang_ja"); onTriggered: Translations.current = "ja" }
        }

        Menu {
            title: Translations.t("menu.calls")
            MenuItem {
                text: Translations.t("menu.mute_toggle")
                checkable: true
                checked: root.micMuted
                onTriggered: {
                    root.micMuted = !root.micMuted
                    voiceCallManager.setMute(root.micMuted)
                }
            }
            MenuItem {
                text: Translations.t("menu.hangup_all")
                onTriggered: {
                    voiceCallManager.hangupAll()
                    if (root.activeCallWindow) { root.activeCallWindow.close(); root.activeCallWindow = null }
                    if (root.outgoingCallWindow) { root.outgoingCallWindow.close(); root.outgoingCallWindow = null }
                }
            }
        }

        Menu {
            title: Translations.t("menu.help")
            MenuItem { text: Translations.t("menu.about"); onTriggered: aboutSheet.open() }
            MenuSeparator {}
            MenuItem {
                text: Translations.t("menu.terminal")
                onTriggered: root.showStub(
                    Translations.t("menu.terminal"),
                    Translations.t("terminal_not_ported"))
            }
            MenuItem {
                text: Translations.t("tab_wns_keenly")
                onTriggered: tabBar.currentIndex = 4
            }
            MenuSeparator {}
            MenuItem {
                text: Translations.t("menu.tutorial")
                onTriggered: root.showStub(
                    Translations.t("menu.tutorial"),
                    Translations.t("tutorial_not_ported"))
            }
        }
    }

    // ── Status bar — mirrors legacy _setup_statusbar ──
    footer: Rectangle {
        implicitHeight: 26
        color: root.theme.header_bg

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Kirigami.Units.smallSpacing
            anchors.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.largeSpacing

            Label {
                text: Translations.t("status.searching")
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
                text: root.micMuted ? Translations.t("mic.off") : Translations.t("mic.on")
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
                text: Translations.t("status.no_calls")
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

        SplashScreen {
            anchors.fill: parent
        }

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

            // ── Left: peer panel (legacy: fixed 280px) ──
            ColumnLayout {
                id: sidebarColumn
                Layout.preferredWidth: root.sidebarCollapsed ? 0 : 280
                Layout.minimumWidth: 0
                Layout.maximumWidth: 320
                Layout.fillHeight: true
                clip: true
                spacing: 0

                Behavior on Layout.preferredWidth {
                    NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
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
                                text: Translations.t("contacts_header")
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
                            peerIp: Translations.t("sidebar.favorites")
                            iconName: "bookmarks"
                            showOnlineIndicator: false
                            showSecurityLabel: false
                            selected: root.currentPeerIp === root.kSelfChatId
                            onClicked: root.currentPeerIp = root.kSelfChatId
                        }

                        // Themed search field — background/border/placeholder
                        // now follow ThemeManager instead of the system
                        // palette, which is why it used to stay the same
                        // colour when switching themes.
                        TextField {
                            id: searchField
                            Layout.fillWidth: true
                            Layout.margins: Kirigami.Units.smallSpacing
                            placeholderText: Translations.t("sidebar.search_placeholder")
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
                                text: Translations.t("no_contacts_title")
                                explanation: Translations.t("no_contacts_explanation")
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
                visible: !root.sidebarCollapsed
            }

            // ── Right: permanent tab strip (legacy: QTabWidget, 5 tabs) ──
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                TabBar {
                    id: tabBar
                    Layout.fillWidth: true
                    implicitHeight: 32

                    background: Rectangle { color: root.theme.header_bg }

                    TabButton {
                        text: Translations.t("tab_main_chat")
                        background: Rectangle { color: checked ? root.theme.bg : "transparent" }
                    }
                    TabButton {
                        text: Translations.t("tab_main_notes")
                        background: Rectangle { color: checked ? root.theme.bg : "transparent" }
                    }
                    TabButton {
                        text: Translations.t("tab_main_calls")
                        background: Rectangle { color: checked ? root.theme.bg : "transparent" }
                    }
                    TabButton {
                        // Violla — media player tab (was hardcoded "♫")
                        text: Translations.t("tab_player_violla")
                        background: Rectangle { color: checked ? root.theme.bg : "transparent" }
                    }
                    TabButton {
                        // Keenly — internal browser tab (was hardcoded "🌐 WNS")
                        text: Translations.t("tab_wns_keenly")
                        background: Rectangle { color: checked ? root.theme.bg : "transparent" }
                    }
                }

                // StackLayout swaps children instantly with no transition,
                // which is what made switching tabs feel stiff. Cheap fix
                // without ripping TabBar+StackLayout out for a SwipeView:
                // fade content out/in around the index change.
                StackLayout {
                    id: contentStack
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: tabBar.currentIndex

                    opacity: 1.0
                    Behavior on opacity {
                        NumberAnimation { duration: 110; easing.type: Easing.OutQuad }
                    }

                    Loader {
                        visible: !root.compactMode || root.currentPeerIp.length > 0
                        sourceComponent: root.currentPeerIp.length > 0 ? chatComponent : placeholderComponent
                    }
                    NotesTab {}
                    CallsTab {}
                    PlayerTab {}
                    WnsTab {}
                }

                Connections {
                    target: tabBar
                    function onCurrentIndexChanged() {
                        contentStack.opacity = 0
                        fadeInTimer.restart()
                    }
                }
                Timer {
                    id: fadeInTimer
                    interval: 20
                    onTriggered: contentStack.opacity = 1
                }
            }
        }

        Rectangle {
            id: collapseButton
            width: 32
            height: 32
            radius: 4
            color: hamburgerMouse.containsMouse ? root.theme.btn_hover : "transparent"
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 4
            z: 10

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
                onClicked: root.sidebarCollapsed = !root.sidebarCollapsed
            }
        }
    }

    Kirigami.OverlaySheet {
        id: settingsSheet
        title: Translations.t("sidebar.settings")

        ColumnLayout {
            width: parent.width

            Label { text: Translations.t("settings.username"); color: root.theme.text }
            TextField {
                Layout.fillWidth: true
                text: appSettings.username
                onEditingFinished: appSettings.username = text
            }

            Label { text: Translations.t("menu.language"); color: root.theme.text }
            ComboBox {
                Layout.fillWidth: true
                model: Translations.availableLanguages
                currentIndex: model.indexOf(Translations.current)
                onActivated: Translations.current = model[currentIndex]
            }

            Label { text: Translations.t("settings.theme"); color: root.theme.text }
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
        title: Translations.t("menu.about")
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
            displayTitle: isSelfChat ? Translations.t("sidebar.favorites") : root.currentPeerIp
            messagesModel: root.modelForPeer(root.currentPeerIp)
            showBackButton: root.compactMode

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
            // ChatModel has no delete/forward API yet (see ChatModel.h) —
            // tell the user honestly instead of pretending it worked.
            onForwardRequested: root.showStub(
                Translations.t("msg_forward"),
                Translations.t("forward_not_ported"))
            onDeleteRequested: root.showStub(
                Translations.t("msg_delete"),
                Translations.t("delete_not_ported"))
        }
    }

    Component {
        id: placeholderComponent
        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            text: Translations.t("select_contact_placeholder")
        }
    }
}
