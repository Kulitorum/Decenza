#pragma once

#include <QRegularExpression>
#include <QString>
#include <QStringList>

// Decenza stores notes as plain text; visualizer.coffee stores them as rich text
// (Lexxy/ActionText HTML) since 2026-08-02, and reads them differently per route:
//   - shot upload: the value is Markdown (Parsers::Base#note_html →
//     RichTextSanitizer.from_markdown, GFM with hard_wrap on)
//   - shot and coffee-bag PATCH: the value is HTML (SanitizedRichText setter →
//     RichTextSanitizer.sanitize), so a bare newline is just whitespace
//   - every read (GET /api/shots/:id, /api/coffee_bags/:id): HTML
//     (rich_text_html in shot/jsonable.rb and coffee_bag.rb)
namespace VisualizerNotes {

// For a PATCH: the HTML RichTextSanitizer.from_plain_text would build — blank
// lines split paragraphs, single newlines become <br>.
inline QString plainToHtml(const QString& plain)
{
    QString text = plain;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    if (text.trimmed().isEmpty())
        return QString();
    static const QRegularExpression paragraphBreak(QStringLiteral("\n{2,}"));
    QStringList paragraphs;
    for (const QString& paragraph : text.split(paragraphBreak)) {
        QString escaped = paragraph.toHtmlEscaped();
        escaped.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
        paragraphs << QStringLiteral("<p>%1</p>").arg(escaped);
    }
    return paragraphs.join(QString());
}

// For an upload: backslash-escape every character kramdown's GFM parser treats
// as markup (ESCAPED_CHARS_GFM in kramdown-parser-gfm), so the note renders as
// the literal text the user typed. Newlines need nothing: hard_wrap defaults on.
inline QString escapeMarkdown(const QString& plain)
{
    static const QString kEscaped = QStringLiteral("\\.*_+`<>()[]{}#!:|\"'$=-~");
    QString out;
    out.reserve(plain.size() + plain.size() / 8);
    for (const QChar c : plain) {
        if (kEscaped.contains(c))
            out += QLatin1Char('\\');
        out += c;
    }
    return out;
}

// For a read: the plain text of Visualizer's HTML. A value with no leading tag
// is already plain (a pre-2026-08 shot, or a .shot file) and is returned as is.
inline QString htmlToPlain(const QString& html)
{
    if (!html.trimmed().startsWith(QLatin1Char('<')))
        return html;
    QString s = html;
    static const QRegularExpression sourceNewlines(QStringLiteral("[\\r\\n]+"));
    static const QRegularExpression lineBreak(QStringLiteral("<br\\s*/?>"),
                                              QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rowEnd(QStringLiteral("</(li|tr)\\s*>"),
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression listItem(QStringLiteral("<li[^>]*>"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression blockEnd(
        QStringLiteral("</(p|div|h[1-6]|blockquote|pre|ul|ol|table)\\s*>"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression anyTag(QStringLiteral("<[^>]*>"));
    static const QRegularExpression numericEntity(QStringLiteral("&#(x?)([0-9a-fA-F]+);"));
    static const QRegularExpression trailingSpace(QStringLiteral("[ \\t]+\\n"));
    static const QRegularExpression extraBlankLines(QStringLiteral("\\n{3,}"));

    s.replace(sourceNewlines, QStringLiteral(" "));
    s.replace(lineBreak, QStringLiteral("\n"));
    s.replace(rowEnd, QStringLiteral("\n"));
    s.replace(listItem, QStringLiteral("- "));
    s.replace(blockEnd, QStringLiteral("\n\n"));
    s.remove(anyTag);

    QString decoded;
    qsizetype last = 0;
    auto it = numericEntity.globalMatch(s);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        decoded += s.mid(last, m.capturedStart() - last);
        bool ok = false;
        const char32_t code = m.captured(2).toUInt(&ok, m.captured(1).isEmpty() ? 10 : 16);
        decoded += ok ? QString::fromUcs4(&code, 1) : m.captured(0);
        last = m.capturedEnd();
    }
    decoded += s.mid(last);
    decoded.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "))
        .replace(QStringLiteral("&lt;"), QStringLiteral("<"))
        .replace(QStringLiteral("&gt;"), QStringLiteral(">"))
        .replace(QStringLiteral("&quot;"), QStringLiteral("\""))
        .replace(QStringLiteral("&apos;"), QStringLiteral("'"))
        .replace(QStringLiteral("&amp;"), QStringLiteral("&"));

    decoded.replace(trailingSpace, QStringLiteral("\n"));
    decoded.replace(extraBlankLines, QStringLiteral("\n\n"));
    return decoded.trimmed();
}

// Whether two notes say the same thing once each is reduced to what the other
// side can store: whitespace runs and leading indentation do not survive the
// upload's Markdown rendering, so they are not a difference.
inline bool sameNotes(const QString& a, const QString& b)
{
    static const QRegularExpression spaceRun(QStringLiteral("[ \\t]+"));
    static const QRegularExpression blankRun(QStringLiteral("\\n{2,}"));
    auto normalize = [](const QString& s) {
        QStringList lines = htmlToPlain(s).split(QLatin1Char('\n'));
        for (QString& line : lines)
            line = line.replace(spaceRun, QStringLiteral(" ")).trimmed();
        return lines.join(QLatin1Char('\n')).replace(blankRun, QStringLiteral("\n\n")).trimmed();
    };
    return normalize(a) == normalize(b);
}

} // namespace VisualizerNotes
