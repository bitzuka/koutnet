// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// The connection mode sits on the left edge rather than three pages into the
// settings because it is the thing this application is for.
//
// The rail only picks a mode; applying it goes through the same path the
// settings page uses, because switching tears a relay tunnel up or down.
QQC2.Control {
    id: root

    property int currentMode: 0

    signal modeSelected(int mode)

    // Width comes from the square buttons plus the column's own insets;
    // hardcoding it is how the rail ends up half a pixel off the separator.
    implicitWidth: Kirigami.Units.iconSizes.smallMedium + Kirigami.Units.gridUnit * 2

    padding: 0

    // The values are persisted, so the order here is the enum's, not a nicer one.
    //
    // Three buttons and not five: self-hosted, somebody else's and the
    // maintainer's deployment were one protocol at three addresses, and picking
    // an address belongs in a text field on the settings page.
    readonly property var modes: [
        {
            mode: 0,
            name: i18nc("@item connection mode, peers found on the local network or over a VPN adapter", "Local network or VPN"),
            icon: "network-workgroup",
            reason: i18nc("@info:tooltip why a connection mode cannot be used", "no local interface is up")
        },
        {
            mode: 1,
            name: i18nc("@item connection mode, a K-Server at whatever address is configured", "K-Server"),
            icon: "network-server",
            reason: i18nc("@info:tooltip why a connection mode cannot be used", "not built yet")
        },
        {
            mode: 2,
            name: i18nc("@item connection mode, a plain relay that is not a K-Server", "Relay server"),
            icon: "network-vpn",
            reason: i18nc("@info:tooltip why a connection mode cannot be used", "no relay address is set")
        }
    ]

    contentItem: QQC2.ScrollView {
        id: railScroll

        // Three buttons never need a scrollbar, and this rail has no room for
        // one; the view is here so a fourth mode does not fall off the bottom.
        QQC2.ScrollBar.vertical.policy: QQC2.ScrollBar.AlwaysOff
        QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff
        contentWidth: availableWidth

        ColumnLayout {
            width: railScroll.availableWidth
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

                    onPicked: root.modeSelected(modelData.mode)
                }
            }
        }
    }
}
