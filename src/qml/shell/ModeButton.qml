// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.delegates as Delegates

// RoundedItemDelegate rather than a ToolButton so the selected fill matches the
// conversation list beside it - on a rail of identical circles that fill is the
// only thing saying which mode is current.
Delegates.RoundedItemDelegate {
    id: root

    // NetworkManager.ConnectionMode, as an int - the enum is not registered in
    // QML and the persisted setting is an int either way.
    required property int mode
    required property string modeName
    property string unavailableReason: ""
    property bool current: false

    signal picked()

    readonly property bool available: root.unavailableReason.length === 0

    // The base class ties this to the view's current item; the rail is a column
    // and has no current item, so it is said here instead.
    checked: root.current
    enabled: root.available
    // An unreachable mode is greyed rather than hidden: a button that appears
    // later moves everything under it.
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
        color: root.current ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
        isMask: true
    }
}
