// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
pragma Singleton

import QtQuick

// A power level is a number between 0 and 100 in the protocol and a word in the
// interface. The two names below are the ones every Matrix client uses and the
// ones a homeserver's own documentation uses, so they are not invented here;
// what is decided here is that only the two thresholds get a word, because a
// room that has given somebody 63 has said something the interface cannot
// summarise and should not pretend to.
//
// A singleton rather than a copy in each of the two places that needs it: the
// member list and the member card have to agree about what 50 is called.
QtObject {
    readonly property int administrator: 100
    readonly property int moderator: 50

    function label(level) {
        if (level >= administrator)
            return i18nc("@info:status a Matrix room member with full power in the room", "Administrator")
        if (level >= moderator)
            return i18nc("@info:status a Matrix room member who can moderate the room", "Moderator")
        return ""
    }

    // Only the two that have a word get a badge; an ordinary member is the
    // default and a badge on every row says nothing.
    function iconName(level) {
        if (level >= administrator)
            return "user-group-properties"
        if (level >= moderator)
            return "user-group-new"
        return ""
    }
}
