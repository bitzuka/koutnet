// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// The one secret besides the login password that a Matrix session is asked
// for: the string that unlocks the room keys the homeserver keeps. A recovery
// key and a passphrase are tried in that order, so one field and one button
// serve both. The answer is asynchronous - the homeserver has to decrypt and
// hand the keys over - so the dialog only says "trying" in the meantime and
// the window reports the outcome.
Kirigami.PromptDialog {
    id: root

    // See the note on Kirigami.Theme in Main.qml.
    Kirigami.Theme.inherit: false
    Kirigami.Theme.highlightColor: Brand.accent

    title: i18nc("@title:window the dialog that unlocks the room key backup", "Restore room keys")
    subtitle: i18nc("@info", "The homeserver stores an encrypted copy of this account's room keys. Unlock it with the recovery key from when the backup was made, or with the backup passphrase.")
    standardButtons: Kirigami.Dialog.Cancel
    customFooterActions: [
        Kirigami.Action {
            text: i18nc("@action:button try the entered string against the room key backup", "Unlock")
            icon.name: "object-unlock"
            enabled: backupField.text.length > 0 && !root.busy
            onTriggered: {
                matrixManager.unlockKeyBackup(backupField.text.trim())
                root.busy = true
            }
        }
    ]

    property bool busy: false

    onOpened: {
        root.busy = false
        backupField.text = ""
        backupField.forceActiveFocus()
    }

    Connections {
        target: matrixManager

        function onKeyBackupUnlocked() {
            root.busy = false
            root.close()
        }
        function onKeyBackupFailed(reason) {
            root.busy = false
            backupField.text = ""
            root.conflictReason = reason
        }
    }

    // The failure is shown inside the dialog rather than as a window toast:
    // it names the string that was just typed, which the toast would lose.
    property string conflictReason: ""

    QQC2.TextField {
        id: backupField
        Layout.fillWidth: true
        echoMode: TextInput.Password
        placeholderText: i18nc("@info:placeholder a recovery key or the backup passphrase", "Recovery key or passphrase")
        inputMethodHints: Qt.ImhNoPredictiveText
        enabled: !root.busy
        onAccepted: if (text.length > 0 && !root.busy) {
            matrixManager.unlockKeyBackup(text.trim())
            root.busy = true
        }
    }

    QQC2.Label {
        Layout.fillWidth: true
        visible: root.busy
        text: i18nc("@info:status the homeserver is decrypting the backup", "Asking for the keys...")
        font: Kirigami.Theme.smallFont
    }

    QQC2.Label {
        Layout.fillWidth: true
        visible: root.conflictReason.length > 0
        text: root.conflictReason
        font: Kirigami.Theme.smallFont
        color: Kirigami.Theme.negativeTextColor
        wrapMode: Text.WordWrap
    }
}