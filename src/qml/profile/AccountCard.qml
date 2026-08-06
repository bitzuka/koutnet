// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import koutnet.app

// The same card as a peer's - see qml/peer/PeerCard.qml - because clicking your own
// face asks the same question as clicking somebody else's and deserves the same
// answer in the same shape. What differs is that there is no reachability to report
// about yourself and the two actions write. The name and handle are read straight off
// AppSettings rather than passed in: threading two strings through a popup only to
// have them go stale is worse than the coupling.
QQC2.Popup {
    id: root

    signal editProfileRequested()
    // The window owns the one message strip, this card only asks for it.
    signal notifyRequested(string text)

    readonly property string handle: appSettings.username || ""
    readonly property string shownName: appSettings.displayName && appSettings.displayName.length > 0
        ? appSettings.displayName
        : root.handle

    implicitWidth: Kirigami.Units.gridUnit * 18

    // See Main.qml: a popup is reparented into the overlay, which is its own chain.
    Kirigami.Theme.inherit: false
    Kirigami.Theme.highlightColor: Brand.accent

    modal: false
    dim: false
    focus: true
    closePolicy: QQC2.Popup.CloseOnEscape | QQC2.Popup.CloseOnPressOutside
    padding: 0

    // Above the row, because the account row is the last thing in the column.
    function openAt(item) {
        if (!item)
            return
        root.parent = item
        root.x = 0
        root.y = -root.height - Kirigami.Units.smallSpacing
        root.open()
    }

    background: Kirigami.ShadowedRectangle {
        radius: Kirigami.Units.cornerRadius
        color: Kirigami.Theme.backgroundColor
        border.width: 1
        border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.2)
        shadow.size: Kirigami.Units.gridUnit
        shadow.color: Qt.rgba(0, 0, 0, 0.3)
        shadow.yOffset: 2
    }

    // Copying goes through an off-screen editor because QML has no clipboard object
    // without a C++ helper - the same trick the conversation uses.
    TextEdit {
        id: clipboardHelper
        visible: false
        function copyText(str) {
            text = str
            selectAll()
            copy()
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        ProfileHeader {
            Layout.fillWidth: true

            compact: true
            topCornerRadius: Kirigami.Units.cornerRadius
            // Your own reachability is that the process is running.
            showPresence: false

            displayName: root.shownName
            handle: root.handle
            statusEmoji: appSettings.statusEmoji
            statusText: appSettings.statusText
            avatarSource: appSettings.avatarPath
            bannerSource: appSettings.bannerPath
            badgeSource: appSettings.nameBadgePath
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            QQC2.Button {
                Layout.fillWidth: true
                icon.name: "document-edit"
                text: i18nc("@action:button open the profile section of the settings", "Edit profile")
                onClicked: {
                    root.editProfileRequested()
                    root.close()
                }
            }

            // The handle is how somebody else starts a conversation with you.
            QQC2.Button {
                Layout.fillWidth: true
                icon.name: "edit-copy"
                enabled: root.handle.length > 0
                text: i18nc("@action:button put your own handle on the clipboard", "Copy handle")
                onClicked: {
                    clipboardHelper.copyText(i18nc("@info a handle, %1 is the user name", "@%1", root.handle))
                    root.notifyRequested(i18nc("@info:status", "Handle copied to the clipboard"))
                    root.close()
                }
            }
        }
    }
}
