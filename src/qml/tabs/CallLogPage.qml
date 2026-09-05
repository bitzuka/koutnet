// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.delegates as Delegates
import koutnet.app

// Backed by HistoryManager.loadCallLog() - the same store VoiceCallManager will
// append to once call events are wired to it.
Kirigami.ScrollablePage {
    id: root

    title: i18nc("@title call log", "Calls")

    Kirigami.Theme.highlightColor: Brand.accent

    ListView {
        id: callList

        model: HistoryManager.loadCallLog()

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.largeSpacing * 4
            visible: callList.count === 0
            icon.name: "call-start"
            text: i18nc("@info the call log is empty", "No calls")
            explanation: i18nc("@info", "Calls you make and receive will be listed here.")
        }

        delegate: Delegates.RoundedItemDelegate {
            id: callRow

            required property var modelData

            // Outgoing and incoming are one row with the arrow turned round.
            icon.name: callRow.modelData.is_own === true ? "call-start" : "call-stop"
            text: callRow.modelData.sender || i18nc("@item call log entry with no name attached", "Unknown")

            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                Delegates.SubtitleContentItem {
                    itemDelegate: callRow
                    Layout.fillWidth: true
                    subtitle: callRow.modelData.text || ""
                }

                QQC2.Label {
                    Layout.alignment: Qt.AlignVCenter
                    text: callRow.modelData.ts
                        ? new Date(callRow.modelData.ts * 1000).toLocaleString(Qt.locale(), Locale.ShortFormat)
                        : ""
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                }
            }
        }
    }
}
