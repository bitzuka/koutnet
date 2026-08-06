// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// The K-Server sign-in, which is a Matrix sign-in. One card: where, who, and
// the password, then the state of the session underneath it.
//
// No registration, no single sign-on and no device verification here. Each of
// those is a flow of its own and a half of one is worse than a link to a
// browser, which is what the homeserver already has.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import koutnet.app

FormCard.FormCardPage {
    id: root

    title: i18nc("@title:window", "K-Server account")

    // See Main.qml: FormCardPage starts a theme chain of its own.
    Kirigami.Theme.highlightColor: Brand.accent

    readonly property real kContentWidth: Math.max(Kirigami.Units.gridUnit * 20,
        Math.min(root.width - Kirigami.Units.largeSpacing * 4, Kirigami.Units.gridUnit * 48))

    FormCard.FormHeader {
        maximumWidth: root.kContentWidth
        title: i18nc("@title:group", "Matrix account")
    }

    FormCard.FormCard {
        Layout.fillWidth: true
        maximumWidth: root.kContentWidth

        FormCard.FormTextDelegate {
            id: explanation
            text: i18nc("@info", "K-Server mode speaks Matrix.")
            description: i18nc("@info:whatsthis",
                "Signing in here puts your Matrix rooms in the conversation list beside the peers found on the local network. Encrypted rooms are not readable yet.")
        }

        FormCard.FormDelegateSeparator { above: explanation; below: homeserverField }

        FormCard.FormTextFieldDelegate {
            id: homeserverField
            label: i18nc("@label:textbox", "Homeserver")
            placeholderText: i18nc("@info:placeholder an example homeserver address", "matrix.org")
            text: appSettings.matrixHomeserver
            enabled: !matrixManager.loggedIn && !matrixManager.busy
            // Left blank on purpose when the user id below is a full one: the
            // homeserver is then in its domain, and asking twice is asking to
            // have the two disagree.
            statusMessage: i18nc("@info:whatsthis", "Optional if the user ID below is written in full.")
        }

        FormCard.FormDelegateSeparator { above: homeserverField; below: userField }

        FormCard.FormTextFieldDelegate {
            id: userField
            label: i18nc("@label:textbox a Matrix user identifier", "User ID")
            placeholderText: i18nc("@info:placeholder an example Matrix user id", "@you:matrix.org")
            text: appSettings.matrixUserId
            enabled: !matrixManager.loggedIn && !matrixManager.busy
        }

        FormCard.FormDelegateSeparator { above: userField; below: passwordField }

        FormCard.FormPasswordFieldDelegate {
            id: passwordField
            label: i18nc("@label:textbox", "Password")
            enabled: !matrixManager.loggedIn && !matrixManager.busy
            // Never written anywhere: it is handed to libQuotient and the
            // homeserver answers with a token, which is what gets stored.
            onAccepted: root.signIn()
        }

        FormCard.FormDelegateSeparator { above: passwordField; below: actionButton }

        FormCard.FormButtonDelegate {
            id: actionButton
            icon.name: matrixManager.loggedIn ? "system-log-out" : "network-connect"
            text: matrixManager.loggedIn
                ? i18nc("@action:button", "Sign out")
                : i18nc("@action:button", "Sign in")
            enabled: !matrixManager.busy
            onClicked: {
                if (matrixManager.loggedIn)
                    matrixManager.logout()
                else
                    root.signIn()
            }
        }
    }

    FormCard.FormHeader {
        maximumWidth: root.kContentWidth
        title: i18nc("@title:group", "Session")
    }

    FormCard.FormCard {
        Layout.fillWidth: true
        maximumWidth: root.kContentWidth

        FormCard.FormTextDelegate {
            text: matrixManager.statusText
            description: matrixManager.loggedIn && matrixManager.homeserver.length > 0
                ? matrixManager.homeserver
                : ""

            leading: QQC2.BusyIndicator {
                running: matrixManager.busy
                visible: matrixManager.busy
                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                implicitHeight: Kirigami.Units.iconSizes.smallMedium
            }
        }
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        Layout.maximumWidth: root.kContentWidth
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Kirigami.Units.largeSpacing
        Layout.leftMargin: Kirigami.Units.largeSpacing
        Layout.rightMargin: Kirigami.Units.largeSpacing

        type: Kirigami.MessageType.Error
        visible: !matrixManager.loggedIn && !matrixManager.busy && matrixManager.lastError.length > 0
        text: matrixManager.lastError
    }

    function signIn() {
        passwordField.statusMessage = ""
        if (userField.text.trim().length === 0 || passwordField.text.length === 0) {
            passwordField.statusMessage = i18nc("@info:status", "Enter a user ID and a password.")
            return
        }
        appSettings.matrixHomeserver = homeserverField.text.trim()
        matrixManager.login(homeserverField.text, userField.text.trim(), passwordField.text)
        // Cleared immediately: the field is the only copy this side keeps, and
        // the window can stay open for the rest of the session.
        passwordField.text = ""
    }
}
