#include "markdownrenderer.h"

#include <QTextDocument>

QString MarkdownRenderer::toHtml(const QString& markdown) const
{
    if (markdown.isEmpty())
        return QString();

    QTextDocument doc;
    doc.setMarkdown(markdown);
    const QString full = doc.toHtml();

    // Strip the <html>/<head>/<body …> wrapper and keep the body's children. Done by index
    // rather than regex because toHtml() emits a fixed, machine-generated shape — and if that
    // shape ever changes, falling back to the full document is safe (it still renders; it just
    // carries the baked-in font).
    const int bodyOpen = full.indexOf(QStringLiteral("<body"));
    if (bodyOpen < 0)
        return full;
    const int contentStart = full.indexOf('>', bodyOpen);
    if (contentStart < 0)
        return full;
    const int bodyClose = full.lastIndexOf(QStringLiteral("</body>"));
    if (bodyClose < 0 || bodyClose <= contentStart)
        return full;

    return full.mid(contentStart + 1, bodyClose - contentStart - 1).trimmed();
}
