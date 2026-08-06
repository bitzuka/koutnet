// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import koutnet.app

// The identity block every profile surface starts with. One component in two sizes
// rather than four near-copies: the two used to be written out separately and had
// drifted apart in every dimension they shared, which for an identity block is the
// whole point of having one. Type sizes are multiples of Kirigami.Theme.defaultFont
// rather than heading levels - level 1 is a section title, and the name here is the
// content rather than a label on it - which also keeps it following the desktop font
// size. The wide one used to be much larger: a nine-unit banner under a six-unit
// face under a doubled name filled a window with four facts and put the settings it
// sat on below the fold. Same shape now at about two thirds of that height.
Item {
    id: root

    property string displayName: ""
    property string handle: ""
    property bool online: false
    property double lastSeenSecs: 0
    // Only somebody else has reachability worth drawing; your own is that the
    // process is running.
    property bool showPresence: true
    property string statusEmoji: ""
    property string statusText: ""
    property string avatarSource: ""
    property string bannerSource: ""
    property string badgeSource: ""

    property bool compact: false
    // Set by whoever draws a rounded surface under this; the banner is full bleed.
    property real topCornerRadius: 0

    property bool editable: false
    // Separate from editable, because a status is changed ten times a day.
    property bool statusEditable: false

    signal bannerPickRequested()
    signal avatarPickRequested()
    signal statusPickRequested()

    readonly property real kAvatarSize: root.compact
        ? Kirigami.Units.gridUnit * 3.5
        : Kirigami.Units.gridUnit * 4.5
    readonly property real kBannerHeight: root.compact
        ? Kirigami.Units.gridUnit * 4
        : Kirigami.Units.gridUnit * 5.5
    readonly property real kAvatarOverhang: 0.45

    // The padding a form delegate puts round its text, so the display name and the
    // "About me" body under it share one left edge.
    readonly property real kContentPadding: Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing

    // Nothing here is in a layout - the avatar overlapping the banner is the whole
    // point, and an overlap cannot be expressed between layout children - so the
    // height is added up by hand or the enclosing ColumnLayout gives it none.
    implicitHeight: banner.height
        + root.kAvatarSize * (1 - root.kAvatarOverhang)
        + Kirigami.Units.smallSpacing
        + identity.implicitHeight
        + Kirigami.Units.smallSpacing

    // AnimatedImage so a GIF banner plays instead of freezing on its first frame.
    // It clears its own playing flag whenever it loads a source with no frames, and
    // the empty path at startup is one of those, so playback is re-armed on load.
    component LoopingImage: AnimatedImage {
        fillMode: Image.PreserveAspectCrop
        onStatusChanged: if (status === AnimatedImage.Ready) playing = true
    }

    // Inside a card the top two corners have to be rounded and the bottom two must
    // not, and a per-corner radius is what Rectangle at this Qt floor lacks.
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
        // The brand colour and not the theme's alternate background, which on most
        // schemes is a grey barely a shade off the page - there and invisible.
        color: Brand.accent

        // The gradient needs a plain Rectangle, which has one radius for all four
        // corners, so it is only laid on where the strip is square.
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

    // A sibling declared before the avatar rather than a child of it with a negative
    // z: Avatar has no border of its own, and going through its children would rely
    // on where it paints its own circle.
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

    // Under the avatar rather than beside it, which a 6-unit avatar and a doubled
    // name size do not both fit into on a narrow window.
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
                level: root.compact ? 3 : 2
                font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * (root.compact ? 1.3 : 1.5))
                // Bold on top of the heading level, because the handle underneath is
                // the same family and the weight is what separates them.
                font.bold: true
                text: root.displayName
                textFormat: Text.PlainText
                elide: Text.ElideRight
            }

            // Scaled off the name beside it rather than iconSizes.small, which next
            // to text this size read as a speck.
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
            font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * (root.compact ? 1.0 : 1.1))
            color: Kirigami.Theme.disabledTextColor
        }

        // On the card only; the wide surfaces give it a labelled block of its own.
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

        // A button rather than a label, so a status that can be changed does not look
        // like one that cannot, and an icon rather than a placeholder character,
        // because with nothing set there is no emoji to show.
        QQC2.ToolButton {
            Layout.topMargin: Kirigami.Units.smallSpacing
            visible: root.statusEditable
            display: root.statusEmoji.length > 0 ? QQC2.AbstractButton.TextOnly
                                                 : QQC2.AbstractButton.IconOnly
            icon.name: "face-smile"
            text: root.statusEmoji
            font.pointSize: Math.round(Kirigami.Theme.defaultFont.pointSize * 1.3)

            Accessible.name: i18nc("@action:button", "Set a status emoji")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            QQC2.ToolTip.text: Accessible.name

            onClicked: root.statusPickRequested()
        }

        // RelativeTime.now is read so this ages on its own while the surface is
        // open and nothing arrives.
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            visible: root.showPresence
            spacing: Kirigami.Units.smallSpacing

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                width: Math.round(Kirigami.Units.iconSizes.small * 0.6)
                height: width
                radius: width / 2
                color: root.online ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.disabledTextColor
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: RelativeTime.presenceLabel(root.online, root.lastSeenSecs, RelativeTime.now)
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.pointSize: Kirigami.Theme.defaultFont.pointSize
                color: root.online ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.disabledTextColor
            }
        }
    }
}
