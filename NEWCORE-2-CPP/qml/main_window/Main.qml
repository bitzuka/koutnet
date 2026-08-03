// SPDX-FileCopyrightText: 2026 bitzuka <matveypotyzhno@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - Main application window
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import koutnet.app

Kirigami.ApplicationWindow {
    id: root

    title: welcomeLoader.active ? root.tr("welcome.title") : "KOutNet"
    width: 1000
    height: 650
    minimumWidth: 480
    minimumHeight: 360
    visible: true

    readonly property var theme: ThemeManager.colors

    // Q_INVOKABLE calls do not register as binding dependencies, only
    // NOTIFYable properties do. Reading Translations.current through the
    // comma operator makes every tr() binding depend on it, so a language
    // switch actually re-evaluates the UI.
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

    // Prepends a "system default" row so an empty saved device id still
    // selects something instead of leaving the combo blank.
    function deviceList(devices) {
        var out = [{ id: "", description: root.tr("settings.device_default") }]
        for (var i = 0; i < devices.length; ++i)
            out.push(devices[i])
        return out
    }

    property string currentPeerIp: ""
    readonly property bool compactMode: width < 480
    readonly property string kSelfChatId: "__self__"

    // Overlay sidebar: fixed width, slides over the content on X rather
    // than resizing a Layout column. Animating Layout.preferredWidth
    // instead reflows the whole content column every frame, text included.
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

    // Describes the peer at the top of ChatPage: username, os, e2e,
    // avatarLetter, isFavorites, lastSeen. peersModel.count is read only to
    // register a dependency, so this re-evaluates as peers come and go. Same
    // trick as tr() above, on a ListModel instead of a Q_PROPERTY.
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
                    ip: ip,
                    username: p.username || ip,
                    displayName: p.display_name || "",
                    bio: p.bio || "",
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

    // Call windows (Outgoing / Incoming / Active)
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

    // Generic "not wired up yet" info sheet
    function showStub(titleText, bodyText) {
        stubSheet.title = titleText
        stubBody.text = bodyText
        stubSheet.open()
    }

    Kirigami.OverlaySheet {
        id: stubSheet
        Label {
            id: stubBody
            width: Kirigami.Units.gridUnit * 20
            wrapMode: Text.WordWrap
            color: root.theme.text
        }
    }

    // Menu bar
    menuBar: MenuBar {
        background: Rectangle { color: root.theme.header_bg }

        Menu {
            title: root.tr("menu.file")
            MenuItem {
                text: root.tr("menu.my_profile")
                onTriggered: yourProfileSheet.open()
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
                        text: root.tr("theme." + modelData)
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
                        root.showStub(root.tr("lang_choose"), root.tr("lang_restart_notice"))
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

    // Status bar
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
                text: root.tr("status.ip_label") + (networkManager.localIp || "—")
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

    // Covers the whole window until the user clicks Continue, then unloads
    // itself. Parented to the window overlay so it sits above the menu bar and
    // the page content without fighting ApplicationWindow's own layout.
    Loader {
        id: welcomeLoader
        parent: root.overlay
        anchors.fill: parent
        z: 1000
        active: appSettings.showWelcome

        sourceComponent: WelcomeScreen {
            languageLabels: root.languageLabels
            onContinueRequested: welcomeLoader.active = false
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

            // Sidebar (pushing)
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

            // Right: content + tabs
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

        // Hamburger toggle - top-left, inside sidebar when open, over content when closed
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
        id: yourProfileSheet
        title: root.tr("menu.my_profile")

        YourProfile {
            onStubRequested: (title, body) => root.showStub(title, body)
        }
    }

    Kirigami.OverlaySheet {
        id: otherProfileSheet
        property var peer: null
        title: otherProfileSheet.peer ? otherProfileSheet.peer.username : ""

        OtherProfile { peer: otherProfileSheet.peer }
    }

    Kirigami.OverlaySheet {
        id: settingsSheet
        title: root.tr("settings.title")

        // Relay and maintainer VDS are the two that route through a relay,
        // so they are the two that need a host and port.
        readonly property bool usesRelay: appSettings.connectionMode === 3
                                       || appSettings.connectionMode === 4

        // Leaving the mic open after the dialog closes would hold the device
        // against the next call.
        onClosed: audioDevices.stopMicTest()

        ColumnLayout {
            width: Kirigami.Units.gridUnit * 26
            spacing: Kirigami.Units.smallSpacing

            TabBar {
                id: settingsTabs
                Layout.fillWidth: true
                TabButton { text: root.tr("settings.tab_general") }
                TabButton { text: root.tr("settings.tab_audio") }
                TabButton { text: root.tr("settings.tab_network") }
            }

            StackLayout {
                Layout.fillWidth: true
                currentIndex: settingsTabs.currentIndex

                ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label { text: root.tr("settings.username"); color: root.theme.text }
                    TextField {
                        Layout.fillWidth: true
                        text: appSettings.username
                        onEditingFinished: appSettings.username = text
                    }

                    Label { text: root.tr("settings.display_name"); color: root.theme.text }
                    TextField {
                        Layout.fillWidth: true
                        text: appSettings.displayName
                        onEditingFinished: appSettings.displayName = text
                    }

                    Label { text: root.tr("lang_choose"); color: root.theme.text }
                    ComboBox {
                        id: langCombo
                        Layout.fillWidth: true
                        model: Translations.availableLanguages
                        displayText: root.languageLabel(Translations.current)
                        currentIndex: model.indexOf(Translations.current)
                        delegate: ItemDelegate {
                            width: langCombo.width
                            text: root.languageLabel(modelData)
                        }
                        onActivated: {
                            Translations.current = model[currentIndex]
                            appSettings.language = model[currentIndex]
                        }
                    }

                    Label { text: root.tr("settings.theme"); color: root.theme.text }
                    ComboBox {
                        id: themeCombo
                        Layout.fillWidth: true
                        model: ThemeManager.availableThemes
                        displayText: root.tr("theme." + ThemeManager.currentTheme)
                        currentIndex: model.indexOf(ThemeManager.currentTheme)
                        delegate: ItemDelegate {
                            width: themeCombo.width
                            text: root.tr("theme." + modelData)
                        }
                        onActivated: ThemeManager.currentTheme = model[currentIndex]
                    }
                }

                ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label { text: root.tr("settings.microphone"); color: root.theme.text }
                    ComboBox {
                        id: micCombo
                        Layout.fillWidth: true
                        textRole: "description"
                        valueRole: "id"
                        model: root.deviceList(audioDevices.inputs)
                        currentIndex: indexOfValue(appSettings.audioInputId)
                        onActivated: appSettings.audioInputId = currentValue
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        Button {
                            text: audioDevices.micTestRunning ? root.tr("settings.stop_test")
                                                              : root.tr("settings.test_mic")
                            onClicked: {
                                if (audioDevices.micTestRunning)
                                    audioDevices.stopMicTest()
                                else
                                    audioDevices.startMicTest(appSettings.audioInputId)
                            }
                        }
                        ProgressBar {
                            Layout.fillWidth: true
                            from: 0
                            to: 1
                            value: audioDevices.micLevel
                        }
                    }

                    Label { text: root.tr("settings.speakers"); color: root.theme.text }
                    ComboBox {
                        id: spkCombo
                        Layout.fillWidth: true
                        textRole: "description"
                        valueRole: "id"
                        model: root.deviceList(audioDevices.outputs)
                        currentIndex: indexOfValue(appSettings.audioOutputId)
                        onActivated: appSettings.audioOutputId = currentValue
                    }

                    Button {
                        text: root.tr("settings.test_speakers")
                        enabled: !audioDevices.tonePlaying
                        onClicked: audioDevices.playTestTone(appSettings.audioOutputId)
                    }

                    Label {
                        text: root.tr("settings.volume") + ": " + appSettings.audioVolume + "%"
                        color: root.theme.text
                    }
                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        stepSize: 1
                        value: appSettings.audioVolume
                        onMoved: appSettings.audioVolume = Math.round(value)
                    }

                    CheckBox {
                        text: root.tr("menu.mute_toggle")
                        checked: appSettings.micMuted
                        onToggled: appSettings.micMuted = checked
                    }
                    CheckBox {
                        text: root.tr("settings.vad")
                        checked: appSettings.vadEnabled
                        onToggled: appSettings.vadEnabled = checked
                    }
                }

                ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label { text: root.tr("settings.network_mode"); color: root.theme.text }
                    ComboBox {
                        id: modeCombo
                        Layout.fillWidth: true
                        model: [
                            root.tr("settings.mode_lan"),
                            root.tr("settings.mode_kserver_self"),
                            root.tr("settings.mode_kserver_client"),
                            root.tr("settings.mode_relay"),
                            root.tr("settings.mode_vds"),
                        ]
                        currentIndex: appSettings.connectionMode
                        // The unbuilt modes stay on the list so the shape of
                        // the plan is visible, but they cannot be selected.
                        delegate: ItemDelegate {
                            width: modeCombo.width
                            enabled: networkManager.modeAvailable(index)
                            text: enabled
                                ? modelData
                                : modelData + "  (" + root.tr("settings.mode_unavailable") + ")"
                        }
                        onActivated: {
                            if (networkManager.modeAvailable(currentIndex))
                                appSettings.connectionMode = currentIndex
                            else
                                currentIndex = appSettings.connectionMode
                        }
                    }

                    Label { text: root.tr("settings.vds_host"); color: root.theme.text }
                    TextField {
                        Layout.fillWidth: true
                        enabled: settingsSheet.usesRelay
                        text: appSettings.relayHost
                        onEditingFinished: appSettings.relayHost = text
                    }

                    Label { text: root.tr("settings.vds_port"); color: root.theme.text }
                    TextField {
                        Layout.fillWidth: true
                        enabled: settingsSheet.usesRelay
                        text: appSettings.relayPort > 0 ? String(appSettings.relayPort) : ""
                        validator: IntValidator { bottom: 0; top: 65535 }
                        onEditingFinished: appSettings.relayPort = parseInt(text.length > 0 ? text : "0")
                    }

                    // AppSettings only persists; the running NetworkManager has
                    // to be told separately, and switching mode tears the relay
                    // tunnel up or down, so it waits for an explicit click.
                    Button {
                        text: root.tr("settings.save")
                        onClicked: {
                            networkManager.setRelayServer(appSettings.relayHost, appSettings.relayPort, 0)
                            networkManager.setConnectionMode(appSettings.connectionMode)
                            root.showStub(root.tr("settings.title"), root.tr("settings.saved"))
                        }
                    }
                }
            }
        }
    }

    Kirigami.OverlaySheet {
        id: aboutSheet
        title: root.tr("menu.about")
        Label {
            width: Kirigami.Units.gridUnit * 20
            wrapMode: Text.WordWrap
            text: root.tr("about.description")
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
            onProfileRequested: {
                otherProfileSheet.peer = root.peerInfoFor(peerIp)
                otherProfileSheet.open()
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
