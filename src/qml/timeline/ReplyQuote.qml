// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// One line of the original and no more: a quote that wrapped to four lines would
// be taller than the reply under it, and the point of it is to say which message,
// not to say it again.
RowLayout {
    id: root

    property string author: ""
    property string excerpt: ""
    // Empty for a reply from before quotes carried an id; the stripe leads nowhere.
    property string targetId: ""

    readonly property bool followable: root.targetId.length > 0

    signal jumpRequested(string msgId)

    spacing: Kirigami.Units.smallSpacing

    Rectangle {
        Layout.fillHeight: true
        Layout.preferredWidth: Math.round(Kirigami.Units.smallSpacing / 2)
        radius: width / 2
        color: Kirigami.Theme.highlightColor
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0

        QQC2.Label {
            Layout.fillWidth: true
            visible: root.author.length > 0
            text: root.author
            textFormat: Text.PlainText
            elide: Text.ElideRight
            font.pointSize: Kirigami.Theme.smallFont.pointSize
            font.bold: true
            color: Kirigami.Theme.highlightColor
        }

        QQC2.Label {
            Layout.fillWidth: true
            text: root.excerpt
            textFormat: Text.PlainText
            elide: Text.ElideRight
            maximumLineCount: 1
            font: Kirigami.Theme.smallFont
            color: Kirigami.Theme.disabledTextColor
        }
    }

    HoverHandler {
        enabled: root.followable
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        enabled: root.followable
        acceptedButtons: Qt.LeftButton
        onTapped: root.jumpRequested(root.targetId)
    }
}
