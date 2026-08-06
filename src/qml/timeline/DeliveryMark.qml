// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// The hourglass is the gap between the message appearing in the timeline and the
// datagram actually being written - the only honest thing "sending" can mean here.
// One tick means the datagram left this machine, which over UDP is the end of what
// this side can observe: nothing comes back to say it arrived. Two ticks means the
// peer returned a signed read receipt. Glyphs rather than Breeze icons:
// breeze-icons has dialog-ok for a single tick and nothing that reads as a pair.
QQC2.Label {
    id: root

    required property bool read
    // Never true alongside read: a receipt cannot arrive before its datagram.
    required property bool pending

    // Escapes rather than the characters themselves, so this file stays ASCII.
    text: root.pending ? "\u231B" : (root.read ? "\u2713\u2713" : "\u2713")
    textFormat: Text.PlainText
    font: Kirigami.Theme.smallFont
    color: root.read ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor

    Accessible.name: root.pending
        ? i18nc("@info:whatsthis state of an outgoing message", "Sending")
        : (root.read
            ? i18nc("@info:whatsthis state of an outgoing message", "Read by the recipient")
            : i18nc("@info:whatsthis state of an outgoing message", "Sent, delivery not confirmed"))

    HoverHandler {
        id: markHover
    }

    QQC2.ToolTip.visible: markHover.hovered
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
    QQC2.ToolTip.text: root.pending
        ? i18nc("@info:tooltip state of an outgoing message", "Sending")
        : (root.read
            ? i18nc("@info:tooltip state of an outgoing message", "Read by the recipient")
            : i18nc("@info:tooltip state of an outgoing message", "Sent - delivery not confirmed"))
}
