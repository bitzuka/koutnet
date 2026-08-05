// SPDX-FileCopyrightText: 2022 Tobias Fella <tobias.fella@kde.org>
// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Adapted from NeoChat's src/chatbar/EmojiPicker.qml.
//
// This replaces the hand-written grid over a hardcoded table that used to be
// here. Categories, search over the CLDR short names, a recently-used list and
// press-and-hold skin tones all come from upstream; the table behind them is
// NeoChat's generated emojis.h, ported alongside it.
//
// Dropped from upstream: the Emojis/Stickers NavigationTabBar and everything it
// switched to - ImagePacksModel, StickerModel, EmoticonFilterModel and the
// currentRoom they hang off are Matrix image packs, which this project has no
// equivalent of. What is left is the emoji half, unchanged in shape.
//
// This is the panel, not the window round it: EmojiPopup.qml is what the
// composer opens, the same way upstream keeps EmojiDialog separate.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import koutnet.app

ColumnLayout {
    id: root

    readonly property var currentEmojiModel: EmojiModel.categories

    readonly property int categoryIconSize: Math.round(Kirigami.Units.gridUnit * 2.5)
    // Guarded because a ListView reports currentIndex -1 while its model is
    // still being set, and upstream indexes straight into the array.
    readonly property var currentCategory: categories.currentIndex >= 0
        ? root.currentEmojiModel[categories.currentIndex].category
        : EmojiModel.Smileys
    readonly property alias categoryCount: categories.count

    signal chosen(string emoji)

    onActiveFocusChanged: if (activeFocus) {
        searchField.forceActiveFocus();
    }

    spacing: 0

    QQC2.ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: root.categoryIconSize + QQC2.ScrollBar.horizontal.height
        QQC2.ScrollBar.horizontal.height: QQC2.ScrollBar.horizontal.visible ? QQC2.ScrollBar.horizontal.implicitHeight : 0
        visible: categories.count !== 0

        ListView {
            id: categories
            clip: true
            focus: true
            orientation: ListView.Horizontal

            Keys.onReturnPressed: if (emojiGrid.count > 0) {
                emojiGrid.focus = true;
            }
            Keys.onEnterPressed: if (emojiGrid.count > 0) {
                emojiGrid.focus = true;
            }

            KeyNavigation.down: emojiGrid.count > 0 ? emojiGrid : categories
            KeyNavigation.tab: emojiGrid.count > 0 ? emojiGrid : categories

            keyNavigationEnabled: true
            keyNavigationWraps: true
            Keys.forwardTo: searchField
            interactive: width !== contentWidth

            model: root.currentEmojiModel
            Component.onCompleted: categories.forceActiveFocus()

            delegate: Kirigami.NavigationTabButton {
                required property var modelData
                required property int index

                width: root.categoryIconSize
                height: width
                checked: categories.currentIndex === index
                text: modelData.emoji
                QQC2.ToolTip.text: modelData.name
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                QQC2.ToolTip.visible: hovered
                onClicked: {
                    categories.currentIndex = index;
                    categories.focus = true;
                }
            }
        }
    }

    Kirigami.Separator {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
    }

    Kirigami.SearchField {
        id: searchField
        Layout.margins: Kirigami.Units.smallSpacing
        Layout.fillWidth: true

        // The focus is managed by the parent and we don't want to use the
        // standard shortcut as it could block other SearchFields from using it.
        focusSequence: ""
    }

    EmojiGrid {
        id: emojiGrid
        targetIconSize: root.categoryIconSize
        model: searchField.text.length === 0
            ? EmojiModel.emojis(root.currentCategory)
            : EmojiModel.filterModel(searchField.text, false)
        Layout.fillWidth: true
        Layout.fillHeight: true
        onChosen: unicode => root.chosen(unicode)
        header: categories
        Keys.forwardTo: searchField
    }

    function clearSearchField() {
        searchField.text = "";
    }
}
