// KOutNet — Splash screen
// Ported from gdf_ui_base.py: SplashScreen (_build, _tick_dots)
import QtQuick

Rectangle {
    id: splash
    radius: 18
    border.color: "#4A90D9"
    border.width: 2
    gradient: Gradient {
        orientation: Gradient.Horizontal
        GradientStop { position: 0.0; color: "#2A2A3E" }
        GradientStop { position: 0.4; color: "#1E1E2E" }
        GradientStop { position: 1.0; color: "#15151F" }
    }

    property int dotCount: 0

    Column {
        anchors.centerIn: parent
        spacing: 8

        Text {
            text: "📱"
            font.pixelSize: 56
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Text {
            text: "KOUTNET"
            color: "white"
            font.pixelSize: 30
            font.bold: true
            font.letterSpacing: 3
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Text {
            text: "v2.0  •  KOutNet"
            color: "#888"
            font.pixelSize: 12
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 26
        radius: 0
        color: "#00000000"

        Text {
            id: loadingLabel
            anchors.centerIn: parent
            color: "#4A90D9"
            font.pixelSize: 11
            text: "Loading" + ".".repeat(splash.dotCount)
        }
    }

    Timer {
        interval: 400
        running: true
        repeat: true
        onTriggered: splash.dotCount = (splash.dotCount + 1) % 4
    }
}