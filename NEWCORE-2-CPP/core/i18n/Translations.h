// SPDX-FileCopyrightText: 2026 bitzuka <matveypotyzhno@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - i18n
// Loads translation tables from an i18n/ directory found on disk next to
// the built binary (koutnet/i18n/en.json, koutnet/i18n/ru.json) - editable
// without a rebuild.
#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <QStringList>

namespace koutnet {

class Translations : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString current READ current WRITE setCurrent NOTIFY currentChanged)
    Q_PROPERTY(QStringList availableLanguages READ availableLanguages CONSTANT)

public:
    explicit Translations(QObject *parent = nullptr);

    QString current() const { return m_current; }
    QStringList availableLanguages() const { return m_dictionary.keys(); }
    void setCurrent(const QString &language);

    Q_INVOKABLE QString t(const QString &key) const;

Q_SIGNALS:
    void currentChanged();

private:
    void loadLanguage(const QString &i18nDir, const QString &language);
    // Warns about keys present in one language file and missing from
    // another. With 14 hand-maintained JSON files that is easy to do and
    // hard to spot, since the UI just shows the raw key. Runs at startup so
    // it lands in the log rather than in a bug report.
    void validateDictionary() const;

    QString m_current = QString();
    QMap<QString, QMap<QString, QString>> m_dictionary;
};

} // namespace koutnet
