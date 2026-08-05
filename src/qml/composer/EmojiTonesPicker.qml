// SPDX-FileCopyrightText: 2022 Tobias Fella <tobias.fella@kde.org>
// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Adapted from NeoChat's src/chatbar/EmojiTonesPicker.qml.
//
// The skin tones of one emoji, on a strip that opens over the grid when the key
// is held down.
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

import koutnet.app

QQC2.Popup {
    id: root

    signal chosen(string emoji)

    Component.onCompleted: {
        tonesList.currentIndex = 0;
        tonesList.forceActiveFocus();
    }

    required property string shortName
    required property string unicode
    required property int categoryIconSize
    width: root.categoryIconSize * tonesList.count + 2 * padding
    height: root.categoryIconSize + 2 * padding
    y: -height
    padding: 2
    modal: true
    dim: true
    clip: false
    onOpened: x = Math.min(parent.mapFromGlobal(QQC2.Overlay.overlay.width - root.width, 0).x, -(width - parent.width) / 2)
    background: Kirigami.ShadowedRectangle {
        color: Kirigami.Theme.backgroundColor
        radius: Kirigami.Units.cornerRadius
        shadow {
            size: Kirigami.Units.largeSpacing
            color: Qt.rgba(0.0, 0.0, 0.0, 0.3)
            yOffset: 2
        }
        border {
            color: Kirigami.ColorUtils.tintWithAlpha(color, Kirigami.Theme.textColor, 0.15)
            width: 1
        }
    }

    ListView {
        id: tonesList
        width: parent.width
        height: parent.height
        orientation: Qt.Horizontal
        model: EmojiModel.tones(root.shortName)
        keyNavigationEnabled: true
        keyNavigationWraps: true

        delegate: EmojiDelegate {
            id: emojiDelegate
            checked: tonesList.currentIndex === model.index
            emoji: modelData.unicode
            name: modelData.shortName

            width: root.categoryIconSize
            height: width

            Keys.onEnterPressed: clicked()
            Keys.onReturnPressed: clicked()
            onClicked: {
                root.chosen(modelData.unicode);
                // Upstream passes the whole gadget here; the slot takes a short
                // name, so the tone is filed under the name it will be found by.
                EmojiModel.emojiUsed(modelData.shortName);
                root.close();
            }
        }
    }
}
