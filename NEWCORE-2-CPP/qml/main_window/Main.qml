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

    onCurrentPeerIpChanged: {
        if (currentPeerIp.length > 0 && currentPeerIp !== kSelfChatId)
            networkManager.sendReadReceipt(currentPeerIp, "dm")
    }

    // ── Menu bar — mirrors legacy _setup_menubar (File / View / Calls / Help) ──
    menuBar: MenuBar {
        Menu {
            title: Translations.t("menu_file") || "Файл"
            MenuItem { text: Translations.t("menu_my_profile") || "Мой профиль" }
            MenuItem { text: Translations.t("menu_settings") || "Настройки"; onTriggered: settingsSheet.open() }
            MenuSeparator {}
            MenuItem { text: Translations.t("menu_check_updates") || "Проверить обновления" }
            MenuSeparator {}
            MenuItem { text: Translations.t("menu_quit") || "Выход"; onTriggered: Qt.quit() }
        }

        Menu {
            title: Translations.t("menu_view") || "Вид"

            Menu {
                title: Translations.t("menu_themes") || "Темы"
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
                text: Translations.t("menu_public_chat") || "Публичный чат"
                onTriggered: root.currentPeerIp = "public"
            }
            MenuItem {
                text: "♫  1-2-3"
                onTriggered: tabBar.currentIndex = 3
            }
            MenuSeparator {}
            MenuItem {
                text: Translations.t("menu_fullscreen") || "Полноэкранный режим"
                onTriggered: root.visibility = (root.visibility === Window.FullScreen)
                    ? Window.Windowed : Window.FullScreen
            }
            MenuItem { text: Translations.t("menu_lang_ru") || "Русский"; onTriggered: Translations.current = "ru" }
            MenuItem { text: Translations.t("menu_lang_en") || "English"; onTriggered: Translations.current = "en" }
            MenuItem { text: Translations.t("menu_lang_ja") || "日本語"; onTriggered: Translations.current = "ja" }
        }

        Menu {
            title: Translations.t("menu_calls") || "Звонки"
            MenuItem {
                text: Translations.t("menu_mute_toggle") || "Выключить микрофон"
                checkable: true
                checked: root.micMuted
                onTriggered: root.micMuted = !root.micMuted
            }
            MenuItem { text: Translations.t("menu_hangup_all") || "Завершить все звонки" }
        }

        Menu {
            title: Translations.t("menu_help") || "Справка"
            MenuItem { text: Translations.t("menu_about") || "О программе"; onTriggered: aboutSheet.open() }
            MenuSeparator {}
            MenuItem { text: Translations.t("menu_terminal") || "Терминал" }
            MenuItem { text: Translations.t("menu_wns") || "WNS"; onTriggered: tabBar.currentIndex = 4 }
            MenuSeparator {}
            MenuItem { text: Translations.t("menu_tutorial") || "Обучение" }
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
                text: Translations.t("searching") || "Поиск пиров..."
                color: root.theme.text_dim
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                Layout.fillWidth: true
            }

            Label {
                text: "IP: " + (networkManager.localIp || "—")
                color: "#8090B0"
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }

            Label {
                text: root.micMuted ? (Translations.t("mic_off") || "Микрофон выкл") : (Translations.t("mic_on") || "Микрофон вкл")
                color: root.micMuted ? "#FF8080" : "#80FF80"
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                MouseArea { anchors.fill: parent; onClicked: root.micMuted = !root.micMuted }
            }

            Label {
                text: Translations.t("no_calls") || "Нет звонков"
                color: "#A0A0A0"
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

                        TextField {
                            Layout.fillWidth: true
                            Layout.margins: Kirigami.Units.smallSpacing
                            placeholderText: Translations.t("sidebar.search_placeholder")
                            text: root.contactSearchText
                            color: root.theme.text
                            onTextChanged: root.contactSearchText = text
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

                    TabButton { text: Translations.t("tab_main_chat") || "Чат" }
                    TabButton { text: Translations.t("tab_main_notes") || "Заметки" }
                    TabButton { text: Translations.t("tab_main_calls") || "Звонки" }
                    TabButton { text: "♫" }
                    TabButton { text: "🌐 WNS" }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: tabBar.currentIndex

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

            Label { text: "Тема"; color: root.theme.text }
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
        title: Translations.t("menu_about") || "О программе"
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
