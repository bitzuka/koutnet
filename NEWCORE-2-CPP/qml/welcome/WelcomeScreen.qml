// SPDX-FileCopyrightText: 2026 bitzuka <matveypotyzhno@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import koutnet.app

// Startup screen, shown in place of the old 2.2 second splash. It does not
// decide its own lifetime: the hosting Window listens for continueRequested
// and hides itself.
Item {
    id: root
    readonly property var theme: ThemeManager.colors

    // A build identifier is not prose, so it stays out of the translations.
    readonly property string buildLabel: "Developer build 0.0.001"
    readonly property string githubUrl: "https://github.com/bitzuka/koutnet"
    readonly property string telegramUrl: "https://t.me/KOutNet"

    // Passed in by the host rather than duplicated here, so the endonym list
    // stays defined in exactly one place.
    property var languageLabels: ({})

    signal continueRequested()

    function tr(key) {
        return (Translations.current, Translations.t(key))
    }

    function languageName(code) {
        return root.languageLabels[code] || code.toUpperCase()
    }

    // Translated strings carry <a href="github"> instead of a full URL, so a
    // translator editing the sentence cannot break where the link points.
    function openNamedLink(link) {
        if (link === "github")
            Qt.openUrlExternally(root.githubUrl)
        else if (link === "telegram")
            Qt.openUrlExternally(root.telegramUrl)
        else
            Qt.openUrlExternally(link)
    }

    Rectangle {
        anchors.fill: parent
        color: root.theme.bg
    }

    ColumnLayout {
        id: hero
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Kirigami.Units.gridUnit * 2
        width: Kirigami.Units.gridUnit * 15
        spacing: Kirigami.Units.smallSpacing

        Image {
            Layout.alignment: Qt.AlignHCenter
            // Relative to this file inside the QML module resource tree, so it
            // resolves the same whether the app runs from a build dir or an
            // installed prefix.
            source: "../../assets/koutnet_logo.png"
            sourceSize.height: Kirigami.Units.gridUnit * 6
            fillMode: Image.PreserveAspectFit
        }

        Kirigami.Heading {
            Layout.alignment: Qt.AlignHCenter
            level: 1
            text: "KOutNet"
            color: root.theme.text
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: Kirigami.Units.largeSpacing
            text: root.buildLabel
            color: root.theme.text_dim
        }

        Button {
            Layout.fillWidth: true
            text: root.tr("menu.about")
            onClicked: aboutDialog.open()
        }

        ComboBox {
            id: themePick
            Layout.fillWidth: true
            model: ThemeManager.availableThemes
            displayText: root.tr("theme." + ThemeManager.currentTheme)
            currentIndex: model.indexOf(ThemeManager.currentTheme)
            delegate: ItemDelegate {
                width: themePick.width
                text: root.tr("theme." + modelData)
            }
            onActivated: ThemeManager.currentTheme = model[currentIndex]
        }

        ComboBox {
            id: langPick
            Layout.fillWidth: true
            model: Translations.availableLanguages
            displayText: root.languageName(Translations.current)
            currentIndex: model.indexOf(Translations.current)
            delegate: ItemDelegate {
                width: langPick.width
                text: root.languageName(modelData)
            }
            onActivated: {
                Translations.current = model[currentIndex]
                appSettings.language = model[currentIndex]
            }
        }

        ColumnLayout {
            Layout.topMargin: Kirigami.Units.smallSpacing
            spacing: 0

            CheckBox {
                text: root.tr("welcome.check_updates")
                checked: appSettings.checkUpdatesOnStart
                onToggled: appSettings.checkUpdatesOnStart = checked
            }
            // The flag persists, but nothing consumes it yet, so say so
            // rather than implying the app will actually look for updates.
            Label {
                Layout.leftMargin: Kirigami.Units.gridUnit * 2
                text: root.tr("welcome.in_development")
                font.pixelSize: 11
                color: root.theme.text_dim
            }
        }
    }

    RowLayout {
        id: columns
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: footer.top
        anchors.margins: Kirigami.Units.gridUnit * 2
        anchors.bottomMargin: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.gridUnit * 2

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Heading {
                level: 4
                text: root.tr("welcome.community")
                color: root.theme.text
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: root.tr("welcome.community_body")
                color: root.theme.text_dim
            }
            RowLayout {
                spacing: Kirigami.Units.largeSpacing
                Label {
                    text: "Telegram:"
                    font.bold: true
                    color: root.theme.text
                }
                Label {
                    text: '<a href="telegram">@KOutNet</a>'
                    textFormat: Text.RichText
                    linkColor: root.theme.accent
                    color: root.theme.text_dim
                    onLinkActivated: (link) => root.openNamedLink(link)

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Heading {
                level: 4
                text: root.tr("welcome.contribute")
                color: root.theme.text
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.RichText
                text: root.tr("welcome.contribute_body")
                linkColor: root.theme.accent
                color: root.theme.text_dim
                onLinkActivated: (link) => root.openNamedLink(link)

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }
    }

    RowLayout {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Kirigami.Units.gridUnit
        spacing: Kirigami.Units.largeSpacing

        CheckBox {
            text: root.tr("welcome.dont_show")
            checked: !appSettings.showWelcome
            onToggled: appSettings.showWelcome = !checked
        }

        Item { Layout.fillWidth: true }

        Button {
            text: root.tr("welcome.continue")
            highlighted: true
            onClicked: root.continueRequested()
        }
    }

    Dialog {
        id: aboutDialog
        anchors.centerIn: parent
        modal: true
        title: root.tr("menu.about")
        standardButtons: Dialog.Close

        Label {
            width: Kirigami.Units.gridUnit * 18
            wrapMode: Text.Wrap
            text: root.tr("about.description") + "\n\n" + root.buildLabel
            color: root.theme.text
        }
    }
}
