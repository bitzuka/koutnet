// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import koutnet.app

// PeerCard's opposite number, and deliberately the same shape: the banner, the
// labelled block, the row of buttons. What is inside them is different, because
// a room member is not a peer. There is no key exchange to report - the session
// is with the homeserver, not with this person - no address they answered on and
// no call to place, so what is left is who they are and what the room lets them
// do. No presence dot either: this build does not subscribe to Matrix presence,
// and a permanently grey one reads as somebody who is offline rather than as a
// question that was never asked.
QQC2.Popup {
    id: root

    // MatrixRoomBridge::memberInfo()'s map: { userId, displayName, powerLevel,
    // isLocalMember, avatarUrl, trustKnown, userVerified, deviceCount,
    // verifiedDeviceCount }.
    property var member: null

    // The "mx:" chat id of the room the member is in, so the kick and ban
    // buttons know which room to ask. The card itself has no room: it floats
    // over whichever page opened it.
    property string chatId: ""

    signal notifyRequested(string text)

    readonly property string userId: root.member ? (root.member.userId || "") : ""
    readonly property string shownName: root.member ? (root.member.displayName || "") : ""
    readonly property int powerLevel: root.member ? (root.member.powerLevel || 0) : 0
    readonly property string powerLabel: RoomRoles.label(root.powerLevel)

    // Whether the trust tables could be asked at all. Everything below reads
    // this first, because "no verified devices" and "nobody looked" must not
    // print the same sentence.
    readonly property bool trustKnown: root.member ? root.member.trustKnown === true : false
    readonly property int deviceCount: root.member ? (root.member.deviceCount || 0) : 0
    readonly property int verifiedDeviceCount: root.member ? (root.member.verifiedDeviceCount || 0) : 0
    readonly property bool allDevicesVerified: root.trustKnown && root.deviceCount > 0 && root.verifiedDeviceCount === root.deviceCount
    // Whether this card is showing the signed-in account rather than somebody
    // else. The two get different advice, because only one of them is something
    // this build can act on.
    readonly property bool isSelf: root.member ? root.member.isLocalMember === true : false
    readonly property bool isBanned: root.member ? root.member.isBanned === true : false

    // Fixed rather than grown from the content, so a member with a long name
    // gets an elide instead of a card the width of the screen.
    implicitWidth: Kirigami.Units.gridUnit * 18

    // See Main.qml: a popup is reparented into the overlay, which is its own chain.
    Kirigami.Theme.inherit: false
    Kirigami.Theme.highlightColor: Brand.accent

    modal: false
    dim: false
    focus: true
    closePolicy: QQC2.Popup.CloseOnEscape | QQC2.Popup.CloseOnPressOutside
    padding: 0

    // Reparenting is what makes x and y mean "beside that item"; it also gives
    // the close policy something sensible to measure against.
    function openAt(item, memberInfo) {
        if (!item)
            return
        root.member = memberInfo
        root.parent = item
        root.x = 0
        root.y = item.height + Kirigami.Units.smallSpacing
        root.open()
    }

    TextEdit {
        id: clipboardHelper
        visible: false
        function copyText(str) {
            text = str
            selectAll()
            copy()
        }
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
            // The card's background is rounded and the banner is full bleed, so
            // the strip rounds its own top corners or it squares the card off.
            topCornerRadius: Kirigami.Units.cornerRadius
            showPresence: false

            displayName: root.shownName
            avatarSource: root.member ? (root.member.avatarUrl || "") : ""
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
        }

        ProfileBlock {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.largeSpacing
            visible: root.member !== null

            label: i18nc("@label:textbox caption over a Matrix user identifier", "Matrix address")

            Kirigami.SelectableLabel {
                Layout.fillWidth: true
                text: root.userId
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
            }
        }

        ProfileBlock {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.largeSpacing
            visible: root.member !== null

            label: i18nc("@label:textbox caption over what a member may do in a room", "Permissions")

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    Layout.alignment: Qt.AlignVCenter
                    visible: RoomRoles.iconName(root.powerLevel).length > 0
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                    source: RoomRoles.iconName(root.powerLevel)
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    // The number as well as the word: a room can hand out any
                    // level between the two named ones, and the word alone
                    // would round it away.
                    text: root.powerLabel.length > 0
                        ? i18nc("@info:status %1 is a role name, %2 the numeric Matrix power level",
                                "%1 (power level %2)", root.powerLabel, root.powerLevel)
                        : i18nc("@info:status %1 is the numeric Matrix power level",
                                "Member (power level %1)", root.powerLevel)
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                }
            }
        }

        // The one thing this card must never do is imply a person is who they
        // say they are when nothing has checked. The counts below come straight
        // from libQuotient's trust table, so a device shows as verified here
        // only once something actually verified it.
        ProfileBlock {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.largeSpacing
            visible: root.member !== null

            label: i18nc("@label:textbox caption over whether a member's devices have been verified", "Device verification")

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                    source: root.allDevicesVerified ? "security-medium" : "security-low"
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    text: !root.trustKnown
                        ? i18nc("@info:status the devices of a room member could not be looked up", "Not checked")
                        : (root.deviceCount === 0
                            ? i18nc("@info:status no encryption-capable devices are known for a room member", "No known devices")
                            : i18ncp("@info:status %1 is how many devices a member has, %2 how many of them are verified",
                                     "%2 of %1 device verified", "%2 of %1 devices verified",
                                     root.deviceCount, root.verifiedDeviceCount))
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                // Says what this build can and cannot do, and no more than
                // that. KOutNet verifies its own account's sessions; verifying
                // somebody else needs cross-signing, which it does not set up,
                // so an unverified device of theirs here means exactly that
                // nothing has checked it.
                text: root.isSelf
                    ? i18nc("@info:whatsthis about the signed-in account's own sessions",
                            "These are your own sessions. Verify them from the room information column to let them share room keys.")
                    : i18nc("@info:whatsthis about another person's sessions",
                            "KOutNet cannot verify another person's device yet. Anything unverified here was not checked by this application.")
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            QQC2.Button {
                Layout.fillWidth: true
                icon.name: "edit-copy"
                text: i18nc("@action:button copy a member's Matrix address", "Copy address")
                onClicked: {
                    clipboardHelper.copyText(root.userId)
                    root.notifyRequested(i18nc("@info:status", "Matrix address copied to the clipboard"))
                    root.close()
                }
            }

            QQC2.Button {
                Layout.fillWidth: true
                visible: !root.isSelf && root.chatId.length > 0
                    && root.isBanned
                icon.name: "network-connect"
                text: i18nc("@action:button let a banned member back into a room", "Unban")
                onClicked: {
                    matrixRooms.unbanMember(root.chatId, root.userId)
                    root.close()
                }
            }

            QQC2.Button {
                Layout.fillWidth: true
                visible: !root.isSelf && root.chatId.length > 0 && !root.isBanned
                icon.name: "user-properties"
                text: i18nc("@action:button remove a member from a room", "Kick")
                onClicked: {
                    matrixRooms.kickMember(root.chatId, root.userId)
                    root.close()
                }
            }

            QQC2.Button {
                Layout.fillWidth: true
                visible: !root.isSelf && root.chatId.length > 0 && !root.isBanned
                icon.name: "process-stop"
                text: i18nc("@action:button ban a member from a room", "Ban")
                onClicked: {
                    matrixRooms.banMember(root.chatId, root.userId)
                    root.close()
                }
            }

            QQC2.Button {
                Layout.fillWidth: true
                visible: !root.isSelf && root.userId.length > 0
                icon.name: "window-close"
                text: i18nc("@action:button mute a user across the whole account", "Ignore")
                onClicked: {
                    matrixRooms.ignoreUser(root.userId)
                    root.notifyRequested(i18nc("@info:status", "Ignored %1", root.userId))
                    root.close()
                }
            }
        }
    }
}
