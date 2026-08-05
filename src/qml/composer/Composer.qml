// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
// TypingIndicator lives under qml/timeline; every QML file in this application
// is registered into the one module, so the import is how it is reached.
import koutnet.app

// Where a message is written: what it answers, then the field and the three
// things that can be done to it.
//
// A TextArea and not a TextField. A one-line field turns a paragraph into a
// horizontally scrolling slot that cannot be proof-read, and every messenger
// worth using grows instead. It grows to a cap and then scrolls, because a
// composer that can eat the conversation above it is its own problem.
ColumnLayout {
    id: root

    // Set while a reply is in progress. Cleared by the send, or by the cross.
    property string replyAuthor: ""
    property string replyExcerpt: ""
    property string replyId: ""
    property bool peerTyping: false
    property string peerName: ""

    readonly property bool replying: root.replyExcerpt.length > 0
    readonly property alias text: input.text

    signal sendRequested(string text, string replyExcerpt, string replyAuthor, string replyId)
    signal attachRequested()
    signal emojiRequested()
    signal replyCancelled()
    // Throttled: one notice per typingNoticeMs of continuous writing, not one
    // per keystroke. A datagram per character is a keylogger with extra steps
    // as far as the wire is concerned, and it floods the peer's rate limit.
    signal typingNotice()

    readonly property int typingNoticeMs: 3000

    spacing: 0

    function focusInput() {
        input.forceActiveFocus()
    }

    function insert(str) {
        input.insert(input.cursorPosition, str)
        input.forceActiveFocus()
    }

    function send() {
        if (input.text.trim().length === 0)
            return
        root.sendRequested(input.text, root.replyExcerpt, root.replyAuthor, root.replyId)
        input.clear()
        root.replyCancelled()
    }

    Timer {
        id: typingThrottle
        interval: root.typingNoticeMs
        repeat: false
    }

    // The peer's typing notice sits above the composer rather than in the
    // timeline: floating it over the newest message covers the thing it is
    // announcing, and here it is already where the eye is.
    TypingIndicator {
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.largeSpacing
        Layout.rightMargin: Kirigami.Units.largeSpacing
        visible: root.peerTyping
        Layout.preferredHeight: visible ? -1 : 0
        peerName: root.peerName
    }

    // What this message is answering, with a way out of it.
    QQC2.ToolBar {
        Layout.fillWidth: true
        visible: root.replying

        contentItem: RowLayout {
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
                    visible: root.replyAuthor.length > 0
                    text: i18nc("@info:status what a message being written is answering, %1 is the author",
                                "Replying to %1", root.replyAuthor)
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    font.bold: true
                    color: Kirigami.Theme.highlightColor
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    text: root.replyExcerpt
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                }
            }

            QQC2.ToolButton {
                display: QQC2.AbstractButton.IconOnly
                icon.name: "dialog-close"
                text: i18nc("@action:button stop replying to the quoted message", "Cancel the reply")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                QQC2.ToolTip.text: text
                onClicked: root.replyCancelled()
            }
        }
    }

    QQC2.ToolBar {
        Layout.fillWidth: true
        position: QQC2.ToolBar.Footer

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.ToolButton {
                Layout.alignment: Qt.AlignBottom
                display: QQC2.AbstractButton.IconOnly
                icon.name: "mail-attachment"
                text: i18nc("@action:button attach a file to the message", "Attach a file")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                QQC2.ToolTip.text: text
                onClicked: root.attachRequested()
            }

            QQC2.ToolButton {
                Layout.alignment: Qt.AlignBottom
                display: QQC2.AbstractButton.IconOnly
                icon.name: "smiley"
                text: i18nc("@action:button open the emoji picker", "Insert an emoji")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                QQC2.ToolTip.text: text
                onClicked: root.emojiRequested()
            }

            QQC2.ScrollView {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
                // Eight grid units is about five lines of the default font.
                // Past that the composer is a text editor and can scroll.
                Layout.maximumHeight: Kirigami.Units.gridUnit * 8
                Layout.preferredHeight: Math.min(input.implicitHeight, Layout.maximumHeight)

                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

                QQC2.TextArea {
                    id: input

                    placeholderText: i18nc("@info:placeholder", "Write a message...")
                    wrapMode: TextEdit.Wrap
                    // The toolbar already draws an edge round the whole row, and
                    // a second frame inside it is a box in a box.
                    background: null

                    Keys.onPressed: (event) => {
                        const isReturn = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                        if (!isReturn)
                            return
                        // Shift+Enter is the newline, Enter is the send. The
                        // other way round loses a message every time somebody
                        // reaches for a second paragraph.
                        if (event.modifiers & Qt.ShiftModifier)
                            return
                        root.send()
                        event.accepted = true
                    }

                    onTextChanged: {
                        if (input.text.length === 0 || typingThrottle.running)
                            return
                        typingThrottle.restart()
                        root.typingNotice()
                    }
                }
            }

            QQC2.Button {
                Layout.alignment: Qt.AlignBottom
                text: i18nc("@action:button", "Send")
                icon.name: "document-send"
                display: QQC2.AbstractButton.IconOnly
                // Highlighted is what carries the accent here, so the button
                // needs no background of its own.
                highlighted: true
                enabled: input.text.trim().length > 0
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                QQC2.ToolTip.text: i18nc("@info:tooltip", "Send the message. Shift+Enter writes a new line instead.")
                onClicked: root.send()
            }
        }
    }
}
