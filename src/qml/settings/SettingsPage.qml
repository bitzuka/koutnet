// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import koutnet.app

// One page of settings grew past what a single scroll could carry, so the
// groups are tabs now: who you are, what language the window speaks, how it
// looks, and one tab per transport it can talk to. Nothing here writes the
// config file directly, AppSettings coalesces that, except the LAN/VPN tab,
// which has a button because static peers are a transport change, not a
// field apply.
Kirigami.Page {
    id: root

    signal saved()
    // Pushed by the window rather than from here: layers are one deep by
    // design, and a page that pushes over itself is the exception that broke it.
    signal matrixAccountRequested()

    title: i18nc("@title:window", "Settings")
    padding: 0

    // See Main.qml: this page and every FormCard in it start their own chains.
    Kirigami.Theme.highlightColor: Brand.accent

    readonly property string shownName: appSettings.displayName.length > 0
        ? appSettings.displayName : appSettings.username

    // A FormCard fills its row and then draws the card centred at its own
    // maximumWidth. The identity block is the one thing here that is not a FormCard,
    // so it needs the same width and alignment or it starts at the window edge -
    // which is why every card below is handed this as well, instead of keeping its
    // own default. Thirty grid units is that default, and a form that narrow leaves
    // two thirds of a desktop monitor empty either side; this grows with the window
    // and stops at forty-eight, roughly a hundred characters of the interface font,
    // which is still one column the eye can track without losing the line.
    readonly property real kContentWidth: Math.max(Kirigami.Units.gridUnit * 20,
        Math.min(root.width - Kirigami.Units.largeSpacing * 4, Kirigami.Units.gridUnit * 48))

    // A "system default" row, so an empty saved device id still selects something.
    function deviceList(devices) {
        const out = [{ id: "", description: i18nc("@item:inlistbox audio device", "System default") }]
        for (let i = 0; i < devices.length; ++i)
            out.push(devices[i])
        return out
    }

    // The languages po/ holds translations for; "" is the system language and
    // en is the source strings. The tab reads this back to front: displayName
    // is what is drawn, code is what is stored.
    readonly property var languages: [
        { code: "", displayName: i18nc("@item:inlistbox interface language", "Follow the system") },
        { code: "en", displayName: "English" },
        { code: "ru", displayName: "Русский" },
        { code: "uk", displayName: "Українська" },
        { code: "de", displayName: "Deutsch" },
        { code: "fr", displayName: "Français" },
        { code: "es", displayName: "Español" },
        { code: "it", displayName: "Italiano" },
        { code: "pt", displayName: "Português" },
        { code: "pl", displayName: "Polski" },
        { code: "tr", displayName: "Türkçe" },
        { code: "ar", displayName: "العربية" },
        { code: "hi", displayName: "हिन्दी" },
        { code: "ja", displayName: "日本語" },
        { code: "zh", displayName: "中文" },
    ]

    function languageIndex(code) {
        for (let i = 0; i < root.languages.length; ++i) {
            if (root.languages[i].code === code)
                return i
        }
        return 0
    }

    // Leaving the mic open would hold the device against the next call.
    Component.onDestruction: audioDevices.stopMicTest()

    FileDialog {
        id: wallpaperDialog
        title: i18nc("@title:window", "Choose a wallpaper")
        nameFilters: [i18nc("@item:inlistbox file dialog filter, keep the glob patterns",
                            "Images (*.png *.jpg *.jpeg *.webp)")]
        onAccepted: appSettings.wallpaperPath = selectedFile
    }

    // GIFs here but not for the wallpaper: a banner is the size of a postcard.
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

    // The composer's panel, which is where the emoji data and the search already
    // are; a picker of its own here would be a second copy of both.
    EmojiPopup {
        id: statusSheet
        onPicked: (emoji) => appSettings.statusEmoji = emoji
    }

    header: QQC2.TabBar {
        id: tabBar

        QQC2.TabButton { text: i18nc("@title:tab settings section", "Profile") }
        QQC2.TabButton { text: i18nc("@title:tab settings section", "Language") }
        QQC2.TabButton { text: i18nc("@title:tab settings section", "Interface") }
        QQC2.TabButton { text: "Matrix" }
        QQC2.TabButton { text: i18nc("@title:tab settings section", "LAN / VPN") }
        QQC2.TabButton { text: "Telegram" }
        QQC2.TabButton { text: "Rocket.Chat" }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: tabBar.currentIndex

        // ---- Profile ----
        QQC2.ScrollView {
            id: profileScroll

            contentWidth: availableWidth

            ColumnLayout {
                width: profileScroll.availableWidth
                spacing: 0

                FormCard.FormHeader {
                    maximumWidth: root.kContentWidth
                    title: i18nc("@title:group your own identity as other people see it", "Profile")
                }

                // No presence line: your own reachability is that the process is running. No
                // edit mode either - everything below writes straight through, and an "Edit
                // profile" button on a page of settings was a second word for "type here".
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

                // One card rather than two loose blocks above a card of three fields: the loose
                // ones each carried their own width, alignment, padding and top margin to line up
                // with the card below, four numbers per block that only ever agreed by hand.
                FormCard.FormCard {
                    maximumWidth: root.kContentWidth
                    // FormHeader supplies its own gap above every other card here; this one has
                    // the identity block over it, and the page lays children out with no spacing.
                    Layout.topMargin: Kirigami.Units.smallSpacing

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

                    FormCard.FormDelegateSeparator { above: displayNameField; below: statusDelegate }

                    // No form delegate exists for a row of three controls, so this is an
                    // AbstractFormDelegate with the row inside, which keeps height and padding.
                    FormCard.AbstractFormDelegate {
                        id: statusDelegate
                        background: null

                        contentItem: ColumnLayout {
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Label {
                                Layout.fillWidth: true
                                text: i18nc("@label:textbox caption over what somebody says they are up to", "Custom status")
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                // An icon rather than a placeholder character: with nothing set
                                // there is no emoji to show, and a face says what the slot is for.
                                QQC2.ToolButton {
                                    display: appSettings.statusEmoji.length > 0 ? QQC2.AbstractButton.TextOnly
                                                                                : QQC2.AbstractButton.IconOnly
                                    icon.name: "face-smile"
                                    text: appSettings.statusEmoji

                                    Accessible.name: i18nc("@action:button", "Set a status emoji")
                                    QQC2.ToolTip.visible: hovered
                                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                                    QQC2.ToolTip.text: Accessible.name

                                    onClicked: statusSheet.open()
                                }

                                // Device-local for now: the presence packet carries the emoji and
                                // repeats on a timer, so free text has no business in it.
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
                    }

                    FormCard.FormDelegateSeparator { above: statusDelegate; below: bioDelegate }

                    FormCard.AbstractFormDelegate {
                        id: bioDelegate
                        background: null

                        contentItem: ColumnLayout {
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Label {
                                Layout.fillWidth: true
                                text: i18nc("@title:group free-form text about yourself", "About me")
                            }

                            // Plain TextArea rather than rendered Markdown behind an edit mode:
                            // this is the page where things are typed, so what is on screen is the
                            // source. It is a peer's copy that gets rendered.
                            QQC2.TextArea {
                                Layout.fillWidth: true
                                Layout.minimumHeight: Math.round(Kirigami.Units.gridUnit * 3)
                                wrapMode: TextEdit.Wrap
                                text: appSettings.bio
                                placeholderText: i18nc("@info:placeholder", "Tell us about yourself...")
                                onEditingFinished: appSettings.bio = text
                            }
                        }
                    }

                    FormCard.FormDelegateSeparator { above: bioDelegate; below: badgeButton }

                    FormCard.FormButtonDelegate {
                        id: badgeButton
                        text: i18nc("@action:button", "Choose name badge")
                        icon.name: "insert-image"
                        onClicked: badgeDialog.open()
                    }
                }

                // read-only, one line per transport so all four identities sit together.
                // missing ones show "-" instead of a blank row.
                FormCard.FormHeader {
                    maximumWidth: root.kContentWidth
                    title: i18nc("@title:group the identities this account has on each backend", "Usernames")
                }

                FormCard.FormCard {
                    id: usernamesCard
                    maximumWidth: root.kContentWidth

                    readonly property string kNone: i18nc("@info:status no username on this backend", "-")

                    FormCard.FormTextDelegate {
                        text: i18nc("@label the local-network identity", "LAN / VPN")
                        description: appSettings.username.length > 0 ? appSettings.username : usernamesCard.kNone
                    }

                    FormCard.FormDelegateSeparator {}

                    FormCard.FormTextDelegate {
                        text: "Matrix"
                        description: (matrixManager.loggedIn && matrixManager.userId.length > 0)
                            ? matrixManager.userId
                            : usernamesCard.kNone
                    }

                    FormCard.FormDelegateSeparator {}

                    FormCard.FormTextDelegate {
                        text: "Telegram"
                        description: usernamesCard.kNone
                    }

                    FormCard.FormDelegateSeparator {}

                    FormCard.FormTextDelegate {
                        text: "Rocket.Chat"
                        description: usernamesCard.kNone
                    }
                }
            }
        }

        // ---- Language ----
        QQC2.ScrollView {
            id: languageScroll

            contentWidth: availableWidth

            ColumnLayout {
                width: languageScroll.availableWidth
                spacing: 0

                FormCard.FormHeader {
                    maximumWidth: root.kContentWidth
                    title: i18nc("@title:group the language the interface speaks", "Language")
                }

                FormCard.FormCard {
                    maximumWidth: root.kContentWidth

                    FormCard.FormComboBoxDelegate {
                        id: languageCombo
                        text: i18nc("@label:listbox", "Interface language")
                        description: i18nc("@info:whatsthis", "Follows the system until a language is picked here. Takes effect the next time KOutNet starts.")
                        textRole: "displayName"
                        valueRole: "code"
                        model: root.languages
                        currentIndex: root.languageIndex(appSettings.language)
                        onActivated: appSettings.language = languageCombo.currentValue
                    }
                }
            }
        }

        // ---- Interface ----
        QQC2.ScrollView {
            id: interfaceScroll

            contentWidth: availableWidth

            ColumnLayout {
                width: interfaceScroll.availableWidth
                spacing: 0

                FormCard.FormHeader {
                    maximumWidth: root.kContentWidth
                    title: i18nc("@title:group", "Appearance")
                }

                FormCard.FormCard {
                    maximumWidth: root.kContentWidth
                    FormCard.FormComboBoxDelegate {
                        id: schemeCombo
                        text: i18nc("@label:listbox", "Colour scheme")
                        description: i18nc("@info:whatsthis", "Every other colour comes from the desktop's own scheme.")
                        // Index order matches ColorSchemeSelector.Mode, so no lookup table.
                        model: [
                            i18nc("@item:inlistbox colour scheme", "Follow the system"),
                            i18nc("@item:inlistbox colour scheme", "Light"),
                            i18nc("@item:inlistbox colour scheme", "Dark"),
                        ]
                        currentIndex: ColorSchemeSelector.mode
                        onActivated: (index) => ColorSchemeSelector.mode = index
                    }

                    FormCard.FormDelegateSeparator { above: schemeCombo; below: wallpaperButton }

                    // Said here as well as in the kcfg because a user of an encrypted messenger
                    // is entitled to know the picture never goes on the wire.
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

                    // A slider because this value is judged by looking at the result, and an
                    // AbstractFormDelegate because the form delegates of this version have none.
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
                                    // moved rather than valueChanged: the latter also fires while
                                    // the binding settles, rewriting the setting on every open.
                                    onMoved: appSettings.wallpaperOpacity = Math.round(opacitySlider.value)
                                }

                                QQC2.Label {
                                    text: i18nc("@info:status a percentage, %1 is a number", "%1%", appSettings.wallpaperOpacity)
                                    color: Kirigami.Theme.disabledTextColor

                                    // The veil that keeps the text legible never reaches zero.
                                    QQC2.ToolTip.visible: opacityHover.hovered
                                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                                    QQC2.ToolTip.text: i18nc("@info:tooltip", "A veil over the picture keeps the interface readable even at the top of the range.")

                                    HoverHandler {
                                        id: opacityHover
                                    }
                                }
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
                    maximumWidth: root.kContentWidth
                    title: i18nc("@title:group the system tray icon and notifications", "Tray and notifications")
                }

                FormCard.FormCard {
                    maximumWidth: root.kContentWidth
                    FormCard.FormSwitchDelegate {
                        id: trayDelegate
                        text: i18nc("@option:check", "Show an icon in the system tray")
                        // Registering a status notifier item is a D-Bus name claim, and dropping
                        // it while the window was hidden would leave no way back, so the switch is
                        // read at start rather than applied live - and says so.
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

                    // The three cases themselves are switches in System Settings, next to every
                    // other application's, rather than here.
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
                    maximumWidth: root.kContentWidth
                    title: i18nc("@title:group", "Audio")
                }

                FormCard.FormCard {
                    maximumWidth: root.kContentWidth
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

                    // A level meter is not one of the form delegates, hence AbstractFormDelegate.
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
            }
        }

        // ---- Matrix ----
        QQC2.ScrollView {
            id: matrixScroll

            contentWidth: availableWidth

            ColumnLayout {
                width: matrixScroll.availableWidth
                spacing: 0

                FormCard.FormHeader {
                    maximumWidth: root.kContentWidth
                    title: i18nc("@title:group where the account lives", "Account")
                }

                FormCard.FormCard {
                    maximumWidth: root.kContentWidth
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
                        text: i18nc("@option:radio account scope, hosted on a Matrix homeserver", "Global")
                        // Local is always valid - it is just this device. Only Global depends on
                        // a Matrix connection that does not exist yet, so only it is captioned.
                        description: appSettings.globalAccountRegistered
                            ? i18nc("@info:whatsthis", "Synced through a Matrix homeserver.")
                            : i18nc("@info:status this identity has no Matrix account", "Not registered")
                        checked: appSettings.globalAccount
                        onToggled: if (globalRadio.checked) appSettings.globalAccount = true
                    }

                    FormCard.FormDelegateSeparator { above: globalRadio; below: matrixButton }

                    // The sign-in is a page of its own rather than three fields here: it has
                    // a session state to report and a sign-out, neither of which belongs
                    // between two radio buttons.
                    FormCard.FormButtonDelegate {
                        id: matrixButton
                        icon.name: "network-server"
                        text: i18nc("@action:button", "Matrix account...")
                        description: matrixManager.statusText
                        onClicked: root.matrixAccountRequested()
                    }
                }

                FormCard.FormHeader {
                    maximumWidth: root.kContentWidth
                    title: i18nc("@title:group the Matrix homeserver to talk to", "Homeserver")
                }

                FormCard.FormCard {
                    maximumWidth: root.kContentWidth

                    // Empty means a homeserver on this machine, which is what self-hosting is.
                    FormCard.FormTextFieldDelegate {
                        id: matrixHostField
                        label: i18nc("@label:textbox", "Homeserver address")
                        description: i18nc("@info:whatsthis", "Leave empty for a server running on this machine. Takes effect the next time the account signs in.")
                        text: appSettings.kServerHost
                        onEditingFinished: appSettings.kServerHost = text
                    }

                    FormCard.FormDelegateSeparator { above: matrixHostField; below: matrixPortField }

                    FormCard.FormSpinBoxDelegate {
                        id: matrixPortField
                        label: i18nc("@label:spinbox", "Homeserver port")
                        from: 0
                        to: 65535
                        value: appSettings.kServerPort
                        onValueChanged: appSettings.kServerPort = matrixPortField.value
                    }
                }
            }
        }

        // ---- LAN / VPN ----
        QQC2.ScrollView {
            id: lanScroll

            contentWidth: availableWidth

            ColumnLayout {
                width: lanScroll.availableWidth
                spacing: 0

                FormCard.FormHeader {
                    maximumWidth: root.kContentWidth
                    title: i18nc("@title:group the local-network side of the connection", "LAN / VPN")
                }

                FormCard.FormCard {
                    maximumWidth: root.kContentWidth

                    FormCard.FormTextFieldDelegate {
                        id: staticPeersField
                        label: i18nc("@label:textbox", "Static peers")
                        placeholderText: i18nc("@info:placeholder a list of addresses", "10.0.0.5, 192.168.2.14")
                        text: appSettings.staticPeers.join(", ")
                        onEditingFinished: {
                            const parts = staticPeersField.text.split(RegExp("[,\\s;]+"))
                            appSettings.staticPeers = parts.filter(p => p.length > 0)
                        }
                    }

                    FormCard.FormDelegateSeparator { above: staticPeersField; below: applyDelegate }

                    // AppSettings only persists; the running NetworkManager is told separately,
                    // and switching peers is a transport change, so it waits for the button.
                    FormCard.FormButtonDelegate {
                        id: applyDelegate
                        text: i18nc("@action:button apply the connection settings to the running network layer", "Apply connection settings")
                        icon.name: "dialog-ok-apply"
                        onClicked: {
                            networkManager.setStaticPeers(appSettings.staticPeers)
                            networkManager.setConnectionMode(appSettings.connectionMode)
                            root.saved()
                        }
                    }
                }
            }
        }

        // ---- Telegram ----
        Item {
            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.largeSpacing * 4
                icon.name: "send-to"
                text: "Telegram"
                explanation: i18nc("@info a transport that is not implemented yet", "Not built yet.")
            }
        }

        // ---- Rocket.Chat ----
        Item {
            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.largeSpacing * 4
                icon.name: "chat-partner"
                text: "Rocket.Chat"
                explanation: i18nc("@info a transport that is not implemented yet", "Not built yet.")
            }
        }
    }
}
