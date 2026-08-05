// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// The strip down the left edge of the window: how this client reaches the
// network, one round button per mode.
//
// It is here rather than buried three pages into the settings because the
// connection mode is the thing this application is for. A messenger that can be
// pointed at a LAN, a VPN, a K-Server or the maintainer's relay has to say which
// of those it is doing without being asked.
//
// The rail only picks a mode. Writing it to the running network layer is
// deliberately somebody else's job - switching mode tears a relay tunnel up or
// down, so it goes through the same apply that the settings page uses.
QQC2.Control {
    id: root

    // NetworkManager.ConnectionMode as an int, mirrored from AppSettings.
    property int currentMode: 0

    signal modeSelected(int mode)

    // Width comes from the buttons, which are square, plus the column's own
    // insets. Hardcoding a grid unit count here and a padding there is how a
    // rail ends up half a pixel wider than the separator beside it.
    implicitWidth: Kirigami.Units.iconSizes.smallMedium + Kirigami.Units.gridUnit * 2

    padding: 0

    // Mirrors NetworkManager::ConnectionMode. The values are persisted, so the
    // order here is the enum's order and not a nicer one.
    //
    // Every mode stays on the rail whether or not it works yet. Which of them
    // can be picked is NetworkManager's answer, not this file's - landing a
    // K-Server means changing modeAvailable() and nothing here.
    readonly property var modes: [
        {
            mode: 0,
            name: i18nc("@item connection mode, peers found on the local network or over a VPN adapter", "Local network or VPN"),
            icon: "network-workgroup",
            reason: i18nc("@info:tooltip why a connection mode cannot be used", "no local interface is up")
        },
        {
            mode: 1,
            name: i18nc("@item connection mode, a K-Server run by this user", "K-Server, self-hosted"),
            icon: "network-server",
            reason: i18nc("@info:tooltip why a connection mode cannot be used", "not built yet")
        },
        {
            mode: 2,
            name: i18nc("@item connection mode, somebody else's K-Server", "K-Server, someone else's"),
            icon: "network-connect",
            reason: i18nc("@info:tooltip why a connection mode cannot be used", "not built yet")
        },
        {
            mode: 3,
            name: i18nc("@item connection mode, a plain relay that is not a K-Server", "Relay server"),
            icon: "network-vpn",
            reason: i18nc("@info:tooltip why a connection mode cannot be used", "no relay address is set")
        },
        {
            mode: 4,
            name: i18nc("@item connection mode, the relay the project maintainer runs", "Maintainer's server"),
            icon: "cloudstatus",
            reason: i18nc("@info:tooltip why a connection mode cannot be used", "no server has been deployed yet")
        }
    ]

    contentItem: QQC2.ScrollView {
        id: railScroll

        // A rail this narrow has no room for a scrollbar, and five buttons never
        // need one; it is here only so a sixth mode does not fall off the bottom.
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
