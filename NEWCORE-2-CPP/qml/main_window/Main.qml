// KOutNet — Main application window
// Ported from gdf_ui_base.py: SplashScreen class
import QtQuick
import QtQuick.Controls
import koutnet.app

ApplicationWindow {
    id: root
    width: 616
    height: 338
    visible: true
    title: "KOutNet"
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

    SplashScreen {
        anchors.fill: parent
    }
}