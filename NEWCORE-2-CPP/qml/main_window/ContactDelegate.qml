import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import org.kde.kirigami as Kirigami

// Single row in the peer list: status dot, IP, and encryption/OS info.
Item {
    id: root

    property string peerIp: ""
    property string peerOs: ""
    property bool e2e: false
    property bool selected: false

    signal clicked()

    height: 60

    Rectangle {
        anchors.fill: parent
        color: root.selected
            ? Kirigami.Theme.highlightColor
            : (mouseArea.containsMouse ? Kirigami.Theme.alternateBackgroundColor : "transparent")
        opacity: root.selected ? 0.25 : 1
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Kirigami.Units.smallSpacing
        anchors.rightMargin: Kirigami.Units.smallSpacing
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

            Text {
                anchors.centerIn: parent
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
                text: (root.e2e ? "E2E" : "Plain") + (root.peerOs.length > 0 ? " • " + root.peerOs : "")
                color: root.e2e ? "#2ecc71" : Kirigami.Theme.disabledTextColor
                elide: Text.ElideRight
                Layout.fillWidth: true
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.clicked()
    }
}
