// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import koutnet.app

// Own-profile page. The identity block at the top is ProfileHeader, shared with
// the peer's page; everything under it that used to be an editable field
// floating on a Rectangle is a FormCard section that only appears while editing.
//
// Local and Global differ only in the body. Local is this device's identity and
// always usable; Global needs a K-Server that does not exist yet, so an
// unregistered Global identity gets a registration prompt instead of a body.
// Avatar, banner, name and about are device-local either way.
Kirigami.ScrollablePage {
    id: root

    // The window owns the one message strip, this page only asks for it.
    signal notImplemented(string text)

    property bool editMode: false
    readonly property bool profileUsable: !appSettings.globalAccount || appSettings.globalAccountRegistered
    readonly property string shownName: appSettings.displayName.length > 0
        ? appSettings.displayName : appSettings.username

    // The one column everything on this page lines up in.
    //
    // A FormCard fills the row it is given and then draws its card centred at its
    // own maximumWidth, leaving the rest of the row empty. Anything here that is
    // not a FormCard has to be handed the same width and the same alignment or it
    // starts at the window edge instead - which is why the header, the media tabs
    // and the placeholders each used to begin somewhere different from the cards
    // between them. Same value as FormCard.maximumWidth.
    readonly property real kContentWidth: Kirigami.Units.gridUnit * 30

    title: i18nc("@title:window", "My profile")

    // See the note on Kirigami.Theme in Main.qml.
    Kirigami.Theme.highlightColor: Brand.accent

    function commitBio() {
        appSettings.bio = aboutEdit.text
    }

    function toggleEditMode() {
        if (root.editMode)
            root.commitBio()
        root.editMode = !root.editMode
    }

    actions: [
        Kirigami.Action {
            text: root.editMode ? i18nc("@action:button", "Done")
                                : i18nc("@action:button", "Edit profile")
            icon.name: root.editMode ? "dialog-ok-apply" : "document-edit"
            onTriggered: root.toggleEditMode()
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Share this profile")
            icon.name: "document-share"
            onTriggered: root.notImplemented(i18nc("@info", "Sharing a profile will appear once a K-Server is connected."))
        }
    ]

    // AnimatedImage plays animated GIFs where plain Image freezes on the first
    // frame, and renders png/jpg/webp identically, so one type covers both. It
    // also clears its own playing flag whenever it loads a source with no frames,
    // and the empty path at startup is one of those, so playback gets re-armed on
    // every load.
    component LoopingImage: AnimatedImage {
        fillMode: Image.PreserveAspectCrop
        onStatusChanged: if (status === AnimatedImage.Ready) playing = true
    }

    // The backdrop the "Change background" picker sets, over an opaque base.
    //
    // The base is a layer of its own because it is the thing that makes the page
    // opaque, and the scrim above it is a fraction. Dimming the only opaque item
    // in the background is what made the whole page forty-five percent
    // see-through: the window's wallpaper came up through it - see Main.qml - and
    // took the text with it.
    //
    // The picture is gated on having loaded rather than on the path being
    // non-empty, so a file AnimatedImage cannot decode leaves the base showing
    // rather than a hole with the wallpaper behind it.
    background: Rectangle {
        color: Kirigami.Theme.backgroundColor

        LoopingImage {
            id: backdrop
            anchors.fill: parent
            source: appSettings.profileBackgroundPath
            visible: backdrop.status === AnimatedImage.Ready
        }

        // Dimmed, because the picture is chosen by the user and the text on top of
        // it still has to be readable.
        Rectangle {
            anchors.fill: parent
            visible: backdrop.visible
            color: Kirigami.Theme.backgroundColor
            opacity: 0.55
        }
    }

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
        id: backgroundDialog
        title: i18nc("@title:window", "Change background")
        nameFilters: [i18nc("@item:inlistbox file dialog filter, keep the glob patterns",
                            "Images (*.png *.jpg *.jpeg *.webp *.gif)")]
        onAccepted: appSettings.profileBackgroundPath = selectedFile
    }

    // The same panel the composer writes messages with, which is where the emoji
    // data and the search already are. A picker of its own here would be a second
    // copy of both.
    EmojiPopup {
        id: statusSheet
        onPicked: (emoji) => appSettings.statusEmoji = emoji
    }

    FileDialog {
        id: badgeDialog
        title: i18nc("@title:window", "Choose name badge")
        nameFilters: [i18nc("@item:inlistbox file dialog filter, keep the glob patterns",
                            "Images (*.png *.jpg *.jpeg *.webp *.gif)")]
        onAccepted: appSettings.nameBadgePath = selectedFile
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        // Banner, avatar, name, handle, presence and status - see
        // qml/profile/ProfileHeader.qml, which the peer's page uses too. The two
        // picker buttons are only offered here, and only while editing.
        //
        // online is hardcoded true because this is the local identity: the
        // process is running, which is the only sense in which "am I reachable"
        // has an answer this end can give. What the user says they are is the
        // status emoji and the presence setting, which are separate.
        ProfileHeader {
            Layout.fillWidth: true
            Layout.maximumWidth: root.kContentWidth
            Layout.alignment: Qt.AlignHCenter

            displayName: root.shownName
            handle: appSettings.username
            online: true
            statusEmoji: appSettings.statusEmoji
            avatarSource: appSettings.avatarPath
            bannerSource: appSettings.bannerPath
            badgeSource: appSettings.nameBadgePath
            editable: root.editMode
            statusEditable: true

            onAvatarPickRequested: avatarDialog.open()
            onBannerPickRequested: bannerDialog.open()
            onStatusPickRequested: statusSheet.open()
        }

        // Everything editable, in one place, only while editing. This used to be
        // text fields and two hand-drawn radio rectangles wedged into the layout
        // above with a visible binding each.
        FormCard.FormCard {
            Layout.fillWidth: true
            visible: root.editMode

            FormCard.FormTextFieldDelegate {
                id: displayNameField
                label: i18nc("@label:textbox", "Display name")
                text: appSettings.displayName
                onEditingFinished: appSettings.displayName = text
            }

            FormCard.FormDelegateSeparator { above: displayNameField; below: usernameField }

            FormCard.FormTextFieldDelegate {
                id: usernameField
                label: i18nc("@label:textbox the handle peers see", "Username")
                text: appSettings.username
                onEditingFinished: appSettings.username = text
            }

            FormCard.FormDelegateSeparator { above: usernameField; below: statusButton }

            // The header carries the same picker, because a status is changed far
            // more often than a display name and should not need edit mode. This
            // one is here for the clear button beside it, which has nowhere
            // sensible to sit on the header itself.
            FormCard.FormButtonDelegate {
                id: statusButton
                text: i18nc("@action:button", "Status emoji")
                description: appSettings.statusEmoji.length > 0
                    ? appSettings.statusEmoji
                    : i18nc("@info:placeholder no custom status is set", "None")
                icon.name: "face-smile"
                onClicked: statusSheet.open()
            }

            FormCard.FormDelegateSeparator { above: statusButton; below: clearStatusButton }

            FormCard.FormButtonDelegate {
                id: clearStatusButton
                text: i18nc("@action:button remove the custom status emoji", "Clear status emoji")
                icon.name: "edit-clear"
                enabled: appSettings.statusEmoji.length > 0
                onClicked: appSettings.statusEmoji = ""
            }

            FormCard.FormDelegateSeparator { above: clearStatusButton; below: badgeButton }

            FormCard.FormButtonDelegate {
                id: badgeButton
                text: i18nc("@action:button", "Choose name badge")
                icon.name: "insert-image"
                onClicked: badgeDialog.open()
            }

            FormCard.FormDelegateSeparator { above: badgeButton; below: backgroundButton }

            FormCard.FormButtonDelegate {
                id: backgroundButton
                text: i18nc("@action:button", "Change background")
                description: appSettings.profileBackgroundPath
                icon.name: "insert-image"
                onClicked: backgroundDialog.open()
            }

            FormCard.FormDelegateSeparator { above: backgroundButton; below: clearBackgroundButton }

            FormCard.FormButtonDelegate {
                id: clearBackgroundButton
                text: i18nc("@action:button", "Remove background")
                icon.name: "edit-clear"
                enabled: appSettings.profileBackgroundPath.length > 0
                onClicked: appSettings.profileBackgroundPath = ""
            }
        }

        FormCard.FormHeader {
            Layout.fillWidth: true
            visible: root.editMode
            title: i18nc("@title:group where the account lives", "Account")
        }

        // Always available regardless of profileUsable, so an unregistered Global
        // identity can still be switched back to Local from here.
        FormCard.FormCard {
            Layout.fillWidth: true
            visible: root.editMode

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
                // Local is always a valid identity - it is just this device, there
                // is nothing to register. Only Global depends on a K-Server
                // connection that does not exist yet, so only it gets the caption.
                description: appSettings.globalAccountRegistered
                    ? i18nc("@info:whatsthis", "Synced through a K-Server.")
                    : i18nc("@info:status this identity has no K-Server account", "Not registered")
                checked: appSettings.globalAccount
                onToggled: if (globalRadio.checked) appSettings.globalAccount = true
            }
        }

        // About me.
        FormCard.FormHeader {
            Layout.fillWidth: true
            visible: root.profileUsable
            title: i18nc("@title:group free-form text about yourself", "About me")
        }

        FormCard.FormCard {
            Layout.fillWidth: true
            visible: root.profileUsable

            FormCard.FormTextAreaDelegate {
                id: aboutEdit
                visible: root.editMode
                label: i18nc("@label:textbox", "About me")
                placeholderText: i18nc("@info:placeholder", "Tell us about yourself...")
                text: appSettings.bio
                onEditingFinished: appSettings.bio = text
            }

            // Not a FormTextDelegate: the bio is Markdown, and that one elides to
            // a single line and renders as plain text. AbstractFormDelegate is
            // here for the padding, so the read view lines up with the edit view.
            FormCard.AbstractFormDelegate {
                visible: !root.editMode
                background: null
                focusPolicy: Qt.NoFocus

                contentItem: Kirigami.SelectableLabel {
                    text: appSettings.bio.length > 0
                        ? appSettings.bio
                        : i18nc("@info:placeholder", "Add information about yourself")
                    textFormat: Text.MarkdownText
                    wrapMode: Text.WordWrap
                    color: appSettings.bio.length > 0
                        ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                }
            }
        }

        // Media shelves. Nothing is behind any of them yet, which is what the
        // placeholder underneath says.
        QQC2.TabBar {
            id: mediaTabs
            Layout.fillWidth: true
            Layout.maximumWidth: root.kContentWidth
            Layout.alignment: Qt.AlignHCenter
            visible: root.profileUsable

            QQC2.TabButton { text: i18nc("@title:tab", "Music"); icon.name: "audio-x-generic" }
            QQC2.TabButton { text: i18nc("@title:tab", "Photos"); icon.name: "folder-pictures" }
            QQC2.TabButton { text: i18nc("@title:tab", "Videos"); icon.name: "folder-videos" }
            QQC2.TabButton { text: i18nc("@title:tab", "Albums"); icon.name: "view-list-details" }
            QQC2.TabButton { text: i18nc("@title:tab", "Clips"); icon.name: "video-x-generic" }
            QQC2.TabButton { text: i18nc("@title:tab", "Articles"); icon.name: "text-x-generic" }
        }

        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            Layout.maximumWidth: root.kContentWidth
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: Kirigami.Units.gridUnit * 8
            visible: root.profileUsable && !appSettings.globalAccount
            icon.name: "folder"
            text: i18nc("@info", "Nothing here yet - upload files from your device")
        }

        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            Layout.maximumWidth: root.kContentWidth
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: Kirigami.Units.gridUnit * 8
            visible: root.profileUsable && appSettings.globalAccount
            icon.name: "network-disconnect"
            text: i18nc("@info", "No internet connection")
        }

        FormCard.FormHeader {
            Layout.fillWidth: true
            visible: root.profileUsable
            title: i18nc("@title profile section", "Friends")
        }

        FormCard.FormCard {
            Layout.fillWidth: true
            visible: root.profileUsable

            FormCard.FormTextDelegate {
                text: i18nc("@info the friend list is empty", "No one yet")
            }
        }

        // Nothing real exists behind an unregistered Global identity yet (no
        // K-Server), so the profile body stays hidden instead of showing empty and
        // misleading shelves.
        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            Layout.maximumWidth: root.kContentWidth
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: Kirigami.Units.gridUnit * 12
            visible: !root.profileUsable
            icon.name: "im-user"
            text: i18nc("@info:status this identity has no K-Server account", "Not registered")
            explanation: i18nc("@info", "Register this identity on a K-Server to use the global profile")
            helpfulAction: Kirigami.Action {
                text: i18nc("@action:button", "Register")
                icon.name: "list-add-user"
                onTriggered: root.notImplemented(i18nc("@info", "Registration will appear once a K-Server is connected."))
            }
        }
    }
}
