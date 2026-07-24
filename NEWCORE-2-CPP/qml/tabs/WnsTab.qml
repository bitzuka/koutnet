import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import koutnet.app

// WNS (internal network browser) tab (🌐). Layout placeholder — legacy
// NetScape rendered custom markup by hand; the plan is a from-scratch
// rendering engine (own layout/paint pipeline, not a Qt WebEngine
// wrapper), wired in separately.
Item {
    id: root
    readonly property var theme: ThemeManager.colors

    Rectangle { anchors.fill: parent; color: theme.bg }

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        text: "🌐 Keenly"
        explanation: "Свой рендер-движок — следующим этапом"
        icon.name: "internet-web-browser"
    }
}
