// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
//
// Declares the interface implemented in EmojiModel.cpp, which is adapted from
// NeoChat and carries its authors' copyright there. This header is written
// from scratch: the version that came with the port was GPL-3.0-only, with no
// later-version and no KDE-Accepted clause, which would have pinned the whole
// application and closed the branch KDE e.V. relies on.
//
// The Category values and the role numbers are dictated by the generated
// emoji table in emojis.h, so they are spelled the way that table expects.
// The Custom category is absent on purpose: NeoChat fills it from Matrix
// image packs, and KOutNet has nothing to put there.
#pragma once

#include <KConfigGroup>
#include <KSharedConfig>
#include <QAbstractListModel>
#include <QQmlEngine>

// One entry of the table. A gadget rather than a QObject because the picker
// hands thousands of these to QML and none of them need an identity.
struct Emoji {
    Emoji() = default;

    Emoji(QString unicode, QString shortName, bool isCustom = false)
        : unicode(std::move(unicode))
        , shortName(std::move(shortName))
        , isCustom(isCustom)
    {
    }

    Emoji(QString unicode, QString shortName, QString description)
        : unicode(std::move(unicode))
        , shortName(std::move(shortName))
        , description(std::move(description))
    {
    }

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

// The table behind the composer's picker: every emoji filed by category,
// searchable by short name, with skin tone variants and a recently used list.
class EmojiModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList categories READ categories CONSTANT)
    // The short row the reaction picker offers before the full keyboard.
    Q_PROPERTY(QVariantList quickReactions READ quickReactions CONSTANT)

public:
    enum RoleNames {
        ShortNameRole = Qt::DisplayRole,
        UnicodeRole,
        InvalidRole = 50,
        DisplayRole = 51,
        ReplacedTextRole = 52,
        DescriptionRole = 53,
    };
    Q_ENUM(RoleNames)

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

    // One table for the whole application; building it per view would parse
    // several thousand entries again for nothing.
    static EmojiModel &instance()
    {
        static EmojiModel model;
        return model;
    }

    static EmojiModel *create(QQmlEngine *engine, QJSEngine *)
    {
        engine->setObjectOwnership(&instance(), QQmlEngine::CppOwnership);
        return &instance();
    }

    [[nodiscard]] QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override;
    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // Substring match on the short name. limit caps the answer at ten, which
    // is what the reaction row has space for; the search box wants all of it.
    Q_INVOKABLE static QVariantList filterModel(const QString &filter, bool limit = true);

    Q_INVOKABLE QVariantList emojis(EmojiModel::Category category) const;

    Q_INVOKABLE [[nodiscard]] QList<Emoji> tones(const QString &baseEmoji) const;

    [[nodiscard]] QStringList lastUsedEmojis() const;
    [[nodiscard]] QVariantList categories() const;
    [[nodiscard]] QVariantList quickReactions() const;

Q_SIGNALS:
    void historyChanged();

public Q_SLOTS:
    // Moves the emoji to the front of the recently used list, or puts it there.
    void emojiUsed(const QString &shortName);

private:
    explicit EmojiModel(QObject *parent = nullptr);

    [[nodiscard]] QVariantList emojiHistory() const;

    static QHash<Category, QVariantList> _emojis;

    KSharedConfig::Ptr m_config;
    KConfigGroup m_configGroup;
};
