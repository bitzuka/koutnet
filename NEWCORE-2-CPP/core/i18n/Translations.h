// KOutNet — i18n
// Loads translation tables from an i18n/ directory found on disk next to
// the built binary (koutnet/i18n/en.json, koutnet/i18n/ru.json) — editable
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

signals:
    void currentChanged();

private:
    void loadLanguage(const QString &i18nDir, const QString &language);

    QString m_current = "ru";
    QMap<QString, QMap<QString, QString>> m_dictionary;
};

} // namespace koutnet
