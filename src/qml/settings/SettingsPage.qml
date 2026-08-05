// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import koutnet.app

// Settings, as FormCard sections rather than a tab bar full of bare labels and
// text fields. Five groups down one scrollable page: it is a short list, and a
// list this short reads better whole than split across pages the user has to
// hunt through.
//
// The profile is the first of them rather than a page of its own. It was one,
// and a page whose content was an identity block, six fields and a row of media
// shelves with nothing behind them is a page that reads as empty. The fields
// were settings all along.
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

    readonly property string shownName: appSettings.displayName.length > 0
        ? appSettings.displayName : appSettings.username

    // A FormCard fills the row it is given and then draws its card centred at its
    // own maximumWidth, leaving the rest of the row empty. Anything here that is
    // not a FormCard has to be handed the same width and the same alignment or it
    // starts at the window edge instead. Same value as FormCard.maximumWidth.
    readonly property real kContentWidth: Kirigami.Units.gridUnit * 30
    // What a form delegate puts round its text, so the blocks under the banner
    // share one left edge with the cards below them.
    readonly property real kContentPadding: Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing

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

    FileDialog {
        id: wallpaperDialog
        title: i18nc("@title:window", "Choose a wallpaper")
        nameFilters: [i18nc("@item:inlistbox file dialog filter, keep the glob patterns",
                            "Images (*.png *.jpg *.jpeg *.webp)")]
        onAccepted: appSettings.wallpaperPath = selectedFile
    }

    // GIFs are allowed here and not for the wallpaper: a banner is a strip the
    // size of a postcard and the wallpaper is the whole window.
    FileDialog {
        id: avatarDialog
        title: i18nc("@title:window", "Change avatar")
        nameFilters: [i18nc("@item:inlistbox file dialog filter, keep the glob patterns",
                            "Images (*.png *.jpg *.jpeg *.webp *.gif)")]
        onAccepted: appSettings.avatarPath = selectedFile
    }

    FileDialog {
        id: bannerDialog
        title: i18nc("@title:window", "Change banner")
        nameFilters: [i18nc("@item:inlistbox file dialog filter, keep the glob patterns",
                            "Images (*.png *.jpg *.jpeg *.webp *.gif)")]
        onAccepted: appSettings.bannerPath = selectedFile
    }

    FileDialog {
        id: badgeDialog
        title: i18nc("@title:window", "Choose name badge")
        nameFilters: [i18nc("@item:inlistbox file dialog filter, keep the glob patterns",
                            "Images (*.png *.jpg *.jpeg *.webp *.gif)")]
        onAccepted: appSettings.nameBadgePath = selectedFile
    }

    // The same panel the composer writes messages with, which is where the emoji
    // data and the search already are. A picker of its own here would be a second
    // copy of both.
    EmojiPopup {
        id: statusSheet
        onPicked: (emoji) => appSettings.statusEmoji = emoji
    }

    FormCard.FormHeader {
        title: i18nc("@title:group your own identity as other people see it", "Profile")
    }

    // Banner, the avatar over its lower-left, the name and the handle - see
    // qml/profile/ProfileHeader.qml, which the peer's profile and both cards are
    // built out of too.
    //
    // No presence line: your own reachability is that the process is running,
    // which is not news. There is no edit mode either. Everything below writes
    // straight through, which is what a settings page does anyway, and an
    // "Edit profile" button on a page of settings was a second word for "type
    // here".
    ProfileHeader {
        Layout.fillWidth: true
        Layout.maximumWidth: root.kContentWidth
        Layout.alignment: Qt.AlignHCenter

        displayName: root.shownName
        handle: appSettings.username
        avatarSource: appSettings.avatarPath
        bannerSource: appSettings.bannerPath
        badgeSource: appSettings.nameBadgePath
        showPresence: false
        editable: true

        onAvatarPickRequested: avatarDialog.open()
        onBannerPickRequested: bannerDialog.open()
    }

    ProfileBlock {
        Layout.fillWidth: true
        Layout.maximumWidth: root.kContentWidth
        Layout.alignment: Qt.AlignHCenter
        Layout.leftMargin: root.kContentPadding
        Layout.rightMargin: root.kContentPadding

        label: i18nc("@label:textbox caption over what somebody says they are up to", "Custom status")

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            // Falls back to an icon rather than to a placeholder character: with
            // nothing set there is no emoji to show, and a face is what says
            // what the slot is for.
            QQC2.ToolButton {
                display: appSettings.statusEmoji.length > 0 ? QQC2.AbstractButton.TextOnly
                                                            : QQC2.AbstractButton.IconOnly
                icon.name: "face-smile"
                text: appSettings.statusEmoji
                font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * 1.4)

                Accessible.name: i18nc("@action:button", "Set a status emoji")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                QQC2.ToolTip.text: Accessible.name

                onClicked: statusSheet.open()
            }

            // Device-local for now. The presence packet carries the emoji and
            // repeats on a timer, so a line of free text is not something to put
            // in it - see NetworkManager::setStatus.
            QQC2.TextField {
                Layout.fillWidth: true
                text: appSettings.statusText
                placeholderText: i18nc("@info:placeholder", "What are you up to?")
                onEditingFinished: appSettings.statusText = text
            }

            QQC2.ToolButton {
                display: QQC2.AbstractButton.IconOnly
                icon.name: "edit-clear"
                enabled: appSettings.statusEmoji.length > 0 || appSettings.statusText.length > 0
                text: i18nc("@action:button remove the custom status emoji", "Clear status emoji")

                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                QQC2.ToolTip.text: text

                onClicked: {
                    appSettings.statusEmoji = ""
                    appSettings.statusText = ""
                }
            }
        }
    }

    // The blocks between the banner and the cards carry their own gaps: the page
    // lays its children out with no spacing, on the assumption that every one of
    // them is a card with a FormHeader over it.
    ProfileBlock {
        Layout.fillWidth: true
        Layout.maximumWidth: root.kContentWidth
        Layout.alignment: Qt.AlignHCenter
        Layout.leftMargin: root.kContentPadding
        Layout.rightMargin: root.kContentPadding
        Layout.topMargin: Kirigami.Units.largeSpacing

        label: i18nc("@title:group free-form text about yourself", "About me")

        // Plain TextArea rather than a rendered Markdown label with an edit mode
        // behind it: this is the page where things are typed, so what is on
        // screen is the source. It is a peer's copy that gets rendered.
        QQC2.TextArea {
            Layout.fillWidth: true
            Layout.minimumHeight: Kirigami.Units.gridUnit * 5
            wrapMode: TextEdit.Wrap
            text: appSettings.bio
            placeholderText: i18nc("@info:placeholder", "Tell us about yourself...")
            onEditingFinished: appSettings.bio = text
        }
    }

    FormCard.FormCard {
        Layout.topMargin: Kirigami.Units.largeSpacing

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

        FormCard.FormDelegateSeparator { above: displayNameField; below: badgeButton }

        FormCard.FormButtonDelegate {
            id: badgeButton
            text: i18nc("@action:button", "Choose name badge")
            icon.name: "insert-image"
            onClicked: badgeDialog.open()
        }
    }

    FormCard.FormHeader {
        title: i18nc("@title:group where the account lives", "Account")
    }

    FormCard.FormCard {
        FormCard.FormRadioDelegate {
            id: localRadio
            text: i18nc("@option:radio account scope, this device only", "Local")
            description: i18nc("@info:whatsthis", "This device only. Always usable.")
            checked: !appSettings.globalAccount
            onToggled: if (localRadio.checked) appSettings.globalAccount = false
        }

        FormCard.FormDelegateSeparator { above: localRadio; below: globalRadio }

        FormCard.FormRadioDelegate {
            id: globalRadio
            text: i18nc("@option:radio account scope, hosted on a K-Server", "Global")
            // Local is always a valid identity - it is just this device, there is
            // nothing to register. Only Global depends on a K-Server connection
            // that does not exist yet, so only it gets the caption.
            description: appSettings.globalAccountRegistered
                ? i18nc("@info:whatsthis", "Synced through a K-Server.")
                : i18nc("@info:status this identity has no K-Server account", "Not registered")
            checked: appSettings.globalAccount
            onToggled: if (globalRadio.checked) appSettings.globalAccount = true
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

        FormCard.FormDelegateSeparator { above: schemeCombo; below: wallpaperButton }

        // Local decoration and nothing else: the picture is never put on the
        // wire, and no peer is told it exists. Said here as well as in the kcfg
        // because it is the sort of thing a user of an encrypted messenger is
        // entitled to be told without reading the source.
        FormCard.FormButtonDelegate {
            id: wallpaperButton
            text: i18nc("@action:button", "Wallpaper")
            description: appSettings.wallpaperPath.length > 0
                ? appSettings.wallpaperPath
                : i18nc("@info:whatsthis", "A picture behind the interface. Stays on this device and is never sent to anyone.")
            icon.name: "preferences-desktop-wallpaper"
            onClicked: wallpaperDialog.open()
        }

        FormCard.FormDelegateSeparator { above: wallpaperButton; below: wallpaperOpacityDelegate }

        // A slider rather than a spin box, because this is a value that is judged
        // by looking at the result and not by typing a number. There is no slider
        // among the form delegates at the version this targets, so it is an
        // AbstractFormDelegate with one in it - the same shape as the microphone
        // meter below, which keeps the row's height and padding.
        FormCard.AbstractFormDelegate {
            id: wallpaperOpacityDelegate
            background: null
            enabled: appSettings.wallpaperPath.length > 0

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18nc("@label:slider how much of the wallpaper shows through", "Wallpaper opacity")
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.largeSpacing

                    QQC2.Slider {
                        id: opacitySlider
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        stepSize: 1
                        value: appSettings.wallpaperOpacity
                        // moved rather than valueChanged: the latter also fires
                        // while the binding above is settling, which writes the
                        // setting back over itself on every page open.
                        onMoved: appSettings.wallpaperOpacity = Math.round(opacitySlider.value)
                    }

                    QQC2.Label {
                        text: i18nc("@info:status a percentage, %1 is a number", "%1%", appSettings.wallpaperOpacity)
                        color: Kirigami.Theme.disabledTextColor
                    }
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: i18nc("@info:whatsthis", "The interface stays readable: a veil over the picture keeps its strength even at the top of the range.")
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                }
            }
        }

        FormCard.FormDelegateSeparator { above: wallpaperOpacityDelegate; below: clearWallpaperButton }

        FormCard.FormButtonDelegate {
            id: clearWallpaperButton
            text: i18nc("@action:button", "Remove wallpaper")
            icon.name: "edit-clear"
            enabled: appSettings.wallpaperPath.length > 0
            onClicked: appSettings.wallpaperPath = ""
        }

        FormCard.FormDelegateSeparator { above: clearWallpaperButton; below: compactDelegate }

        FormCard.FormSwitchDelegate {
            id: compactDelegate
            text: i18nc("@option:check", "Compact mode")
            description: i18nc("@info:whatsthis", "A reduced layout for a narrow window: the conversation list and the conversation only, with tighter rows.")
            checked: appSettings.compactMode
            onToggled: appSettings.compactMode = compactDelegate.checked
        }
    }

    FormCard.FormHeader {
        title: i18nc("@title:group the system tray icon and notifications", "Tray and notifications")
    }

    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            id: trayDelegate
            text: i18nc("@option:check", "Show an icon in the system tray")
            // Registering a status notifier item is a D-Bus name claim, and
            // dropping it while the window was hidden would leave no way back to
            // it, so the switch is read at start rather than applied live. Said
            // out loud, because a switch that appears to do nothing is worse than
            // one that admits when it takes effect.
            description: i18nc("@info:whatsthis", "Takes effect the next time KOutNet starts.")
            checked: appSettings.trayEnabled
            onToggled: appSettings.trayEnabled = trayDelegate.checked
        }

        FormCard.FormDelegateSeparator { above: trayDelegate; below: minimizeDelegate }

        FormCard.FormSwitchDelegate {
            id: minimizeDelegate
            text: i18nc("@option:check", "Close to the tray instead of quitting")
            enabled: appSettings.trayEnabled
            checked: appSettings.minimizeToTray
            onToggled: appSettings.minimizeToTray = minimizeDelegate.checked
        }

        FormCard.FormDelegateSeparator { above: minimizeDelegate; below: awayDelegate }

        // What separates "the window is behind something" from "nobody is there",
        // which is what decides whether an arriving message gets a popup, a
        // sound, or both. The three cases themselves are switches in System
        // Settings, next to every other application's, rather than here.
        FormCard.FormSpinBoxDelegate {
            id: awayDelegate
            label: i18nc("@label:spinbox minutes of no input before counting as away", "Count as away after (minutes)")
            from: 1
            to: 240
            stepSize: 1
            value: appSettings.awayAfterMinutes
            onValueChanged: appSettings.awayAfterMinutes = awayDelegate.value
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
