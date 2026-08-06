// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// ListSectionHeader already draws the label, the rule and the trailing row, so
// only the folding is written here. It is an ItemDelegate underneath, which is
// why clicking the label folds as well as clicking the chevron.
Kirigami.ListSectionHeader {
    id: root

    property int itemCount: 0
    // Owned by whoever placed the heading. The rows this folds live inside a
    // ListView delegate, which cannot see an id declared in the view's header,
    // so the one copy of the state has to sit above both of them.
    property bool expanded: true

    signal toggleRequested()

    // Hiding an empty section is the caller's question, not this one's.

    Accessible.name: root.text
    Accessible.description: root.expanded
        ? i18nc("@info:whatsthis state of a foldable list section", "Expanded")
        : i18nc("@info:whatsthis state of a foldable list section", "Collapsed")

    onClicked: root.toggleRequested()

    QQC2.Label {
        visible: !root.expanded && root.itemCount > 0
        text: String(root.itemCount)
        font: Kirigami.Theme.smallFont
        color: Kirigami.Theme.disabledTextColor
    }

    QQC2.ToolButton {
        display: QQC2.AbstractButton.IconOnly
        icon.name: root.expanded ? "go-up" : "go-down"
        icon.width: Kirigami.Units.iconSizes.small
        icon.height: Kirigami.Units.iconSizes.small
        // The label carries the state rather than a checked property, so the
        // button and the heading cannot end up disagreeing about it.
        text: root.expanded
            ? i18nc("@action:button fold a section of the conversation list, %1 is the section name", "Collapse %1", root.text)
            : i18nc("@action:button unfold a section of the conversation list, %1 is the section name", "Expand %1", root.text)
        activeFocusOnTab: false

        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC2.ToolTip.text: text

        onClicked: root.toggleRequested()
    }
}
