// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// What is known about an outgoing message, which is less than most messengers
// imply.
//
// A tick used to go up the moment the message was handed to the socket, which
// over UDP says nothing: the receiver may have dropped it, and that is exactly
// what it looked like when it did. So there are two states and not three. The
// arrow means the datagram left this machine. The pair of ticks means the peer
// sent back a signed read receipt, which is the only thing here anybody else
// has vouched for.
QQC2.Label {
    id: root

    required property bool read

    // Escapes rather than the characters themselves, so this file stays ASCII.
    text: root.read ? "\u2713\u2713" : "\u2191"
    textFormat: Text.PlainText
    font: Kirigami.Theme.smallFont
    color: root.read ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor

    Accessible.name: root.read
        ? i18nc("@info:whatsthis state of an outgoing message", "Read by the recipient")
        : i18nc("@info:whatsthis state of an outgoing message", "Sent, delivery not confirmed")

    HoverHandler {
        id: markHover
    }

    QQC2.ToolTip.visible: markHover.hovered
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
    QQC2.ToolTip.text: root.read
        ? i18nc("@info:tooltip state of an outgoing message", "Read by the recipient")
        : i18nc("@info:tooltip state of an outgoing message", "Sent - delivery not confirmed")
}
