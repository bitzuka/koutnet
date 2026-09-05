// SPDX-FileCopyrightText: None
// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: LGPL-2.0-or-later
//
// Adapted from NeoChat's src/libneochat/emojitones.h. Upstream carries no
// named copyright holder, so the "None" line above is theirs, kept as it stands.
#pragma once

#include "EmojiModel.h"

#include <QMultiHash>

// The skin-tone variants, keyed on the description of the emoji they vary -
// "waving hand" maps to the five toned waving hands. Only EmojiModel::tones()
// reads it, hence the friend rather than a public accessor.
class EmojiTones
{
private:
    static QMultiHash<QString, Emoji> tones();

    friend class EmojiModel;
};
