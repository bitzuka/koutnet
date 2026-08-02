import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects
import org.kde.kirigami as Kirigami
import koutnet.app

// Own-profile editor, VK-styled layout.
//
// Local and Global differ only in the body. Local is this device's identity
// and always usable; Global needs a K-Server that does not exist yet, so an
// unregistered Global identity gets a registration prompt instead of a body.
// Avatar, banner, name and about are device-local either way.
//
// The gated sections sit in two ColumnLayouts with one visible binding each.
// Fewer independent toggles under a single parent Layout means less chance
// of stale geometry when flipping between modes.
Item {
    id: root
    readonly property var theme: ThemeManager.colors
    property bool editMode: false
    readonly property bool profileUsable: !appSettings.globalAccount || appSettings.globalAccountRegistered
    implicitWidth: Kirigami.Units.gridUnit * 46
    implicitHeight: Kirigami.Units.gridUnit * 34

    // Main owns the stub sheet, this page only asks for it.
    signal stubRequested(string title, string body)

    function tr(key) {
        return (Translations.current, Translations.t(key))
    }

    function commitBio() {
        if (aboutEdit.visible) {
            appSettings.bio = aboutEdit.text
            aboutEdit.visible = false
        }
    }

    function toggleEditMode() {
        if (root.editMode) {
            commitBio()
            root.editMode = false
        } else {
            root.editMode = true
            aboutEdit.text = appSettings.bio
            aboutEdit.visible = true
        }
    }

    // AnimatedImage plays animated GIFs where plain Image freezes on the
    // first frame, and renders png/jpg/webp identically, so one type covers
    // both. It also clears its own playing flag whenever it loads a source
    // with no frames, and the empty path at startup is one of those, so
    // playback gets re-armed on every load.
    component LoopingImage: AnimatedImage {
        fillMode: Image.PreserveAspectCrop
        onStatusChanged: if (status === AnimatedImage.Ready) playing = true
    }

    FileDialog {
        id: avatarDialog
        title: root.tr("profile.change_avatar")
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.gif)"]
        onAccepted: appSettings.avatarPath = selectedFile
    }
    FileDialog {
        id: bannerDialog
        title: root.tr("profile.change_banner")
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.gif)"]
        onAccepted: appSettings.bannerPath = selectedFile
    }
    FileDialog {
        id: backgroundDialog
        title: root.tr("profile.change_background")
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.gif)"]
        onAccepted: appSettings.profileBackgroundPath = selectedFile
    }
    FileDialog {
        id: badgeDialog
        title: root.tr("profile.change_badge")
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.gif)"]
        onAccepted: appSettings.nameBadgePath = selectedFile
    }

    // Full-page backdrop, separate from the banner strip. Declared first so
    // it paints at the bottom of the stack. The dimming Rectangle over it
    // keeps text legible whatever image gets picked.
    Item {
        id: backgroundLayer
        anchors.fill: parent

        LoopingImage {
            anchors.fill: parent
            source: appSettings.profileBackgroundPath
            visible: appSettings.profileBackgroundPath.length > 0
        }
        Rectangle {
            anchors.fill: parent
            color: root.theme.bg
            opacity: appSettings.profileBackgroundPath.length > 0 ? 0.55 : 1
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header group: banner + avatar + name row
        ColumnLayout {
            id: headerGroup
            Layout.fillWidth: true
            spacing: 0
            visible: root.profileUsable

            // Banner + avatar
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Kirigami.Units.gridUnit * 9

                Rectangle {
                    id: banner
                    anchors.fill: parent
                    color: root.theme.bg3
                    clip: true

                    LoopingImage {
                        anchors.fill: parent
                        source: appSettings.bannerPath
                        visible: appSettings.bannerPath.length > 0
                    }
                }

                Rectangle {
                    width: Kirigami.Units.gridUnit * 1.8
                    height: width
                    radius: width / 2
                    color: Qt.rgba(0, 0, 0, 0.55)
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        anchors.centerIn: parent
                        width: parent.width * 0.55
                        height: width
                        source: "document-edit-symbolic"
                        color: "white"
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: bannerDialog.open()
                        ToolTip.visible: containsMouse
                        ToolTip.text: root.tr("profile.change_banner")
                        hoverEnabled: true
                    }
                }

                Rectangle {
                    id: avatarFrame
                    width: Kirigami.Units.gridUnit * 6.5
                    height: width
                    radius: width / 2
                    color: root.theme.bg
                    anchors.left: parent.left
                    anchors.leftMargin: Kirigami.Units.largeSpacing
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: -height * 0.35

                    Item {
                        id: avatarClipArea
                        anchors.fill: parent
                        anchors.margins: 3

                        Rectangle {
                            id: avatarMaskShape
                            anchors.fill: parent
                            radius: width / 2
                            color: root.theme.item_sel
                            // MultiEffect needs a texture provider for its
                            // maskSource. A plain Rectangle only becomes one
                            // once layer.enabled is set.
                            layer.enabled: true
                            opacity: 0
                        }
                        LoopingImage {
                            id: avatarImg
                            anchors.fill: parent
                            source: appSettings.avatarPath
                            opacity: 0
                        }
                        // clip: true on a rounded Rectangle only clips to the
                        // bounding box, not the rounded shape (a Qt Quick
                        // limitation) - MultiEffect with maskSource gives an
                        // actual per-pixel circular mask instead.
                        MultiEffect {
                            anchors.fill: parent
                            source: avatarImg
                            maskEnabled: true
                            maskSource: avatarMaskShape
                            visible: appSettings.avatarPath.length > 0
                        }
                        Rectangle {
                            anchors.fill: parent
                            radius: width / 2
                            color: root.theme.item_sel
                            visible: appSettings.avatarPath.length === 0
                            Label {
                                anchors.centerIn: parent
                                text: (appSettings.displayName.length > 0 ? appSettings.displayName : appSettings.username).charAt(0).toUpperCase()
                                font.pixelSize: Kirigami.Units.gridUnit * 2.2
                                font.bold: true
                                color: "white"
                            }
                        }

                        // Hover overlay, centred on the avatar. A corner
                        // badge here reads as a presence dot instead.
                        Rectangle {
                            anchors.fill: parent
                            radius: width / 2
                            color: "black"
                            opacity: avatarHover.containsMouse ? 0.45 : 0
                            Behavior on opacity { NumberAnimation { duration: 120 } }
                        }
                        Kirigami.Icon {
                            anchors.centerIn: parent
                            width: parent.width * 0.32
                            height: width
                            source: "list-add"
                            color: "white"
                            opacity: avatarHover.containsMouse ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 120 } }
                        }
                        MouseArea {
                            id: avatarHover
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: avatarDialog.open()
                        }
                    }

                    // Presence dot, same online/offline colours the contact
                    // list uses so it reads the same way everywhere.
                    Rectangle {
                        width: Kirigami.Units.gridUnit * 1.1
                        height: width
                        radius: width / 2
                        color: root.theme.online
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        border.color: root.theme.bg
                        border.width: 2
                    }
                }
            }

            // Name row
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.largeSpacing
                Layout.topMargin: Kirigami.Units.gridUnit * 2.4
                spacing: Kirigami.Units.smallSpacing

                ColumnLayout {
                    spacing: 2

                    RowLayout {
                        spacing: 6
                        visible: !root.editMode
                        Kirigami.Heading {
                            level: 2
                            text: appSettings.displayName.length > 0 ? appSettings.displayName : appSettings.username
                            color: root.theme.text
                        }
                        Image {
                            source: appSettings.nameBadgePath ? appSettings.nameBadgePath : ""
                            visible: appSettings.nameBadgePath.length > 0
                            Layout.preferredWidth: Kirigami.Units.gridUnit
                            Layout.preferredHeight: Kirigami.Units.gridUnit
                            fillMode: Image.PreserveAspectFit
                        }
                    }
                    Label {
                        visible: !root.editMode
                        text: "@" + appSettings.username
                        color: root.theme.text_dim
                    }

                    ColumnLayout {
                        visible: root.editMode
                        spacing: 4
                        TextField {
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                            placeholderText: root.tr("profile.username_placeholder")
                            text: appSettings.displayName
                            onEditingFinished: appSettings.displayName = text
                        }
                        RowLayout {
                            spacing: 6
                            Label { text: "@"; color: root.theme.text_dim }
                            TextField {
                                Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                                text: appSettings.username
                                onEditingFinished: appSettings.username = text
                            }
                            ToolButton {
                                text: root.tr("profile.change_badge")
                                onClicked: badgeDialog.open()
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: root.editMode ? root.tr("profile.edit_done") : root.tr("profile.edit")
                    highlighted: root.editMode
                    onClicked: root.toggleEditMode()
                }
                ToolButton {
                    icon.name: "insert-image-symbolic"
                    onClicked: backgroundDialog.open()
                    ToolTip.visible: hovered
                    ToolTip.text: root.tr("profile.change_background")
                }
                ToolButton {
                    visible: appSettings.profileBackgroundPath.length > 0
                    icon.name: "edit-clear-symbolic"
                    onClicked: appSettings.profileBackgroundPath = ""
                    ToolTip.visible: hovered
                    ToolTip.text: root.tr("profile.clear_background")
                }
                ToolButton {
                    icon.name: "document-share"
                    onClicked: root.stubRequested(root.tr("profile.edit"), root.tr("profile.tab_content_placeholder"))
                }
                ToolButton {
                    icon.name: "overflow-menu"
                    onClicked: root.stubRequested(root.tr("profile.edit"), root.tr("profile.tab_content_placeholder"))
                }
            }
        }

        // Account toggle
        // Always visible regardless of profileUsable, so an unregistered
        // Global identity can still be switched back to Local from here.
        ColumnLayout {
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            spacing: 2

            Row {
                spacing: 0
                Rectangle {
                    width: localLabel.implicitWidth + 20
                    height: 32
                    radius: 6
                    color: "transparent"
                    border.width: !appSettings.globalAccount ? 2 : 1
                    border.color: !appSettings.globalAccount ? root.theme.accent : root.theme.border
                    Label {
                        id: localLabel
                        anchors.centerIn: parent
                        text: root.tr("profile.local_account")
                        color: !appSettings.globalAccount ? root.theme.accent : root.theme.text_dim
                    }
                    MouseArea { anchors.fill: parent; onClicked: appSettings.globalAccount = false }
                }
                Rectangle {
                    width: globalLabel.implicitWidth + 20
                    height: 32
                    radius: 6
                    color: "transparent"
                    border.width: appSettings.globalAccount ? 2 : 1
                    border.color: appSettings.globalAccount ? root.theme.accent : root.theme.border
                    Label {
                        id: globalLabel
                        anchors.centerIn: parent
                        text: root.tr("profile.global_account")
                        color: appSettings.globalAccount ? root.theme.accent : root.theme.text_dim
                    }
                    MouseArea { anchors.fill: parent; onClicked: appSettings.globalAccount = true }
                }
            }
            // Local is always a valid identity - it's just this device,
            // nothing to "register." Only Global depends on a K-Server
            // connection that doesn't exist yet, so only it gets the
            // caption.
            Label {
                visible: appSettings.globalAccount && !appSettings.globalAccountRegistered
                text: root.tr("profile.not_registered")
                color: root.theme.text_dim
                font.pixelSize: 13
            }
        }

        // Body group: presence + about/tabs/friends
        ColumnLayout {
            id: bodyGroup
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            visible: root.profileUsable

            Label {
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.bottomMargin: Kirigami.Units.smallSpacing
                text: root.tr("profile.status_online")
                color: root.theme.text_dim
                font.pixelSize: 13
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.largeSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: root.width * 0.65
                    Layout.alignment: Qt.AlignTop
                    spacing: Kirigami.Units.smallSpacing

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(60, aboutText.implicitHeight + 24)
                        radius: 8
                        color: root.theme.bg3
                        border.color: root.theme.border

                        Text {
                            id: aboutText
                            anchors.fill: parent
                            anchors.margins: 12
                            visible: !aboutEdit.visible
                            wrapMode: Text.Wrap
                            textFormat: Text.MarkdownText
                            color: root.theme.text
                            text: appSettings.bio.length > 0 ? appSettings.bio : root.tr("profile.username_placeholder")
                        }
                        MouseArea {
                            anchors.fill: parent
                            visible: !aboutEdit.visible
                            onClicked: { aboutEdit.text = appSettings.bio; aboutEdit.visible = true; aboutEdit.forceActiveFocus() }
                        }
                        TextArea {
                            id: aboutEdit
                            anchors.fill: parent
                            anchors.margins: 12
                            visible: false
                            wrapMode: TextArea.Wrap
                            placeholderText: root.tr("profile.about_placeholder")
                            color: root.theme.text
                            background: null
                            onActiveFocusChanged: {
                                if (!activeFocus && visible && !root.editMode)
                                    root.commitBio()
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Rectangle {
                            id: mediaTabsRoot
                            Layout.fillWidth: true
                            implicitHeight: 36
                            color: "transparent"

                            property var tabKeys: ["profile.tab_music", "profile.tab_photos", "profile.tab_videos", "profile.tab_albums", "profile.tab_clips", "profile.tab_articles"]
                            property var tabIcons: ["audio-x-generic-symbolic", "folder-pictures-symbolic", "folder-videos-symbolic", "view-list-details-symbolic", "video-symbolic", "text-x-generic-symbolic"]
                            property int currentIndex: 0

                            RowLayout {
                                anchors.fill: parent
                                spacing: 0
                                Repeater {
                                    model: mediaTabsRoot.tabKeys
                                    delegate: Rectangle {
                                        Layout.preferredWidth: mediaTabRow.implicitWidth + 24
                                        Layout.fillHeight: true
                                        color: "transparent"
                                        RowLayout {
                                            id: mediaTabRow
                                            anchors.centerIn: parent
                                            spacing: 6
                                            Kirigami.Icon {
                                                source: mediaTabsRoot.tabIcons[index]
                                                Layout.preferredWidth: 16
                                                Layout.preferredHeight: 16
                                                color: mediaTabsRoot.currentIndex === index ? root.theme.accent : root.theme.text_dim
                                            }
                                            Label {
                                                text: root.tr(modelData)
                                                font.bold: mediaTabsRoot.currentIndex === index
                                                color: mediaTabsRoot.currentIndex === index ? root.theme.accent : root.theme.text_dim
                                            }
                                        }
                                        Rectangle {
                                            visible: mediaTabsRoot.currentIndex === index
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            height: 2
                                            color: root.theme.accent
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: mediaTabsRoot.currentIndex = index
                                        }
                                    }
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                    }

                    // Local vs Global placeholder - Local never depended on
                    // a server so it never shows a connectivity message;
                    // Global genuinely has nothing behind it yet.
                    Kirigami.PlaceholderMessage {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !appSettings.globalAccount
                        text: root.tr("profile.local_tab_empty")
                        icon.name: "folder-symbolic"
                    }
                    Kirigami.PlaceholderMessage {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: appSettings.globalAccount
                        text: root.tr("profile.no_internet")
                        icon.name: "network-disconnect-symbolic"
                    }
                }

                ColumnLayout {
                    Layout.preferredWidth: root.width * 0.32
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignTop
                    spacing: Kirigami.Units.largeSpacing

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: friendsCol.implicitHeight + 24
                        radius: 8
                        color: root.theme.bg3
                        border.color: root.theme.border

                        ColumnLayout {
                            id: friendsCol
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8
                            Kirigami.Heading {
                                level: 5
                                text: root.tr("profile.friends")
                                color: root.theme.text
                            }
                            Label {
                                text: root.tr("profile.no_friends_yet")
                                color: root.theme.text_dim
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }

        // Registration prompt (Global, not yet registered)
        // Nothing real exists behind an unregistered Global identity yet
        // (no K-Server), so the whole profile body stays hidden instead
        // of showing empty/misleading tabs and an empty avatar.
        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Kirigami.Units.largeSpacing
            visible: !root.profileUsable
            icon.name: "im-user-symbolic"
            text: root.tr("profile.not_registered")
            explanation: root.tr("profile.register_explanation")
            helpfulAction: Kirigami.Action {
                text: root.tr("profile.register_now")
                icon.name: "list-add-user"
                onTriggered: root.stubRequested(root.tr("profile.register_now"), root.tr("profile.tab_content_placeholder"))
            }
        }
    }
}
