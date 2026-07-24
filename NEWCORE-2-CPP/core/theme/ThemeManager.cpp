#include "ThemeManager.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>

namespace {

QVariantMap palette(const char *label, const char *bg, const char *bg2, const char *bg3,
                     const char *border, const char *text, const char *textDim,
                     const char *accent, const char *accent2, const char *btnBg,
                     const char *btnHover, const char *btnPress, const char *itemBg,
                     const char *itemSel, const char *headerBg, const char *msgOwn,
                     const char *msgOther, const char *online, const char *offline)
{
    QVariantMap m;
    m["label"] = QString::fromUtf8(label);
    m["bg"] = QString::fromUtf8(bg);
    m["bg2"] = QString::fromUtf8(bg2);
    m["bg3"] = QString::fromUtf8(bg3);
    m["border"] = QString::fromUtf8(border);
    m["text"] = QString::fromUtf8(text);
    m["text_dim"] = QString::fromUtf8(textDim);
    m["accent"] = QString::fromUtf8(accent);
    m["accent2"] = QString::fromUtf8(accent2);
    m["btn_bg"] = QString::fromUtf8(btnBg);
    m["btn_hover"] = QString::fromUtf8(btnHover);
    m["btn_press"] = QString::fromUtf8(btnPress);
    m["item_bg"] = QString::fromUtf8(itemBg);
    m["item_sel"] = QString::fromUtf8(itemSel);
    m["header_bg"] = QString::fromUtf8(headerBg);
    m["msg_own"] = QString::fromUtf8(msgOwn);
    m["msg_other"] = QString::fromUtf8(msgOther);
    m["online"] = QString::fromUtf8(online);
    m["offline"] = QString::fromUtf8(offline);
    return m;
}

} // namespace

QHash<QString, QVariantMap> ThemeManager::buildPalettes()
{
    QHash<QString, QVariantMap> p;

    p["dark"] = palette("Тёмная", "#323232", "#282828", "#1E1E1E", "#484848",
        "#E0E0E0", "#909090", "#0078D4", "#005A9E", "#4A4A4A", "#5A5A5A",
        "#3A3A3A", "#2E2E2E", "#0063B1", "#3C3C3C", "#1A3A5C", "#383838",
        "#2ECC71", "#E74C3C");

    p["light"] = palette("Светлая", "#F0F0F0", "#FAFAFA", "#FFFFFF", "#C8C8C8",
        "#1A1A1A", "#707070", "#0078D4", "#005A9E", "#E0E0E0", "#D0D0D0",
        "#C0C0C0", "#F8F8F8", "#0078D4", "#E8E8E8", "#C8E6FA", "#EEEEEE",
        "#27AE60", "#E74C3C");

    p["dark_blue"] = palette("Тёмно-синяя", "#1A2540", "#131B30", "#0D1220", "#2A3858",
        "#C8D8FF", "#6878A8", "#4080FF", "#2060DD", "#2A3A60", "#3A4A70",
        "#1A2A50", "#182038", "#2060CC", "#202848", "#1A3060", "#1E2840",
        "#00E676", "#FF5252");

    p["dark_red"] = palette("Тёмно-красная", "#2A1010", "#200808", "#160404", "#4A2020",
        "#FFD0D0", "#A06060", "#CC2020", "#AA1010", "#4A1818", "#5A2828",
        "#3A0808", "#281010", "#AA0000", "#381818", "#3A1010", "#2A1818",
        "#00E676", "#FF5252");

    p["gray"] = palette("Серая", "#606060", "#505050", "#404040", "#707070",
        "#F0F0F0", "#B0B0B0", "#909090", "#707070", "#707070", "#808080",
        "#606060", "#585858", "#888888", "#686868", "#5A5A7A", "#484848",
        "#90EE90", "#FF9090");

    p["midnight"] = palette("Полночь", "#0D0D1A", "#080810", "#040408", "#1A1A3A",
        "#B0B8FF", "#5058A0", "#6040FF", "#4020DD", "#151528", "#202040",
        "#0A0A18", "#0E0E20", "#4030CC", "#121224", "#120A30", "#0E0E22",
        "#00FFB0", "#FF4060");

    p["forest"] = palette("Лес", "#1A2A1A", "#122012", "#0A160A", "#2A3E2A",
        "#C8EEC8", "#6A8A6A", "#40AA40", "#208820", "#1E321E", "#284228",
        "#142214", "#162616", "#308830", "#1E301E", "#143014", "#182018",
        "#80FF80", "#FF6060");

    p["win95"] = palette("1.7543", "#C0C0C0", "#D4D0C8", "#FFFFFF", "#808080",
        "#000000", "#444444", "#000080", "#000060", "#C0C0C0", "#D4D0C8",
        "#B0B0B0", "#FFFFFF", "#000080", "#000080", "#E0E8FF", "#F0F0F0",
        "#008000", "#FF0000");

    p["aurora"] = palette("🌌 Aurora", "#1a1a2e", "#16213e", "#0f3460", "#533483",
        "#e0e0ff", "#8888bb", "#e94560", "#c73652", "#1f2a4a", "#2a3a6a",
        "#0f1f3a", "#1f2a4a", "#c73652", "#16213e", "#e9456022", "#0f346022",
        "#2ECC71", "#E74C3C");

    p["sunset"] = palette("🌅 Sunset", "#1a0a0a", "#2d1515", "#3d2020", "#7a3030",
        "#ffd0c0", "#b07060", "#ff6b35", "#e55a25", "#3d2020", "#5a3030",
        "#2d1515", "#3d2020", "#e55a25", "#2d1515", "#ff6b3530", "#3d202040",
        "#2ECC71", "#E74C3C");

    p["ocean"] = palette("🌊 Ocean", "#020f1a", "#041828", "#062038", "#0e4d6e",
        "#c0e8ff", "#5090b0", "#00b4d8", "#0096b4", "#062038", "#0a3050",
        "#041828", "#062038", "#0096b4", "#041828", "#00b4d830", "#06203840",
        "#2ECC71", "#E74C3C");

    p["neon"] = palette("⚡ Neon", "#0a0a0a", "#111111", "#1a1a1a", "#333333",
        "#f0f0f0", "#888888", "#00ff88", "#00cc66", "#1a1a1a", "#222222",
        "#111111", "#1a1a1a", "#00cc66", "#111111", "#00ff8825", "#1a1a1a80",
        "#2ECC71", "#E74C3C");

    p["sakura"] = palette("🌸 Sakura", "#1a0a12", "#280f1e", "#38182c", "#6a3050",
        "#ffd0e8", "#b07090", "#ff6eb4", "#e050a0", "#38182c", "#502040",
        "#280f1e", "#38182c", "#e050a0", "#280f1e", "#ff6eb430", "#38182c40",
        "#2ECC71", "#E74C3C");

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
    emit themeChanged();
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
    return m_palettes.value(name).value("label").toString();
}

QString ThemeManager::settingsFilePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/theme.txt";
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
    QFile f(settingsFilePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream(&f) << m_current;
        f.close();
    }
}
