// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import koutnet.app

// Everything here comes from the KAboutData built in main.cpp, so the page cannot
// drift from what --version and DrKonqi report. Not FormCard.AboutPage, which wants
// a KAboutData object where QML gets a flat map.
FormCard.FormCardPage {
    id: root

    title: i18nc("@title:window", "About KOutNet")

    Kirigami.Theme.highlightColor: Brand.accent

    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.gridUnit
        Layout.bottomMargin: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.smallSpacing

        Image {
            Layout.alignment: Qt.AlignHCenter
            // Relative to this file in the QML module resource tree, so it resolves
            // the same from a build directory as from an installed prefix.
            source: "../../assets/512-apps-org.kde.koutnet.png"
            sourceSize.height: Kirigami.Units.gridUnit * 5
            fillMode: Image.PreserveAspectFit
        }

        Kirigami.Heading {
            Layout.alignment: Qt.AlignHCenter
            level: 1
            text: aboutData.name
        }

        Kirigami.SelectableLabel {
            Layout.alignment: Qt.AlignHCenter
            text: i18nc("@info:status %1 is the version number", "Version %1", aboutData.version)
            color: Kirigami.Theme.disabledTextColor
        }
    }

    FormCard.FormCard {
        FormCard.FormTextDelegate {
            text: aboutData.description
        }
    }

    FormCard.FormHeader {
        title: i18nc("@title:group", "Details")
    }

    FormCard.FormCard {
        FormCard.FormTextDelegate {
            id: copyrightDelegate
            text: i18nc("@label", "Copyright")
            description: aboutData.copyright
        }

        FormCard.FormDelegateSeparator { above: copyrightDelegate; below: licenseDelegate }

        FormCard.FormTextDelegate {
            id: licenseDelegate
            text: i18nc("@label", "License")
            description: aboutData.license
        }

        FormCard.FormDelegateSeparator { above: licenseDelegate; below: authorDelegate }

        FormCard.FormTextDelegate {
            id: authorDelegate
            text: i18nc("@label the person who wrote the program", "Author")
            description: aboutData.author
        }

        FormCard.FormDelegateSeparator { above: authorDelegate; below: homepageDelegate }

        FormCard.FormButtonDelegate {
            id: homepageDelegate
            text: i18nc("@action:button", "Visit the project page")
            description: aboutData.homepage
            icon.name: "internet-web-browser"
            onClicked: Qt.openUrlExternally(aboutData.homepage)
        }
    }
}
