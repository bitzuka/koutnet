// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtMultimedia
import org.kde.kirigami as Kirigami
import Qt.labs.platform 1.1 as Labs
import QtPositioning
// TypingIndicator lives under qml/timeline; every file here is one module.
import koutnet.app

// A TextArea and not a TextField: a one-line field turns a paragraph into a
// horizontally scrolling slot that cannot be proof-read. It grows to a cap and then
// scrolls, because a composer that can eat the conversation above it is a problem.
ColumnLayout {
    id: root

    property string replyAuthor: ""
    property string replyExcerpt: ""
    property string replyId: ""
    property bool peerTyping: false
    property string peerName: ""

    // Edit takes over the composer: a second editor is a second place to look.
    property int editingRow: -1

    // A spoiler carries the text hidden until the reader uncovers it.
    property bool spoilerMode: false

    property bool recording: false
    property int voiceMs: 0
    property string voicePath: ""
    property string voiceActualPath: ""

    readonly property bool replying: root.replyExcerpt.length > 0
    readonly property bool editing: root.editingRow >= 0
    readonly property alias text: input.text

    signal sendRequested(string text, string replyExcerpt, string replyAuthor, string replyId)
    signal editSubmitted(int row, string text)
    signal attachRequested()
    signal emojiRequested()
    signal replyCancelled()
    // Matrix understands a hidden message (MSC2446); other transports get it as
    // plain text, which the bridge does when it does not know the dialect.
    signal spoilerRequested(string text)
    signal locationRequested(real latitude, real longitude, string label)
    // Ask the page to open a single sticker picker. The page owns the dialog
    // and converts the URL to a local path; this signal carries nothing.
    signal stickerPickRequested()
    // Path and duration of a recorded voice clip.
    signal voiceCaptured(string filePath, int durationMs)
    // a poll to create: question and the non-empty answer options, in order.
    signal pollRequested(string question, var answers)
    // One notice per typingNoticeMs, not one per keystroke: a datagram per
    // character is a keylogger and it floods.
    signal typingNotice()

    readonly property int typingNoticeMs: 3000

    spacing: 0

    function focusInput() {
        input.forceActiveFocus()
    }

    function startEdit(row, body) {
        // One field, two jobs, so starting either puts the other away.
        root.replyCancelled()
        root.editingRow = row
        input.text = body
        input.cursorPosition = input.length
        input.forceActiveFocus()
    }

    function cancelEdit() {
        root.editingRow = -1
        input.clear()
    }

    function insert(str) {
        input.insert(input.cursorPosition, str)
        input.forceActiveFocus()
    }

    function send() {
        if (input.text.trim().length === 0)
            return
        if (root.editing) {
            root.editSubmitted(root.editingRow, input.text)
            root.cancelEdit()
            return
        }
        // Sent as a spoiler, then toggle is cleared.
        if (root.spoilerMode) {
            root.spoilerRequested(input.text)
            root.spoilerMode = false
            input.clear()
            root.replyCancelled()
            return
        }
        root.sendRequested(input.text, root.replyExcerpt, root.replyAuthor, root.replyId)
        input.clear()
        root.replyCancelled()
    }

    Timer {
        id: typingThrottle
        interval: root.typingNoticeMs
        repeat: false
    }

    // Above the composer rather than in the timeline: floating it over the newest
    // message covers the thing it is announcing.
    TypingIndicator {
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.largeSpacing
        Layout.rightMargin: Kirigami.Units.largeSpacing
        visible: root.peerTyping
        Layout.preferredHeight: visible ? -1 : 0
        peerName: root.peerName
    }

    // Same shape as the reply bar below, being the same kind of statement.
    QQC2.ToolBar {
        Layout.fillWidth: true
        visible: root.editing

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: "document-edit"
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: i18nc("@info:status the composer is rewriting an existing message", "Editing a message")
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                font.bold: true
                color: Kirigami.Theme.highlightColor
            }

            QQC2.ToolButton {
                display: QQC2.AbstractButton.IconOnly
                icon.name: "dialog-close"
                text: i18nc("@action:button leave the message as it was", "Cancel the edit")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                QQC2.ToolTip.text: text
                onClicked: root.cancelEdit()
            }
        }
    }

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

            QQC2.ToolButton {
                Layout.alignment: Qt.AlignBottom
                display: QQC2.AbstractButton.IconOnly
                icon.name: root.spoilerMode ? "visibility" : "visibility-off"
                text: root.spoilerMode
                    ? i18nc("@action:button send the next message hidden until revealed", "Spoiler on: the message will be hidden")
                    : i18nc("@action:button hide the next message until the reader reveals it", "Hide the message as a spoiler")
                checked: root.spoilerMode
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                QQC2.ToolTip.text: text
                onClicked: root.spoilerMode = !root.spoilerMode
            }

            QQC2.ToolButton {
                Layout.alignment: Qt.AlignBottom
                display: QQC2.AbstractButton.IconOnly
                icon.name: "map-symbolic"
                text: i18nc("@action:button share a location in the message", "Share a location")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                QQC2.ToolTip.text: text
                onClicked: locationDialog.open()
            }

            QQC2.ToolButton {
                Layout.alignment: Qt.AlignBottom
                display: QQC2.AbstractButton.IconOnly
                icon.name: root.recording ? "media-playback-stop" : "microphone"
                text: root.recording
                    ? i18nc("@action:button stop recording the voice message", "Stop recording")
                    : i18nc("@action:button record and send a voice message", "Send a voice message")
                checked: root.recording
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                QQC2.ToolTip.text: text
                onClicked: root.recording ? root.stopVoice() : root.startVoice()
            }

            QQC2.ToolButton {
                Layout.alignment: Qt.AlignBottom
                display: QQC2.AbstractButton.IconOnly
                icon.name: "emblem-favorite"
                text: i18nc("@action:button send a picture as a sticker", "Send a sticker")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                QQC2.ToolTip.text: text
                onClicked: root.stickerPickRequested()
            }

            QQC2.ToolButton {
                Layout.alignment: Qt.AlignBottom
                display: QQC2.AbstractButton.IconOnly
                icon.name: "office-chart-pie"
                text: i18nc("@action:button create a poll to send", "Create a poll")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                QQC2.ToolTip.text: text
                onClicked: pollDialog.open()
            }

            QQC2.ScrollView {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
                // Eight grid units is five lines; past that it is a text editor.
                Layout.maximumHeight: Kirigami.Units.gridUnit * 8
                Layout.preferredHeight: Math.min(input.implicitHeight, Layout.maximumHeight)

                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

                QQC2.TextArea {
                    id: input

                    placeholderText: i18nc("@info:placeholder", "Write a message...")
                    wrapMode: TextEdit.Wrap
                    // The toolbar already draws an edge round the whole row.
                    background: null

                    Keys.onPressed: (event) => {
                        // Escape abandons an edit, as every other field does.
                        if (event.key === Qt.Key_Escape && root.editing) {
                            root.cancelEdit()
                            event.accepted = true
                            return
                        }
                        const isReturn = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                        if (!isReturn)
                            return
                        // Shift+Enter newline, Enter send: the other way round loses
                        // a message every time somebody wants a second paragraph.
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
                text: root.editing
                    ? i18nc("@action:button save the message being rewritten", "Save")
                    : i18nc("@action:button", "Send")
                icon.name: root.editing ? "document-save" : "document-send"
                display: QQC2.AbstractButton.IconOnly
                // Highlighted carries the accent, so there is no background here.
                highlighted: true
                enabled: input.text.trim().length > 0
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                QQC2.ToolTip.text: root.editing
                    ? i18nc("@info:tooltip", "Save the change. Escape leaves the message as it was.")
                    : i18nc("@info:tooltip", "Send the message. Shift+Enter writes a new line instead.")
                onClicked: root.send()
            }
        }
    }

    // A small form rather than a map picker: the map would be a second surface to
    // load, and a coordinate is what the wire actually carries (geo:lat,lon).
    Kirigami.PromptDialog {
        id: locationDialog

        title: i18nc("@title:window share a location", "Share a location")

        // Defaults to a recognisable spot so the field is never empty on first try.
        property real latitude: 0.0
        property real longitude: 0.0
        property string label: ""

        onOpened: {
            locationDialog.latitude = 0.0
            locationDialog.longitude = 0.0
            locationDialog.label = ""
        }

        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel

        onAccepted: root.locationRequested(locationDialog.latitude, locationDialog.longitude, locationDialog.label)

        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.TextField {
                Layout.fillWidth: true
                placeholderText: i18nc("@info:placeholder", "Latitude, e.g. 48.858370")
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onTextChanged: locationDialog.latitude = parseFloat(text) || 0.0
            }

            QQC2.TextField {
                Layout.fillWidth: true
                placeholderText: i18nc("@info:placeholder", "Longitude, e.g. 2.294481")
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onTextChanged: locationDialog.longitude = parseFloat(text) || 0.0
            }

            QQC2.TextField {
                Layout.fillWidth: true
                placeholderText: i18nc("@info:placeholder a name for the shared place", "Label (optional)")
                onTextChanged: locationDialog.label = text
            }

            QQC2.Button {
                Layout.fillWidth: true
                icon.name: "gps"
                text: i18nc("@action:button fill the coordinates from the device's position", "Use my location")
                onClicked: locationSource.update()
            }

            // Resolves the device position into the fields so the writer does not
            // have to type geo coordinates by hand.
            PositionSource {
                id: locationSource
                updateInterval: 0
                onPositionChanged: {
                    const c = locationSource.position.coordinate
                    if (c.isValid()) {
                        locationDialog.latitude = c.latitude
                        locationDialog.longitude = c.longitude
                    }
                }
            }
        }
    }

    // The sticker picker lives on ChatPage. Composer only asks for it, so the
    // path is picked once and does not travel through two dialogs.

    // One CaptureSession for the whole composer.
    CaptureSession {
        id: capture
        audioInput: AudioInput {}
        recorder: MediaRecorder {
            id: voiceRecorder
            audioBitRate: 24000
            audioSampleRate: 48000
            audioChannelCount: 1
            // NOTE: QMediaFormat is not instantiable from QML in this Qt build, so the
            // container cannot be forced to Ogg/Opus here; the recorder emits MP4/AAC.
            // The file therefore uses a .m4a extension and the bridge tags it audio/mp4
            // so the bytes, extension and mime agree (a .ogg name around MP4 bytes is
            // what made the server reject it before).
            // Fires at record START, not finalisation; only the path is recorded here.
            onActualLocationChanged: (loc) => root.voiceActualPath = loc
        }
    }

    Timer {
        id: voiceTimer
        interval: 1000
        repeat: true
        onTriggered: root.voiceMs += 1000
    }

    // Waits for the MP4 trailer to flush after stop(). voiceActualPath
    // (if set) is preferred over the intended path.
    Timer {
        id: voiceFlushTimer
        interval: 1200
        onTriggered: root.handVoiceOff(root.voiceActualPath || root.voicePath)
    }

    function startVoice() {
        const dir = Labs.StandardPaths.writableLocation(Labs.StandardPaths.TempLocation)
        root.voicePath = dir + "/koutnet-voice-" + Date.now() + ".m4a"
        voiceRecorder.outputLocation = root.voicePath
        root.voiceActualPath = ""
        root.voiceMs = 0
        root.voiceHandedOff = false
        voiceRecorder.record()
        root.recording = true
        voiceTimer.restart()
    }

    function stopVoice() {
        voiceRecorder.stop()
        voiceTimer.stop()
        root.recording = false
        // Hand the clip off only after the recorder has flushed the trailer.
        // Doing it here or from actualLocationChanged (which fires at record
        // start) uploads an incomplete file.
        voiceFlushTimer.restart()
    }

    // Either the recorder's actualLocation signal or stopVoice() may fire; only
    // the first one gets through so a clip is never uploaded twice.
    property bool voiceHandedOff: false
    function handVoiceOff(filePath) {
        if (root.voiceHandedOff)
            return
        root.voiceHandedOff = true
        root.voiceCaptured(filePath, root.voiceMs)
    }

    // A question and growing answer rows; the wire only needs the option list.
    Kirigami.PromptDialog {
        id: pollDialog

        title: i18nc("@title:window create a poll", "Create a poll")

        // The editable answer list. Started with two empties; the dialog collects
        // the non-empty ones and ignores the rest on send.
        property var answers: ["", ""]

        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel

        onAccepted: {
            const chosen = []
            for (let i = 0; i < pollDialog.answers.length; ++i) {
                const a = pollDialog.answers[i].trim()
                if (a.length > 0)
                    chosen.push(a)
            }
            if (questionField.text.trim().length > 0 && chosen.length >= 2)
                root.pollRequested(questionField.text.trim(), chosen)
        }
        onOpened: pollDialog.answers = ["", ""]

        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.TextField {
                id: questionField
                Layout.fillWidth: true
                placeholderText: i18nc("@info:placeholder the poll question", "Question")
            }

            Repeater {
                model: pollDialog.answers
                delegate: QQC2.TextField {
                    Layout.fillWidth: true
                    placeholderText: i18nc("@info:placeholder an answer option, %1 is its number", "Answer %1", index + 1)
                    text: pollDialog.answers[index]
                    onTextChanged: pollDialog.answers[index] = text
                }
            }

            QQC2.Button {
                Layout.alignment: Qt.AlignLeft
                text: i18nc("@action:button add another answer row to the poll", "Add answer")
                icon.name: "list-add"
                onClicked: pollDialog.answers = pollDialog.answers.concat("")
            }
        }
    }
}
