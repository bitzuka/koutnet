// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import koutnet.app

// A peer at a glance, hung off whatever was clicked to ask for it.
//
// Not the third column and not a page: clicking a face in a list is a question
// worth one card - who is this, are they about, and the two things most likely
// to be wanted next. PeerInfoPage is still where the fingerprint, the operating
// system and the addresses live, and the card has a way through to it.
//
// No address on it. The chat is keyed on one, but that is routing and not
// something to put under somebody's name.
QQC2.Popup {
    id: root

    // The map Main.qml builds: { ip, username, displayName, bio, os, e2e,
    // avatarLetter, isFavorites, online, lastSeen }.
    property var peer: null

    signal messageRequested(string chatId)
    signal callRequested(string chatId)
    signal detailsRequested(string chatId)

    readonly property string chatId: root.peer ? (root.peer.ip || "") : ""
    readonly property string shownName: root.peer
        ? ((root.peer.displayName && root.peer.displayName.length > 0)
            ? root.peer.displayName
            : (root.peer.username || ""))
        : ""
    readonly property string handle: root.peer ? (root.peer.username || "") : ""
    readonly property bool online: root.peer ? root.peer.online === true : false

    // See the note on Kirigami.Theme in Main.qml: a popup is reparented into
    // the window overlay, which starts a theme chain of its own.
    Kirigami.Theme.inherit: false
    Kirigami.Theme.highlightColor: Brand.accent

    modal: false
    dim: false
    focus: true
    closePolicy: QQC2.Popup.CloseOnEscape | QQC2.Popup.CloseOnPressOutside
    padding: Kirigami.Units.largeSpacing

    // Anchored to the thing that was clicked rather than centred on the window.
    // Reparenting is what makes x and y mean "beside that item"; it also gives
    // CloseOnPressOutsideParent something sensible to measure against.
    function openAt(item, peerInfo) {
        if (!item)
            return
        root.peer = peerInfo
        root.parent = item
        root.x = 0
        root.y = item.height + Kirigami.Units.smallSpacing
        root.open()
    }

    background: Kirigami.ShadowedRectangle {
        radius: Kirigami.Units.cornerRadius
        color: Kirigami.Theme.backgroundColor
        border.width: 1
        border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.2)
        shadow.size: Kirigami.Units.gridUnit
        shadow.color: Qt.rgba(0, 0, 0, 0.3)
        shadow.yOffset: 2
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            Components.Avatar {
                Layout.alignment: Qt.AlignTop
                implicitWidth: Kirigami.Units.gridUnit * 3
                implicitHeight: Kirigami.Units.gridUnit * 3
                name: root.shownName
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 0

                Kirigami.Heading {
                    Layout.fillWidth: true
                    // Wide enough to be worth opening and narrow enough not to
                    // become a second window.
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 14
                    level: 4
                    text: root.shownName
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    visible: root.handle.length > 0
                    text: i18nc("@info a peer's handle, %1 is the user name", "@%1", root.handle)
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                }

                // RelativeTime.now is read so this ages on its own while the
                // card sits open and nothing arrives.
                QQC2.Label {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    text: root.peer
                        ? RelativeTime.presenceLabel(root.online, root.peer.lastSeen || 0, RelativeTime.now)
                        : ""
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    font: Kirigami.Theme.smallFont
                    color: root.online ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.disabledTextColor
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Button {
                Layout.fillWidth: true
                icon.name: "mail-message-new"
                text: i18nc("@action:button open the conversation with this peer", "Message")
                onClicked: {
                    root.messageRequested(root.chatId)
                    root.close()
                }
            }

            QQC2.Button {
                Layout.fillWidth: true
                icon.name: "call-start"
                enabled: root.online
                text: i18nc("@action:button start a voice call with this peer", "Call")
                onClicked: {
                    root.callRequested(root.chatId)
                    root.close()
                }
            }

            QQC2.ToolButton {
                display: QQC2.AbstractButton.IconOnly
                icon.name: "documentinfo"
                text: i18nc("@action:button show the full peer details column", "Details")

                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                QQC2.ToolTip.text: text

                onClicked: {
                    root.detailsRequested(root.chatId)
                    root.close()
                }
            }
        }
    }
}
