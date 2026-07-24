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

    property string currentPeerIp: ""
    readonly property bool compactMode: width < 480
    readonly property string kSelfChatId: "__self__"

    property bool sidebarCollapsed: false
    property string contactSearchText: ""

    // ip -> ChatModel instance
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

    // Tell the peer we've read their messages whenever we switch into their
    // chat. Not sent for the self-chat (no peer to notify).
    onCurrentPeerIpChanged: {
        if (currentPeerIp.length > 0 && currentPeerIp !== kSelfChatId)
            networkManager.sendReadReceipt(currentPeerIp, "dm")
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

        RowLayout {
            anchors.fill: parent
            spacing: 0

            ColumnLayout {
                id: sidebarColumn
                Layout.preferredWidth: root.sidebarCollapsed ? 0 : 240
                Layout.minimumWidth: 0
                Layout.maximumWidth: 320
                Layout.fillHeight: true
                clip: true
                spacing: 0

                Behavior on Layout.preferredWidth {
                    NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    Layout.leftMargin: Kirigami.Units.smallSpacing + 36

                    Kirigami.Heading {
                        text: Translations.t("contacts_header")
                        level: 1
                        font.bold: true
                        font.weight: Font.Black
                    }
                    Item { Layout.fillWidth: true }
                }

                Kirigami.Separator { Layout.fillWidth: true }

                ItemDelegate {
                    Layout.fillWidth: true
                    height: 44
                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing
                        Kirigami.Icon { source: "settings-configure"; Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
                        Label { text: Translations.t("sidebar.settings"); Layout.fillWidth: true }
                    }
                    onClicked: settingsSheet.open()
                }

                ItemDelegate {
                    Layout.fillWidth: true
                    height: 44
                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing
                        Kirigami.Icon { source: "help-contents"; Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
                        Label { text: Translations.t("sidebar.help"); Layout.fillWidth: true }
                    }
                    onClicked: helpSheet.open()
                }

                Kirigami.Separator { Layout.fillWidth: true }

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
                    onTextChanged: root.contactSearchText = text
                }

                Kirigami.Separator { Layout.fillWidth: true }

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

            Kirigami.Separator {
                Layout.fillHeight: true
                visible: !root.sidebarCollapsed
            }

            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !root.compactMode || root.currentPeerIp.length > 0
                sourceComponent: root.currentPeerIp.length > 0 ? chatComponent : placeholderComponent
            }
        }

        Rectangle {
            id: collapseButton
            width: 32
            height: 32
            radius: 4
            color: hamburgerMouse.containsMouse ? Kirigami.Theme.hoverColor : "transparent"
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 4
            z: 10

            Column {
                anchors.centerIn: parent
                spacing: 3
                Repeater {
                    model: 3
                    Rectangle { width: 16; height: 2; radius: 1; color: Kirigami.Theme.textColor }
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

            Label { text: Translations.t("settings.username") }
            TextField {
                Layout.fillWidth: true
                text: appSettings.username
                onEditingFinished: appSettings.username = text
            }

            Label { text: Translations.t("menu.language") }
            ComboBox {
                Layout.fillWidth: true
                model: Translations.availableLanguages
                currentIndex: model.indexOf(Translations.current)
                onActivated: Translations.current = model[currentIndex]
            }
        }
    }

    Kirigami.OverlaySheet {
        id: helpSheet
        title: Translations.t("sidebar.help")

        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: Translations.t("sidebar.help_text")
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
