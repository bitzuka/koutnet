// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "ColorSchemeSelector.h"

#include "koutnet_app_debug.h"

#include <KColorSchemeManager>

#include <QModelIndex>

namespace
{

// The file names Breeze installs into /usr/share/color-schemes, minus the
// extension. Ids rather than names on purpose: a name is translated, so
// matching on one would stop working the moment the desktop is not in English.
QString lightSchemeId()
{
    return QStringLiteral("BreezeLight");
}

QString darkSchemeId()
{
    return QStringLiteral("BreezeDark");
}

} // namespace

ColorSchemeSelector::ColorSchemeSelector(QObject *parent)
    : QObject(parent)
{
    // KColorSchemeManager restores whatever was activated last time on its own,
    // so there is nothing to load here - only to work out which of the three
    // modes that restore landed on. Anything that is neither of the two Breeze
    // schemes reads as "the desktop decides", including a scheme the user picked
    // by hand outside this application.
    const QString active = KColorSchemeManager::instance()->activeSchemeId();
    if (active == darkSchemeId())
        m_mode = Dark;
    else if (active == lightSchemeId())
        m_mode = Light;
}

void ColorSchemeSelector::setMode(Mode mode)
{
    if (m_mode == mode)
        return;

    m_mode = mode;

    auto *manager = KColorSchemeManager::instance();
    const QString id = schemeIdFor(mode);
    // An invalid index is what activateScheme() takes for "follow the system",
    // which is right for FollowSystem and wrong-but-harmless for a Breeze scheme
    // that is not installed. Saying so beats silently ignoring the click.
    const QModelIndex index = manager->indexForSchemeId(id);
    if (!id.isEmpty() && !index.isValid())
        qCWarning(KOUTNET_LOG_APP) << "colour scheme is not installed, falling back to the system one:" << id;
    manager->activateScheme(index);

    Q_EMIT modeChanged();
}

QString ColorSchemeSelector::schemeIdFor(Mode mode)
{
    switch (mode) {
    case Light:
        return lightSchemeId();
    case Dark:
        return darkSchemeId();
    case FollowSystem:
        break;
    }
    return QString();
}
