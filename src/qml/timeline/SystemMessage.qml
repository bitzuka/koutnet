// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// The application talking, rather than either side of the conversation: a key
// changed, a transfer failed, a call ended.
//
// Centred and unattributed on purpose. It sits in the same column as the
// messages but takes no gutter, no avatar and no time, because none of those are
// true of it.
QQC2.Label {
    horizontalAlignment: Text.AlignHCenter
    wrapMode: Text.WordWrap
    textFormat: Text.PlainText
    topPadding: Kirigami.Units.smallSpacing
    bottomPadding: Kirigami.Units.smallSpacing
    font: Kirigami.Theme.smallFont
    color: Kirigami.Theme.disabledTextColor
}
