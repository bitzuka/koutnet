// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Effects
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components

// like Components.Avatar but plays an animated GIF instead of freezing on frame one.
// keeps the same shape and initials fallback; still images go through the normal Avatar.
Item {
    id: root

    // same properties as Components.Avatar, so it is a drop-in
    property string name: ""
    property string source: ""
    property string iconSource: ""
    // kept for compatibility; the AnimatedImage below is always async anyway
    property bool asynchronous: true

    // AnimatedImage knows it is animated once frameCount > 1; until then the plain Avatar draws
    readonly property bool animated: probe.status === AnimatedImage.Ready && probe.frameCount > 1

    implicitWidth: Kirigami.Units.iconSizes.large
    implicitHeight: Kirigami.Units.iconSizes.large

    // the real Avatar, used whenever the source is not an animation
    Components.Avatar {
        anchors.fill: parent
        visible: !root.animated
        name: root.name
        source: root.animated ? "" : root.source
        iconSource: root.iconSource
        asynchronous: root.asynchronous
    }

    // the animated path. AnimatedImage cannot round its corners, so it is masked to a circle
    AnimatedImage {
        id: probe
        anchors.fill: parent
        source: root.source
        cache: true
        fillMode: Image.PreserveAspectCrop
        // hidden; the user sees the masked copy, but it keeps playing so the mask has frames
        visible: false
        playing: true
        // re-arm playback on load, an empty source at startup clears the flag
        onStatusChanged: if (status === AnimatedImage.Ready) playing = true
    }

    MultiEffect {
        anchors.fill: parent
        visible: root.animated
        source: probe
        maskEnabled: true
        maskSource: circleMask
    }

    // the circular mask; a layer item, never drawn to screen, just sampled by the effect
    Item {
        id: circleMask
        anchors.fill: parent
        layer.enabled: true
        visible: false

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "black"
        }
    }
}
