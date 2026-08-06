// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtMultimedia
import org.kde.kirigami as Kirigami

// Two sources, one presentation. A LAN transfer has already put the file on this
// machine and hands over a path; a Matrix attachment is still on a homeserver and
// hands over a URL that libQuotient's network manager knows how to fetch - see the
// factory in main.cpp. Everything below sourceUrl is written against the URL and
// does not care which of the two produced it.
ColumnLayout {
    id: root

    // The LAN side: a local path, no scheme.
    property string filePath: ""
    // The Matrix side: an already-usable URL, mxc:// or otherwise.
    property string mediaUrl: ""
    // "", "image", "video", "audio" or "file". Empty for a LAN attachment,
    // which says what it is with isImage instead.
    property string mediaKind: ""

    property string fileName: ""
    property bool isImage: false
    // What the sender said the picture measures. Used to reserve the right
    // shape before a byte of it has arrived, so the timeline does not jump once
    // one does; zero falls back to whatever the decoder reports.
    property int mediaWidth: 0
    property int mediaHeight: 0
    property int mediaDurationMs: 0

    property real maxImageWidth: Kirigami.Units.gridUnit * 18

    signal imageActivated(string source)
    signal fileActivated(string source)

    readonly property string sourceUrl: root.mediaUrl.length > 0
        ? root.mediaUrl
        : (root.filePath.length > 0 ? "file://" + root.filePath : "")

    readonly property bool showsImage: root.sourceUrl.length > 0
        && (root.isImage || root.mediaKind === "image")
    readonly property bool showsVideo: root.sourceUrl.length > 0 && root.mediaKind === "video"
    readonly property bool showsAudio: root.sourceUrl.length > 0 && root.mediaKind === "audio"
    // An attachment event whose media this session cannot reach. Said in words
    // rather than drawn as a broken frame.
    readonly property bool unreachable: root.sourceUrl.length === 0

    // mm:ss, and hh:mm:ss once there is an hour to show.
    function clockOf(ms) {
        if (!ms || ms <= 0)
            return ""
        const total = Math.round(ms / 1000)
        const s = total % 60
        const m = Math.floor(total / 60) % 60
        const h = Math.floor(total / 3600)
        const pad = (n) => (n < 10 ? "0" + n : "" + n)
        return h > 0 ? h + ":" + pad(m) + ":" + pad(s) : m + ":" + pad(s)
    }

    spacing: Kirigami.Units.smallSpacing

    Image {
        id: preview

        // The declared size wins over the decoded one: the decoded one is not
        // known until the download finishes, and using it moves every message
        // below this one at that moment.
        readonly property real naturalWidth: root.mediaWidth > 0
            ? root.mediaWidth
            : (sourceSize.width > 0 ? sourceSize.width : root.maxImageWidth)
        readonly property real naturalHeight: root.mediaHeight > 0
            ? root.mediaHeight
            : (sourceSize.height > 0 ? sourceSize.height : 0)

        // Sized off the cap and not its own width: a height that reads back the
        // width the layout just gave it is the binding loop this is written around.
        readonly property real shownWidth: Math.min(root.maxImageWidth, preview.naturalWidth)

        Layout.preferredWidth: preview.shownWidth
        Layout.preferredHeight: preview.naturalHeight > 0
            ? Math.round(preview.shownWidth * (preview.naturalHeight / preview.naturalWidth))
            : Kirigami.Units.gridUnit * 8
        // Tall pictures get cropped by the viewer rather than by pushing three
        // messages off the screen.
        Layout.maximumHeight: Kirigami.Units.gridUnit * 20

        visible: root.showsImage
        source: root.showsImage ? root.sourceUrl : ""
        fillMode: Image.PreserveAspectFit
        mipmap: true
        asynchronous: true
        // A room picture can be a twelve-megapixel photograph, and decoding one
        // of those at full size for a thumbnail is the difference between a
        // scroll that stutters and one that does not.
        sourceSize.width: Math.round(root.maxImageWidth)

        Accessible.role: Accessible.Graphic
        Accessible.name: root.fileName

        QQC2.BusyIndicator {
            anchors.centerIn: parent
            running: preview.status === Image.Loading
            visible: running
        }

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            onTapped: root.imageActivated(root.sourceUrl)
        }
    }

    // Loaded rather than declared: a MediaPlayer is a decoder, and one per row
    // would open every recording in the backlog at once.
    Loader {
        Layout.fillWidth: true
        active: root.showsVideo || root.showsAudio
        visible: active

        sourceComponent: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            MediaPlayer {
                id: player

                source: root.sourceUrl
                audioOutput: AudioOutput {}
                videoOutput: root.showsVideo ? videoSurface : null
            }

            Rectangle {
                Layout.preferredWidth: Math.min(root.maxImageWidth,
                                                root.mediaWidth > 0 ? root.mediaWidth : root.maxImageWidth)
                Layout.preferredHeight: root.mediaWidth > 0 && root.mediaHeight > 0
                    ? Math.round(width * (root.mediaHeight / root.mediaWidth))
                    : Kirigami.Units.gridUnit * 10
                Layout.maximumHeight: Kirigami.Units.gridUnit * 20
                visible: root.showsVideo
                radius: Kirigami.Units.cornerRadius
                // Black rather than the theme's background: a letterboxed frame
                // against a light surface reads as a rendering fault.
                color: "black"

                VideoOutput {
                    id: videoSurface
                    anchors.fill: parent
                    fillMode: VideoOutput.PreserveAspectFit
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.ToolButton {
                    display: QQC2.AbstractButton.IconOnly
                    icon.name: player.playbackState === MediaPlayer.PlayingState
                        ? "media-playback-pause"
                        : "media-playback-start"
                    text: player.playbackState === MediaPlayer.PlayingState
                        ? i18nc("@action:button pause the attached recording", "Pause")
                        : i18nc("@action:button play the attached recording", "Play")
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    QQC2.ToolTip.text: text
                    onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
                }

                QQC2.Slider {
                    Layout.fillWidth: true
                    from: 0
                    // Never zero: a slider with an empty range cannot be moved,
                    // and duration is zero until the header has been read.
                    to: Math.max(1, player.duration > 0 ? player.duration : root.mediaDurationMs)
                    // Following the position while the handle is held fights
                    // whoever is holding it.
                    value: pressed ? value : player.position
                    onMoved: player.position = value
                }

                QQC2.Label {
                    text: root.clockOf(player.duration > 0 ? player.duration : root.mediaDurationMs)
                    textFormat: Text.PlainText
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                }

                QQC2.ToolButton {
                    display: QQC2.AbstractButton.IconOnly
                    icon.name: "document-open"
                    text: i18nc("@action:button open the attachment outside this window", "Open")
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    QQC2.ToolTip.text: text
                    onClicked: root.fileActivated(root.sourceUrl)
                }
            }
        }
    }

    QQC2.ItemDelegate {
        Layout.fillWidth: true
        visible: !root.showsImage && !root.showsVideo && !root.showsAudio && !root.unreachable
        text: root.fileName
        icon.name: "document-open"
        onClicked: root.fileActivated(root.sourceUrl)
    }

    QQC2.Label {
        Layout.fillWidth: true
        visible: root.unreachable
        text: i18nc("@info:status %1 is the attachment's name", "%1 cannot be opened from here.", root.fileName)
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        font: Kirigami.Theme.smallFont
        color: Kirigami.Theme.disabledTextColor
    }
}
