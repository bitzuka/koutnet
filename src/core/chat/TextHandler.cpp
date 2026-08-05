// SPDX-FileCopyrightText: 2023 James Graham <james.h.graham@protonmail.com>
// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
//
// Adapted from NeoChat's src/libneochat/texthandler.cpp and utils.h. See the
// note at the top of TextHandler.h for what is ported and what is not.

#include "TextHandler.h"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

// Ported verbatim from NeoChat's utils.h, minus the Matrix ones (mxId,
// matrixLink) and minus the magnet/matrix schemes, which mean nothing here.
//
// The (*SKIP)(*F) prefix is what stops the pass from linkifying the inside of an
// anchor it has already written.
namespace TextRegex
{
static const QRegularExpression plainUrl(
    uR"(<a.*?<\/a>(*SKIP)(*F)|\b((www\.(?!\.)(?!(\w|\.|-)+@)|(https?|ftp):(//)?\w)(&(?![lg]t;)|[^&\s<>"])+(&(?![lg]t;)|[^?&!,.\s<>"\]):])))"_s,
    QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
static const QRegularExpression emailAddress(uR"(<a.*?<\/a>(*SKIP)(*F)|\b(mailto:)?((\w|\.|-)+@(\w|\.|-)+\.\w+\b))"_s,
                                             QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
}

TextHandler::TextHandler(QObject *parent)
    : QObject(parent)
{
}

QString TextHandler::escapeHtml(QString stringIn)
{
    // Upstream leaves & alone because cmark has already escaped it by the time
    // the string reaches them. Nothing escapes it on the way here, so it goes
    // first - after the other two it would eat the entities they just wrote.
    stringIn.replace(u'&', u"&amp;"_s);
    stringIn.replace(u'<', u"&lt;"_s);
    stringIn.replace(u'>', u"&gt;"_s);
    return stringIn;
}

QString TextHandler::unescapeHtml(QString stringIn)
{
    // For those situations where brackets in code block get double escaped
    stringIn.replace(u"&amp;lt;"_s, u"<"_s);
    stringIn.replace(u"&amp;gt;"_s, u">"_s);
    stringIn.replace(u"&lt;"_s, u"<"_s);
    stringIn.replace(u"&gt;"_s, u">"_s);
    stringIn.replace(u"&amp;"_s, u"&"_s);
    stringIn.replace(u"&quot;"_s, u"\""_s);
    stringIn.replace(u"&#x27;"_s, u"'"_s);
    stringIn.replace(u"&nbsp;"_s, u" "_s);
    return stringIn;
}

// Upstream's loop, with the Matrix id pass dropped and one thing added: the
// anchor's visible text is shortened when the URL is long, while href keeps the
// whole thing. A pasted tracking URL is otherwise three lines of the timeline.
QString TextHandler::linkifyUrls(QString stringIn, int elideAt)
{
    const auto elide = [elideAt](const QString &url) {
        if (elideAt <= 0 || url.length() <= elideAt) {
            return url;
        }
        // Cut in the middle rather than at the end: the host is the part that
        // says where the link goes, and it is at the front.
        const int head = elideAt * 2 / 3;
        const int tail = elideAt - head;
        return url.left(head) + QString::fromUtf8("\xe2\x80\xa6") + url.right(tail);
    };

    QRegularExpressionMatch match;
    int start = 0;
    for (int index = 0; index != -1; index = stringIn.indexOf(TextRegex::plainUrl, start, &match)) {
        int skip = 0;
        if (match.captured(0).size() > 0) {
            if (stringIn.left(index).count(u"<code>"_s) == stringIn.left(index).count(u"</code>"_s)) {
                const auto target = match.captured(1);
                // A bare www. link is not a URL until it has a scheme, or the
                // text item will resolve it against the document and open
                // nothing at all.
                const auto href = target.startsWith(u"www."_s, Qt::CaseInsensitive) ? u"https://"_s + target : target;
                auto replacement = u"<a href=\"%1\">%2</a>"_s.arg(href, elide(target));
                stringIn = stringIn.replace(index, match.captured(0).size(), replacement);
                skip = replacement.length();
            } else {
                skip = match.captured().length();
            }
        }
        start = index + skip;
        match = {};
    }
    start = 0;
    match = {};
    for (int index = 0; index != -1; index = stringIn.indexOf(TextRegex::emailAddress, start, &match)) {
        int skip = 0;
        if (match.captured(0).size() > 0) {
            if (stringIn.left(index).count(u"<code>"_s) == stringIn.left(index).count(u"</code>"_s)) {
                auto replacement = u"<a href=\"mailto:%1\">%1</a>"_s.arg(match.captured(2));
                stringIn = stringIn.replace(index, match.captured(0).size(), replacement);
                skip = replacement.length();
            } else {
                skip = match.captured().length();
            }
        }
        start = index + skip;
        match = {};
    }

    return stringIn;
}

// Ported from upstream unchanged. Pairs up an inline syntax, skipping anything
// that falls inside a code span, inside an anchor, or inside a bare URL.
void TextHandler::processWithinHTML(QString &buffer, const QString &syntax, const QString &beginTag, const QString &endTag)
{
    qsizetype beginCodeBlockTag = buffer.indexOf(u"<code>"_s);
    qsizetype endCodeBlockTag = buffer.indexOf(u"</code>"_s, beginCodeBlockTag + 1);
    qsizetype beginlinkBlockTag = buffer.indexOf(u"<a href"_s);
    qsizetype endLinkBlockTag = buffer.indexOf(u"</a>"_s, beginCodeBlockTag + 1);
    QRegularExpressionMatch plainLinkMatch;
    qsizetype plainLink = buffer.indexOf(TextRegex::plainUrl, 0, &plainLinkMatch);

    // Index to search from
    qsizetype lastPos = 0;
    while (true) {
        const qsizetype pos = buffer.indexOf(syntax, lastPos);
        if (pos == -1) {
            break;
        }

        // If we're inside a code block, ignore and move the search past this code block
        const bool validCodeBlock = beginCodeBlockTag != -1 && endCodeBlockTag != -1;
        if (validCodeBlock && pos > beginCodeBlockTag && pos < endCodeBlockTag) {
            lastPos = endCodeBlockTag + 7;

            // since we moved past this code block, make sure to update the indices for the next one
            beginCodeBlockTag = buffer.indexOf(u"<code>"_s, lastPos + 1);
            endCodeBlockTag = buffer.indexOf(u"</code>"_s, beginCodeBlockTag + 1);

            continue;
        }
        const bool validLinkBlock = beginlinkBlockTag != -1 && endLinkBlockTag != -1;
        if (validLinkBlock && pos > beginlinkBlockTag && pos < endLinkBlockTag) {
            lastPos = endLinkBlockTag + 4;

            beginlinkBlockTag = buffer.indexOf(u"<a href"_s, lastPos + 1);
            endLinkBlockTag = buffer.indexOf(u"</a>"_s, beginlinkBlockTag + 1);

            continue;
        }
        if (plainLink != -1 && pos > plainLink && pos < plainLink + plainLinkMatch.capturedLength()) {
            lastPos = plainLink + plainLinkMatch.capturedLength();

            plainLink = buffer.indexOf(TextRegex::plainUrl, lastPos, &plainLinkMatch);
            continue;
        }

        const auto findNextPos = [&pos, &buffer, &syntax] {
            return buffer.indexOf(syntax, pos + 1);
        };

        // Find the next valid closing character
        qsizetype nextPos = findNextPos();
        if (nextPos == -1) {
            break;
        }

        // Replace the beginning syntax
        buffer.replace(pos, syntax.length(), beginTag);

        // Update positions and re-search since the underlying text buffer changed
        nextPos = findNextPos();

        // Now replace the end syntax
        buffer.replace(nextPos, syntax.length(), endTag);

        // If we have begun checking spoilers past our current code block, make sure we're in the next one (if it exists)
        if (nextPos > endCodeBlockTag) {
            beginCodeBlockTag = buffer.indexOf(u"<code>"_s, nextPos + 1);
            endCodeBlockTag = buffer.indexOf(u"</code>"_s, beginCodeBlockTag + 1);
        }

        // Move the search pointer past this point.
        // Not technically needed in most cases since we replaced the original tag, but needed for code blocks
        // which still have the characters.
        lastPos = nextPos + syntax.length();
    }
}

// Local. Single backticks, done before anything else touches the segment so the
// <code> markers are in place for every guard above to see.
QString TextHandler::markInlineCode(const QString &stringIn)
{
    QString buffer = stringIn;
    processWithinHTML(buffer, u"`"_s, u"<code>"_s, u"</code>"_s);
    return buffer;
}

// Local. Whole-word match on the name, so "sam" does not light up "sample".
QString TextHandler::highlightMentions(QString stringIn, const QString &name, const QString &colour)
{
    if (name.isEmpty()) {
        return stringIn;
    }
    // Captured rather than matched bare so the replacement can use \1: the
    // whole-match backreference is the one that differs between regex engines.
    const QRegularExpression mention(u"(\\b"_s + QRegularExpression::escape(name) + u"\\b)"_s, QRegularExpression::CaseInsensitiveOption);
    return stringIn.replace(mention, u"<span style=\"color:%1; font-weight:bold;\">\\1</span>"_s.arg(colour));
}

QString TextHandler::renderSegment(QString segment, const QVariantMap &options) const
{
    const auto colour = [&options](const char *key, const QString &fallback) {
        const auto v = options.value(QString::fromUtf8(key)).toString();
        return v.isEmpty() ? fallback : v;
    };

    segment = escapeHtml(std::move(segment));
    segment = markInlineCode(segment);
    segment = linkifyUrls(std::move(segment), options.value(u"elideLinksAt"_s, 48).toInt());

    // Order matters: the two-character syntaxes go first, or the single-asterisk
    // pass would take the first character of a ** pair and leave the second.
    processWithinHTML(segment, u"**"_s, u"<b>"_s, u"</b>"_s);
    processWithinHTML(segment, u"~~"_s, u"<s>"_s, u"</s>"_s);

    const bool revealed = options.value(u"spoilerRevealed"_s, false).toBool();
    const QString spoilerBg = colour("spoilerBackground", u"#808080"_s);
    // Hidden is the text colour painted on itself, which is a solid block the
    // width of the words behind it. Revealed is left completely unstyled rather
    // than tinted: a background that stays behind revealed text is a second
    // thing to read past, and the reader has already asked to see it.
    const QString spoilerOpen = revealed ? u"<span>"_s : u"<span style=\"background-color:%1; color:%1;\">"_s.arg(spoilerBg);
    processWithinHTML(segment, u"||"_s, spoilerOpen, u"</span>"_s);

    processWithinHTML(segment, u"*"_s, u"<i>"_s, u"</i>"_s);
    // Upstream also pairs a single _ into <u>, to match what Qt's markdown
    // writer emits. Not done here: upstream is reading markdown somebody wrote
    // on purpose, and this is reading whatever was typed - which turns
    // some_variable_name into "some<u>variable</u>name" the first time anybody
    // pastes an identifier.

    segment = highlightMentions(std::move(segment), options.value(u"mentionName"_s).toString(), colour("mentionColor", u"#3daee9"_s));

    segment.replace(u'\n', u"<br />"_s);
    return segment;
}

QStringList TextHandler::codeBlocks(const QString &plain) const
{
    QStringList blocks;
    const auto parts = plain.split(u"```"_s);
    // Odd indices are the inside of a fence, but only when the fences pair up.
    // An unclosed trailing fence is still being typed and has no block in it.
    if (parts.size() % 2 == 0) {
        return blocks;
    }
    for (int i = 1; i < parts.size(); i += 2) {
        QString body = parts.at(i);
        // A language tag on the opening fence is not part of the code.
        const int firstNewline = body.indexOf(u'\n');
        if (firstNewline >= 0 && !body.left(firstNewline).contains(u' ')) {
            body = body.mid(firstNewline + 1);
        }
        blocks.append(body.trimmed());
    }
    return blocks;
}

QString TextHandler::toRichText(const QString &plain, const QVariantMap &options) const
{
    const auto parts = plain.split(u"```"_s);
    const bool fencesBalanced = parts.size() % 2 == 1;

    QString out;
    for (int i = 0; i < parts.size(); ++i) {
        const bool isCode = (i % 2 == 1) && fencesBalanced;
        if (!isCode) {
            // An unclosed trailing fence is text, including the ``` itself,
            // which is what the writer sees while they are still typing it.
            if (i > 0 && !fencesBalanced) {
                out += escapeHtml(u"```"_s);
            }
            out += renderSegment(parts.at(i), options);
            continue;
        }

        QString body = parts.at(i);
        const int firstNewline = body.indexOf(u'\n');
        if (firstNewline >= 0 && !body.left(firstNewline).contains(u' ')) {
            body = body.mid(firstNewline + 1);
        }
        const auto bg = options.value(u"codeBackground"_s).toString();
        out += u"<pre style=\"background-color:%1;\"><code>%2</code></pre>"_s.arg(bg.isEmpty() ? u"#00000018"_s : bg,
                                                                                 escapeHtml(body.trimmed()).replace(u'\n', u"<br />"_s));
    }
    return out;
}
