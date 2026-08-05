// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import koutnet.app

// Settings, as FormCard sections rather than a tab bar full of bare labels and
// text fields. Four groups down one scrollable page: it is a short list, and a
// list this short reads better whole than split across pages the user has to
// hunt through.
//
// Nothing here writes the config file directly - AppSettings coalesces that. The
// one exception is the Network group, which has a button, because switching mode
// tears the relay tunnel up or down and should wait to be asked.
FormCard.FormCardPage {
    id: root

    signal saved()

    title: i18nc("@title:window", "Settings")

    // See the note on Kirigami.Theme in Main.qml: FormCardPage and every FormCard
    // inside it start theme chains of their own.
    Kirigami.Theme.highlightColor: Brand.accent

    // Relay and maintainer VDS are the two that route through a relay, so they
    // are the two that need a host and port.
    readonly property bool usesRelay: appSettings.connectionMode === 3
                                   || appSettings.connectionMode === 4

    // Prepends a "system default" row so an empty saved device id still selects
    // something instead of leaving the combo blank.
    function deviceList(devices) {
        const out = [{ id: "", description: i18nc("@item:inlistbox audio device", "System default") }]
        for (let i = 0; i < devices.length; ++i)
            out.push(devices[i])
        return out
    }

    // Leaving the mic open after the page closes would hold the device against
    // the next call.
    Component.onDestruction: audioDevices.stopMicTest()

    FormCard.FormHeader {
        title: i18nc("@title:group", "Identity")
    }

    FormCard.FormCard {
        FormCard.FormTextFieldDelegate {
            id: usernameField
            label: i18nc("@label:textbox the handle peers see", "Username")
            text: appSettings.username
            onEditingFinished: appSettings.username = text
        }

        FormCard.FormDelegateSeparator { above: usernameField; below: displayNameField }

        FormCard.FormTextFieldDelegate {
            id: displayNameField
            label: i18nc("@label:textbox", "Display name")
            description: i18nc("@info:whatsthis", "The name shown to peers, as opposed to the handle above.")
            text: appSettings.displayName
            onEditingFinished: appSettings.displayName = text
        }
    }

    FormCard.FormHeader {
        title: i18nc("@title:group", "Appearance")
    }

    FormCard.FormCard {
        FormCard.FormComboBoxDelegate {
            id: schemeCombo
            text: i18nc("@label:listbox", "Colour scheme")
            description: i18nc("@info:whatsthis", "Every other colour comes from the desktop's own scheme.")
            // Index order matches ColorSchemeSelector.Mode, so the current mode
            // is the index and back again without a lookup table.
            model: [
                i18nc("@item:inlistbox colour scheme", "Follow the system"),
                i18nc("@item:inlistbox colour scheme", "Light"),
                i18nc("@item:inlistbox colour scheme", "Dark"),
            ]
            currentIndex: ColorSchemeSelector.mode
            onActivated: (index) => ColorSchemeSelector.mode = index
        }
    }

    FormCard.FormHeader {
        title: i18nc("@title:group", "Audio")
    }

    FormCard.FormCard {
        FormCard.FormComboBoxDelegate {
            id: micCombo
            text: i18nc("@label:listbox", "Microphone")
            textRole: "description"
            valueRole: "id"
            model: root.deviceList(audioDevices.inputs)
            currentIndex: micCombo.indexOfValue(appSettings.audioInputId)
            onActivated: appSettings.audioInputId = micCombo.currentValue
        }

        FormCard.FormDelegateSeparator { above: micCombo; below: micTestDelegate }

        // A level meter is not one of the form delegates, so this is an
        // AbstractFormDelegate with the meter as its content - which is what that
        // class is for, and keeps the row the same height and padding as the rest.
        FormCard.AbstractFormDelegate {
            id: micTestDelegate
            background: null

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                QQC2.Button {
                    text: audioDevices.micTestRunning ? i18nc("@action:button", "Stop test")
                                                      : i18nc("@action:button", "Test microphone")
                    onClicked: {
                        if (audioDevices.micTestRunning)
                            audioDevices.stopMicTest()
                        else
                            audioDevices.startMicTest(appSettings.audioInputId)
                    }
                }

                QQC2.ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    value: audioDevices.micLevel
                }
            }
        }

        FormCard.FormDelegateSeparator { above: micTestDelegate; below: speakerCombo }

        FormCard.FormComboBoxDelegate {
            id: speakerCombo
            text: i18nc("@label:listbox", "Speakers")
            textRole: "description"
            valueRole: "id"
            model: root.deviceList(audioDevices.outputs)
            currentIndex: speakerCombo.indexOfValue(appSettings.audioOutputId)
            onActivated: appSettings.audioOutputId = speakerCombo.currentValue
        }

        FormCard.FormDelegateSeparator { above: speakerCombo; below: speakerTestDelegate }

        FormCard.FormButtonDelegate {
            id: speakerTestDelegate
            text: i18nc("@action:button", "Test speakers")
            icon.name: "audio-volume-high"
            enabled: !audioDevices.tonePlaying
            onClicked: audioDevices.playTestTone(appSettings.audioOutputId)
        }

        FormCard.FormDelegateSeparator { above: speakerTestDelegate; below: volumeDelegate }

        FormCard.FormSpinBoxDelegate {
            id: volumeDelegate
            label: i18nc("@label:spinbox playback volume in percent", "Volume")
            from: 0
            to: 100
            stepSize: 1
            value: appSettings.audioVolume
            onValueChanged: appSettings.audioVolume = volumeDelegate.value
        }

        FormCard.FormDelegateSeparator { above: volumeDelegate; below: muteDelegate }

        FormCard.FormSwitchDelegate {
            id: muteDelegate
            text: i18nc("@option:check", "Mute microphone")
            checked: appSettings.micMuted
            onToggled: appSettings.micMuted = muteDelegate.checked
        }

        FormCard.FormDelegateSeparator { above: muteDelegate; below: vadDelegate }

        FormCard.FormSwitchDelegate {
            id: vadDelegate
            text: i18nc("@option:check", "Voice activity detection")
            description: i18nc("@info:whatsthis", "Only send audio while you are speaking.")
            checked: appSettings.vadEnabled
            onToggled: appSettings.vadEnabled = vadDelegate.checked
        }
    }

    FormCard.FormHeader {
        title: i18nc("@title:group", "Network")
    }

    FormCard.FormCard {
        FormCard.FormComboBoxDelegate {
            id: modeCombo
            text: i18nc("@label:listbox", "Network mode")
            // The unbuilt modes stay on the list so the shape of the plan is
            // visible, but NetworkManager decides which can be picked.
            model: [
                i18nc("@item:inlistbox network mode", "Local network (LAN)"),
                i18nc("@item:inlistbox network mode", "K-Server (self-hosted)"),
                i18nc("@item:inlistbox network mode", "K-Server (join someone else's)"),
                i18nc("@item:inlistbox network mode", "Relay (not a K-Server)"),
                i18nc("@item:inlistbox network mode", "Maintainer's VDS"),
            ]
            currentIndex: appSettings.connectionMode
            onActivated: (index) => {
                if (networkManager.modeAvailable(index))
                    appSettings.connectionMode = index
                else
                    modeCombo.currentIndex = appSettings.connectionMode
            }
        }

        FormCard.FormDelegateSeparator { above: modeCombo; below: relayHostField }

        FormCard.FormTextFieldDelegate {
            id: relayHostField
            label: i18nc("@label:textbox", "Relay server address")
            enabled: root.usesRelay
            text: appSettings.relayHost
            onEditingFinished: appSettings.relayHost = text
        }

        FormCard.FormDelegateSeparator { above: relayHostField; below: relayPortField }

        FormCard.FormSpinBoxDelegate {
            id: relayPortField
            label: i18nc("@label:spinbox", "Relay server port")
            enabled: root.usesRelay
            from: 0
            to: 65535
            value: appSettings.relayPort
            onValueChanged: appSettings.relayPort = relayPortField.value
        }

        FormCard.FormDelegateSeparator { above: relayPortField; below: applyDelegate }

        // AppSettings only persists; the running NetworkManager has to be told
        // separately, and switching mode tears the relay tunnel up or down, so it
        // waits for an explicit click.
        FormCard.FormButtonDelegate {
            id: applyDelegate
            text: i18nc("@action:button apply the connection settings to the running network layer", "Apply connection settings")
            icon.name: "dialog-ok-apply"
            onClicked: {
                networkManager.setRelayServer(appSettings.relayHost, appSettings.relayPort, 0)
                networkManager.setConnectionMode(appSettings.connectionMode)
                root.saved()
            }
        }
    }
}
