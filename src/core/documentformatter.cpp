#include "documentformatter.h"
#include <QTextBlock>
#include <QTextFragment>
#include <QFont>
#include <QDebug>

DocumentFormatter::DocumentFormatter(QObject *parent)
    : QObject(parent)
{
}

// --- Property accessors ---

QQuickTextDocument *DocumentFormatter::document() const
{
    return m_document;
}

void DocumentFormatter::setDocument(QQuickTextDocument *document)
{
    if (m_document == document)
        return;
    m_document = document;
    emit documentChanged();
}

int DocumentFormatter::selectionStart() const
{
    return m_selectionStart;
}

void DocumentFormatter::setSelectionStart(int position)
{
    if (m_selectionStart == position)
        return;
    m_selectionStart = position;
    emit selectionStartChanged();
    emit formatChanged();
}

int DocumentFormatter::selectionEnd() const
{
    return m_selectionEnd;
}

void DocumentFormatter::setSelectionEnd(int position)
{
    if (m_selectionEnd == position)
        return;
    m_selectionEnd = position;
    emit selectionEndChanged();
    emit formatChanged();
}

int DocumentFormatter::cursorPosition() const
{
    return m_cursorPosition;
}

void DocumentFormatter::setCursorPosition(int position)
{
    if (m_cursorPosition == position)
        return;
    m_cursorPosition = position;
    emit cursorPositionChanged();
    emit formatChanged();
}

// --- Format queries (for toolbar button state) ---

QTextCharFormat DocumentFormatter::charFormatAtCursor() const
{
    QTextDocument *doc = textDocument();
    if (!doc)
        return {};
    const int maxPos = doc->characterCount() - 1;
    QTextCursor cursor(doc);
    if (m_selectionStart != m_selectionEnd) {
        cursor.setPosition(qBound(0, m_selectionStart, maxPos));
        cursor.setPosition(qBound(0, m_selectionEnd, maxPos), QTextCursor::KeepAnchor);
    } else {
        cursor.setPosition(qBound(0, m_cursorPosition, maxPos));
    }
    return cursor.charFormat();
}

bool DocumentFormatter::bold() const
{
    return charFormatAtCursor().fontWeight() >= QFont::Bold;
}

bool DocumentFormatter::italic() const
{
    return charFormatAtCursor().fontItalic();
}

QString DocumentFormatter::currentColor() const
{
    auto fmt = charFormatAtCursor();
    if (fmt.foreground().style() == Qt::NoBrush)
        return QString();
    return fmt.foreground().color().name();
}

int DocumentFormatter::currentFontSize() const
{
    auto fmt = charFormatAtCursor();
    // Check pixel size first (what we set), then point size
    int px = fmt.property(QTextFormat::FontPixelSize).toInt();
    if (px > 0)
        return px;
    int pt = static_cast<int>(fmt.fontPointSize());
    if (pt > 0)
        return pt;
    return 0;
}

// --- Internal helpers ---

QTextDocument *DocumentFormatter::textDocument() const
{
    if (!m_document)
        return nullptr;
    return m_document->textDocument();
}

QTextCursor DocumentFormatter::textCursor() const
{
    QTextDocument *doc = textDocument();
    if (!doc)
        return QTextCursor();
    const int maxPos = doc->characterCount() - 1;
    QTextCursor cursor(doc);
    if (m_selectionStart != m_selectionEnd) {
        cursor.setPosition(qBound(0, m_selectionStart, maxPos));
        cursor.setPosition(qBound(0, m_selectionEnd, maxPos), QTextCursor::KeepAnchor);
    } else {
        cursor.setPosition(qBound(0, m_cursorPosition, maxPos));
    }
    return cursor;
}

void DocumentFormatter::mergeFormatOnSelection(const QTextCharFormat &format)
{
    QTextCursor cursor = textCursor();
    if (!cursor.hasSelection()) {
        qDebug() << "DocumentFormatter: no selection, skipping merge. start:" << m_selectionStart << "end:" << m_selectionEnd;
        return;
    }
    qDebug() << "DocumentFormatter: merging format on selection" << m_selectionStart << "-" << m_selectionEnd;
    cursor.mergeCharFormat(format);
    emit formatChanged();
}

// --- Formatting operations ---

void DocumentFormatter::toggleBold()
{
    QTextCharFormat fmt;
    fmt.setFontWeight(bold() ? QFont::Normal : QFont::Bold);
    mergeFormatOnSelection(fmt);
}

void DocumentFormatter::toggleItalic()
{
    QTextCharFormat fmt;
    fmt.setFontItalic(!italic());
    mergeFormatOnSelection(fmt);
}

void DocumentFormatter::setColor(const QString &color)
{
    QTextCharFormat fmt;
    fmt.setForeground(QColor(color));
    mergeFormatOnSelection(fmt);
}

void DocumentFormatter::setColorOnRange(const QString &color, int selStart, int selEnd)
{
    QTextDocument *doc = textDocument();
    if (!doc || selStart == selEnd)
        return;
    const int maxPos = doc->characterCount() - 1;
    QTextCursor cursor(doc);
    cursor.setPosition(qBound(0, selStart, maxPos));
    cursor.setPosition(qBound(0, selEnd, maxPos), QTextCursor::KeepAnchor);
    QTextCharFormat fmt;
    fmt.setForeground(QColor(color));
    cursor.mergeCharFormat(fmt);
    emit formatChanged();
}

void DocumentFormatter::setFontSize(int pixelSize)
{
    QTextCharFormat fmt;
    fmt.setProperty(QTextFormat::FontPixelSize, pixelSize);
    mergeFormatOnSelection(fmt);
}

void DocumentFormatter::clearFormatting()
{
    QTextCursor cursor = textCursor();
    if (!cursor.hasSelection())
        return;
    QTextCharFormat fmt; // default format — clears all
    cursor.setCharFormat(fmt);
    emit formatChanged();
}

// --- Segment extraction from QTextDocument ---

QVariantList DocumentFormatter::toSegments() const
{
    QVariantList segments;
    QTextDocument *doc = textDocument();
    if (!doc) {
        return segments;
    }

    QTextBlock block = doc->begin();
    bool firstBlock = true;

    while (block.isValid()) {
        // Insert newline segment between blocks (not before first)
        if (!firstBlock) {
            QVariantMap nlSeg;
            nlSeg[QStringLiteral("text")] = QStringLiteral("\n");
            segments.append(nlSeg);
        }
        firstBlock = false;

        for (auto it = block.begin(); !it.atEnd(); ++it) {
            QTextFragment fragment = it.fragment();
            if (!fragment.isValid())
                continue;

            QString text = fragment.text();
            if (text.isEmpty())
                continue;

            QTextCharFormat fmt = fragment.charFormat();
            QVariantMap seg;
            seg[QStringLiteral("text")] = text;

            if (fmt.fontWeight() >= QFont::Bold)
                seg[QStringLiteral("bold")] = true;

            if (fmt.fontItalic())
                seg[QStringLiteral("italic")] = true;

            if (fmt.foreground().style() != Qt::NoBrush) {
                QColor c = fmt.foreground().color();
                // Skip black/default — only store explicit colors
                if (c != QColor(Qt::black) && c != QColor(0, 0, 0))
                    seg[QStringLiteral("color")] = c.name();
            }

            int px = fmt.property(QTextFormat::FontPixelSize).toInt();
            if (px > 0) {
                seg[QStringLiteral("size")] = px;
            } else {
                int pt = static_cast<int>(fmt.fontPointSize());
                if (pt > 0)
                    seg[QStringLiteral("size")] = pt;
            }

            segments.append(seg);
        }

        block = block.next();
    }

    return segments;
}

// --- Load segments into QTextDocument ---

void DocumentFormatter::fromSegments(const QVariantList &segments)
{
    QTextDocument *doc = textDocument();
    if (!doc) {
        qDebug() << "DocumentFormatter::fromSegments: no document!";
        return;
    }
    qDebug() << "DocumentFormatter::fromSegments: loading" << segments.size() << "segments";

    QTextCursor cursor(doc);
    cursor.select(QTextCursor::Document);
    cursor.removeSelectedText();

    for (const QVariant &v : segments) {
        QVariantMap seg = v.toMap();
        QString text = seg.value(QStringLiteral("text")).toString();
        if (text.isEmpty())
            continue;

        QTextCharFormat fmt;

        if (seg.value(QStringLiteral("bold")).toBool())
            fmt.setFontWeight(QFont::Bold);

        if (seg.value(QStringLiteral("italic")).toBool())
            fmt.setFontItalic(true);

        QString color = seg.value(QStringLiteral("color")).toString();
        if (!color.isEmpty())
            fmt.setForeground(QColor(color));

        int size = seg.value(QStringLiteral("size")).toInt();
        if (size > 0)
            fmt.setProperty(QTextFormat::FontPixelSize, size);

        // Handle newlines — insert as block separators
        if (text == QStringLiteral("\n")) {
            cursor.insertBlock();
        } else {
            cursor.setCharFormat(fmt);
            cursor.insertText(text);
        }
    }
}

// --- Compile segments to HTML (static) ---

QString DocumentFormatter::segmentsToHtml(const QVariantList &segments)
{
    QString html;

    for (const QVariant &v : segments) {
        QVariantMap seg = v.toMap();
        QString text = seg.value(QStringLiteral("text")).toString();
        if (text.isEmpty())
            continue;

        // Handle newlines
        if (text == QStringLiteral("\n")) {
            html += QStringLiteral("<br>");
            continue;
        }

        // Escape HTML entities in text
        QString escaped = text.toHtmlEscaped();

        // Build inline styles
        QStringList styles;
        QString color = seg.value(QStringLiteral("color")).toString();
        if (!color.isEmpty())
            styles.append(QStringLiteral("color:") + color);

        int size = seg.value(QStringLiteral("size")).toInt();
        if (size > 0)
            styles.append(QStringLiteral("font-size:") + QString::number(size) + QStringLiteral("px"));

        bool isBold = seg.value(QStringLiteral("bold")).toBool();
        bool isItalic = seg.value(QStringLiteral("italic")).toBool();

        // Wrap in span if there are styles
        if (!styles.isEmpty())
            escaped = QStringLiteral("<span style=\"") + styles.join(QStringLiteral("; ")) + QStringLiteral("\">") + escaped + QStringLiteral("</span>");

        // Wrap in bold/italic tags
        if (isBold)
            escaped = QStringLiteral("<b>") + escaped + QStringLiteral("</b>");
        if (isItalic)
            escaped = QStringLiteral("<i>") + escaped + QStringLiteral("</i>");

        html += escaped;
    }

    return html.isEmpty() ? QStringLiteral("Text") : html;
}
