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

    // The row itself travels with the request: the account card is anchored to
    // it, and only this file knows which item that is.
    signal profileRequested(Item anchorItem)
    signal settingsRequested()
    signal micToggled()
    signal deafenToggled()

    position: QQC2.ToolBar.Footer

    // More than the toolbar default, which is sized for a single line of icons.
    // This row is two lines of text beside an avatar and it is on screen at all
    // times, so it is the last place in the window that should look squeezed.
    padding: Kirigami.Units.largeSpacing

    contentItem: RowLayout {
        // Nothing between the identity and the controls: the gap that separates
        // them is the cluster's own left margin below, so the row reads as two
        // groups rather than as four evenly spaced items.
        spacing: 0

        QQC2.ToolButton {
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Both labels elide, but they still ask for the width of the whole
            // name and a RowLayout honours that, so without a floor here this
            // button shouldered the three controls off the end of a narrow column.
            Layout.minimumWidth: Kirigami.Units.gridUnit * 4

            // No tooltip: the content of this button is the name and the handle,
            // so the tooltip repeated what was written underneath it and covered
            // it up while doing so.
            Accessible.name: i18nc("@action:button open your own profile", "My profile")

            // The card is hung off the whole row rather than off this button, so
            // it lines up with the column edge instead of with the avatar.
            onClicked: root.profileRequested(root)

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Components.Avatar {
                    Layout.alignment: Qt.AlignVCenter
                    // Bigger than a conversation row's. This is the one face that
                    // is always on screen, and it is what the row is anchored on.
                    implicitWidth: Kirigami.Units.iconSizes.large
                    implicitHeight: Kirigami.Units.iconSizes.large
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
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: root.shownName
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        maximumLineCount: 1
                        horizontalAlignment: Text.AlignLeft
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        visible: root.handle.length > 0
                        text: i18nc("@info your own handle, %1 is the user name", "@%1", root.handle)
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        maximumLineCount: 1
                        horizontalAlignment: Text.AlignLeft
                        font: Kirigami.Theme.smallFont
                        color: Kirigami.Theme.disabledTextColor
                    }
                }
            }
        }

        // What belongs to you rather than to a conversation: one cluster, held
        // off the name.
        RowLayout {
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: Kirigami.Units.largeSpacing
            spacing: 0

            // Deafened holds the microphone down whatever the mute toggle says, so
            // the icon follows both and the button itself goes quiet: a control
            // that cannot change anything should not look as though it can.
            QQC2.ToolButton {
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
}
