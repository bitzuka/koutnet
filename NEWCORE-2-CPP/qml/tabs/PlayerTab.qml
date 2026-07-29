import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import koutnet.app

// Media player tab (♫). Layout placeholder — legacy Player used
// QtMultimedia directly; the plan going forward is an FFmpeg-backed
// engine instead, wired in separately once the pipeline is designed.
Item {
    id: root
    readonly property var theme: ThemeManager.colors

    function tr(key) {
        return (Translations.current, Translations.t(key))
    }

    Rectangle { anchors.fill: parent; color: theme.bg }

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        text: root.tr("player.title")
        explanation: root.tr("player.explanation")
        icon.name: "media-playback-start"
    }
}
