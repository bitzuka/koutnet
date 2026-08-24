// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import QtMultimedia
import org.kde.kirigami as Kirigami
import koutnet.app

// Qt Multimedia has used FFmpeg as its default backend on Linux since 6.8, so
// MediaPlayer already decodes through libav here; a hand-written demux and decode
// loop would buy control we have no use for yet. Two layouts over one player:
// cinema hands the whole area to the picture, and music has none to show so it
// spends the space on the track and the queue. The layout inside is deliberately
// left as it was - only the root type, the colours and the fixed radii changed.
Kirigami.Page {
    id: root

    title: i18nc("@title:service", "Violla")
    padding: 0

    Kirigami.Theme.highlightColor: Brand.accent

    property int current: -1
    property int priorVisibility: Window.Windowed
    property bool playlistOpen: true

    readonly property bool cinema: root.current >= 0 && playlist.get(root.current).video
    readonly property bool fullScreen: root.Window.window
        && root.Window.window.visibility === Window.FullScreen

    // The extension answers immediately, which keeps the layout from flipping a beat
    // after the file opens; MediaPlayer.hasVideo corrects it once the media loads.
    function looksLikeVideo(url) {
        return /\.(mp4|mkv|webm|avi|mov|m4v|mpg|mpeg|wmv|flv|ts|ogv)$/i.test(String(url))
    }

    function clockOf(ms) {
        if (!ms || ms < 0)
            return "0:00"
        const total = Math.floor(ms / 1000)
        const s = total % 60
        const m = Math.floor(total / 60) % 60
        const h = Math.floor(total / 3600)
        const mm = (h > 0 && m < 10) ? "0" + m : String(m)
        return (h > 0 ? h + ":" : "") + mm + ":" + (s < 10 ? "0" + s : s)
    }

    function nameOf(url) {
        const s = String(url)
        return decodeURIComponent(s.substring(s.lastIndexOf("/") + 1))
    }

    function metaText(key, fallback) {
        if (!player.metaData)
            return fallback
        const v = player.metaData.stringValue(key)
        return (v && v.length > 0) ? v : fallback
    }

    function playAt(index) {
        if (index < 0 || index >= playlist.count)
            return
        root.current = index
        player.source = playlist.get(index).url
        player.play()
    }

    function step(delta) {
        if (playlist.count === 0)
            return
        root.playAt((root.current + delta + playlist.count) % playlist.count)
    }

    function toggleFullScreen() {
        const win = root.Window.window
        if (!win)
            return
        if (win.visibility === Window.FullScreen) {
            win.visibility = root.priorVisibility
        } else {
            root.priorVisibility = win.visibility
            win.visibility = Window.FullScreen
        }
    }

    // A plain property, so the toggle can override the default for this track.
    onCinemaChanged: root.playlistOpen = !root.cinema

    ListModel { id: playlist }

    MediaPlayer {
        id: player
        videoOutput: videoOut
        audioOutput: AudioOutput {
            volume: volumeSlider.value
            muted: muteButton.checked
        }

        // Once the file is open the player knows, so stop guessing for this entry.
        onHasVideoChanged: {
            if (root.current >= 0)
                playlist.setProperty(root.current, "video", player.hasVideo)
        }

        // Rolls onto the next entry, and stops at the end rather than looping.
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.EndOfMedia && root.current + 1 < playlist.count)
                root.playAt(root.current + 1)
        }
    }

    FileDialog {
        id: openDialog
        title: i18nc("@title:window", "Open files")
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            i18nc("@item:inlistbox file dialog filter, keep the glob patterns",
                  "Media (*.mp3 *.flac *.ogg *.opus *.wav *.m4a *.aac *.mp4 *.mkv *.webm *.avi *.mov)"),
            i18nc("@item:inlistbox file dialog filter, keep the glob pattern", "All files (*)")
        ]
        onAccepted: {
            const startAt = playlist.count
            for (let i = 0; i < selectedFiles.length; ++i) {
                const u = String(selectedFiles[i])
                playlist.append({ url: u, video: root.looksLikeVideo(u) })
            }
            if (player.playbackState !== MediaPlayer.PlayingState)
                root.playAt(startAt)
        }
    }

    Shortcut {
        sequence: "Escape"
        enabled: root.fullScreen
        onActivated: root.toggleFullScreen()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.cinema ? 0 : 1

                Rectangle {
                    color: "black"

                    VideoOutput {
                        id: videoOut
                        anchors.fill: parent
                    }

                    TapHandler {
                        onDoubleTapped: root.toggleFullScreen()
                    }
                }

                Item {
                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - Kirigami.Units.gridUnit * 4,
                                        Kirigami.Units.gridUnit * 22)
                        spacing: Kirigami.Units.largeSpacing

                        // Cover art needs a C++ image provider to get a QImage out of
                        // the tags, so for now the slot is a plate and an icon.
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 11
                            Layout.preferredHeight: width
                            radius: Kirigami.Units.cornerRadius
                            color: Kirigami.Theme.alternateBackgroundColor
                            border.color: Kirigami.Theme.disabledTextColor

                            Kirigami.Icon {
                                anchors.centerIn: parent
                                width: parent.width * 0.4
                                height: width
                                source: "audio-x-generic"
                                color: Kirigami.Theme.disabledTextColor
                            }
                        }

                        Kirigami.Heading {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            level: 2
                            color: Kirigami.Theme.textColor
                            text: root.current < 0
                                 ? i18nc("@title:service", "Violla")
                                : root.metaText(MediaMetaData.Title,
                                                root.nameOf(playlist.get(root.current).url))
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            color: Kirigami.Theme.disabledTextColor
                            visible: root.current >= 0
                            text: i18nc("@info:status %1 is the album artist, %2 the album title",
                                        "%1 - %2",
                                        root.metaText(MediaMetaData.AlbumArtist,
                                                      i18nc("@info:status", "Unknown artist")),
                                        root.metaText(MediaMetaData.AlbumTitle,
                                                      i18nc("@info:status", "Unknown album")))
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            color: Kirigami.Theme.disabledTextColor
                            visible: root.current < 0
                            text: i18nc("@info", "Open a file to start playback")
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                Layout.fillHeight: true
                visible: root.playlistOpen && !root.fullScreen
                color: Kirigami.Theme.alternateBackgroundColor

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        Kirigami.Heading {
                            Layout.fillWidth: true
                            level: 5
                            text: i18nc("@title playlist panel", "Playlist")
                            color: Kirigami.Theme.textColor
                        }
                        QQC2.ToolButton {
                            icon.name: "edit-clear-all"
                            enabled: playlist.count > 0
                            onClicked: {
                                player.stop()
                                player.source = ""
                                playlist.clear()
                                root.current = -1
                            }
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.text: i18nc("@info:tooltip", "Clear playlist")
                        }
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        visible: playlist.count === 0
                        wrapMode: Text.Wrap
                        text: i18nc("@info", "Nothing queued yet")
                        color: Kirigami.Theme.disabledTextColor
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: playlist
                        spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                        delegate: QQC2.ItemDelegate {
                            required property int index
                            required property string url
                            required property bool video

                            width: ListView.view.width
                            highlighted: index === root.current
                            icon.name: video ? "video-x-generic" : "audio-x-generic"
                            text: root.nameOf(url)
                            onClicked: root.playAt(index)
                        }
                    }
                }
            }
        }

        // One transport: moving it when the mode changes hides the controls.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: transport.implicitHeight + Kirigami.Units.largeSpacing
            // Near-black in cinema so the transport does not glow next to the picture.
            color: root.cinema ? "#101010" : Kirigami.Theme.alternateBackgroundColor

            RowLayout {
                id: transport
                anchors.fill: parent
                anchors.margins: Kirigami.Units.smallSpacing
                spacing: Kirigami.Units.smallSpacing

                QQC2.ToolButton {
                    icon.name: "media-skip-backward"
                    enabled: playlist.count > 1
                    onClicked: root.step(-1)
                }
                QQC2.ToolButton {
                    icon.name: player.playbackState === MediaPlayer.PlayingState
                        ? "media-playback-pause" : "media-playback-start"
                    enabled: playlist.count > 0
                    onClicked: {
                        if (player.playbackState === MediaPlayer.PlayingState)
                            player.pause()
                        else if (root.current < 0)
                            root.playAt(0)
                        else
                            player.play()
                    }
                }
                QQC2.ToolButton {
                    icon.name: "media-playback-stop"
                    enabled: player.playbackState !== MediaPlayer.StoppedState
                    onClicked: player.stop()
                }
                QQC2.ToolButton {
                    icon.name: "media-skip-forward"
                    enabled: playlist.count > 1
                    onClicked: root.step(1)
                }

                QQC2.Label {
                    text: root.clockOf(player.position)
                    color: Kirigami.Theme.disabledTextColor
                }
                QQC2.Slider {
                    Layout.fillWidth: true
                    enabled: player.seekable
                    from: 0
                    to: Math.max(1, player.duration)
                    // Following position while the handle is held fights the user.
                    value: pressed ? value : player.position
                    onMoved: player.position = value
                }
                QQC2.Label {
                    text: root.clockOf(player.duration)
                    color: Kirigami.Theme.disabledTextColor
                }

                QQC2.ToolButton {
                    id: muteButton
                    checkable: true
                    icon.name: checked ? "audio-volume-muted" : "audio-volume-high"
                }
                QQC2.Slider {
                    id: volumeSlider
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                    from: 0
                    to: 1
                    value: 0.8
                }

                QQC2.ToolButton {
                    icon.name: "document-open"
                    onClicked: openDialog.open()
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Open files")
                }
                QQC2.ToolButton {
                    icon.name: "view-media-playlist"
                    checkable: true
                    checked: root.playlistOpen
                    onToggled: root.playlistOpen = checked
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: i18nc("@info:tooltip show the playlist panel", "Playlist")
                }
                QQC2.ToolButton {
                    icon.name: root.fullScreen ? "view-restore" : "view-fullscreen"
                    visible: root.cinema
                    onClicked: root.toggleFullScreen()
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: i18nc("@info:tooltip video player", "Fullscreen")
                }
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: player.error !== MediaPlayer.NoError
            wrapMode: Text.Wrap
            color: Kirigami.Theme.negativeTextColor
            text: player.errorString
        }
    }
}
