import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import QtMultimedia
import org.kde.kirigami as Kirigami
import koutnet.app

// Violla, the in-app media player.
//
// Qt Multimedia has used FFmpeg as its default backend on Linux since 6.8, so
// MediaPlayer already decodes through libav here. A hand-written demux and
// decode loop would buy control we have no use for yet.
//
// Two layouts over one player. Cinema hands the whole area to the picture and
// gets out of the way; music has no picture to show, so it spends the space
// on the track and the queue instead. The transport bar is shared and does
// not move between them.
Item {
    id: root
    readonly property var theme: ThemeManager.colors

    property int current: -1
    property int priorVisibility: Window.Windowed
    property bool playlistOpen: true

    readonly property bool cinema: root.current >= 0 && playlist.get(root.current).video
    readonly property bool fullScreen: root.Window.window
        && root.Window.window.visibility === Window.FullScreen

    function tr(key) {
        return (Translations.current, Translations.t(key))
    }

    // The extension answers immediately, which keeps the layout from flipping
    // a beat after the file opens. MediaPlayer.hasVideo is authoritative but
    // only once the media has loaded, and it corrects this below.
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

    // Cinema wants the picture, music wants the queue. Still a plain property
    // so the toggle can override the default for the current track.
    onCinemaChanged: root.playlistOpen = !root.cinema

    ListModel { id: playlist }

    MediaPlayer {
        id: player
        videoOutput: videoOut
        audioOutput: AudioOutput {
            volume: volumeSlider.value
            muted: muteButton.checked
        }

        // Once the file is open the player knows for certain, so write the
        // answer back and stop guessing for this entry.
        onHasVideoChanged: {
            if (root.current >= 0)
                playlist.setProperty(root.current, "video", player.hasVideo)
        }

        // Rolls onto the next entry on its own, which is the point of a
        // playlist. Stops at the end rather than looping.
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.EndOfMedia && root.current + 1 < playlist.count)
                root.playAt(root.current + 1)
        }
    }

    FileDialog {
        id: openDialog
        title: root.tr("player.open")
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            "Media (*.mp3 *.flac *.ogg *.opus *.wav *.m4a *.aac *.mp4 *.mkv *.webm *.avi *.mov)",
            "All files (*)"
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

    Rectangle { anchors.fill: parent; color: root.theme.bg }

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

                // Cinema.
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

                // Music.
                Item {
                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - Kirigami.Units.gridUnit * 4,
                                        Kirigami.Units.gridUnit * 22)
                        spacing: Kirigami.Units.largeSpacing

                        // Cover art needs an image provider on the C++ side to
                        // get a QImage out of the tags, so for now the slot is
                        // held by the theme accent and an icon.
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 11
                            Layout.preferredHeight: width
                            radius: 12
                            color: root.theme.bg3
                            border.color: root.theme.border

                            Kirigami.Icon {
                                anchors.centerIn: parent
                                width: parent.width * 0.4
                                height: width
                                source: "audio-x-generic"
                                color: root.theme.text_dim
                            }
                        }

                        Kirigami.Heading {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            level: 2
                            color: root.theme.text
                            text: root.current < 0
                                ? root.tr("player.title")
                                : root.metaText(MediaMetaData.Title,
                                                root.nameOf(playlist.get(root.current).url))
                        }

                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            color: root.theme.text_dim
                            visible: root.current >= 0
                            text: root.metaText(MediaMetaData.AlbumArtist,
                                                root.tr("player.unknown_artist"))
                                + "  -  "
                                + root.metaText(MediaMetaData.AlbumTitle,
                                                root.tr("player.unknown_album"))
                        }

                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            color: root.theme.text_dim
                            visible: root.current < 0
                            text: root.tr("player.explanation")
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                Layout.fillHeight: true
                visible: root.playlistOpen && !root.fullScreen
                color: root.theme.bg2

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        Kirigami.Heading {
                            Layout.fillWidth: true
                            level: 5
                            text: root.tr("player.playlist")
                            color: root.theme.text
                        }
                        ToolButton {
                            icon.name: "edit-clear-all"
                            enabled: playlist.count > 0
                            onClicked: {
                                player.stop()
                                player.source = ""
                                playlist.clear()
                                root.current = -1
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: root.tr("player.clear")
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: playlist.count === 0
                        wrapMode: Text.Wrap
                        text: root.tr("player.playlist_empty")
                        color: root.theme.text_dim
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: playlist
                        spacing: 2

                        delegate: ItemDelegate {
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

        // One transport for both layouts. Moving it around when the mode
        // changes would just make the controls hard to find.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: transport.implicitHeight + Kirigami.Units.largeSpacing
            color: root.cinema ? "#101010" : root.theme.bg2

            RowLayout {
                id: transport
                anchors.fill: parent
                anchors.margins: Kirigami.Units.smallSpacing
                spacing: Kirigami.Units.smallSpacing

                ToolButton {
                    icon.name: "media-skip-backward"
                    enabled: playlist.count > 1
                    onClicked: root.step(-1)
                }
                ToolButton {
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
                ToolButton {
                    icon.name: "media-playback-stop"
                    enabled: player.playbackState !== MediaPlayer.StoppedState
                    onClicked: player.stop()
                }
                ToolButton {
                    icon.name: "media-skip-forward"
                    enabled: playlist.count > 1
                    onClicked: root.step(1)
                }

                Label {
                    text: root.clockOf(player.position)
                    color: root.theme.text_dim
                }
                Slider {
                    Layout.fillWidth: true
                    enabled: player.seekable
                    from: 0
                    to: Math.max(1, player.duration)
                    // Following position while the handle is held fights the
                    // user for it, so only track playback when idle.
                    value: pressed ? value : player.position
                    onMoved: player.position = value
                }
                Label {
                    text: root.clockOf(player.duration)
                    color: root.theme.text_dim
                }

                ToolButton {
                    id: muteButton
                    checkable: true
                    icon.name: checked ? "audio-volume-muted" : "audio-volume-high"
                }
                Slider {
                    id: volumeSlider
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                    from: 0
                    to: 1
                    value: 0.8
                }

                ToolButton {
                    icon.name: "document-open"
                    onClicked: openDialog.open()
                    ToolTip.visible: hovered
                    ToolTip.text: root.tr("player.open")
                }
                ToolButton {
                    icon.name: "view-media-playlist"
                    checkable: true
                    checked: root.playlistOpen
                    onToggled: root.playlistOpen = checked
                    ToolTip.visible: hovered
                    ToolTip.text: root.tr("player.playlist")
                }
                ToolButton {
                    icon.name: root.fullScreen ? "view-restore" : "view-fullscreen"
                    visible: root.cinema
                    onClicked: root.toggleFullScreen()
                    ToolTip.visible: hovered
                    ToolTip.text: root.tr("player.fullscreen")
                }
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: player.error !== MediaPlayer.NoError
            wrapMode: Text.Wrap
            color: "#ff6b6b"
            text: player.errorString
        }
    }
}
