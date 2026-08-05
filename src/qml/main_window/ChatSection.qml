// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// A heading in the conversation list that the rows under it fold into.
//
// ListSectionHeader draws the label, the rule beside it and the trailing row
// this puts the chevron in, so the only thing written here is the folding. It is
// an ItemDelegate underneath, which is why clicking the label works as well as
// clicking the chevron - a heading with a hit target the size of a chevron is a
// heading nobody folds twice.
Kirigami.ListSectionHeader {
    id: root

    // How many rows are under it. Shown only while it is folded, where the count
    // is the only thing left saying the section has anything in it.
    property int itemCount: 0
    // Owned by whoever placed the heading, not by the heading. The rows this
    // folds are somewhere else in the list - inside a ListView delegate, which
    // cannot see an id declared in the view's header - so the one copy of the
    // state has to live above both of them.
    property bool expanded: true

    signal toggleRequested()

    // Whether an empty section is worth drawing is the caller's question, not
    // this one's: the conversation list hides one, the peer drawer keeps its
    // headings put so the sections do not shuffle as facts arrive.

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
