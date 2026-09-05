// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import koutnet.app

// The connection mode sits on the left edge rather than three pages into the
// settings because it is the thing this application is for.
//
// The rail only picks a mode; applying it goes through the same path the
// settings page uses, so the switch lands wherever the settings say.
QQC2.Control {
    id: root

    property int currentMode: 0
    // the chat list model, so the mode buttons can show their unread count
    property var chats: null

    signal modeSelected(int mode)

    // Width comes from the square buttons plus the column's own insets;
    // hardcoding it is how the rail ends up half a pixel off the separator.
    implicitWidth: Kirigami.Units.iconSizes.smallMedium + Kirigami.Units.gridUnit * 2

    padding: 0

    // The values are persisted, so the order here is the enum's, not a nicer one.
    //
    // Two buttons and not five: self-hosted, somebody else's and the
    // maintainer's deployment were one protocol at three addresses, picking an
    // address belongs in a text field on the settings page, and the relay
    // mode that came after them is gone.
    readonly property var modes: [
        {
            mode: 0,
            name: i18nc("@item connection mode, peers found on the local network or over a VPN adapter", "Local network or VPN"),
            icon: "network-workgroup",
            reason: i18nc("@info:tooltip why a connection mode cannot be used", "no local interface is up")
        },
        {
            mode: 1,
            name: i18nc("@item connection mode, a Matrix homeserver at whatever address is configured", "Matrix"),
            icon: "network-server",
            reason: i18nc("@info:tooltip why a connection mode cannot be used", "not built yet")
        }
    ]

    contentItem: QQC2.ScrollView {
        id: railScroll

        // Two buttons never need a scrollbar, and this rail has no room for
        // one; the view is here so a third mode does not fall off the bottom.
        QQC2.ScrollBar.vertical.policy: QQC2.ScrollBar.AlwaysOff
        QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff
        // do not bind contentWidth to availableWidth, it makes a loop with the Control width

        ColumnLayout {
            width: railScroll.width
            spacing: 0

            Repeater {
                model: root.modes

                delegate: ModeButton {
                    required property var modelData

                    Layout.fillWidth: true
                    Layout.preferredHeight: width

                    mode: modelData.mode
                    modeName: modelData.name
                    icon.name: modelData.icon
                    current: root.currentMode === modelData.mode
                    unavailableReason: networkManager.modeAvailable(modelData.mode) ? "" : modelData.reason
                    unread: root.chats ? root.chats.unreadForGroup(modelData.mode) : 0

                    onPicked: root.modeSelected(modelData.mode)
                }
            }
        }
    }
}
