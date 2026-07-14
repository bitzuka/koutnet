// KOutNet — Main chat window shell
// Peer list is driven live by NetworkManager (userOnline/userOffline
// signals) via the LAN discovery already implemented in NetworkManager.cpp.
// Chat history / message view is a placeholder — full chat UI is a
// separate, much larger pass (see qml/chats/ TODOs).
import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    color: "#15151F"

    property var peers: ({}) // ip -> {ip, os, e2e, ...}
    property string selectedIp: ""

    ListModel { id: peerModel }

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
                    text: networkManager && networkManager.isRunning
                          ? "Online — " + networkManager.hostIp
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
                            onClicked: root.selectedIp = model.ip
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

        // ── Chat area (placeholder) ────────────────────────────────────
        Rectangle {
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
                anchors.margins: 16
                visible: root.selectedIp !== ""
                spacing: 8

                Text {
                    text: "Chat with " + root.selectedIp
                    color: "white"
                    font.pixelSize: 16
                    font.bold: true
                }
                Text {
                    text: "Message history and input — TODO (next pass)"
                    color: "#666"
                    font.pixelSize: 12
                }
            }
        }
    }
}
