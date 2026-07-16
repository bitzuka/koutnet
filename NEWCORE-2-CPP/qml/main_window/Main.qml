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

    property var histories: ({})

    function modelForPeer(ip) {
        if (!histories[ip])
            histories[ip] = Qt.createQmlObject('import QtQml.Models; ListModel {}', root)
        return histories[ip]
    }

    function appendMessage(ip, text, fromMe) {
        modelForPeer(ip).append({ text: text, fromMe: fromMe, isFile: false, filePath: "", isImage: false })
    }

    function appendFile(ip, filePath, fromMe) {
        const lower = filePath.toLowerCase()
        const isImage = lower.endsWith(".png") || lower.endsWith(".jpg")
                        || lower.endsWith(".jpeg") || lower.endsWith(".gif")
                        || lower.endsWith(".bmp") || lower.endsWith(".webp")
        modelForPeer(ip).append({
            text: filePath.split("/").pop(), fromMe: fromMe,
            isFile: true, filePath: filePath, isImage: isImage
        })
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

    // Splash — separate top-level Window (not toggled flags on the main
    // one; X11 WMs don't reliably re-decorate a window whose flags change
    // at runtime).
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
                root.appendMessage(msg.from_ip, msg.text, false)
        }
    }

    Connections {
        target: fileTransferHandler
        function onFileSaved(meta, localPath) {
            const fromIp = meta.from_ip
            if (fromIp)
                root.appendFile(fromIp, localPath, false)
        }
    }

    pageStack.initialPage: mainPage

    Kirigami.Page {
        id: mainPage
        globalToolBarStyle: Kirigami.ApplicationHeaderStyle.None
        title: "KOutNet"
        padding: 0

        SplitView {
            anchors.fill: parent
            orientation: Qt.Horizontal

            ColumnLayout {
                SplitView.preferredWidth: 220
                SplitView.minimumWidth: 160
                SplitView.maximumWidth: 360
                visible: !root.compactMode || root.currentPeerIp.length === 0
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        text: Translations.t("contacts_header")
                        level: 1
                        font.bold: true
                        font.weight: Font.Black
                    }
                    Item { Layout.fillWidth: true }
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
                        peerIp: model.ip
                        peerOs: model.os || ""
                        e2e: model.e2e === true
                        selected: model.ip === root.currentPeerIp
                        onClicked: root.currentPeerIp = model.ip
                    }
                }
            }

            Loader {
                SplitView.fillWidth: true
                visible: !root.compactMode || root.currentPeerIp.length > 0
                sourceComponent: root.currentPeerIp.length > 0 ? chatComponent : placeholderComponent
            }
        }
    }

    Component {
        id: chatComponent
        ChatPage {
            peerIp: root.currentPeerIp
            messagesModel: root.modelForPeer(root.currentPeerIp)
            showBackButton: root.compactMode

            onReturnToListRequested: root.currentPeerIp = ""
            onSendRequested: function(text) {
                networkManager.sendPrivate(text, peerIp)
                root.appendMessage(peerIp, text, true)
            }
            onAttachRequested: function(localFilePath) {
                networkManager.sendFile(peerIp, localFilePath)
                root.appendFile(peerIp, localFilePath, true)
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
