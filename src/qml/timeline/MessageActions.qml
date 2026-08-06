// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// A strip on hover rather than four buttons on every row: four per message is
// forty on a screenful, all competing with the text they belong to. The same four
// are on the context menu for anyone without a pointer.
QQC2.Control {
    id: root

    // Nothing to edit on a file, and nothing this client can do about a peer's.
    property bool canEdit: false

    signal reactRequested()
    signal replyRequested()
    signal editRequested()
    signal menuRequested()

    padding: Math.round(Kirigami.Units.smallSpacing / 2)

    // The strip sits over the message text, so it needs an edge to be legible against.
    background: Kirigami.ShadowedRectangle {
        radius: Kirigami.Units.cornerRadius
        color: Kirigami.Theme.backgroundColor
        border.width: 1
        border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.2)
        shadow.size: Kirigami.Units.smallSpacing
        shadow.color: Qt.rgba(0, 0, 0, 0.15)
        shadow.yOffset: 1
    }

    contentItem: RowLayout {
        spacing: 0

        QQC2.ToolButton {
            display: QQC2.AbstractButton.IconOnly
            icon.name: "smiley-add"
            icon.width: Kirigami.Units.iconSizes.small
            icon.height: Kirigami.Units.iconSizes.small
            text: i18nc("@action:button react to this message with an emoji", "React")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            QQC2.ToolTip.text: text
            onClicked: root.reactRequested()
        }

        QQC2.ToolButton {
            display: QQC2.AbstractButton.IconOnly
            icon.name: "mail-replied-symbolic"
            icon.width: Kirigami.Units.iconSizes.small
            icon.height: Kirigami.Units.iconSizes.small
            text: i18nc("@action:button quote this message in your next one", "Reply")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            QQC2.ToolTip.text: text
            onClicked: root.replyRequested()
        }

        QQC2.ToolButton {
            visible: root.canEdit
            display: QQC2.AbstractButton.IconOnly
            icon.name: "document-edit"
            icon.width: Kirigami.Units.iconSizes.small
            icon.height: Kirigami.Units.iconSizes.small
            text: i18nc("@action:button change what this message says", "Edit")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            QQC2.ToolTip.text: text
            onClicked: root.editRequested()
        }

        QQC2.ToolButton {
            display: QQC2.AbstractButton.IconOnly
            icon.name: "overflow-menu"
            icon.width: Kirigami.Units.iconSizes.small
            icon.height: Kirigami.Units.iconSizes.small
            text: i18nc("@action:button everything else that can be done to this message", "More")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            QQC2.ToolTip.text: text
            onClicked: root.menuRequested()
        }
    }
}
