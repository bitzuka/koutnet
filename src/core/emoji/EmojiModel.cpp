// SPDX-FileCopyrightText: 2017 Konstantinos Sideris <siderisk@auth.gr>
// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Adapted from NeoChat's src/libneochat/models/emojimodel.cpp.
//
// The custom-emoji paths are gone - see the note in EmojiModel.h - so
// filterModelNoCustom() has folded into filterModel() and categoriesWithCustom()
// is not here. Everything else is upstream's, including the u""_s literal style,
// which is left alone so the port stays comparable to the file it came from.

#include <QVariant>

#include "EmojiModel.h"
#include "EmojiTones.h"

#include <KLocalizedString>

struct {
    EmojiModel::Category category;
    const char8_t *escaped_sequence;
    const char8_t *shortcode;
    const char8_t *description;
} constexpr const emoji_data[] = {
#include "emojis.h"
};

using namespace Qt::StringLiterals;

EmojiModel::EmojiModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_config(KSharedConfig::openStateConfig())
    , m_configGroup(KConfigGroup(m_config, u"Editor"_s))
{
    if (_emojis.isEmpty()) {
        for (const auto &emoji : emoji_data) {
            _emojis[emoji.category].push_back(QVariant::fromValue(Emoji(QString::fromUtf8(emoji.escaped_sequence),
                                                                        QStringLiteral(":%1:").arg(QString::fromUtf8(emoji.shortcode)),
                                                                        QString::fromUtf8(emoji.description))));
        }
    }
}

int EmojiModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    int total = 0;
    for (const auto &category : std::as_const(_emojis)) {
        total += category.count();
    }
    return total;
}

QVariant EmojiModel::data(const QModelIndex &index, int role) const
{
    auto row = index.row();
    for (const auto &category : std::as_const(_emojis)) {
        if (row >= category.count()) {
            row -= category.count();
            continue;
        }
        auto emoji = category[row].value<Emoji>();
        switch (role) {
        case ShortNameRole:
            return emoji.shortName;
        case UnicodeRole:
        case ReplacedTextRole:
            return emoji.unicode;
        case InvalidRole:
            return u"invalid"_s;
        case DisplayRole:
            return u"%2   %1"_s.arg(emoji.shortName, emoji.unicode);
        case DescriptionRole:
            return emoji.description;
        }
    }
    return {};
}

QHash<int, QByteArray> EmojiModel::roleNames() const
{
    return {{ShortNameRole, "shortName"}, {UnicodeRole, "unicode"}};
}

QStringList EmojiModel::lastUsedEmojis() const
{
    return m_configGroup.readEntry(u"lastUsedEmojis"_s, QStringList());
}

QVariantList EmojiModel::filterModel(const QString &filter, bool limit)
{
    QVariantList result;

    const auto &values = _emojis.values();
    for (const auto &e : values) {
        for (const auto &variant : e) {
            const auto &emoji = qvariant_cast<Emoji>(variant);
            if (emoji.shortName.contains(filter, Qt::CaseInsensitive)) {
                result.append(variant);
                if (result.length() > 10 && limit) {
                    return result;
                }
            }
        }
    }
    return result;
}

void EmojiModel::emojiUsed(const QString &shortName)
{
    auto list = lastUsedEmojis();

    auto it = list.begin();
    while (it != list.end()) {
        if (*it == shortName) {
            it = list.erase(it);
        } else {
            it++;
        }
    }

    list.push_front(shortName);

    m_configGroup.writeEntry(u"lastUsedEmojis"_s, list);

    Q_EMIT historyChanged();
}

QVariantList EmojiModel::emojis(Category category) const
{
    if (category == History) {
        return emojiHistory();
    }
    return _emojis[category];
}

QList<Emoji> EmojiModel::tones(const QString &baseEmoji) const
{
    if (baseEmoji.endsWith(u"tone"_s)) {
        return EmojiTones::tones().values(baseEmoji.split(u":"_s)[0]);
    }
    return EmojiTones::tones().values(baseEmoji);
}

QHash<EmojiModel::Category, QVariantList> EmojiModel::_emojis;

QVariantList EmojiModel::categories() const
{
    return QVariantList{
        {QVariantMap{
            {u"category"_s, EmojiModel::History},
            {u"name"_s, i18nc("Previously used emojis", "History")},
            {u"emoji"_s, u"\U0000231b\U0000fe0f"_s},
        }},
        {QVariantMap{
            {u"category"_s, EmojiModel::Smileys},
            {u"name"_s, i18nc("'Smileys' is a category of emoji", "Smileys")},
            {u"emoji"_s, u"\U0001f60f"_s},
        }},
        {QVariantMap{
            {u"category"_s, EmojiModel::People},
            {u"name"_s, i18nc("'People' is a category of emoji", "People")},
            {u"emoji"_s, u"\U0001f64b\U0000200d\U00002642\U0000fe0f"_s},
        }},
        {QVariantMap{
            {u"category"_s, EmojiModel::Nature},
            {u"name"_s, i18nc("'Nature' is a category of emoji", "Nature")},
            {u"emoji"_s, u"\U0001f332"_s},
        }},
        {QVariantMap{
            {u"category"_s, EmojiModel::Food},
            {u"name"_s, i18nc("'Food' is a category of emoji", "Food")},
            {u"emoji"_s, u"\U0001f35b"_s},
        }},
        {QVariantMap{
            {u"category"_s, EmojiModel::Activities},
            {u"name"_s, i18nc("'Activities' is a category of emoji", "Activities")},
            {u"emoji"_s, u"\U0001f681"_s},
        }},
        {QVariantMap{
            {u"category"_s, EmojiModel::Travel},
            {u"name"_s, i18nc("'Travel' is  a category of emoji", "Travel")},
            {u"emoji"_s, u"\U0001f685"_s},
        }},
        {QVariantMap{
            {u"category"_s, EmojiModel::Objects},
            {u"name"_s, i18nc("'Objects' is a category of emoji", "Objects")},
            {u"emoji"_s, u"\U0001f4a1"_s},
        }},
        {QVariantMap{
            {u"category"_s, EmojiModel::Symbols},
            {u"name"_s, i18nc("'Symbols' is a category of emoji", "Symbols")},
            {u"emoji"_s, u"\U0001f523"_s},
        }},
        {QVariantMap{
            {u"category"_s, EmojiModel::Flags},
            {u"name"_s, i18nc("'Flags' is a category of emoji", "Flags")},
            {u"emoji"_s, u"\U0001f3c1"_s},
        }},
    };
}

QVariantList EmojiModel::quickReactions() const
{
    QVariantList reactions;
    reactions.append(filterModel(QStringLiteral(":thumbsup:"), true));
    reactions.append(filterModel(QStringLiteral(":thumbsdown:"), true));
    reactions.append(filterModel(QStringLiteral(":smile:"), true));
    reactions.append(filterModel(QStringLiteral(":tada:"), true));
    reactions.append(filterModel(QStringLiteral(":red heart:"), true));

    return reactions;
}

QVariantList EmojiModel::emojiHistory() const
{
    QVariantList list;
    const auto &lastUsed = lastUsedEmojis();
    for (const auto &historicEmoji : lastUsed) {
        for (const auto &emojiCategory : std::as_const(_emojis)) {
            for (const auto &emoji : emojiCategory) {
                if (qvariant_cast<Emoji>(emoji).shortName == historicEmoji) {
                    list.append(emoji);
                }
            }
        }
    }
    return list;
}
