// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "ThemeManager.h"

#include <KLocalizedString>

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTextStream>
#include <QDebug>
#include "koutnet_app_debug.h"

namespace {

QVariantMap palette(const QString &label, const QString &bg, const QString &bg2, const QString &bg3,
                     const QString &border, const QString &text, const QString &textDim,
                     const QString &accent, const QString &accent2, const QString &btnBg,
                     const QString &btnHover, const QString &btnPress, const QString &itemBg,
                     const QString &itemSel, const QString &headerBg, const QString &msgOwn,
                     const QString &msgOther, const QString &online, const QString &offline)
{
    QVariantMap m;
    m[QStringLiteral("label")] = label;
    m[QStringLiteral("bg")] = bg;
    m[QStringLiteral("bg2")] = bg2;
    m[QStringLiteral("bg3")] = bg3;
    m[QStringLiteral("border")] = border;
    m[QStringLiteral("text")] = text;
    m[QStringLiteral("text_dim")] = textDim;
    m[QStringLiteral("accent")] = accent;
    m[QStringLiteral("accent2")] = accent2;
    m[QStringLiteral("btn_bg")] = btnBg;
    m[QStringLiteral("btn_hover")] = btnHover;
    m[QStringLiteral("btn_press")] = btnPress;
    m[QStringLiteral("item_bg")] = itemBg;
    m[QStringLiteral("item_sel")] = itemSel;
    m[QStringLiteral("header_bg")] = headerBg;
    m[QStringLiteral("msg_own")] = msgOwn;
    m[QStringLiteral("msg_other")] = msgOther;
    m[QStringLiteral("online")] = online;
    m[QStringLiteral("offline")] = offline;
    return m;
}

} // namespace

QHash<QString, QVariantMap> ThemeManager::buildPalettes()
{
    QHash<QString, QVariantMap> p;

    p[QStringLiteral("dark")] = palette(QStringLiteral("dark"), QStringLiteral("#323232"), QStringLiteral("#282828"), QStringLiteral("#1E1E1E"), QStringLiteral("#484848"),
        QStringLiteral("#E0E0E0"), QStringLiteral("#909090"), QStringLiteral("#0078D4"), QStringLiteral("#005A9E"), QStringLiteral("#4A4A4A"), QStringLiteral("#5A5A5A"),
        QStringLiteral("#3A3A3A"), QStringLiteral("#2E2E2E"), QStringLiteral("#0063B1"), QStringLiteral("#3C3C3C"), QStringLiteral("#1A3A5C"), QStringLiteral("#383838"),
        QStringLiteral("#2ECC71"), QStringLiteral("#E74C3C"));

    p[QStringLiteral("light")] = palette(QStringLiteral("light"), QStringLiteral("#F0F0F0"), QStringLiteral("#FAFAFA"), QStringLiteral("#FFFFFF"), QStringLiteral("#C8C8C8"),
        QStringLiteral("#1A1A1A"), QStringLiteral("#707070"), QStringLiteral("#0078D4"), QStringLiteral("#005A9E"), QStringLiteral("#E0E0E0"), QStringLiteral("#D0D0D0"),
        QStringLiteral("#C0C0C0"), QStringLiteral("#F8F8F8"), QStringLiteral("#0078D4"), QStringLiteral("#E8E8E8"), QStringLiteral("#C8E6FA"), QStringLiteral("#EEEEEE"),
        QStringLiteral("#27AE60"), QStringLiteral("#E74C3C"));

    p[QStringLiteral("dark_blue")] = palette(QStringLiteral("dark_blue"), QStringLiteral("#1A2540"), QStringLiteral("#131B30"), QStringLiteral("#0D1220"), QStringLiteral("#2A3858"),
        QStringLiteral("#C8D8FF"), QStringLiteral("#6878A8"), QStringLiteral("#4080FF"), QStringLiteral("#2060DD"), QStringLiteral("#2A3A60"), QStringLiteral("#3A4A70"),
        QStringLiteral("#1A2A50"), QStringLiteral("#182038"), QStringLiteral("#2060CC"), QStringLiteral("#202848"), QStringLiteral("#1A3060"), QStringLiteral("#1E2840"),
        QStringLiteral("#00E676"), QStringLiteral("#FF5252"));

    p[QStringLiteral("dark_red")] = palette(QStringLiteral("dark_red"), QStringLiteral("#2A1010"), QStringLiteral("#200808"), QStringLiteral("#160404"), QStringLiteral("#4A2020"),
        QStringLiteral("#FFD0D0"), QStringLiteral("#A06060"), QStringLiteral("#CC2020"), QStringLiteral("#AA1010"), QStringLiteral("#4A1818"), QStringLiteral("#5A2828"),
        QStringLiteral("#3A0808"), QStringLiteral("#281010"), QStringLiteral("#AA0000"), QStringLiteral("#381818"), QStringLiteral("#3A1010"), QStringLiteral("#2A1818"),
        QStringLiteral("#00E676"), QStringLiteral("#FF5252"));

    p[QStringLiteral("gray")] = palette(QStringLiteral("gray"), QStringLiteral("#606060"), QStringLiteral("#505050"), QStringLiteral("#404040"), QStringLiteral("#707070"),
        QStringLiteral("#F0F0F0"), QStringLiteral("#B0B0B0"), QStringLiteral("#909090"), QStringLiteral("#707070"), QStringLiteral("#707070"), QStringLiteral("#808080"),
        QStringLiteral("#606060"), QStringLiteral("#585858"), QStringLiteral("#888888"), QStringLiteral("#686868"), QStringLiteral("#5A5A7A"), QStringLiteral("#484848"),
        QStringLiteral("#90EE90"), QStringLiteral("#FF9090"));

    p[QStringLiteral("midnight")] = palette(QStringLiteral("midnight"), QStringLiteral("#0D0D1A"), QStringLiteral("#080810"), QStringLiteral("#040408"), QStringLiteral("#1A1A3A"),
        QStringLiteral("#B0B8FF"), QStringLiteral("#5058A0"), QStringLiteral("#6040FF"), QStringLiteral("#4020DD"), QStringLiteral("#151528"), QStringLiteral("#202040"),
        QStringLiteral("#0A0A18"), QStringLiteral("#0E0E20"), QStringLiteral("#4030CC"), QStringLiteral("#121224"), QStringLiteral("#120A30"), QStringLiteral("#0E0E22"),
        QStringLiteral("#00FFB0"), QStringLiteral("#FF4060"));

    p[QStringLiteral("forest")] = palette(QStringLiteral("forest"), QStringLiteral("#1A2A1A"), QStringLiteral("#122012"), QStringLiteral("#0A160A"), QStringLiteral("#2A3E2A"),
        QStringLiteral("#C8EEC8"), QStringLiteral("#6A8A6A"), QStringLiteral("#40AA40"), QStringLiteral("#208820"), QStringLiteral("#1E321E"), QStringLiteral("#284228"),
        QStringLiteral("#142214"), QStringLiteral("#162616"), QStringLiteral("#308830"), QStringLiteral("#1E301E"), QStringLiteral("#143014"), QStringLiteral("#182018"),
        QStringLiteral("#80FF80"), QStringLiteral("#FF6060"));

    p[QStringLiteral("win95")] = palette(QStringLiteral("win95"), QStringLiteral("#C0C0C0"), QStringLiteral("#D4D0C8"), QStringLiteral("#FFFFFF"), QStringLiteral("#808080"),
        QStringLiteral("#000000"), QStringLiteral("#444444"), QStringLiteral("#000080"), QStringLiteral("#000060"), QStringLiteral("#C0C0C0"), QStringLiteral("#D4D0C8"),
        QStringLiteral("#B0B0B0"), QStringLiteral("#FFFFFF"), QStringLiteral("#000080"), QStringLiteral("#000080"), QStringLiteral("#E0E8FF"), QStringLiteral("#F0F0F0"),
        QStringLiteral("#008000"), QStringLiteral("#FF0000"));

    p[QStringLiteral("aurora")] = palette(QStringLiteral("aurora"), QStringLiteral("#1a1a2e"), QStringLiteral("#16213e"), QStringLiteral("#0f3460"), QStringLiteral("#533483"),
        QStringLiteral("#e0e0ff"), QStringLiteral("#8888bb"), QStringLiteral("#e94560"), QStringLiteral("#c73652"), QStringLiteral("#1f2a4a"), QStringLiteral("#2a3a6a"),
        QStringLiteral("#0f1f3a"), QStringLiteral("#1f2a4a"), QStringLiteral("#c73652"), QStringLiteral("#16213e"), QStringLiteral("#e9456022"), QStringLiteral("#0f346022"),
        QStringLiteral("#2ECC71"), QStringLiteral("#E74C3C"));

    p[QStringLiteral("sunset")] = palette(QStringLiteral("sunset"), QStringLiteral("#1a0a0a"), QStringLiteral("#2d1515"), QStringLiteral("#3d2020"), QStringLiteral("#7a3030"),
        QStringLiteral("#ffd0c0"), QStringLiteral("#b07060"), QStringLiteral("#ff6b35"), QStringLiteral("#e55a25"), QStringLiteral("#3d2020"), QStringLiteral("#5a3030"),
        QStringLiteral("#2d1515"), QStringLiteral("#3d2020"), QStringLiteral("#e55a25"), QStringLiteral("#2d1515"), QStringLiteral("#ff6b3530"), QStringLiteral("#3d202040"),
        QStringLiteral("#2ECC71"), QStringLiteral("#E74C3C"));

    p[QStringLiteral("ocean")] = palette(QStringLiteral("ocean"), QStringLiteral("#020f1a"), QStringLiteral("#041828"), QStringLiteral("#062038"), QStringLiteral("#0e4d6e"),
        QStringLiteral("#c0e8ff"), QStringLiteral("#5090b0"), QStringLiteral("#00b4d8"), QStringLiteral("#0096b4"), QStringLiteral("#062038"), QStringLiteral("#0a3050"),
        QStringLiteral("#041828"), QStringLiteral("#062038"), QStringLiteral("#0096b4"), QStringLiteral("#041828"), QStringLiteral("#00b4d830"), QStringLiteral("#06203840"),
        QStringLiteral("#2ECC71"), QStringLiteral("#E74C3C"));

    p[QStringLiteral("neon")] = palette(QStringLiteral("neon"), QStringLiteral("#0a0a0a"), QStringLiteral("#111111"), QStringLiteral("#1a1a1a"), QStringLiteral("#333333"),
        QStringLiteral("#f0f0f0"), QStringLiteral("#888888"), QStringLiteral("#00ff88"), QStringLiteral("#00cc66"), QStringLiteral("#1a1a1a"), QStringLiteral("#222222"),
        QStringLiteral("#111111"), QStringLiteral("#1a1a1a"), QStringLiteral("#00cc66"), QStringLiteral("#111111"), QStringLiteral("#00ff8825"), QStringLiteral("#1a1a1a80"),
        QStringLiteral("#2ECC71"), QStringLiteral("#E74C3C"));

    p[QStringLiteral("sakura")] = palette(QStringLiteral("sakura"), QStringLiteral("#1a0a12"), QStringLiteral("#280f1e"), QStringLiteral("#38182c"), QStringLiteral("#6a3050"),
        QStringLiteral("#ffd0e8"), QStringLiteral("#b07090"), QStringLiteral("#ff6eb4"), QStringLiteral("#e050a0"), QStringLiteral("#38182c"), QStringLiteral("#502040"),
        QStringLiteral("#280f1e"), QStringLiteral("#38182c"), QStringLiteral("#e050a0"), QStringLiteral("#280f1e"), QStringLiteral("#ff6eb430"), QStringLiteral("#38182c40"),
        QStringLiteral("#2ECC71"), QStringLiteral("#E74C3C"));

    return p;
}

ThemeManager::ThemeManager(QObject *parent) : QObject(parent)
{
    m_palettes = buildPalettes();
    m_current = QStringLiteral("dark");
    loadSavedTheme();
}

void ThemeManager::setTheme(const QString &name)
{
    if (!m_palettes.contains(name) || m_current == name)
        return;
    m_current = name;
    saveTheme();
    Q_EMIT themeChanged();
}

QVariantMap ThemeManager::colors() const
{
    return m_palettes.value(m_current, m_palettes.value(QStringLiteral("dark")));
}

QStringList ThemeManager::availableThemes() const
{
    return m_palettes.keys();
}

QString ThemeManager::themeLabel(const QString &name) const
{
    // Gettext uses the English text as the key, so a name assembled at
    // runtime cannot be looked up and every theme has to be spelled out.
    // The context matters too: "Dark" naming a theme and "Dark" anywhere
    // else are one word in English and two in most other languages.
    if (name == QLatin1StringView("aurora"))
        return i18nc("theme name", "Aurora");
    if (name == QLatin1StringView("dark"))
        return i18nc("theme name", "Dark");
    if (name == QLatin1StringView("dark_blue"))
        return i18nc("theme name", "Dark Blue");
    if (name == QLatin1StringView("dark_red"))
        return i18nc("theme name", "Dark Red");
    if (name == QLatin1StringView("forest"))
        return i18nc("theme name", "Forest");
    if (name == QLatin1StringView("gray"))
        return i18nc("theme name", "Gray");
    if (name == QLatin1StringView("light"))
        return i18nc("theme name", "Light");
    if (name == QLatin1StringView("midnight"))
        return i18nc("theme name", "Midnight");
    if (name == QLatin1StringView("neon"))
        return i18nc("theme name", "Neon");
    if (name == QLatin1StringView("ocean"))
        return i18nc("theme name", "Ocean");
    if (name == QLatin1StringView("sakura"))
        return i18nc("theme name", "Sakura");
    if (name == QLatin1StringView("sunset"))
        return i18nc("theme name", "Sunset");
    if (name == QLatin1StringView("win95"))
        return i18nc("theme name", "Windows 95");

    return name;
}

QString ThemeManager::settingsFilePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/theme.txt");
}

void ThemeManager::loadSavedTheme()
{
    QFile f(settingsFilePath());
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString saved = QString::fromUtf8(f.readAll()).trimmed();
        f.close();
        if (m_palettes.contains(saved))
            m_current = saved;
    }
}

void ThemeManager::saveTheme()
{
    // atomic, so a crash while saving cannot leave a half-written theme name
    // that loadSavedTheme() then throws away
    QSaveFile f(settingsFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(KOUTNET_LOG_APP) << "save failed:" << f.fileName() << f.errorString();
        return;
    }
    {
        QTextStream out(&f);
        out << m_current;
    }
    if (!f.commit())
        qCWarning(KOUTNET_LOG_APP) << "commit failed:" << f.fileName() << f.errorString();
}
