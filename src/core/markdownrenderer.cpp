#include "markdownrenderer.h"

#include <QDebug>
#include <QTextDocument>

QString MarkdownRenderer::toHtml(const QString& markdown) const
{
    if (markdown.isEmpty())
        return QString();

    QTextDocument doc;
    // MarkdownNoHTML is REQUIRED, not tidiness. setMarkdown() defaults to
    // MarkdownDialectGitHub, which does NOT include it, so raw HTML embedded in the source
    // is PARSED and re-emitted as live markup. Verified on Qt 6.11.1: an untrusted
    // `<a href="https://attacker/x">` survives into the output as a real link, and
    // `<b>` becomes real bold. The output then goes to replaceEmojiWithImg(..., allowMarkup
    // = true) — escaping deliberately off — and is rendered as RichText.
    //
    // (An untrusted `<img src="https://attacker/px.png">` happens NOT to survive, but only
    // because the inline-<img> truncation bug eats it. A bug is not a security control.)
    //
    // It also makes the ordering constraint unconditional: with NoHTML the importer cannot
    // reach the inline-<img> truncation path at all, even for source that literally contains
    // an <img> tag.
    // MarkdownFeatures(...) is needed because both names are values of the same enum, so
    // OR-ing them yields int rather than the QFlags type.
    doc.setMarkdown(markdown, QTextDocument::MarkdownFeatures(
                                  QTextDocument::MarkdownDialectGitHub
                                  | QTextDocument::MarkdownNoHTML));
    const QString full = doc.toHtml();

    // Strip the <html>/<head>/<body …> wrapper and keep the body's children. Done by index
    // rather than regex because toHtml() emits a fixed, machine-generated shape.
    //
    // The fallback returns the FULL document, which still renders but carries the baked-in
    // font — i.e. it silently defeats the reason this function exists, and the user's
    // font-size setting stops applying to AI replies and release notes. Not "safe", just
    // survivable, so it says so out loud. The trigger would be a Qt upgrade changing
    // toHtml()'s shape, which is otherwise attributable to nothing.
    auto bail = [&full](const char* why) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            qWarning() << "[Markdown] QTextDocument::toHtml() no longer matches the expected "
                          "<html>/<body> shape (" << why << ") — returning the full document. "
                          "Rendered text will use QTextDocument's default font instead of the "
                          "theme font, ignoring the user's font-size setting. Usually means a "
                          "Qt upgrade changed toHtml(); update MarkdownRenderer::toHtml().";
        }
        return full;
    };

    const int bodyOpen = full.indexOf(QStringLiteral("<body"));
    if (bodyOpen < 0)
        return bail("no <body>");
    const int contentStart = full.indexOf('>', bodyOpen);
    if (contentStart < 0)
        return bail("unterminated <body>");
    const int bodyClose = full.lastIndexOf(QStringLiteral("</body>"));
    if (bodyClose < 0 || bodyClose <= contentStart)
        return bail("no </body>");

    return full.mid(contentStart + 1, bodyClose - contentStart - 1).trimmed();
}
