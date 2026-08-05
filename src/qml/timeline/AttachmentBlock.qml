// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// A file that arrived in the conversation: shown as itself if it is a picture,
// and as a row with a name and an icon if it is not.
//
// Both cases are already a file on this machine - the transfer put it there - so
// neither has a download step and both open with one click.
ColumnLayout {
    id: root

    property string filePath: ""
    property string fileName: ""
    property bool isImage: false
    // How wide the picture is allowed to get. The timeline passes its own
    // content width down; a picture is never blown up past what the file holds.
    property real maxImageWidth: Kirigami.Units.gridUnit * 18

    signal imageActivated(string path)
    signal fileActivated(string path)

    spacing: 0

    Image {
        id: preview

        // Sized off the cap rather than off its own width. A height that reads
        // back the width the layout just gave it is the binding loop this is
        // written around.
        readonly property real shownWidth: Math.min(root.maxImageWidth,
                                                    sourceSize.width > 0 ? sourceSize.width : root.maxImageWidth)

        Layout.preferredWidth: preview.shownWidth
        Layout.preferredHeight: sourceSize.width > 0
            ? Math.round(preview.shownWidth * (sourceSize.height / sourceSize.width))
            : Kirigami.Units.gridUnit * 8
        // Tall pictures get cropped by the viewer rather than by pushing three
        // messages off the screen.
        Layout.maximumHeight: Kirigami.Units.gridUnit * 20

        visible: root.isImage
        source: root.isImage ? "file://" + root.filePath : ""
        fillMode: Image.PreserveAspectFit
        mipmap: true
        asynchronous: true

        Accessible.role: Accessible.Graphic
        Accessible.name: root.fileName

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            onTapped: root.imageActivated(root.filePath)
        }
    }

    QQC2.ItemDelegate {
        Layout.fillWidth: true
        visible: !root.isImage
        text: root.fileName
        icon.name: "document-open"
        onClicked: root.fileActivated(root.filePath)
    }
}
