import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import koutnet.app

// Zapret DPI-bypass control tab, wrapping ZapretManager (a QProcess around
// zapret-linux/service.sh). Point-and-click UI on top, live console below
// carrying whatever service.sh would print in a terminal, so failures are
// visible without dropping to a shell.
Item {
    id: root
    readonly property var theme: ThemeManager.colors

    function tr(key) {
        return (Translations.current, Translations.t(key))
    }

    Rectangle { anchors.fill: parent; color: theme.bg }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Heading {
            level: 3
            text: root.tr("zapret.title")
            color: root.theme.text
        }

        Kirigami.PlaceholderMessage {
            visible: !ZapretManager.deployed
            Layout.fillWidth: true
            text: root.tr("zapret.not_deployed")
            icon.name: "dialog-warning"
        }

        ColumnLayout {
            visible: ZapretManager.deployed
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: root.tr("zapret.strategy"); color: root.theme.text_dim }
                    ComboBox {
                        id: strategyCombo
                        Layout.fillWidth: true
                        model: ZapretManager.availableStrategies
                        enabled: !ZapretManager.running
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: root.tr("zapret.interface"); color: root.theme.text_dim }
                    ComboBox {
                        id: interfaceCombo
                        Layout.fillWidth: true
                        model: ZapretManager.availableInterfaces
                        enabled: !ZapretManager.running
                    }
                }

                ColumnLayout {
                    Label { text: ""; color: "transparent" }
                    Button {
                        text: ZapretManager.running ? root.tr("zapret.stop") : root.tr("zapret.start")
                        highlighted: !ZapretManager.running
                        onClicked: {
                            if (ZapretManager.running) {
                                ZapretManager.stop()
                            } else {
                                ZapretManager.start(
                                    strategyCombo.currentText,
                                    interfaceCombo.currentText)
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Rectangle {
                    width: 10
                    height: 10
                    radius: 5
                    color: ZapretManager.running ? "#2ECC71" : root.theme.text_dim
                }
                Label {
                    text: ZapretManager.running ? root.tr("zapret.status_running") : root.tr("zapret.status_stopped")
                    color: root.theme.text_dim
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: ZapretManager.deployed

            Kirigami.Heading {
                level: 5
                text: root.tr("zapret.console")
                color: root.theme.text_dim
                Layout.fillWidth: true
            }
            ToolButton {
                text: root.tr("zapret.clear_log")
                onClicked: ZapretManager.clearLog()
            }
        }

        ScrollView {
            visible: ZapretManager.deployed
            Layout.fillWidth: true
            Layout.fillHeight: true

            TextArea {
                id: consoleArea
                readOnly: true
                wrapMode: TextArea.Wrap
                font.family: "monospace"
                font.pointSize: 9
                color: root.theme.text
                background: Rectangle { color: root.theme.bg3; radius: 6; border.color: root.theme.border }
                text: ZapretManager.logOutput

                onTextChanged: cursorPosition = text.length
            }
        }
    }
}
