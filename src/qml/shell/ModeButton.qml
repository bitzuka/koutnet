// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.delegates as Delegates

// One connection mode in the rail: a round icon, and a tooltip that says which
// mode it is and what state it is in.
//
// RoundedItemDelegate rather than a ToolButton because the rail wants the same
// selected fill as the conversation list next to it, and that fill is the one
// thing a rail of five identical circles has to get right - it is the only way
// the current mode is visible at all.
Delegates.RoundedItemDelegate {
    id: root

    // NetworkManager.ConnectionMode, as an int - the enum is not registered in
    // QML and the persisted setting is an int either way.
    required property int mode
    // What the mode is called, one line, for the tooltip's first line.
    required property string modeName
    // Why it cannot be picked, or empty when it can.
    property string unavailableReason: ""
    property bool current: false

    signal picked()

    readonly property bool available: root.unavailableReason.length === 0

    // The base class ties this to the view's current item; the rail is a column
    // and has no current item, so it is said here instead.
    checked: root.current
    enabled: root.available
    // An unreachable mode is greyed rather than hidden: the shape of the plan is
    // the point of the rail, and a button that appears later moves everything
    // under it.
    opacity: root.available ? 1 : 0.45

    activeFocusOnTab: true

    Accessible.name: root.modeName
    Accessible.description: root.unavailableReason
    Accessible.onPressAction: root.picked()

    Keys.onSpacePressed: root.picked()
    Keys.onEnterPressed: root.picked()
    Keys.onReturnPressed: root.picked()

    onClicked: root.picked()

    QQC2.ToolTip.visible: root.hovered
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
    QQC2.ToolTip.text: root.available
        ? (root.current
            ? i18nc("@info:tooltip the connection mode in use, %1 is its name", "%1 - in use", root.modeName)
            : root.modeName)
        : i18nc("@info:tooltip a connection mode that cannot be used, %1 is its name, %2 says why",
                "%1 - %2", root.modeName, root.unavailableReason)

    contentItem: Kirigami.Icon {
        source: root.icon.name
        implicitWidth: Kirigami.Units.iconSizes.smallMedium
        implicitHeight: Kirigami.Units.iconSizes.smallMedium
        // The current mode is the one thing on this rail worth an accent.
        color: root.current ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
        isMask: true
    }
}
