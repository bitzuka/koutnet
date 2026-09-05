// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import koutnet.app

// It does not decide its own lifetime: the window pushes it as a layer and pops it
// when continueRequested arrives. A FormCardPage rather than an Item pinned over the
// overlay, which the old one had to fight for z-order to keep its dialogs visible.
FormCard.FormCardPage {
    id: root

    signal continueRequested()
    signal aboutRequested()

    title: i18nc("@title:window", "Welcome to KOutNet")

    Kirigami.Theme.highlightColor: Brand.accent

    // Read from the about data rather than written down a second time.
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
            // Relative to this file in the module resource tree, so a build directory
            // resolves it the same as an installed prefix.
            source: "../../assets/512-apps-io.github.bitzuka.koutnet.png"
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
            // Same index order as the settings page: a ColorSchemeSelector.Mode.
            model: [
                i18nc("@item:inlistbox colour scheme", "Follow the system"),
                i18nc("@item:inlistbox colour scheme", "Light"),
                i18nc("@item:inlistbox colour scheme", "Dark"),
            ]
            currentIndex: ColorSchemeSelector.mode
            onActivated: (index) => ColorSchemeSelector.mode = index
        }

        FormCard.FormDelegateSeparator { above: schemeCombo; below: welcomeDelegate }

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
