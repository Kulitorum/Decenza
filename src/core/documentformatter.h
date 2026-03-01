#pragma once

#include <QObject>
#include <QQuickTextDocument>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextBlock>
#include <QColor>
#include <QVariantList>
#include <QVariantMap>

class DocumentFormatter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(int selectionStart READ selectionStart WRITE setSelectionStart NOTIFY selectionStartChanged)
    Q_PROPERTY(int selectionEnd READ selectionEnd WRITE setSelectionEnd NOTIFY selectionEndChanged)
    Q_PROPERTY(int cursorPosition READ cursorPosition WRITE setCursorPosition NOTIFY cursorPositionChanged)

    // Read current format at cursor/selection (for toolbar button state)
    Q_PROPERTY(bool bold READ bold NOTIFY formatChanged)
    Q_PROPERTY(bool italic READ italic NOTIFY formatChanged)
    Q_PROPERTY(QString currentColor READ currentColor NOTIFY formatChanged)
    Q_PROPERTY(int currentFontSize READ currentFontSize NOTIFY formatChanged)

    // Last non-empty selection range, saved when selectionStart/End change and differ.
    // Survives focus loss (QML TextArea resets selection when focus is lost) so deferred
    // formatting operations (e.g., color picker) can still target the original range.
    Q_PROPERTY(int savedSelectionStart READ savedSelectionStart NOTIFY savedSelectionChanged)
    Q_PROPERTY(int savedSelectionEnd READ savedSelectionEnd NOTIFY savedSelectionChanged)

public:
    explicit DocumentFormatter(QObject *parent = nullptr);

    QQuickTextDocument *document() const;
    void setDocument(QQuickTextDocument *document);

    int selectionStart() const;
    void setSelectionStart(int position);
    int selectionEnd() const;
    void setSelectionEnd(int position);
    int cursorPosition() const;
    void setCursorPosition(int position);

    bool bold() const;
    bool italic() const;
    QString currentColor() const;
    int currentFontSize() const;

    int savedSelectionStart() const;
    int savedSelectionEnd() const;

    // Formatting operations — use mergeCharFormat (additive, preserves other formats)
    Q_INVOKABLE void toggleBold();
    Q_INVOKABLE void toggleItalic();
    Q_INVOKABLE void setColor(const QString &color);
    Q_INVOKABLE void setColorOnRange(const QString &color, int selStart, int selEnd);
    Q_INVOKABLE void setFontSize(int pixelSize);
    Q_INVOKABLE void clearFormatting();

    // Segment conversion
    Q_INVOKABLE QVariantList toSegments() const;
    Q_INVOKABLE void fromSegments(const QVariantList &segments);

    // Compile segments to HTML (static — can be called without a document)
    Q_INVOKABLE static QString segmentsToHtml(const QVariantList &segments);

signals:
    void documentChanged();
    void selectionStartChanged();
    void selectionEndChanged();
    void cursorPositionChanged();
    void formatChanged();
    void savedSelectionChanged();

private:
    QTextCursor textCursor() const;
    QTextCursor textCursorForFormat() const;
    QTextDocument *textDocument() const;
    void mergeFormatOnSelection(const QTextCharFormat &format);
    QTextCharFormat charFormatAtCursor() const;
    void updateSavedSelection();

    QQuickTextDocument *m_document = nullptr;
    int m_selectionStart = 0;
    int m_selectionEnd = 0;
    int m_cursorPosition = 0;
    int m_savedSelectionStart = 0;
    int m_savedSelectionEnd = 0;
};
