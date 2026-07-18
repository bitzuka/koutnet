import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import org.kde.kirigami as Kirigami

// Single row in the peer list: avatar (letter or icon), name, and optional
// online/security status. Built on ItemDelegate rather than a bare
// MouseArea + Item, so click/hover/keyboard handling is proven Qt Quick
// Controls behavior instead of something hand-rolled — a bare MouseArea
// inside a Layout is an easy place for click hit-testing to silently
// misbehave depending on how the parent sizes it.
ItemDelegate {
    id: root

    property string peerIp: ""
    property string peerOs: ""
    property bool e2e: false
    property bool selected: false
    // Empty = show the first letter of peerIp as the avatar (default, used
    // for real contacts). Non-empty = show this Kirigami icon name instead
    // (used for special rows like "Избранное").
    property string iconName: ""
    // Real network peers have a reachability/encryption status; special
    // local-only rows (the self-chat) have neither concept, so both can be
    // turned off rather than showing meaningless "online"/"E2E" labels.
    property bool showOnlineIndicator: true
    property bool showSecurityLabel: true

    height: 60
    highlighted: root.selected
    hoverEnabled: true

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Item {
            width: 36
            height: 36
            Layout.alignment: Qt.AlignVCenter

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Kirigami.Theme.highlightColor
            }

            Kirigami.Icon {
                anchors.centerIn: parent
                width: 20
                height: 20
                visible: root.iconName.length > 0
                source: root.iconName
                color: "white"
                isMask: true
            }

            Text {
                anchors.centerIn: parent
                visible: root.iconName.length === 0
                text: root.peerIp.length > 0 ? root.peerIp.charAt(0) : "?"
                color: "white"
                font.bold: true
            }

            Rectangle {
                width: 10
                height: 10
                radius: 5
                color: "#2ecc71"
                border.color: Kirigami.Theme.backgroundColor
                border.width: 2
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                visible: root.showOnlineIndicator
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Kirigami.Heading {
                text: root.peerIp
                level: 5
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            Text {
                visible: root.showSecurityLabel || root.peerOs.length > 0
                text: (root.showSecurityLabel ? (root.e2e ? "E2E" : "Plain") : "")
                      + (root.showSecurityLabel && root.peerOs.length > 0 ? " • " : "")
                      + root.peerOs
                color: root.e2e ? "#2ecc71" : Kirigami.Theme.disabledTextColor
                elide: Text.ElideRight
                Layout.fillWidth: true
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }
        }
    }
}
