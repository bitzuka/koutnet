// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
//
// Written against libQuotient's KeyVerificationSession rather than ported from
// anywhere, so there is no upstream copyright line here. NeoChat solves the
// same problem and is worth reading if this ever needs reworking, but none of
// it was copied.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import koutnet.app

// Interactive device verification, all five steps of it in one dialog: pick a
// session, or answer one that asked; wait; compare the emoji; say whether they
// matched; read what came of it.
//
// The dialog never decides anything by itself. Every button here maps to one
// call on matrixVerification and the library drives the rest, which is why
// there is no state of our own in this file beyond which page to show.
Kirigami.Dialog {
    id: root

    title: i18nc("@title:dialog", "Verify session")

    preferredWidth: Kirigami.Units.gridUnit * 26
    padding: Kirigami.Units.largeSpacing

    // The footer is built per step rather than switched on, because the
    // destructive answer ("they do not match") must never sit where the
    // harmless one was a moment ago.
    standardButtons: QQC2.Dialog.NoButton

    // Which of the five pages is showing. Derived rather than stored: the
    // session state is the truth and it moves without asking this file.
    readonly property bool picking: !matrixVerification.active
    readonly property bool asking: matrixVerification.active && matrixVerification.awaitingAccept
    readonly property bool comparing: matrixVerification.active && matrixVerification.comparing
    readonly property bool done: matrixVerification.finished
    readonly property bool waiting: matrixVerification.active && !root.asking && !root.comparing && !root.done

    function openForSession() {
        matrixVerification.refreshDevices()
        root.open()
    }

    onClosed: {
        // Closing the window is not an answer, and a session left running would
        // go on holding the one slot the manager has. Anything still live is
        // cancelled on the way out; a finished one is only cleared.
        if (matrixVerification.active && !matrixVerification.finished)
            matrixVerification.cancel()
        matrixVerification.dismiss()
    }

    Connections {
        target: matrixVerification

        function onSessionStarted() {
            if (!root.visible)
                root.open()
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        // 1. No session yet: this account's other sessions, and which of them
        //    this one already trusts.
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.picking
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: i18nc("@info:whatsthis",
                            "Verifying tells your other sessions that this one is really yours, so they will send it the keys "
                            + "for your encrypted rooms. Pick a session you have in front of you, then compare the emoji on both screens.")
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: true
                type: Kirigami.MessageType.Information
                text: i18nc("@info the other client has to be open at the same time",
                            "The other session has to be signed in and open while you do this.")
            }

            Repeater {
                model: matrixVerification.available ? deviceList.items : []

                delegate: RowLayout {
                    required property var modelData

                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        implicitWidth: Kirigami.Units.iconSizes.small
                        implicitHeight: Kirigami.Units.iconSizes.small
                        source: modelData.verified ? "security-medium" : "security-low"
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        QQC2.Label {
                            Layout.fillWidth: true
                            // The device id as well as the name: a name is
                            // whatever the other client was told to call
                            // itself and two of them can be the same, while
                            // the id is what the verification is actually
                            // about.
                            text: modelData.displayName.length > 0
                                ? i18nc("@item a Matrix session, %1 is its name and %2 its device id", "%1 (%2)",
                                        modelData.displayName, modelData.deviceId)
                                : modelData.deviceId
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: modelData.verified
                                ? i18nc("@info:status this session is already verified", "Already verified")
                                : i18nc("@info:status this session has not been verified", "Not verified")
                            textFormat: Text.PlainText
                            font: Kirigami.Theme.smallFont
                            color: Kirigami.Theme.disabledTextColor
                        }
                    }

                    QQC2.Button {
                        text: i18nc("@action:button start verifying another of your sessions", "Verify")
                        enabled: !modelData.verified
                        onClicked: matrixVerification.verifyOwnDevice(modelData.deviceId)
                    }
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                visible: deviceList.items.length === 0
                text: matrixVerification.available
                    ? i18nc("@info:status this account has no other sessions to verify against",
                            "This account has no other sessions. Sign in with another Matrix client first, then come back here.")
                    : i18nc("@info:status verification needs a working key store",
                            "This session could not open its encryption keys, so it cannot be verified. Check that the wallet is running and sign in again.")
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
            }

            // Kept out of the list above: it is the same operation aimed at
            // every already-trusted session at once, and it does nothing at all
            // until at least one of them is trusted, so it must not look like
            // the first thing to try.
            QQC2.Button {
                Layout.alignment: Qt.AlignRight
                visible: matrixVerification.available && deviceList.verifiedCount > 0
                text: i18nc("@action:button ask every already-verified session at once", "Ask all verified sessions")
                onClicked: matrixVerification.verifyFromVerifiedSessions()
            }
        }

        // 2. Something asked us.
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.asking
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: matrixVerification.userVerification
                    ? i18nc("@info another Matrix user asked to verify, %1 is their user id",
                            "%1 wants to verify with you.", matrixVerification.remoteUserId)
                    : i18nc("@info another session of this account asked to verify, %1 is a device id",
                            "Session %1 wants to verify with this one.", matrixVerification.remoteDeviceId)
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: i18nc("@info:whatsthis",
                            "Only accept this if you started it yourself on the other session.")
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
            }
        }

        // 3. In flight, and nothing for anybody to do.
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.waiting
            spacing: Kirigami.Units.largeSpacing

            QQC2.BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: root.waiting
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: matrixVerification.statusText
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        }

        // 4. The comparison. This is the whole security of the exercise: the
        //    emoji are derived from the shared secret, so two screens agreeing
        //    is what rules out somebody in the middle.
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.comparing
            spacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: matrixVerification.statusText
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
            }

            GridLayout {
                Layout.alignment: Qt.AlignHCenter
                columns: 4
                columnSpacing: Kirigami.Units.largeSpacing
                rowSpacing: Kirigami.Units.largeSpacing

                Repeater {
                    model: matrixVerification.emojis

                    delegate: ColumnLayout {
                        required property var modelData

                        spacing: 0

                        QQC2.Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: modelData.emoji
                            textFormat: Text.PlainText
                            font.pixelSize: Kirigami.Units.gridUnit * 2
                        }

                        QQC2.Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: modelData.description
                            textFormat: Text.PlainText
                            font: Kirigami.Theme.smallFont
                        }
                    }
                }
            }
        }

        // 5. What came of it.
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.done
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Icon {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: Kirigami.Units.iconSizes.large
                implicitHeight: Kirigami.Units.iconSizes.large
                source: matrixVerification.verified ? "security-high" : "dialog-warning"
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: matrixVerification.statusText
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    // The device list is read through this rather than bound straight to the
    // invokable, so that one refresh produces one re-read instead of one per
    // delegate.
    QtObject {
        id: deviceList

        property var items: []
        property int verifiedCount: 0

        function reload() {
            const all = matrixVerification.available ? matrixVerification.ownDevices() : []
            let mine = []
            let verified = 0
            for (let i = 0; i < all.length; ++i) {
                if (all[i].isCurrent)
                    continue
                mine.push(all[i])
                if (all[i].verified)
                    ++verified
            }
            deviceList.items = mine
            deviceList.verifiedCount = verified
        }
    }

    Connections {
        target: matrixVerification

        function onDevicesChanged() {
            deviceList.reload()
        }
        function onChanged() {
            deviceList.reload()
        }
    }

    customFooterActions: [
        Kirigami.Action {
            text: i18nc("@action:button accept an incoming verification request", "Accept")
            icon.name: "dialog-ok"
            visible: root.asking
            onTriggered: matrixVerification.acceptRequest()
        },
        Kirigami.Action {
            text: i18nc("@action:button the emoji on both sessions are the same", "They match")
            icon.name: "dialog-ok-apply"
            visible: root.comparing
            onTriggered: matrixVerification.confirmMatch()
        },
        Kirigami.Action {
            text: i18nc("@action:button the emoji on the two sessions differ", "They do not match")
            icon.name: "dialog-cancel"
            visible: root.comparing
            onTriggered: matrixVerification.rejectMismatch()
        },
        Kirigami.Action {
            text: i18nc("@action:button abandon a verification in progress", "Cancel")
            icon.name: "dialog-cancel"
            visible: root.asking || root.waiting
            onTriggered: matrixVerification.cancel()
        },
        Kirigami.Action {
            text: root.done
                ? i18nc("@action:button close the verification dialog after it finished", "Done")
                : i18nc("@action:button close the verification dialog", "Close")
            icon.name: "dialog-close"
            visible: root.picking || root.done
            onTriggered: root.close()
        }
    ]

    Component.onCompleted: deviceList.reload()
}
