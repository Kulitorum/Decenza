#pragma once

#include <QObject>
#include <QString>

// Markdown -> HTML, so QML can render markdown AND emoji in the same block of text.
//
// This is a thin adapter over Qt's own markdown parser (QTextDocument::setMarkdown,
// backed by md4c) — not a markdown implementation. It exists to fix an ORDERING problem,
// not a missing-feature one.
//
// The problem: Qt's Markdown importer truncates the rest of the document at an inline
// <img> tag. Verified on Qt 6.11.1 with a resolvable image:
//
//     "hi <img src=...> there"                   -> "hi"
//     "**<img src=...> Recipes** …\n\n- a\n- b"   -> ""   (whole document lost)
//
// The app rewrites emoji to bundled <img> tags to keep colour glyphs away from the
// platform text renderer (which crashes the render thread on macOS). Doing that BEFORE
// the markdown parse fed it exactly the input it chokes on — the update tab showed only
// the notes above the first emoji, and AI replies had been losing everything after their
// first emoji for far longer.
//
// Doing it AFTER means the parser never sees an <img>, so the bug cannot fire:
//
//     markdown --[Qt parser]--> HTML --[Theme.replaceEmojiWithImg]--> HTML with emoji
//                                                                     |
//                                                          Text { textFormat: RichText }
//
// Markdown's own image syntax (`![alt](url)`) survives the parser, but carries no
// width/height, so a 36x36 Twemoji SVG renders triple-height in 12px text and markdown
// has no syntax to size it. Going through HTML lets the emoji carry explicit dimensions.
class MarkdownRenderer : public QObject {
    Q_OBJECT

public:
    explicit MarkdownRenderer(QObject* parent = nullptr) : QObject(parent) {}

    // Returns the BODY CONTENT only — the <html>/<head>/<body> wrapper is dropped.
    //
    // That wrapper matters: QTextDocument::toHtml() bakes the document's default font into
    // `<body style="font-family:…; font-size:13pt">`, which would override the QML element's
    // own font and ignore the user's font-size settings. It emits no colour, so the theme's
    // text colour still applies once the wrapper is gone.
    //
    // Text content is escaped by toHtml(), so the result is safe to pass through
    // replaceEmojiWithImg() with allowMarkup=true.
    Q_INVOKABLE QString toHtml(const QString& markdown) const;
};
