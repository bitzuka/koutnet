// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// A Flow rather than a RowLayout: fifteen reactions on one message is unusual but
// not wrong, and the row has to wrap rather than push the timeline sideways.
Flow {
    id: root

    // ReactionStore::summary() output: a list of { emoji, count, users }.
    property var reactions: []
    property string selfName: ""

    signal toggled(string emoji)
    signal addRequested()

    spacing: Kirigami.Units.smallSpacing

    Repeater {
        model: root.reactions

        delegate: QQC2.AbstractButton {
            id: pill

            required property var modelData

            readonly property var users: pill.modelData.users || []
            readonly property bool mine: pill.users.indexOf(root.selfName) !== -1

            height: Math.round(Kirigami.Units.gridUnit * 1.5)
            width: Math.max(height, pillLabel.implicitWidth + Kirigami.Units.smallSpacing * 3)

            hoverEnabled: true

            Accessible.name: i18ncp("@info:whatsthis a reaction on a message, %1 is a count, %2 is the emoji",
                                    "%2, reacted to once", "%2, reacted to %1 times",
                                    pill.modelData.count, pill.modelData.emoji)

            QQC2.ToolTip.visible: pill.hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            QQC2.ToolTip.text: pill.users.length > 0
                ? i18nc("@info:tooltip who reacted, %1 is a comma separated list of names",
                        "Reacted by %1", pill.users.join(i18nc("@item separator between names in a list", ", ")))
                : i18nc("@info:tooltip nobody's name is known for this reaction", "Nobody in particular")

            contentItem: QQC2.Label {
                id: pillLabel
                anchors.centerIn: parent
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.PlainText
                // A space rather than a glued "3x": the number is an aside.
                text: i18nc("@item reaction pill, %1 is the emoji and %2 how many reacted with it",
                            "%1 %2", pill.modelData.emoji, pill.modelData.count)
                font: Kirigami.Theme.smallFont
            }

            background: Rectangle {
                radius: height / 2
                // Your own is the accent, so a wall of pills still says which are yours.
                color: pill.mine
                    ? Kirigami.ColorUtils.tintWithAlpha(Kirigami.Theme.backgroundColor, Kirigami.Theme.highlightColor, 0.3)
                    : Kirigami.ColorUtils.tintWithAlpha(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.08)
                border.width: pill.hovered || pill.visualFocus ? 1 : 0
                border.color: Kirigami.Theme.highlightColor
            }

            onClicked: root.toggled(pill.modelData.emoji)
        }
    }

    // Once a message has reactions, the row is where the eye already is.
    QQC2.AbstractButton {
        id: addButton

        height: Math.round(Kirigami.Units.gridUnit * 1.5)
        width: height
        visible: root.reactions.length > 0
        hoverEnabled: true

        text: i18nc("@action:button add a reaction to this message", "Add a reaction")

        QQC2.ToolTip.visible: addButton.hovered
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC2.ToolTip.text: addButton.text

        contentItem: Kirigami.Icon {
            source: "smiley-add"
            implicitWidth: Kirigami.Units.iconSizes.small
            implicitHeight: Kirigami.Units.iconSizes.small
            isMask: true
            color: Kirigami.Theme.disabledTextColor
        }

        background: Rectangle {
            radius: height / 2
            color: addButton.hovered
                ? Kirigami.ColorUtils.tintWithAlpha(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.08)
                : "transparent"
        }

        onClicked: root.addRequested()
    }
}
