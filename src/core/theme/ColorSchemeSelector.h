// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

// Dark, light, or whatever the desktop is set to. This replaced a table of
// thirteen hand-written palettes: the colours themselves belong to the Plasma
// colour scheme, and Kirigami.Theme already reads them, so the only decision
// left to the application is which of the three the user wants.
//
//   ColorSchemeSelector.mode = ColorSchemeSelector.Dark
class ColorSchemeSelector : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)

public:
    enum Mode {
        FollowSystem,
        Light,
        Dark,
    };
    Q_ENUM(Mode)

    explicit ColorSchemeSelector(QObject *parent = nullptr);

    static ColorSchemeSelector *create(QQmlEngine *, QJSEngine *)
    {
        return new ColorSchemeSelector;
    }

    Mode mode() const
    {
        return m_mode;
    }
    void setMode(Mode mode);

Q_SIGNALS:
    void modeChanged();

private:
    // Scheme id for a mode, empty for FollowSystem, which is how
    // KColorSchemeManager spells "stop overriding".
    static QString schemeIdFor(Mode mode);

    Mode m_mode = FollowSystem;
};
