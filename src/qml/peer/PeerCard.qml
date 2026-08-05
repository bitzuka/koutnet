// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import koutnet.app

// A peer at a glance, hung off whatever was clicked to ask for it.
//
// Not the third column and not a page: clicking a face in a list is a question
// worth one card - who is this, are they about, and the two things most likely
// to be wanted next. PeerInfoPage is still where the fingerprint, the operating
// system and the addresses live, and the card has a way through to it.
//
// The shape is qml/profile/ProfileHeader.qml at card size - banner, the avatar
// hanging off its lower-left, the name, the handle, the status, the presence -
// then what is only ever true of somebody else, then the actions. The account
// card beside it is the same component with your own identity in it, which is
// what makes the two read as one design rather than as two.
//
// The popup itself has no padding, because the banner is full bleed and a padded
// popup cannot do that. Every child below sets its own margins instead.
//
// No address on it. The chat is keyed on one, but that is routing and not
// something to put under somebody's name.
QQC2.Popup {
    id: root

    // The map Main.qml builds: { ip, username, displayName, bio, os, e2e,
    // avatarLetter, isFavorites, online, lastSeen, statusEmoji, presence }.
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
    readonly property string statusEmoji: root.peer ? (root.peer.statusEmoji || "") : ""

    // Wide enough to be worth opening and narrow enough not to become a second
    // window. Fixed rather than grown from the content, so a peer with a long
    // name gets an elide instead of a card the width of the screen.
    implicitWidth: Kirigami.Units.gridUnit * 18

    // See the note on Kirigami.Theme in Main.qml: a popup is reparented into
    // the window overlay, which starts a theme chain of its own.
    Kirigami.Theme.inherit: false
    Kirigami.Theme.highlightColor: Brand.accent

    modal: false
    dim: false
    focus: true
    closePolicy: QQC2.Popup.CloseOnEscape | QQC2.Popup.CloseOnPressOutside
    padding: 0

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
        spacing: 0

        ProfileHeader {
            Layout.fillWidth: true

            compact: true
            // The card's own background is rounded and the banner is full bleed,
            // so the strip has to round its own top corners or it squares the
            // card off.
            topCornerRadius: Kirigami.Units.cornerRadius

            displayName: root.shownName
            handle: root.handle
            online: root.online
            lastSeenSecs: root.peer ? (root.peer.lastSeen || 0) : 0
            statusEmoji: root.statusEmoji
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
        }

        // Whether there is a session, which is the one fact about a peer worth a
        // whole line of its own before the actions.
        ProfileBlock {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.largeSpacing
            visible: root.peer !== null

            label: i18nc("@label:textbox caption over whether the session is encrypted", "Session")

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                    source: (root.peer && root.peer.e2e) ? "security-high" : "security-low"
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    text: (root.peer && root.peer.e2e)
                        ? i18nc("@info:status the session is end to end encrypted", "End to end encrypted")
                        : i18nc("@info:status", "No session")
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
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
