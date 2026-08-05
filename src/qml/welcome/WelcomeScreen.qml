// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import koutnet.app

// Startup screen, shown in place of the old 2.2 second splash. It does not decide
// its own lifetime: the window pushes it as a layer and pops it when
// continueRequested arrives.
//
// A FormCardPage rather than an Item pinned over the window overlay. The old one
// had to fight the overlay's z-order to keep its own dialogs visible, and the two
// options on it were a checkbox and a combo box floating on a bare Rectangle.
FormCard.FormCardPage {
    id: root

    signal continueRequested()
    signal aboutRequested()

    title: i18nc("@title:window", "Welcome to KOutNet")

    // See the note on Kirigami.Theme in Main.qml.
    Kirigami.Theme.highlightColor: Brand.accent

    // Read from the about data rather than written down a second time. The version
    // itself is not prose, so only the words around it are translated.
    readonly property string buildLabel: i18nc("@info:status %1 is the version number",
                                               "Developer build %1", aboutData.version)
    readonly property string githubUrl: "https://github.com/bitzuka/koutnet"
    readonly property string telegramUrl: "https://t.me/KOutNet"

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

    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.gridUnit * 2
        Layout.bottomMargin: Kirigami.Units.gridUnit
        spacing: Kirigami.Units.smallSpacing

        Image {
            Layout.alignment: Qt.AlignHCenter
            // Relative to this file inside the QML module resource tree, so it
            // resolves the same whether the app runs from a build directory or an
            // installed prefix.
            source: "../../assets/512-apps-io.github.bitzuka.KOutNet.png"
            sourceSize.height: Kirigami.Units.gridUnit * 6
            fillMode: Image.PreserveAspectFit
        }

        Kirigami.Heading {
            Layout.alignment: Qt.AlignHCenter
            level: 1
            text: aboutData.name
        }

        QQC2.Label {
            Layout.alignment: Qt.AlignHCenter
            text: root.buildLabel
            color: Kirigami.Theme.disabledTextColor
        }
    }

    FormCard.FormCard {
        FormCard.FormComboBoxDelegate {
            id: schemeCombo
            text: i18nc("@label:listbox", "Colour scheme")
            // Same three options and the same index order as the settings page:
            // the index is a ColorSchemeSelector.Mode.
            model: [
                i18nc("@item:inlistbox colour scheme", "Follow the system"),
                i18nc("@item:inlistbox colour scheme", "Light"),
                i18nc("@item:inlistbox colour scheme", "Dark"),
            ]
            currentIndex: ColorSchemeSelector.mode
            onActivated: (index) => ColorSchemeSelector.mode = index
        }

        FormCard.FormDelegateSeparator { above: schemeCombo; below: updatesDelegate }

        FormCard.FormSwitchDelegate {
            id: updatesDelegate
            text: i18nc("@option:check", "Check for updates at startup")
            // The flag persists, but nothing consumes it yet, so say so rather
            // than implying the app will actually look for updates.
            description: i18nc("@info:status this option does nothing yet", "In development")
            checked: appSettings.checkUpdatesOnStart
            onToggled: appSettings.checkUpdatesOnStart = updatesDelegate.checked
        }

        FormCard.FormDelegateSeparator { above: updatesDelegate; below: welcomeDelegate }

        FormCard.FormSwitchDelegate {
            id: welcomeDelegate
            text: i18nc("@option:check", "Show this screen at startup")
            checked: appSettings.showWelcome
            onToggled: appSettings.showWelcome = welcomeDelegate.checked
        }

        FormCard.FormDelegateSeparator { above: welcomeDelegate; below: aboutDelegate }

        FormCard.FormButtonDelegate {
            id: aboutDelegate
            text: i18nc("@action:button", "About KOutNet")
            icon.name: "help-about"
            onClicked: root.aboutRequested()
        }
    }

    FormCard.FormHeader {
        title: i18nc("@title:group", "Community")
    }

    FormCard.FormCard {
        FormCard.FormTextDelegate {
            id: telegramDelegate
            text: i18nc("@label the Telegram chat platform", "Telegram")
            description: i18nc("@info", "Join like-minded KOutNet people in our community: <a href=\"telegram\">@KOutNet</a>")
            onLinkActivated: (link) => root.openNamedLink(link)
        }

        FormCard.FormDelegateSeparator { above: telegramDelegate; below: contributeDelegate }

        FormCard.FormTextDelegate {
            id: contributeDelegate
            text: i18nc("@label", "Contribute")
            description: i18nc("@info", "Want to help make KOutNet better? Visit our <a href=\"github\">GitHub page</a>. Report bugs or contribute code, anyone can do it.")
            onLinkActivated: (link) => root.openNamedLink(link)
        }
    }

    footer: QQC2.ToolBar {
        position: QQC2.ToolBar.Footer

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            Item { Layout.fillWidth: true }

            QQC2.Button {
                text: i18nc("@action:button", "Continue")
                icon.name: "go-next"
                highlighted: true
                onClicked: root.continueRequested()
            }
        }
    }
}
