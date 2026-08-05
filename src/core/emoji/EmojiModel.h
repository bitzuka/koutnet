// SPDX-FileCopyrightText: 2018 Black Hat <bhat@encom.eu.org>
// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only
//
// Adapted from NeoChat's src/libneochat/models/emojimodel.h.
//
// Kept close to the original: the Emoji gadget, the Category enum and the
// RoleNames values are the same, so NeoChat's generated emojis.h table is
// included here unmodified and their EmojiGrid/EmojiDelegate QML ports below
// keep working against it.
//
// What is gone is the custom-emoji half. NeoChat's Custom category is fed by
// Matrix image packs, which KOutNet has nothing to put in; filterModel() and
// filterModelNoCustom() therefore collapse into one, and the Custom category
// and categoriesWithCustom() are dropped rather than left as empty stubs.
#pragma once

#include <KConfigGroup>
#include <KSharedConfig>
#include <QAbstractListModel>
#include <QObject>
#include <QQmlEngine>

struct Emoji {
    Emoji(QString unicode, QString shortname, bool isCustom = false)
        : unicode(std::move(unicode))
        , shortName(std::move(shortname))
        , isCustom(isCustom)
    {
    }
    Emoji(QString unicode, QString shortname, QString description)
        : unicode(std::move(unicode))
        , shortName(std::move(shortname))
        , description(std::move(description))
    {
    }
    Emoji() = default;

    QString unicode;
    QString shortName;
    QString description;
    bool isCustom = false;

    Q_GADGET
    Q_PROPERTY(QString unicode MEMBER unicode)
    Q_PROPERTY(QString shortName MEMBER shortName)
    Q_PROPERTY(QString description MEMBER description)
    Q_PROPERTY(bool isCustom MEMBER isCustom)
};

Q_DECLARE_METATYPE(Emoji)

// The emoji table behind the composer's picker: every emoji, filed by category,
// searchable by short name, with the skin-tone variants and a recently-used list.
class EmojiModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList categories READ categories CONSTANT)
    // The handful the reaction picker offers before the full keyboard.
    Q_PROPERTY(QVariantList quickReactions READ quickReactions CONSTANT)

public:
    static EmojiModel &instance()
    {
        static EmojiModel _instance;
        return _instance;
    }
    static EmojiModel *create(QQmlEngine *engine, QJSEngine *)
    {
        engine->setObjectOwnership(&instance(), QQmlEngine::CppOwnership);
        return &instance();
    }

    enum RoleNames {
        ShortNameRole = Qt::DisplayRole,
        UnicodeRole,
        InvalidRole = 50,
        DisplayRole = 51,
        ReplacedTextRole = 52,
        DescriptionRole = 53,
    };
    Q_ENUM(RoleNames)

    // Category values are what emojis.h is written in terms of, so the order and
    // the spelling of these are fixed by that table.
    enum Category {
        Custom,
        Search,
        SearchNoCustom,
        History,
        Smileys,
        People,
        Nature,
        Food,
        Activities,
        Travel,
        Objects,
        Symbols,
        Flags,
        Component,
    };
    Q_ENUM(Category)

    [[nodiscard]] QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override;
    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // Short-name substring match. limit caps the result at ten, which is what
    // the reaction row wants; the picker's search box asks for everything.
    Q_INVOKABLE static QVariantList filterModel(const QString &filter, bool limit = true);

    Q_INVOKABLE QVariantList emojis(EmojiModel::Category category) const;

    Q_INVOKABLE [[nodiscard]] QList<Emoji> tones(const QString &baseEmoji) const;

    [[nodiscard]] QStringList lastUsedEmojis() const;

    [[nodiscard]] QVariantList categories() const;
    [[nodiscard]] QVariantList quickReactions() const;

Q_SIGNALS:
    void historyChanged();

public Q_SLOTS:
    // Moves the emoji to the front of the recently-used list, or puts it there.
    void emojiUsed(const QString &shortName);

private:
    static QHash<Category, QVariantList> _emojis;

    [[nodiscard]] QVariantList emojiHistory() const;

    KSharedConfig::Ptr m_config;
    KConfigGroup m_configGroup;
    explicit EmojiModel(QObject *parent = nullptr);
};
