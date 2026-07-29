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
#include <QtQml/qqmlregistration.h>

class DocumentFormatter : public QObject
{
    Q_OBJECT
    // Compile-time registration. A runtime qmlRegisterType<>() in main.cpp is invisible to
    // qmltyperegistrar, so the type never reaches Decenza.qmltypes and qmllint reports every
    // use of it as "was not found. Did you add all imports and dependencies?" — 16 such
    // warnings across four QML files for these four types, all from this one cause.
    QML_ELEMENT


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
    // Return the range to the widget's theme colour. Distinct from setting black — the
    // absence of a stored colour is what makes a custom widget follow the theme.
    Q_INVOKABLE void clearColorOnRange(int selStart, int selEnd);
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

#ifdef DECENZA_TESTING
    // Point the formatter at a bare QTextDocument. The QML path goes through
    // QQuickTextDocument, which needs a QQuickItem to exist — a headless test has no reason
    // to build one, and the logic under test only ever touches the QTextDocument beneath.
    void setTextDocumentForTesting(QTextDocument *doc) { m_testDocument = doc; }
#endif

private:
    QTextCursor textCursor() const;
    QTextCursor textCursorForFormat() const;
    QTextDocument *textDocument() const;
    void mergeFormatOnSelection(const QTextCharFormat &format);
    QTextCharFormat charFormatAtCursor() const;
    void updateSavedSelection();

    QQuickTextDocument *m_document = nullptr;
#ifdef DECENZA_TESTING
    QTextDocument *m_testDocument = nullptr;
#endif
    int m_selectionStart = 0;
    int m_selectionEnd = 0;
    int m_cursorPosition = 0;
    int m_savedSelectionStart = 0;
    int m_savedSelectionEnd = 0;
};
