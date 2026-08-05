// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import org.kde.kirigamiaddons.formcard as FormCard
import koutnet.app

// Own-profile page. The banner, the big round avatar and the name row are kept -
// that shape is the point of the screen - but everything under them that used to
// be an editable field floating on a Rectangle is now a FormCard section that
// only appears while editing.
//
// Local and Global differ only in the body. Local is this device's identity and
// always usable; Global needs a K-Server that does not exist yet, so an
// unregistered Global identity gets a registration prompt instead of a body.
// Avatar, banner, name and about are device-local either way.
//
// The circular avatar used to be an Image behind a MultiEffect with a Rectangle
// as its mask texture, because clip on a rounded Rectangle only clips to the
// bounding box. Components.Avatar does that properly and also draws the initials
// fallback, so all of it is gone.
Kirigami.ScrollablePage {
    id: root

    // The window owns the one message strip, this page only asks for it.
    signal notImplemented(string text)

    property bool editMode: false
    readonly property bool profileUsable: !appSettings.globalAccount || appSettings.globalAccountRegistered
    readonly property string shownName: appSettings.displayName.length > 0
        ? appSettings.displayName : appSettings.username

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

    // The backdrop the "Change background" picker sets. Dimmed, because the
    // picture is chosen by the user and the text on top of it still has to be
    // readable.
    background: Item {
        LoopingImage {
            anchors.fill: parent
            source: appSettings.profileBackgroundPath
            visible: appSettings.profileBackgroundPath.length > 0
        }
        Rectangle {
            anchors.fill: parent
            color: Kirigami.Theme.backgroundColor
            opacity: appSettings.profileBackgroundPath.length > 0 ? 0.55 : 1
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
    FileDialog {
        id: badgeDialog
        title: i18nc("@title:window", "Choose name badge")
        nameFilters: [i18nc("@item:inlistbox file dialog filter, keep the glob patterns",
                            "Images (*.png *.jpg *.jpeg *.webp *.gif)")]
        onAccepted: appSettings.nameBadgePath = selectedFile
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        // Banner, with the avatar hanging off its bottom edge.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 9 + avatarFrame.height * 0.4

            Rectangle {
                id: banner
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: Kirigami.Units.gridUnit * 9
                color: Kirigami.Theme.alternateBackgroundColor
                clip: true

                LoopingImage {
                    anchors.fill: parent
                    source: appSettings.bannerPath
                    visible: appSettings.bannerPath.length > 0
                }
            }

            QQC2.ToolButton {
                anchors.right: banner.right
                anchors.bottom: banner.bottom
                anchors.margins: Kirigami.Units.smallSpacing
                display: QQC2.AbstractButton.IconOnly
                icon.name: "document-edit"
                text: i18nc("@info:tooltip", "Change banner")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: text
                onClicked: bannerDialog.open()
            }

            Item {
                id: avatarFrame
                width: Kirigami.Units.gridUnit * 6
                height: width
                anchors.left: parent.left
                anchors.leftMargin: Kirigami.Units.largeSpacing
                anchors.top: banner.bottom
                anchors.topMargin: -height * 0.4

                Components.Avatar {
                    anchors.fill: parent
                    name: root.shownName
                    source: appSettings.avatarPath
                }

                QQC2.ToolButton {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    display: QQC2.AbstractButton.IconOnly
                    icon.name: "document-edit"
                    text: i18nc("@info:tooltip", "Change avatar")
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: text
                    onClicked: avatarDialog.open()
                }
            }
        }

        // Name row.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing
            visible: root.profileUsable

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        level: 1
                        text: root.shownName
                        elide: Text.ElideRight
                    }

                    Image {
                        source: appSettings.nameBadgePath
                        visible: appSettings.nameBadgePath.length > 0
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        fillMode: Image.PreserveAspectFit
                    }
                }

                QQC2.Label {
                    text: i18nc("@info the peer's handle, %1 is the username", "@%1", appSettings.username)
                    color: Kirigami.Theme.disabledTextColor
                }

                QQC2.Label {
                    text: i18nc("@info:status your own presence", "online")
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.positiveTextColor
                }
            }
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

            FormCard.FormDelegateSeparator { above: usernameField; below: badgeButton }

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
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
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
            Layout.preferredHeight: Kirigami.Units.gridUnit * 8
            visible: root.profileUsable && !appSettings.globalAccount
            icon.name: "folder"
            text: i18nc("@info", "Nothing here yet - upload files from your device")
        }

        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
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
