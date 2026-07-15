// KOutNet — Main chat window shell
// Peer list is driven live by NetworkManager (userOnline/userOffline
// signals). Chat history is kept client-side in QML per peer IP — the
// network layer is fire-and-forget, it doesn't echo back sent messages,
// so outgoing text is appended locally right after sendPrivate().
import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    color: "#15151F"

    property var peers: ({})   // ip -> {ip, os, e2e, ...}
    property var histories: ({}) // ip -> array of {text, fromMe, ts}
    property string selectedIp: ""

    ListModel { id: peerModel }
    ListModel { id: chatModel }

    function ensureHistory(ip) {
        if (!histories[ip])
            histories[ip] = []
    }

    function appendMessage(ip, text, fromMe) {
        ensureHistory(ip)
        var entry = { text: text, fromMe: fromMe, ts: Date.now() }
        histories[ip].push(entry)
        if (root.selectedIp === ip)
            chatModel.append(entry)
    }

    function selectPeer(ip) {
        root.selectedIp = ip
        ensureHistory(ip)
        chatModel.clear()
        var h = histories[ip]
        for (var i = 0; i < h.length; ++i)
            chatModel.append(h[i])
    }

    function sendMessage() {
        const text = inputField.text.trim()
        if (text.length === 0 || root.selectedIp === "")
            return
        networkManager.sendPrivate(text, root.selectedIp)
        root.appendMessage(root.selectedIp, text, true)
        inputField.text = ""
    }

    function upsertPeer(info) {
        const ip = info.ip
        if (!ip)
            return
        peers[ip] = info
        for (let i = 0; i < peerModel.count; ++i) {
            if (peerModel.get(i).ip === ip) {
                peerModel.set(i, info)
                return
            }
        }
        peerModel.append(info)
    }

    function removePeer(ip) {
        delete peers[ip]
        for (let i = 0; i < peerModel.count; ++i) {
            if (peerModel.get(i).ip === ip) {
                peerModel.remove(i)
                return
            }
        }
    }

    Connections {
        target: networkManager
        function onUserOnline(peerInfo) { root.upsertPeer(peerInfo) }
        function onUserOffline(ip) { root.removePeer(ip) }
        function onMessage(msg) {
            // Only 1:1 private messages are handled here — public/group
            // chat is a separate pass (see qml/chats/ TODOs).
            if (msg.type === "private")
                root.appendMessage(msg.from_ip, msg.text, false)
        }
    }

    Row {
        anchors.fill: parent

        // ── Peer / contact list ──────────────────────────────────────
        Rectangle {
            id: sidebar
            width: 260
            height: parent.height
            color: "#1E1E2E"

            Column {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                Text {
                    text: "KOutNet"
                    color: "white"
                    font.pixelSize: 20
                    font.bold: true
                }
                Text {
                    text: networkManager && networkManager.isRunning()
                          ? "Online — " + networkManager.hostIp()
                          : "Offline"
                    color: "#4A90D9"
                    font.pixelSize: 11
                }

                Rectangle { width: parent.width; height: 1; color: "#2A2A3E" }

                ListView {
                    width: parent.width
                    height: parent.height - 70
                    model: peerModel
                    spacing: 4
                    clip: true

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 44
                        radius: 8
                        color: root.selectedIp === model.ip ? "#2A2A3E" : "transparent"

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.selectPeer(model.ip)
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            spacing: 2

                            Text {
                                text: model.ip || "?"
                                color: "white"
                                font.pixelSize: 13
                            }
                            Text {
                                text: (model.e2e ? "E2E" : "Plain") + " • " + (model.os || "")
                                color: model.e2e ? "#4ADE80" : "#888"
                                font.pixelSize: 10
                            }
                        }
                    }
                }
            }
        }

        // ── Chat area ────────────────────────────────────────────────
        Rectangle {
            id: chatArea
            width: parent.width - sidebar.width
            height: parent.height
            color: "#15151F"

            Text {
                anchors.centerIn: parent
                visible: root.selectedIp === ""
                text: "Select a peer to start chatting"
                color: "#666"
                font.pixelSize: 14
            }

            Column {
                anchors.fill: parent
                visible: root.selectedIp !== ""
                spacing: 0

                Rectangle {
                    width: parent.width
                    height: 48
                    color: "#1E1E2E"

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        text: root.selectedIp
                        color: "white"
                        font.pixelSize: 14
                        font.bold: true
                    }
                }

                ListView {
                    id: chatList
                    width: parent.width
                    height: parent.height - 48 - 56
                    model: chatModel
                    clip: true
                    spacing: 6

                    onCountChanged: positionViewAtEnd()

                    delegate: Item {
                        width: chatList.width
                        height: bubble.height + 8

                        Rectangle {
                            id: bubble
                            width: Math.min(bubbleText.implicitWidth + 24, chatList.width * 0.7)
                            height: bubbleText.implicitHeight + 16
                            radius: 12
                            color: model.fromMe ? "#4A90D9" : "#2A2A3E"
                            anchors.right: model.fromMe ? parent.right : undefined
                            anchors.left: model.fromMe ? undefined : parent.left
                            anchors.rightMargin: model.fromMe ? 14 : 0
                            anchors.leftMargin: model.fromMe ? 0 : 14
                            anchors.top: parent.top

                            Text {
                                id: bubbleText
                                anchors.centerIn: parent
                                text: model.text
                                color: "white"
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                                width: Math.min(implicitWidth, chatList.width * 0.7 - 24)
                            }
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 56
                    color: "#1E1E2E"

                    Row {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8

                        TextField {
                            id: inputField
                            width: parent.width - sendButton.width - 8
                            height: parent.height
                            placeholderText: "Message..."
                            color: "white"
                            background: Rectangle { color: "#15151F"; radius: 8 }
                            onAccepted: root.sendMessage()
                        }

                        Button {
                            id: sendButton
                            text: "Send"
                            height: parent.height
                            onClicked: root.sendMessage()
                        }
                    }
                }
            }
        }
    }
}
