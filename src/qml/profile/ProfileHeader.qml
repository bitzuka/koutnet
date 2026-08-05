// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import koutnet.app

// The identity block every profile surface in the application starts with:
// a banner, a large avatar hanging off its lower-left corner and ringed in the
// colour behind it, the display name, the handle under it, and the presence.
//
// One component in two sizes rather than four near-copies. compact is the popup
// card - the peer card and the account card - and everything else is the wide one
// that opens in the settings page and on a peer's profile. The two used to be
// written out separately and had drifted apart in every dimension they shared,
// which for an identity block is the whole point of having one.
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
    // Only somebody else has reachability worth drawing. Your own identity has
    // none: the process is running, which is not news.
    property bool showPresence: true
    // What the user says about themselves, which is not the same thing. The
    // compact card puts it under the handle; the wide one leaves it to a
    // labelled block of its own further down.
    property string statusEmoji: ""
    property string statusText: ""
    property string avatarSource: ""
    property string bannerSource: ""
    property string badgeSource: ""

    // Popup size: a shallower banner, a smaller face, and the name at card size
    // rather than at page size.
    property bool compact: false
    // Set by whoever draws a rounded surface under this, because the banner is
    // full bleed and would otherwise square off the top of it.
    property real topCornerRadius: 0

    // Own-profile surfaces get the two picker buttons; a peer's has nothing here
    // that writes.
    property bool editable: false
    // Whether the status slot can be clicked to change it. Separate from
    // editable because a status is a thing people change ten times a day.
    property bool statusEditable: false

    signal bannerPickRequested()
    signal avatarPickRequested()
    signal statusPickRequested()

    readonly property real kAvatarSize: root.compact
        ? Kirigami.Units.gridUnit * 3.5
        : Kirigami.Units.gridUnit * 6
    readonly property real kBannerHeight: root.compact
        ? Kirigami.Units.gridUnit * 4
        : Kirigami.Units.gridUnit * 9
    // How much of the avatar sits over the banner rather than below it.
    readonly property real kAvatarOverhang: 0.45

    // The same horizontal padding a form delegate puts round its text, so the
    // display name and the "About me" body under it share one left edge. How wide
    // the column is is the caller's business, not this one's.
    readonly property real kContentPadding: Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing

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

    // ShadowedRectangle rather than a plain one: inside a card the top two
    // corners have to be rounded and the bottom two must not, and a per-corner
    // radius is the one thing it has that Rectangle at this Qt floor does not.
    Kirigami.ShadowedRectangle {
        id: banner
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.kBannerHeight
        corners.topLeftRadius: root.topCornerRadius
        corners.topRightRadius: root.topCornerRadius
        corners.bottomLeftRadius: 0
        corners.bottomRightRadius: 0
        // With no picture the strip is the brand colour rather than the theme's
        // alternate background, which on most schemes is a grey barely a shade
        // off the page behind it - the banner was there and invisible.
        color: Brand.accent

        // The gradient needs a plain Rectangle, and that one has a single radius
        // for all four corners, so it is only laid on where the strip is square.
        // In a card the flat brand colour and the rounded corners win.
        Rectangle {
            anchors.fill: parent
            visible: root.topCornerRadius <= 0
            gradient: Gradient {
                GradientStop { position: 0.0; color: Brand.accent }
                GradientStop {
                    position: 1.0
                    color: Kirigami.ColorUtils.linearInterpolation(Brand.accent, Kirigami.Theme.backgroundColor, 0.45)
                }
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

    // The ring behind the avatar, which is what lifts it off the banner. A
    // sibling declared before the avatar rather than a child of it with a
    // negative z: Avatar has no border of its own, and going through its children
    // would be relying on where it paints its own circle. Sized from the avatar
    // rather than the other way round.
    Rectangle {
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
        anchors.leftMargin: root.kContentPadding
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
        anchors.leftMargin: root.kContentPadding
        anchors.rightMargin: root.kContentPadding
        anchors.topMargin: Kirigami.Units.smallSpacing
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Heading {
                Layout.fillWidth: true
                level: root.compact ? 3 : 1
                font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * (root.compact ? 1.3 : 2))
                // Bold on top of the heading level, because the handle right
                // underneath is the same family and the weight is what separates
                // them at a glance.
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
                Layout.preferredWidth: root.compact
                    ? Kirigami.Units.iconSizes.smallMedium
                    : Kirigami.Units.iconSizes.medium
                Layout.preferredHeight: Layout.preferredWidth
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: root.handle.length > 0
            text: i18nc("@info a handle, %1 is the user name", "@%1", root.handle)
            textFormat: Text.PlainText
            elide: Text.ElideRight
            font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * (root.compact ? 1.0 : 1.25))
            color: Kirigami.Theme.disabledTextColor
        }

        // The custom status, on the card only. The wide surfaces give it a
        // labelled block of its own, which is where a line of free text belongs
        // once there is room for one.
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            visible: root.compact && (root.statusEmoji.length > 0 || root.statusText.length > 0)
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                visible: root.statusEmoji.length > 0
                text: root.statusEmoji
                textFormat: Text.PlainText
                font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * 1.3)
            }

            QQC2.Label {
                Layout.fillWidth: true
                visible: root.statusText.length > 0
                text: root.statusText
                textFormat: Text.PlainText
                elide: Text.ElideRight
            }
        }

        // The status picker, wherever the status belongs to this end. A button
        // rather than a label, so one that can be changed does not look like one
        // that cannot. It falls back to an icon rather than to a placeholder
        // character: with nothing set there is no emoji to show, and a face is
        // what says what the slot is for.
        QQC2.ToolButton {
            Layout.topMargin: Kirigami.Units.smallSpacing
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

        // Presence: a dot in the colour, and text big enough to read from where
        // somebody actually sits. RelativeTime.now is read so this ages on its
        // own while the surface is open and nothing arrives.
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            visible: root.showPresence
            spacing: Kirigami.Units.smallSpacing

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                width: root.compact
                    ? Math.round(Kirigami.Units.iconSizes.small * 0.6)
                    : Kirigami.Units.iconSizes.small
                height: width
                radius: width / 2
                color: root.online ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.disabledTextColor
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: RelativeTime.presenceLabel(root.online, root.lastSeenSecs, RelativeTime.now)
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.pointSize: root.compact
                    ? Kirigami.Theme.defaultFont.pointSize
                    : Math.round(Kirigami.Theme.defaultFont.pointSize * 1.15)
                color: root.online ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.disabledTextColor
            }
        }
    }
}
