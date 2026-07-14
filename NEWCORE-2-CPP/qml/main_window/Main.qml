// KOutNet — Main application window
// Ported from gdf_main.py (splash_delay = 2200ms -> _continue_after_splash)
import QtQuick
import QtQuick.Controls
import koutnet.app

ApplicationWindow {
    id: root

    property bool showingSplash: true

    width: showingSplash ? 616 : 1000
    height: showingSplash ? 338 : 650
    minimumWidth: showingSplash ? 616 : 480
    minimumHeight: showingSplash ? 338 : 360
    visible: true
    title: "KOutNet"
    color: showingSplash ? "transparent" : "#15151F"
    flags: showingSplash ? (Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint) : Qt.Window

    Component.onCompleted: {
        // Center on screen, matching the legacy splash placement.
        x = Screen.width / 2 - width / 2
        y = Screen.height / 2 - height / 2
    }

    // I_Do_It_Latet.! — "show_splash" / custom splash image should come from
    // AppSettings once core/constructor is ported. Splash is always shown
    // for 2200ms for now, matching the legacy default (S().get("show_splash", True)).
    Timer {
        interval: 2200
        running: root.showingSplash
        onTriggered: root.showingSplash = false
    }

    SplashScreen {
        anchors.fill: parent
        visible: opacity > 0
        opacity: root.showingSplash ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 250 } }
    }

    MainChatWindow {
        anchors.fill: parent
        visible: opacity > 0
        opacity: root.showingSplash ? 0.0 : 1.0
        Behavior on opacity { NumberAnimation { duration: 250 } }
    }
}
