// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import koutnet.app

// Somebody is calling: a question with two answers, which is a dialog.
//
// It used to be a frameless always-on-top Window that slid up from
// Screen.height + 10 with a NumberAnimation, could be dragged around by a
// MouseArea, and drew its accept and reject buttons as coloured circles with a
// Label inside carrying an icon.name that Label has no such property for - so
// both were blank circles. A PromptDialog in the main window is the same question
// asked properly, and it follows the window rather than the screen.
Kirigami.PromptDialog {
    id: root

    property string callerName: ""
    property string callerIp: ""

    signal answered()
    signal declined()

    // See the note on Kirigami.Theme in Main.qml: a dialog is reparented into the
    // window overlay, which is a theme chain of its own.
    Kirigami.Theme.inherit: false
    Kirigami.Theme.highlightColor: Brand.accent

    title: i18nc("@title:window", "Incoming call")
    showCloseButton: false
    // Only the two below. The standard set would put an OK next to them and leave
    // it unclear which one picks up.
    standardButtons: QQC2.Dialog.NoButton

    customFooterActions: [
        Kirigami.Action {
            text: i18nc("@action:button pick up the incoming call", "Answer")
            icon.name: "call-start"
            onTriggered: {
                root.answered()
                root.close()
            }
        },
        Kirigami.Action {
            text: i18nc("@action:button refuse the incoming call", "Decline")
            icon.name: "call-stop"
            onTriggered: {
                root.declined()
                root.close()
            }
        }
    ]

    // Called by the window when the peer gives up before the user answers.
    function callRejected() {
        root.close()
    }

    RowLayout {
        spacing: Kirigami.Units.largeSpacing

        Components.Avatar {
            implicitWidth: Kirigami.Units.gridUnit * 4
            implicitHeight: Kirigami.Units.gridUnit * 4
            Layout.alignment: Qt.AlignVCenter
            name: root.callerName
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 2
                elide: Text.ElideRight
                text: root.callerName
            }

            QQC2.Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: root.callerIp
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
            }
        }
    }
}
