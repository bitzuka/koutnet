// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "ThemeManager.h"

#include <KLocalizedString>

#include "koutnet_app_debug.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

namespace
{

// One named field per colour rather than nineteen positional QStrings. The
// table below is wide enough that a value one slot out reads as plausible, and
// that is how a dark theme ends up drawing dark text.
struct Palette {
    QString bg;
    QString bg2;
    QString bg3;
    QString border;
    QString text;
    QString textDim;
    QString accent;
    QString accent2;
    QString btnBg;
    QString btnHover;
    QString btnPress;
    QString itemBg;
    QString itemSel;
    QString headerBg;
    QString msgOwn;
    QString msgOther;
    QString online;
    QString offline;
};

QVariantMap toMap(const Palette &p)
{
    return QVariantMap{
        {QStringLiteral("bg"), p.bg},
        {QStringLiteral("bg2"), p.bg2},
        {QStringLiteral("bg3"), p.bg3},
        {QStringLiteral("border"), p.border},
        {QStringLiteral("text"), p.text},
        {QStringLiteral("text_dim"), p.textDim},
        {QStringLiteral("accent"), p.accent},
        {QStringLiteral("accent2"), p.accent2},
        {QStringLiteral("btn_bg"), p.btnBg},
        {QStringLiteral("btn_hover"), p.btnHover},
        {QStringLiteral("btn_press"), p.btnPress},
        {QStringLiteral("item_bg"), p.itemBg},
        {QStringLiteral("item_sel"), p.itemSel},
        {QStringLiteral("header_bg"), p.headerBg},
        {QStringLiteral("msg_own"), p.msgOwn},
        {QStringLiteral("msg_other"), p.msgOther},
        {QStringLiteral("online"), p.online},
        {QStringLiteral("offline"), p.offline},
    };
}

} // namespace

QHash<QString, QVariantMap> ThemeManager::buildPalettes()
{
    QHash<QString, QVariantMap> p;

    p[QStringLiteral("dark")] = toMap({
        .bg = QStringLiteral("#323232"),
        .bg2 = QStringLiteral("#282828"),
        .bg3 = QStringLiteral("#1E1E1E"),
        .border = QStringLiteral("#484848"),
        .text = QStringLiteral("#E0E0E0"),
        .textDim = QStringLiteral("#909090"),
        .accent = QStringLiteral("#0078D4"),
        .accent2 = QStringLiteral("#005A9E"),
        .btnBg = QStringLiteral("#4A4A4A"),
        .btnHover = QStringLiteral("#5A5A5A"),
        .btnPress = QStringLiteral("#3A3A3A"),
        .itemBg = QStringLiteral("#2E2E2E"),
        .itemSel = QStringLiteral("#0063B1"),
        .headerBg = QStringLiteral("#3C3C3C"),
        .msgOwn = QStringLiteral("#1A3A5C"),
        .msgOther = QStringLiteral("#383838"),
        .online = QStringLiteral("#2ECC71"),
        .offline = QStringLiteral("#E74C3C"),
    });

    p[QStringLiteral("light")] = toMap({
        .bg = QStringLiteral("#F0F0F0"),
        .bg2 = QStringLiteral("#FAFAFA"),
        .bg3 = QStringLiteral("#FFFFFF"),
        .border = QStringLiteral("#C8C8C8"),
        .text = QStringLiteral("#1A1A1A"),
        .textDim = QStringLiteral("#707070"),
        .accent = QStringLiteral("#0078D4"),
        .accent2 = QStringLiteral("#005A9E"),
        .btnBg = QStringLiteral("#E0E0E0"),
        .btnHover = QStringLiteral("#D0D0D0"),
        .btnPress = QStringLiteral("#C0C0C0"),
        .itemBg = QStringLiteral("#F8F8F8"),
        .itemSel = QStringLiteral("#0078D4"),
        .headerBg = QStringLiteral("#E8E8E8"),
        .msgOwn = QStringLiteral("#C8E6FA"),
        .msgOther = QStringLiteral("#EEEEEE"),
        .online = QStringLiteral("#27AE60"),
        .offline = QStringLiteral("#E74C3C"),
    });

    p[QStringLiteral("dark_blue")] = toMap({
        .bg = QStringLiteral("#1A2540"),
        .bg2 = QStringLiteral("#131B30"),
        .bg3 = QStringLiteral("#0D1220"),
        .border = QStringLiteral("#2A3858"),
        .text = QStringLiteral("#C8D8FF"),
        .textDim = QStringLiteral("#6878A8"),
        .accent = QStringLiteral("#4080FF"),
        .accent2 = QStringLiteral("#2060DD"),
        .btnBg = QStringLiteral("#2A3A60"),
        .btnHover = QStringLiteral("#3A4A70"),
        .btnPress = QStringLiteral("#1A2A50"),
        .itemBg = QStringLiteral("#182038"),
        .itemSel = QStringLiteral("#2060CC"),
        .headerBg = QStringLiteral("#202848"),
        .msgOwn = QStringLiteral("#1A3060"),
        .msgOther = QStringLiteral("#1E2840"),
        .online = QStringLiteral("#00E676"),
        .offline = QStringLiteral("#FF5252"),
    });

    p[QStringLiteral("dark_red")] = toMap({
        .bg = QStringLiteral("#2A1010"),
        .bg2 = QStringLiteral("#200808"),
        .bg3 = QStringLiteral("#160404"),
        .border = QStringLiteral("#4A2020"),
        .text = QStringLiteral("#FFD0D0"),
        .textDim = QStringLiteral("#A06060"),
        .accent = QStringLiteral("#CC2020"),
        .accent2 = QStringLiteral("#AA1010"),
        .btnBg = QStringLiteral("#4A1818"),
        .btnHover = QStringLiteral("#5A2828"),
        .btnPress = QStringLiteral("#3A0808"),
        .itemBg = QStringLiteral("#281010"),
        .itemSel = QStringLiteral("#AA0000"),
        .headerBg = QStringLiteral("#381818"),
        .msgOwn = QStringLiteral("#3A1010"),
        .msgOther = QStringLiteral("#2A1818"),
        .online = QStringLiteral("#00E676"),
        .offline = QStringLiteral("#FF5252"),
    });

    p[QStringLiteral("gray")] = toMap({
        .bg = QStringLiteral("#606060"),
        .bg2 = QStringLiteral("#505050"),
        .bg3 = QStringLiteral("#404040"),
        .border = QStringLiteral("#707070"),
        .text = QStringLiteral("#F0F0F0"),
        .textDim = QStringLiteral("#B0B0B0"),
        .accent = QStringLiteral("#909090"),
        .accent2 = QStringLiteral("#707070"),
        .btnBg = QStringLiteral("#707070"),
        .btnHover = QStringLiteral("#808080"),
        .btnPress = QStringLiteral("#606060"),
        .itemBg = QStringLiteral("#585858"),
        .itemSel = QStringLiteral("#888888"),
        .headerBg = QStringLiteral("#686868"),
        .msgOwn = QStringLiteral("#5A5A7A"),
        .msgOther = QStringLiteral("#484848"),
        .online = QStringLiteral("#90EE90"),
        .offline = QStringLiteral("#FF9090"),
    });

    p[QStringLiteral("midnight")] = toMap({
        .bg = QStringLiteral("#0D0D1A"),
        .bg2 = QStringLiteral("#080810"),
        .bg3 = QStringLiteral("#040408"),
        .border = QStringLiteral("#1A1A3A"),
        .text = QStringLiteral("#B0B8FF"),
        .textDim = QStringLiteral("#5058A0"),
        .accent = QStringLiteral("#6040FF"),
        .accent2 = QStringLiteral("#4020DD"),
        .btnBg = QStringLiteral("#151528"),
        .btnHover = QStringLiteral("#202040"),
        .btnPress = QStringLiteral("#0A0A18"),
        .itemBg = QStringLiteral("#0E0E20"),
        .itemSel = QStringLiteral("#4030CC"),
        .headerBg = QStringLiteral("#121224"),
        .msgOwn = QStringLiteral("#120A30"),
        .msgOther = QStringLiteral("#0E0E22"),
        .online = QStringLiteral("#00FFB0"),
        .offline = QStringLiteral("#FF4060"),
    });

    p[QStringLiteral("forest")] = toMap({
        .bg = QStringLiteral("#1A2A1A"),
        .bg2 = QStringLiteral("#122012"),
        .bg3 = QStringLiteral("#0A160A"),
        .border = QStringLiteral("#2A3E2A"),
        .text = QStringLiteral("#C8EEC8"),
        .textDim = QStringLiteral("#6A8A6A"),
        .accent = QStringLiteral("#40AA40"),
        .accent2 = QStringLiteral("#208820"),
        .btnBg = QStringLiteral("#1E321E"),
        .btnHover = QStringLiteral("#284228"),
        .btnPress = QStringLiteral("#142214"),
        .itemBg = QStringLiteral("#162616"),
        .itemSel = QStringLiteral("#308830"),
        .headerBg = QStringLiteral("#1E301E"),
        .msgOwn = QStringLiteral("#143014"),
        .msgOther = QStringLiteral("#182018"),
        .online = QStringLiteral("#80FF80"),
        .offline = QStringLiteral("#FF6060"),
    });

    p[QStringLiteral("win95")] = toMap({
        .bg = QStringLiteral("#C0C0C0"),
        .bg2 = QStringLiteral("#D4D0C8"),
        .bg3 = QStringLiteral("#FFFFFF"),
        .border = QStringLiteral("#808080"),
        .text = QStringLiteral("#000000"),
        .textDim = QStringLiteral("#444444"),
        .accent = QStringLiteral("#000080"),
        .accent2 = QStringLiteral("#000060"),
        .btnBg = QStringLiteral("#C0C0C0"),
        .btnHover = QStringLiteral("#D4D0C8"),
        .btnPress = QStringLiteral("#B0B0B0"),
        .itemBg = QStringLiteral("#FFFFFF"),
        .itemSel = QStringLiteral("#000080"),
        .headerBg = QStringLiteral("#000080"),
        .msgOwn = QStringLiteral("#E0E8FF"),
        .msgOther = QStringLiteral("#F0F0F0"),
        .online = QStringLiteral("#008000"),
        .offline = QStringLiteral("#FF0000"),
    });

    p[QStringLiteral("aurora")] = toMap({
        .bg = QStringLiteral("#1a1a2e"),
        .bg2 = QStringLiteral("#16213e"),
        .bg3 = QStringLiteral("#0f3460"),
        .border = QStringLiteral("#533483"),
        .text = QStringLiteral("#e0e0ff"),
        .textDim = QStringLiteral("#8888bb"),
        .accent = QStringLiteral("#e94560"),
        .accent2 = QStringLiteral("#c73652"),
        .btnBg = QStringLiteral("#1f2a4a"),
        .btnHover = QStringLiteral("#2a3a6a"),
        .btnPress = QStringLiteral("#0f1f3a"),
        .itemBg = QStringLiteral("#1f2a4a"),
        .itemSel = QStringLiteral("#c73652"),
        .headerBg = QStringLiteral("#16213e"),
        .msgOwn = QStringLiteral("#e9456022"),
        .msgOther = QStringLiteral("#0f346022"),
        .online = QStringLiteral("#2ECC71"),
        .offline = QStringLiteral("#E74C3C"),
    });

    p[QStringLiteral("sunset")] = toMap({
        .bg = QStringLiteral("#1a0a0a"),
        .bg2 = QStringLiteral("#2d1515"),
        .bg3 = QStringLiteral("#3d2020"),
        .border = QStringLiteral("#7a3030"),
        .text = QStringLiteral("#ffd0c0"),
        .textDim = QStringLiteral("#b07060"),
        .accent = QStringLiteral("#ff6b35"),
        .accent2 = QStringLiteral("#e55a25"),
        .btnBg = QStringLiteral("#3d2020"),
        .btnHover = QStringLiteral("#5a3030"),
        .btnPress = QStringLiteral("#2d1515"),
        .itemBg = QStringLiteral("#3d2020"),
        .itemSel = QStringLiteral("#e55a25"),
        .headerBg = QStringLiteral("#2d1515"),
        .msgOwn = QStringLiteral("#ff6b3530"),
        .msgOther = QStringLiteral("#3d202040"),
        .online = QStringLiteral("#2ECC71"),
        .offline = QStringLiteral("#E74C3C"),
    });

    p[QStringLiteral("ocean")] = toMap({
        .bg = QStringLiteral("#020f1a"),
        .bg2 = QStringLiteral("#041828"),
        .bg3 = QStringLiteral("#062038"),
        .border = QStringLiteral("#0e4d6e"),
        .text = QStringLiteral("#c0e8ff"),
        .textDim = QStringLiteral("#5090b0"),
        .accent = QStringLiteral("#00b4d8"),
        .accent2 = QStringLiteral("#0096b4"),
        .btnBg = QStringLiteral("#062038"),
        .btnHover = QStringLiteral("#0a3050"),
        .btnPress = QStringLiteral("#041828"),
        .itemBg = QStringLiteral("#062038"),
        .itemSel = QStringLiteral("#0096b4"),
        .headerBg = QStringLiteral("#041828"),
        .msgOwn = QStringLiteral("#00b4d830"),
        .msgOther = QStringLiteral("#06203840"),
        .online = QStringLiteral("#2ECC71"),
        .offline = QStringLiteral("#E74C3C"),
    });

    p[QStringLiteral("neon")] = toMap({
        .bg = QStringLiteral("#0a0a0a"),
        .bg2 = QStringLiteral("#111111"),
        .bg3 = QStringLiteral("#1a1a1a"),
        .border = QStringLiteral("#333333"),
        .text = QStringLiteral("#f0f0f0"),
        .textDim = QStringLiteral("#888888"),
        .accent = QStringLiteral("#00ff88"),
        .accent2 = QStringLiteral("#00cc66"),
        .btnBg = QStringLiteral("#1a1a1a"),
        .btnHover = QStringLiteral("#222222"),
        .btnPress = QStringLiteral("#111111"),
        .itemBg = QStringLiteral("#1a1a1a"),
        .itemSel = QStringLiteral("#00cc66"),
        .headerBg = QStringLiteral("#111111"),
        .msgOwn = QStringLiteral("#00ff8825"),
        .msgOther = QStringLiteral("#1a1a1a80"),
        .online = QStringLiteral("#2ECC71"),
        .offline = QStringLiteral("#E74C3C"),
    });

    p[QStringLiteral("sakura")] = toMap({
        .bg = QStringLiteral("#1a0a12"),
        .bg2 = QStringLiteral("#280f1e"),
        .bg3 = QStringLiteral("#38182c"),
        .border = QStringLiteral("#6a3050"),
        .text = QStringLiteral("#ffd0e8"),
        .textDim = QStringLiteral("#b07090"),
        .accent = QStringLiteral("#ff6eb4"),
        .accent2 = QStringLiteral("#e050a0"),
        .btnBg = QStringLiteral("#38182c"),
        .btnHover = QStringLiteral("#502040"),
        .btnPress = QStringLiteral("#280f1e"),
        .itemBg = QStringLiteral("#38182c"),
        .itemSel = QStringLiteral("#e050a0"),
        .headerBg = QStringLiteral("#280f1e"),
        .msgOwn = QStringLiteral("#ff6eb430"),
        .msgOther = QStringLiteral("#38182c40"),
        .online = QStringLiteral("#2ECC71"),
        .offline = QStringLiteral("#E74C3C"),
    });

    return p;
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
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
