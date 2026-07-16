#include "Translations.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace koutnet {

namespace {

// The i18n/ directory lives at the repo root (koutnet/i18n), while the
// binary ends up under NEWCORE-2-CPP/build/. Walk upward from the
// executable looking for it instead of hardcoding a fixed number of "..",
// so this keeps working whether run from build/ or an install layout.
QString findI18nDir()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        const QString candidate = dir.filePath("i18n");
        if (QDir(candidate).exists())
            return candidate;
        if (!dir.cdUp())
            break;
    }
    return QString();
}

} // namespace

Translations::Translations(QObject *parent)
    : QObject(parent)
{
    const QString i18nDir = findI18nDir();
    if (i18nDir.isEmpty()) {
        qWarning() << "Translations: could not locate i18n/ directory near" << QCoreApplication::applicationDirPath();
        return;
    }

    loadLanguage(i18nDir, "ru");
    loadLanguage(i18nDir, "en");
}

void Translations::loadLanguage(const QString &i18nDir, const QString &language)
{
    QFile file(QDir(i18nDir).filePath(language + ".json"));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Translations: failed to open" << file.fileName();
        return;
    }

    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        qWarning() << "Translations: invalid JSON in" << file.fileName();
        return;
    }

    QMap<QString, QString> table;
    const auto obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it)
        table.insert(it.key(), it.value().toString());

    m_dictionary[language] = table;
}

void Translations::setCurrent(const QString &language)
{
    if (m_current == language || !m_dictionary.contains(language))
        return;
    m_current = language;
    emit currentChanged();
}

QString Translations::t(const QString &key) const
{
    const auto &table = m_dictionary.contains(m_current)
        ? m_dictionary[m_current]
        : m_dictionary.value("ru");
    return table.value(key, key);
}

} // namespace koutnet
