// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components

// Who you are, pinned to the bottom of the conversation list, with the two
// controls that belong to you rather than to a conversation.
//
// The name and handle are read straight off AppSettings rather than passed in:
// this is the one place in the window that shows them, and threading two strings
// down from the window only to have them go stale is worse than the coupling.
// Mute and deafen are passed in, because the window is what holds the call.
QQC2.ToolBar {
    id: root

    readonly property string handle: appSettings.username || ""
    readonly property string shownName: appSettings.displayName && appSettings.displayName.length > 0
        ? appSettings.displayName
        : root.handle

    property bool micMuted: false
    property bool deafened: false

    signal profileRequested()
    signal settingsRequested()
    signal micToggled()
    signal deafenToggled()

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
                    // The picture, falling back to the initial the way every
                    // conversation row above does. This was the one place in the
                    // window showing a blank circle to somebody who had gone to
                    // the trouble of setting one.
                    name: root.shownName
                    source: appSettings.avatarPath
                    asynchronous: true
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

        // Deafened holds the microphone down whatever the mute toggle says, so
        // the icon follows both and the button itself goes quiet: a control
        // that cannot change anything should not look as though it can.
        QQC2.ToolButton {
            Layout.alignment: Qt.AlignVCenter
            display: QQC2.AbstractButton.IconOnly
            icon.name: (root.micMuted || root.deafened)
                ? "microphone-sensitivity-muted"
                : "audio-input-microphone"
            enabled: !root.deafened
            text: root.micMuted
                ? i18nc("@action:button let your microphone be heard again", "Unmute microphone")
                : i18nc("@action:button silence your own microphone", "Mute microphone")

            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            QQC2.ToolTip.text: text

            onClicked: root.micToggled()
        }

        QQC2.ToolButton {
            Layout.alignment: Qt.AlignVCenter
            display: QQC2.AbstractButton.IconOnly
            icon.name: root.deafened ? "audio-volume-muted" : "audio-volume-high"
            text: root.deafened
                ? i18nc("@action:button start hearing calls again", "Undeafen")
                : i18nc("@action:button stop hearing calls, and stop being heard", "Deafen")

            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            QQC2.ToolTip.text: text

            onClicked: root.deafenToggled()
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
