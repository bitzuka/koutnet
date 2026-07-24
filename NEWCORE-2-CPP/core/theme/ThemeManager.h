#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantMap>
#include <QStringList>

// Ports the legacy Python THEMES palette system. Exposes the active theme's
// colors as a QVariantMap to QML, and lets the user switch between named
// palettes (persisted to disk).
//
// QML usage:
//   color: ThemeManager.colors.msg_own
//   ThemeManager.setTheme("aurora")
class ThemeManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString currentTheme READ currentTheme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY themeChanged)
    Q_PROPERTY(QStringList availableThemes READ availableThemes CONSTANT)

public:
    explicit ThemeManager(QObject *parent = nullptr);

    static ThemeManager *create(QQmlEngine *, QJSEngine *)
    {
        return new ThemeManager;
    }

    QString currentTheme() const { return m_current; }
    void setTheme(const QString &name);

    QVariantMap colors() const;
    QStringList availableThemes() const;

    Q_INVOKABLE QString themeLabel(const QString &name) const;

signals:
    void themeChanged();

private:
    static QHash<QString, QVariantMap> buildPalettes();

    QString m_current;
    QHash<QString, QVariantMap> m_palettes; // theme name -> color map
    QString settingsFilePath() const;
    void loadSavedTheme();
    void saveTheme();
};
