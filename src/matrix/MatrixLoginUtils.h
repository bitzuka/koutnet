// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// The Matrix sign-in helpers that decide nothing about a server. They are kept
// free of Quotient so they can be unit-tested without a Connection and without
// the network, the same way MatrixTranslate is.
#pragma once

#include <QUrl>

namespace koutnet::matrix
{
// The SSO redirect endpoint: the homeserver sends the browser here with a
// loginToken in the query, and we hand that token to Connection::loginWithToken.
// redirectUrl is where the homeserver should send the browser afterwards; it is
// percent-encoded into the query so the whole thing stays one hop.
QUrl ssoRedirectUrl(const QUrl &homeserver, const QUrl &redirectUrl);

// A Megolm recovery key is twelve groups of four base58 characters joined by
// dashes - the form "unlock with security key" shows. A passphrase is anything
// else, and telling the two apart up front lets unlockKeyBackup try the right
// SSSS call first instead of always failing once and retrying.
bool looksLikeRecoveryKey(const QString &candidate);
}
