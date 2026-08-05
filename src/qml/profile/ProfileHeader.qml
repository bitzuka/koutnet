// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import koutnet.app

// The identity block at the top of a profile: banner, a large avatar hanging off
// its lower edge, the name, the handle, the presence and the custom status.
//
// One component for both profile pages rather than the same forty lines twice.
// The two used to drift - the peer's page had no badge slot and the own page had
// no presence colour - and a header is exactly the part of a screen where the two
// sides have to look like one design in two states.
//
// The type sizes are multiples of Kirigami.Theme.defaultFont rather than heading
// levels. A profile header is the one place in this application where the name is
// the content rather than a label on it, and level 1 - which is what this used to
// be - is a section title, not that. Multiples keep it following the desktop's
// font size, which a hardcoded point size would not.
Item {
    id: root

    property string displayName: ""
    property string handle: ""
    // Reachability, which is a fact about the network.
    property bool online: false
    property double lastSeenSecs: 0
    // What the user says about themselves, which is not the same thing. Empty
    // renders the slot as a placeholder rather than collapsing it, so the header
    // does not change height when one is set.
    property string statusEmoji: ""
    property string avatarSource: ""
    property string bannerSource: ""
    property string badgeSource: ""

    // Own-profile pages get the two picker buttons; a peer's page has nothing
    // here that writes.
    property bool editable: false
    // Whether the status slot can be clicked to change it. Separate from
    // editable because it is offered outside edit mode: a status is a thing
    // people change ten times a day.
    property bool statusEditable: false

    signal bannerPickRequested()
    signal avatarPickRequested()
    signal statusPickRequested()

    readonly property real kAvatarSize: Kirigami.Units.gridUnit * 6
    // How much of the avatar sits over the banner rather than below it. Matches
    // PeerCard, so the card and this read as the same design.
    readonly property real kAvatarOverhang: 0.45

    // Nothing here is in a layout - the avatar overlapping the banner is the
    // whole point, and an overlap cannot be expressed between layout children -
    // so the height has to be added up by hand or the ColumnLayout this sits in
    // gives it none. Banner, the part of the avatar below it, the gap, the text,
    // and a margin at the bottom.
    implicitHeight: banner.height
        + root.kAvatarSize * (1 - root.kAvatarOverhang)
        + Kirigami.Units.smallSpacing
        + identity.implicitHeight
        + Kirigami.Units.largeSpacing

    // AnimatedImage rather than Image so an animated GIF banner plays instead of
    // freezing on its first frame; it renders png/jpg/webp identically. It also
    // clears its own playing flag whenever it loads a source with no frames, and
    // the empty path at startup is one of those, so playback is re-armed on load.
    component LoopingImage: AnimatedImage {
        fillMode: Image.PreserveAspectCrop
        onStatusChanged: if (status === AnimatedImage.Ready) playing = true
    }

    Rectangle {
        id: banner
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Kirigami.Units.gridUnit * 9
        clip: true

        // With no picture the strip is the brand colour rather than the theme's
        // alternate background, which on most schemes is a grey barely a shade
        // off the page behind it - the banner was there and invisible.
        gradient: Gradient {
            GradientStop { position: 0.0; color: Brand.accent }
            GradientStop {
                position: 1.0
                color: Kirigami.ColorUtils.linearInterpolation(Brand.accent, Kirigami.Theme.backgroundColor, 0.45)
            }
        }

        LoopingImage {
            anchors.fill: parent
            source: root.bannerSource
            visible: root.bannerSource.length > 0
        }

        QQC2.ToolButton {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Kirigami.Units.smallSpacing
            visible: root.editable
            display: QQC2.AbstractButton.IconOnly
            icon.name: "insert-image"
            text: i18nc("@info:tooltip", "Change banner")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            QQC2.ToolTip.text: text
            onClicked: root.bannerPickRequested()
        }
    }

    // The ring behind the avatar, which is what lifts it off the banner. Sized
    // from the avatar rather than the other way round.
    Rectangle {
        id: avatarRing
        width: root.kAvatarSize + Kirigami.Units.smallSpacing * 3
        height: width
        radius: width / 2
        color: Kirigami.Theme.backgroundColor
        anchors.horizontalCenter: avatarFrame.horizontalCenter
        anchors.verticalCenter: avatarFrame.verticalCenter
    }

    Item {
        id: avatarFrame
        width: root.kAvatarSize
        height: width
        anchors.left: parent.left
        anchors.leftMargin: Kirigami.Units.largeSpacing * 2
        anchors.top: banner.bottom
        anchors.topMargin: -height * root.kAvatarOverhang

        Components.Avatar {
            anchors.fill: parent
            name: root.displayName
            source: root.avatarSource
            asynchronous: true
        }

        QQC2.ToolButton {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            visible: root.editable
            display: QQC2.AbstractButton.IconOnly
            icon.name: "insert-image"
            text: i18nc("@info:tooltip", "Change avatar")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            QQC2.ToolTip.text: text
            onClicked: root.avatarPickRequested()
        }
    }

    // Name, handle and presence, starting under the avatar rather than beside it.
    // Beside it is what a 6-grid-unit avatar and a doubled name size do not both
    // fit into on a narrow window.
    ColumnLayout {
        id: identity

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: avatarFrame.bottom
        anchors.leftMargin: Kirigami.Units.largeSpacing * 2
        anchors.rightMargin: Kirigami.Units.largeSpacing * 2
        anchors.topMargin: Kirigami.Units.smallSpacing
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 1
                font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * 2)
                font.bold: true
                text: root.displayName
                textFormat: Text.PlainText
                elide: Text.ElideRight
            }

            // The name badge, which is a small picture the user picks. Scaled off
            // the name beside it rather than iconSizes.small, which next to text
            // this size read as a speck.
            Image {
                source: root.badgeSource
                visible: root.badgeSource.length > 0
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
        }

        // Handle, and the custom status beside it.
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                visible: root.handle.length > 0
                text: i18nc("@info a handle, %1 is the user name", "@%1", root.handle)
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * 1.25)
                color: Kirigami.Theme.disabledTextColor
            }

            // A button when it can be changed and a plain label when it cannot,
            // so a peer's status is not something that looks clickable.
            // Falls back to an icon rather than to a placeholder character: with
            // nothing set there is no emoji to show, and a face is what says
            // what the slot is for.
            QQC2.ToolButton {
                Layout.alignment: Qt.AlignVCenter
                visible: root.statusEditable
                display: root.statusEmoji.length > 0 ? QQC2.AbstractButton.TextOnly
                                                     : QQC2.AbstractButton.IconOnly
                icon.name: "face-smile"
                text: root.statusEmoji
                font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * 1.8)

                Accessible.name: i18nc("@action:button", "Set a status emoji")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                QQC2.ToolTip.text: Accessible.name

                onClicked: root.statusPickRequested()
            }

            QQC2.Label {
                Layout.alignment: Qt.AlignVCenter
                visible: !root.statusEditable && root.statusEmoji.length > 0
                text: root.statusEmoji
                textFormat: Text.PlainText
                font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * 1.8)
            }

            Item { Layout.fillWidth: true }
        }

        // Presence: a dot in the colour, and text big enough to read from where
        // somebody actually sits. RelativeTime.now is read so this ages on its
        // own while the page is open and nothing arrives.
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                width: Kirigami.Units.iconSizes.small
                height: width
                radius: width / 2
                color: root.online ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.disabledTextColor
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: RelativeTime.presenceLabel(root.online, root.lastSeenSecs, RelativeTime.now)
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * 1.15)
                color: root.online ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.disabledTextColor
            }
        }
    }
}
