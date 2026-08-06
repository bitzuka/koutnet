// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

// Dark, light, or whatever the desktop is set to. This replaced thirteen
// hand-written palettes: the colours belong to the Plasma scheme, which
// Kirigami.Theme already reads.
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
    static QString schemeIdFor(Mode mode);

    Mode m_mode = FollowSystem;
};
