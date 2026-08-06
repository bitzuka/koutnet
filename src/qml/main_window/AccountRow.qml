// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components

// The name and handle are read straight off AppSettings rather than passed in:
// this is the one place that shows them, and threading two strings down only to
// have them go stale is worse than the coupling. Mute and deafen are passed in.
QQC2.ToolBar {
    id: root

    readonly property string handle: appSettings.username || ""
    readonly property string shownName: appSettings.displayName && appSettings.displayName.length > 0
        ? appSettings.displayName
        : root.handle

    property bool micMuted: false
    property bool deafened: false

    // In compact mode the name and handle go: three icon-only controls beside a
    // nine-unit column leave no room for two lines of text.
    property bool compact: false

    signal profileRequested(Item anchorItem)
    signal settingsRequested()
    signal micToggled()
    signal deafenToggled()

    position: QQC2.ToolBar.Footer

    // More than the toolbar default, which is sized for a single line of icons;
    // this row is two lines of text beside an avatar and is always on screen.
    padding: root.compact ? Kirigami.Units.smallSpacing : Kirigami.Units.largeSpacing

    contentItem: RowLayout {
        // The gap between identity and controls is the cluster's own left margin
        // below, so the row reads as two groups and not four spaced items.
        spacing: 0

        QQC2.ToolButton {
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Both labels elide but still ask for the width of the whole name, and
            // a RowLayout honours that, so without a floor this button shouldered
            // the three controls off the end of a narrow column.
            Layout.minimumWidth: root.compact ? 0 : Kirigami.Units.gridUnit * 4

            // No tooltip: it repeated the name written on the button, and covered
            // it up while doing so.
            Accessible.name: i18nc("@action:button open your own profile", "My profile")

            // Hung off the whole row so the card lines up with the column edge.
            onClicked: root.profileRequested(root)

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Components.Avatar {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: root.compact
                        ? Kirigami.Units.iconSizes.medium
                        : Kirigami.Units.iconSizes.large
                    implicitHeight: implicitWidth
                    // Falls back to the initial the way the conversation rows do;
                    // this used to show a blank circle to anybody who set a picture.
                    name: root.shownName
                    source: appSettings.avatarPath
                    asynchronous: true
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    visible: !root.compact
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

        RowLayout {
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: Kirigami.Units.largeSpacing
            spacing: 0

            // Deafened holds the microphone down whatever the mute toggle says, so
            // the icon follows both and the button itself goes quiet.
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
