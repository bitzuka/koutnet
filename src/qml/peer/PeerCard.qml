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
// The shape is a banner strip, the avatar hanging off its lower edge, the name,
// the handle under it, a rule, a block of details, then the actions. It used to
// be a row and a column of labels, which said the same things in the order a
// data structure has them rather than the order somebody reads them in.
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

        // The banner, and the avatar hanging off it. One Item because the
        // overlap is what makes the shape, and an overlap cannot be expressed
        // between two layout children.
        Item {
            Layout.fillWidth: true
            // The banner, plus the part of the avatar that hangs below it. That
            // part is what is left after the overlap, not the overlap itself.
            implicitHeight: bannerStrip.height + avatar.height * (1 - kAvatarOverhang)

            // How much of the avatar sits over the banner rather than below it.
            // The same fraction as the profile header, so the card and the page
            // read as the same design.
            readonly property real kAvatarOverhang: 0.45

            // Only the top corners are rounded: the bottom edge of this strip is
            // in the middle of the card, and rounding it would cut a notch out
            // of the card's own background. ShadowedRectangle is what allows a
            // per-corner radius - a plain Rectangle has the one radius property.
            Kirigami.ShadowedRectangle {
                id: bannerStrip
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: Kirigami.Units.gridUnit * 4
                corners.topLeftRadius: Kirigami.Units.cornerRadius
                corners.topRightRadius: Kirigami.Units.cornerRadius
                corners.bottomLeftRadius: 0
                corners.bottomRightRadius: 0
                // The brand colour, which is the one thing this application says
                // about colour, and the card is where it is worth saying.
                color: Brand.accent
            }

            // The ring that lifts the avatar off the banner. A sibling declared
            // before the avatar rather than a child of it with a negative z:
            // Avatar has no border of its own, and going through its children
            // would be relying on where it paints its own circle.
            Rectangle {
                anchors.centerIn: avatar
                width: avatar.width + Kirigami.Units.smallSpacing * 2
                height: width
                radius: width / 2
                color: Kirigami.Theme.backgroundColor
            }

            Components.Avatar {
                id: avatar
                width: Kirigami.Units.gridUnit * 3.5
                height: width
                anchors.left: parent.left
                anchors.leftMargin: Kirigami.Units.largeSpacing
                anchors.top: bannerStrip.bottom
                anchors.topMargin: -height * parent.kAvatarOverhang
                name: root.shownName
            }
        }

        // Name, handle and presence.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.smallSpacing
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 3
                    // Bold on top of the heading level, because the handle right
                    // underneath is the same family and the weight is what
                    // separates them at a glance.
                    font.bold: true
                    text: root.shownName
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                }

                // The peer's custom status, if it published one.
                QQC2.Label {
                    visible: root.statusEmoji.length > 0
                    text: root.statusEmoji
                    textFormat: Text.PlainText
                    font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * 1.3)
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                visible: root.handle.length > 0
                text: i18nc("@info a peer's handle, %1 is the user name", "@%1", root.handle)
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: Kirigami.Theme.disabledTextColor
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.largeSpacing
        }

        // The detail block. A dimmed caption over a value, twice, which is the
        // shape that reads as a fact rather than as another line of the same
        // paragraph.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.largeSpacing

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                QQC2.Label {
                    text: i18nc("@label:textbox caption over the peer's reachability", "Status")
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        width: Math.round(Kirigami.Units.iconSizes.small * 0.5)
                        height: width
                        radius: width / 2
                        color: root.online ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.disabledTextColor
                    }

                    // RelativeTime.now is read so this ages on its own while the
                    // card sits open and nothing arrives.
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: root.peer
                            ? RelativeTime.presenceLabel(root.online, root.peer.lastSeen || 0, RelativeTime.now)
                            : ""
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: root.peer !== null
                spacing: 0

                QQC2.Label {
                    text: i18nc("@label:textbox caption over whether the session is encrypted", "Session")
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                }

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
