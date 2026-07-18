// Guards MarkdownRenderer, and in particular a SECURITY claim its header makes.
//
// Four call sites feed untrusted text through this and then call
// replaceEmojiWithImg(..., allowMarkup=true) — escaping deliberately OFF:
//   SettingsUpdateTab   (GitHub release notes — remote, unauthenticated)
//   ConversationOverlay (AI replies, x2)
//   SettingsAITab       (AI replies)
//   DialingAssistantPage(AI replies)
//
// The header asserts "text content is escaped by toHtml(), so the result is safe to pass
// through replaceEmojiWithImg() with allowMarkup=true". That claim was written WITHOUT
// being verified — the exact failure mode this whole change exists to correct. These tests
// exist so it is checked rather than believed.

#include <QtTest>
#include <QTextDocument>

#include "core/markdownrenderer.h"

class TestMarkdownRenderer : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void stripsTheBodyWrapper();
    void emptyInputGivesEmptyOutput();
    void preservesMarkdownStructure();
    void rawHtmlInMarkdownIsNeutralised();
    void emojiRoundTripSurvivesIntact();
};

// I1: the <html>/<head>/<body style="font-family:…;font-size:13pt"> wrapper must go, or it
// overrides the QML element's font and the user's font-size setting.
void TestMarkdownRenderer::stripsTheBodyWrapper()
{
    const QString html = MarkdownRenderer().toHtml("# Heading\n\ntext");
    QVERIFY2(!html.contains("<html"), qPrintable(html));
    QVERIFY2(!html.contains("<body"), qPrintable(html));
    QVERIFY2(!html.contains("font-family"), qPrintable("baked-in font survived: " + html));
    QVERIFY2(!html.contains("<!DOCTYPE"), qPrintable(html));
    QVERIFY2(html.contains("Heading"), qPrintable(html));
}

void TestMarkdownRenderer::emptyInputGivesEmptyOutput()
{
    QVERIFY(MarkdownRenderer().toHtml(QString()).isEmpty());
    QVERIFY(MarkdownRenderer().toHtml("").isEmpty());
}

// The reason this class exists at all: markdown formatting must survive, because rendering
// release notes as plain text was showing literal ## and ** to users.
void TestMarkdownRenderer::preservesMarkdownStructure()
{
    const QString html = MarkdownRenderer().toHtml(
        "## Changes\n\n**bold** and *italic*\n\n- alpha\n- beta\n\n> quoted");
    QVERIFY2(html.contains("font-weight:700"), qPrintable(html));   // **bold**
    QVERIFY2(html.contains("font-style:italic"), qPrintable(html)); // *italic*
    QVERIFY2(html.contains("<li"), qPrintable(html));               // list
    QVERIFY2(html.contains("alpha") && html.contains("beta"), qPrintable(html));
    QVERIFY2(html.contains("quoted"), qPrintable(html));            // blockquote
}

// THE SECURITY TEST. Qt's setMarkdown defaults to MarkdownDialectGitHub, which does NOT
// include MarkdownNoHTML — so raw HTML embedded in markdown is PARSED and re-emitted as
// live markup unless we opt out. Left unguarded, a release note or AI reply containing
// <img src="https://attacker/px.png"> would render and fetch that URL on view: an outbound
// request from untrusted content, in a path that has escaping switched off.
void TestMarkdownRenderer::rawHtmlInMarkdownIsNeutralised()
{
    // EACH CASE IN ISOLATION. The first version of this test put the <img> first and passed
    // vacuously: the inline-<img> truncation ate the rest of the document, so the anchor it
    // claimed to check was never reached. Isolated, the anchor DOES survive without
    // MarkdownNoHTML — which is the whole reason that flag is now passed.
    MarkdownRenderer r;

    // The URL may legitimately appear as ESCAPED TEXT (&lt;a href=&quot;…) — that is the
    // neutralised form, and the user should still see what was written. What must never
    // appear is a LIVE tag. Asserting on the URL string alone was wrong: it failed on
    // correct output, which is its own kind of bad test.
    const QString anchor = r.toHtml("click <a href=\"https://example.invalid/x\">here</a> now");
    QVERIFY2(!anchor.contains("<a href=\"https://example.invalid"), qPrintable(
        "an untrusted <a href> became a LIVE link — this reaches "
        "replaceEmojiWithImg(..., allowMarkup=true) and renders as RichText. "
        "QTextDocument::MarkdownNoHTML is missing. Got: " + anchor));
    QVERIFY2(anchor.contains("&lt;a href="), qPrintable(
        "expected the tag neutralised to escaped text, not silently removed: " + anchor));
    QVERIFY2(anchor.contains("click") && anchor.contains("now"),
             qPrintable("surrounding text must survive: " + anchor));

    const QString img = r.toHtml("note <img src=\"https://example.invalid/px.png\"> after");
    QVERIFY2(!img.contains("<img src=\"https://example.invalid"), qPrintable(
        "a remote <img> became a live tag — it would be fetched on view: " + img));
    QVERIFY2(img.contains("note") && img.contains("after"), qPrintable(
        "with NoHTML the <img> must be inert text, NOT truncate the document: " + img));

    const QString bold = r.toHtml("text <b>bold</b> after");
    QVERIFY2(!bold.contains("font-weight:700"), qPrintable(
        "raw <b> was interpreted as real formatting: " + bold));

    const QString script = r.toHtml("text <script>alert(1)</script> after");
    QVERIFY2(!script.contains("<script"), qPrintable(script));
}

// The actual user-visible regression this pipeline fixed: a document whose FIRST line
// carries an emoji must not lose everything after it. inlineImgTruncatesMarkdownDocument
// (tst_textescaping) proves the Qt hazard; this proves our ordering avoids it.
void TestMarkdownRenderer::emojiRoundTripSurvivesIntact()
{
    // Emoji left as a raw codepoint here — replaceEmojiWithImg runs AFTER this step, which
    // is the whole point of the ordering.
    const QString md = QString::fromUtf8("**☕ Recipes** - the whole drink.\n\n- alpha\n- beta");
    const QString html = MarkdownRenderer().toHtml(md);

    QVERIFY2(html.contains("Recipes"), qPrintable(html));
    QVERIFY2(html.contains("alpha"), qPrintable("content after the emoji line was lost: " + html));
    QVERIFY2(html.contains("beta"), qPrintable(html));
    QVERIFY2(html.contains(QString::fromUtf8("☕")),
             qPrintable("the emoji itself must reach replaceEmojiWithImg: " + html));
}

QTEST_MAIN(TestMarkdownRenderer)
#include "tst_markdownrenderer.moc"
