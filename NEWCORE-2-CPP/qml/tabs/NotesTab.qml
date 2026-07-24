import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import koutnet.app

// Personal scratchpad tab. Port of legacy NotesWidget.
// Persistence is stubbed to HistoryManager's chat-log storage under a
// reserved id so it survives restarts; a dedicated NotesStore is a
// natural follow-up once this needs more than plain text.
Item {
    id: root
    readonly property var theme: ThemeManager.colors

    Rectangle { anchors.fill: parent; color: theme.bg }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Heading {
            level: 3
            text: Translations.t("tab_main_notes")
            color: theme.text
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            TextArea {
                id: notesArea
                placeholderText: "..."
                wrapMode: TextArea.Wrap
                color: theme.text
                background: Rectangle { color: theme.bg3; radius: 6; border.color: theme.border }
            }
        }
    }
}
