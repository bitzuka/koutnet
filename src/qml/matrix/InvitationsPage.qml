// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import koutnet.app

// one screen that lists every pending room invitation with accept/decline.
// uses the same model as the sidebar, so accepting here clears it there too.
Kirigami.ScrollablePage {
    id: root

    property var invites: null

    signal inviteAccepted(string chatId)
    signal inviteDeclined(string chatId)

    title: i18nc("@title:window the list of room invitations waiting to be answered", "Invitations")

    // See the note on Kirigami.Theme in Main.qml.
    Kirigami.Theme.highlightColor: Brand.accent

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.largeSpacing * 4
        visible: root.invites === null || root.invites.count === 0
        icon.name: "mail-mark-unread"
        text: i18nc("@info there are no pending room invitations", "No invitations")
        explanation: i18nc("@info", "When someone invites you to a Matrix room, it shows up here to accept or decline.")
    }

    ListView {
        id: invitesView
        model: root.invites
        spacing: Kirigami.Units.smallSpacing

        delegate: Kirigami.AbstractCard {
            width: ListView.view.width

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.largeSpacing

                    AnimatedAvatar {
                        Layout.alignment: Qt.AlignTop
                        implicitWidth: Kirigami.Units.iconSizes.large
                        implicitHeight: Kirigami.Units.iconSizes.large
                        name: model.displayName.length > 0 ? model.displayName : model.chatId
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Kirigami.Heading {
                            Layout.fillWidth: true
                            level: 4
                            text: model.displayName.length > 0 ? model.displayName : model.chatId
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            visible: model.inviterName.length > 0
                            text: i18nc("@info:status %1 is the Matrix id or name of whoever sent the invitation", "Invited by %1", model.inviterName)
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            font: Kirigami.Theme.smallFont
                            color: Kirigami.Theme.disabledTextColor
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: model.chatId
                            textFormat: Text.PlainText
                            elide: Text.ElideMiddle
                            font: Kirigami.Theme.smallFont
                            color: Kirigami.Theme.disabledTextColor
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    Item { Layout.fillWidth: true }

                    QQC2.Button {
                        text: i18nc("@action:button decline a room invitation", "Decline")
                        icon.name: "dialog-cancel"
                        onClicked: root.inviteDeclined(model.chatId)
                    }

                    QQC2.Button {
                        text: i18nc("@action:button accept a room invitation", "Accept")
                        icon.name: "dialog-ok"
                        highlighted: true
                        onClicked: root.inviteAccepted(model.chatId)
                    }
                }
            }
        }
    }
}
