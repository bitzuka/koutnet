// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "MatrixLoginUtils.h"

#include <QRegularExpression>
#include <QStringList>
#include <QUrlQuery>

namespace koutnet::matrix
{
QUrl ssoRedirectUrl(const QUrl &homeserver, const QUrl &redirectUrl)
{
    // The homeserver is taken as given: only the path and the one query
    // parameter are ours, so a homeserver the user typed with a trailing slash
    // or a path of its own still lands on the redirect endpoint.
    QUrl url = homeserver;
    url.setPath(QStringLiteral("/_matrix/client/r0/login/sso/redirect"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("redirectUrl"), redirectUrl.toString());
    url.setQuery(query);
    return url;
}

bool looksLikeRecoveryKey(const QString &candidate)
{
    if (candidate.isEmpty())
        return false;

    // Twelve groups, four characters each, dashes between - no other shape is a
    // valid recovery key, so a passphrase that happens to contain a dash is not
    // mistaken for one. The alphabet is base58 without the four characters a
    // human confuses: 0, O, I and l.
    const QStringList groups = candidate.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    if (groups.size() != 12)
        return false;

    static const QRegularExpression group(QStringLiteral("^[1-9A-HJ-NP-Za-km-z]{4}$"));
    for (const QString &g : groups) {
        if (!group.match(g).hasMatch())
            return false;
    }
    return true;
}
}
