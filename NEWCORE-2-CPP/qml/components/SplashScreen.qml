import QtQuick
import org.kde.kirigami as Kirigami
import koutnet.app

// TODO 1: rewrite SplashScreen with support
// TODO 2: draw or find default image
// GIF custom text and etc..
// simple MVP needed

Item {
    id: root
    readonly property var theme: ThemeManager.colors
    anchors.fill: parent

    Rectangle {
        anchors.fill: parent
        color: root.theme.bg
    }

    Kirigami.Heading {
        anchors.centerIn: parent
        level: 1
        text: "KOutNet"
        color: root.theme.text
    }
}
