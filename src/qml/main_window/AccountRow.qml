// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components

// Who you are, pinned to the bottom of the conversation list.
//
// The name and handle are read straight off AppSettings rather than passed in:
// this is the one place in the window that shows them, and threading two strings
// down from the window only to have them go stale is worse than the coupling.
QQC2.ToolBar {
    id: root

    readonly property string handle: appSettings.username || ""
    readonly property string shownName: appSettings.displayName && appSettings.displayName.length > 0
        ? appSettings.displayName
        : root.handle

    signal profileRequested()
    signal settingsRequested()

    position: QQC2.ToolBar.Footer

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        QQC2.ToolButton {
            Layout.fillWidth: true
            Layout.fillHeight: true

            text: i18nc("@action:button open your own profile", "My profile")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            QQC2.ToolTip.text: text

            onClicked: root.profileRequested()

            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                Components.Avatar {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: Kirigami.Units.iconSizes.medium
                    implicitHeight: Kirigami.Units.iconSizes.medium
                    name: root.shownName
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: root.shownName
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignLeft
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        visible: root.handle.length > 0
                        text: i18nc("@info your own handle, %1 is the user name", "@%1", root.handle)
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignLeft
                        font: Kirigami.Theme.smallFont
                        color: Kirigami.Theme.disabledTextColor
                    }
                }
            }
        }

        QQC2.ToolButton {
            Layout.alignment: Qt.AlignVCenter
            display: QQC2.AbstractButton.IconOnly
            icon.name: "settings-configure"
            text: i18nc("@action:button", "Settings")

            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            QQC2.ToolTip.text: text

            onClicked: root.settingsRequested()
        }
    }
}
