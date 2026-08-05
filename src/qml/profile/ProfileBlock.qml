// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// A dimmed caption over whatever it is a caption for.
//
// The one shape every profile surface is built out of below the identity block:
// a small grey word, then the thing itself. It was written out four times - twice
// on the peer card, twice on the profile pages - and the four had drifted into
// three different gaps and two different label colours.
ColumnLayout {
    id: root

    property string label: ""

    default property alias content: body.data

    spacing: 0

    QQC2.Label {
        Layout.fillWidth: true
        visible: root.label.length > 0
        text: root.label
        textFormat: Text.PlainText
        elide: Text.ElideRight
        font: Kirigami.Theme.smallFont
        color: Kirigami.Theme.disabledTextColor
    }

    ColumnLayout {
        id: body
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing
    }
}
