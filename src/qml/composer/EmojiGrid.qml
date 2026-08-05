// SPDX-FileCopyrightText: 2022 Tobias Fella
// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Adapted from NeoChat's src/chatbar/EmojiGrid.qml.
//
// Upstream's copyright line carries no address; it is reproduced as it stands.
//
// The sticker branch and the custom-emoji branch are gone - neither has anything
// behind it here - so the grid is only ever emoji and only ever emits chosen().
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import koutnet.app

QQC2.ScrollView {
    id: root

    property alias model: emojis.model
    property alias count: emojis.count
    required property int targetIconSize
    readonly property int emojisPerRow: emojis.width / targetIconSize
    required property Item header

    signal chosen(string unicode)

    onActiveFocusChanged: if (activeFocus) {
        emojis.forceActiveFocus();
    }

    GridView {
        id: emojis

        anchors.fill: parent
        anchors.rightMargin: parent.QQC2.ScrollBar.vertical.visible ? parent.QQC2.ScrollBar.vertical.width : 0

        currentIndex: -1
        keyNavigationEnabled: true
        onActiveFocusChanged: if (activeFocus && currentIndex === -1) {
            currentIndex = 0;
        } else {
            currentIndex = -1;
        }
        onModelChanged: currentIndex = -1

        cellWidth: emojis.width / root.emojisPerRow
        cellHeight: root.targetIconSize

        KeyNavigation.up: root.header

        clip: true

        delegate: EmojiDelegate {
            id: emojiDelegate
            checked: emojis.currentIndex === model.index
            emoji: modelData.unicode
            name: modelData.shortName

            width: emojis.cellWidth
            height: emojis.cellHeight

            Keys.onEnterPressed: clicked()
            Keys.onReturnPressed: clicked()
            onClicked: {
                root.chosen(modelData.unicode);
                EmojiModel.emojiUsed(modelData.shortName);
            }
            Keys.onSpacePressed: pressAndHold()
            onPressAndHold: {
                if (EmojiModel.tones(modelData.shortName).length === 0) {
                    return;
                }
                let tones = tonesPopupComponent.createObject(emojiDelegate, {
                    shortName: modelData.shortName,
                    unicode: modelData.unicode,
                    categoryIconSize: root.targetIconSize
                }) as EmojiTonesPicker;
                tones.open();
                tones.forceActiveFocus();
            }
            showTones: !!modelData && EmojiModel.tones(modelData.shortName).length > 0
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            icon.name: "preferences-desktop-emoticons"
            text: i18nc("@info shown when a search matches no emoji", "No emojis")
            visible: emojis.count === 0
        }
    }
    Component {
        id: tonesPopupComponent
        EmojiTonesPicker {
            onChosen: emoji => root.chosen(emoji)
        }
    }
}
