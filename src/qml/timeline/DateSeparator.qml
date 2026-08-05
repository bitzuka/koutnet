// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import koutnet.app

// The rule between two days of a conversation, with the date sitting in a gap in
// the middle of it.
//
// Not Kirigami.Chip: that one is a control - checkable, closable, focusable -
// and this is a label. Not ListSectionHeader either, which puts its text at the
// leading edge and its rule after it; a timeline reads down the middle.
RowLayout {
    id: root

    // Unix seconds of the first message of the day.
    required property double whenSecs

    spacing: Kirigami.Units.largeSpacing

    Kirigami.Separator {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
    }

    QQC2.Label {
        id: dateLabel

        Layout.alignment: Qt.AlignVCenter
        leftPadding: Kirigami.Units.largeSpacing
        rightPadding: Kirigami.Units.largeSpacing
        topPadding: Kirigami.Units.smallSpacing
        bottomPadding: Kirigami.Units.smallSpacing

        // RelativeTime.now is read so the newest chip rewrites itself from
        // "Today" to "Yesterday" at midnight without a message arriving.
        text: RelativeTime.daySeparator(root.whenSecs, RelativeTime.now)
        textFormat: Text.PlainText
        font: Kirigami.Theme.smallFont
        color: Kirigami.Theme.disabledTextColor

        Accessible.role: Accessible.Heading

        background: Rectangle {
            radius: height / 2
            color: Kirigami.ColorUtils.tintWithAlpha(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.07)
        }
    }

    Kirigami.Separator {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
    }
}
