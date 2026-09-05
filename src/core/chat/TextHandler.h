// SPDX-FileCopyrightText: 2023 James Graham <james.h.graham@protonmail.com>
// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
//
// Adapted from NeoChat's src/libneochat/texthandler.cpp/.h, together with the
// TextRegex constants from their src/libneochat/utils.h, which is the same
// author under the same licence.
//
// What is ported is the part of upstream that has nothing to do with Matrix:
// escapeHtml, unescapeHtml, linkifyUrls and processWithinHTML, which between
// them do link and email detection that will not fire inside a code span, and
// paired inline syntax that will not fire inside a link or a code span either.
// Those two guards are the whole difference between this and a naive regex
// pass, and they are upstream's.
//
// What is NOT ported: handleSendText, handleRecieveRichText, textComponents and
// the tokeniser under them. Those want cmark for markdown, Quotient for the
// event being rendered and NeoChat's Blocks framework for the output, and a
// KOutNet message is a plain string with none of that behind it. Rebuilding
// them would be invention rather than a port. toRichText() below is the local
// entry point that takes their place.
//
// The file keeps upstream's u"..."_s literal style throughout rather than
// switching to QStringLiteral halfway down, so the ported functions stay
// comparable line for line with the ones they came from.
#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Turns the plain string a message actually holds into something a rich text
// item can draw: links, code, emphasis, spoilers and mentions.
//
// A singleton with no state of its own. Colours are passed in from QML rather
// than read from a PlatformTheme in here, because the delegate already knows
// which theme chain it is drawing under and this does not.
class TextHandler : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit TextHandler(QObject *parent = nullptr);

    static TextHandler *create(QQmlEngine *, QJSEngine *)
    {
        return new TextHandler;
    }

    // The one call the timeline makes. options is a plain map so the delegate
    // can hand over the theme colours it already has:
    //   linkColor, codeBackground, spoilerBackground, mentionColor (colour names)
    //   mentionName    - highlighted wherever it appears as a word
    //   spoilerRevealed - draws spoilers as ordinary text when true
    Q_INVOKABLE QString toRichText(const QString &plain, const QVariantMap &options = {}) const;

    // The contents of every fenced block in the message, in order, undecorated.
    // What the "copy code" action on a message copies.
    Q_INVOKABLE QStringList codeBlocks(const QString &plain) const;

    // Ported from upstream as they stand.
    Q_INVOKABLE static QString escapeHtml(QString stringIn);
    Q_INVOKABLE static QString unescapeHtml(QString stringIn);
    static QString linkifyUrls(QString stringIn, int elideAt);
    static void processWithinHTML(QString &buffer, const QString &syntax, const QString &beginTag, const QString &endTag);

private:
    // Local: inline `code` spans, and the mention highlight.
    static QString markInlineCode(const QString &stringIn);
    static QString highlightMentions(QString stringIn, const QString &name, const QString &colour);

    // One text segment - everything that is not a fenced block - all the way
    // through the pipeline.
    QString renderSegment(QString segment, const QVariantMap &options) const;
};
