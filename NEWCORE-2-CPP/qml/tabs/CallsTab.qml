import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import koutnet.app

// Call log tab. Port of legacy CallLogWidget, backed by
// HistoryManager.loadCallLog() (the same call-log store VoiceCallManager
// will append to once outgoing/incoming call events are wired to it).
Item {
    id: root
    readonly property var theme: ThemeManager.colors

    Rectangle { anchors.fill: parent; color: theme.bg }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Kirigami.Heading {
            level: 3
            text: Translations.t("tab_main_calls")
            color: theme.text
            Layout.margins: Kirigami.Units.largeSpacing
        }

        ListView {
            id: callList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: HistoryManager.loadCallLog()

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                visible: callList.count === 0
                text: Translations.t("no_calls") || "Нет звонков"
                icon.name: "call-start"
            }

            delegate: ItemDelegate {
                width: callList.width
                height: 48
                contentItem: RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    Kirigami.Icon {
                        source: modelData.is_own === true ? "call-start" : "call-stop"
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                    }
                    Label {
                        text: modelData.sender || modelData.text || ""
                        color: theme.text
                        Layout.fillWidth: true
                    }
                    Label {
                        text: modelData.ts ? new Date(modelData.ts * 1000).toLocaleTimeString() : ""
                        color: theme.text_dim
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }
                }
            }
        }
    }
}
