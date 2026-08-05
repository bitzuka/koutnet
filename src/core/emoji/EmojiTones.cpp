// SPDX-FileCopyrightText: None
// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: LGPL-2.0-or-later
//
// Adapted from NeoChat's src/libneochat/emojitones.cpp. Upstream carries no
// named copyright holder, so the "None" line above is theirs, kept as it stands.

#include "EmojiTones.h"

struct {
    const char8_t *name;
    const char8_t *escaped_sequence;
    const char8_t *shortcode;
    const char8_t *description;
} constexpr const tones_data[] = {
#include "emojitones_data.h"
};

using namespace Qt::StringLiterals;

QMultiHash<QString, Emoji> EmojiTones::tones()
{
    static QMultiHash<QString, Emoji> _tones;
    if (_tones.isEmpty()) {
        for (const auto &tone : tones_data) {
            _tones.insert(QString::fromUtf8(tone.name),
                          Emoji(QString::fromUtf8(tone.escaped_sequence), QString::fromUtf8(tone.shortcode), QString::fromUtf8(tone.description)));
        }
    }
    return _tones;
}
