// Guards the escaping contract of Theme.escapeHtml() / Theme.replaceEmojiWithImg().
//
// These run the REAL functions out of qml/Theme.qml (read from DECENZA_SOURCE_DIR and
// evaluated in a QJSEngine), not a C++ reimplementation of them. A copy would drift from
// the shipping code and then assert that the copy is correct, which is worse than no test.
//
// The contract being guarded (see the comment on escapeHtml in Theme.qml):
//   - & and < are escaped: that is what blocks tag injection from bean names, AI replies,
//     release notes and other externally-sourced text.
//   - > is deliberately NOT escaped: escaping it breaks Markdown blockquotes, and it buys
//     nothing, because a tag cannot be opened without <.
//   - allowMarkup defaults to FALSE, so a new call site is safe by default.
//
// If someone "restores" the > escape as a tidy-up, ConversationOverlay silently stops
// rendering blockquotes from AI replies. That is the regression this file exists to catch.

#include <QtTest>
#include <QJSEngine>
#include <QDir>
#include <QTextDocument>
#include <QFile>
#include <QRegularExpression>

class TestTextEscaping : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }
    void initTestCase();
    void escapesAmpersandAndLessThan();
    void doesNotEscapeGreaterThan();
    void tagInjectionIsNeutralised();
    void replaceEmojiEscapesByDefault();
    void replaceEmojiPreservesMarkupWhenAllowed();
    void emojiBecomesImgTag();
    void variationSelectorSequencesAreRewritten();
    void strayVariationSelectorDoesNotCaptureText();
    void unbundledEmojiIsStrippedNotBroken();
    void inlineImgTruncatesMarkdownDocument();

private:
    QJSEngine m_engine;
    QJSValue m_theme;
    qsizetype m_assetCount = 0;
    QString call(const QString& fn, const QJSValueList& args);
};

// Extract the plain JS function bodies from Theme.qml. Theme.qml is a QML document, so it
// cannot be imported into a bare QJSEngine — but the functions under test are pure JS with
// no QML dependencies, so lifting them verbatim keeps the assertions pointed at shipping
// source rather than a copy.
void TestTextEscaping::initTestCase()
{
    QFile f(QStringLiteral(DECENZA_SOURCE_DIR) + "/qml/Theme.qml");
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(f.errorString()));
    const QString src = QString::fromUtf8(f.readAll());

    // Pull out `function <name>(...) { ... }` blocks by brace matching.
    auto extract = [&src](const QString& name) -> QString {
        const int start = src.indexOf("function " + name + "(");
        if (start < 0) return QString();
        int depth = 0;
        int i = src.indexOf('{', start);
        const int bodyStart = i;
        for (; i < src.size(); ++i) {
            if (src[i] == '{') ++depth;
            else if (src[i] == '}') {
                if (--depth == 0) return src.mid(start, i - start + 1);
            }
        }
        Q_UNUSED(bodyStart);
        return QString();
    };

    const QString escapeHtml = extract("escapeHtml");
    const QString isEmoji = extract("_isEmoji");
    const QString isEmojiPres = extract("_isEmojiPresentation");
    const QString assetPath = extract("_emojiAssetPath");
    const QString stripFn = extract("stripEmoji");
    const QString replaceEmoji = extract("replaceEmojiWithImg");
    QVERIFY2(!escapeHtml.isEmpty(), "escapeHtml() not found in Theme.qml");
    QVERIFY2(!isEmoji.isEmpty(), "_isEmoji() not found in Theme.qml");
    QVERIFY2(!isEmojiPres.isEmpty(), "_isEmojiPresentation() not found in Theme.qml");
    QVERIFY2(!assetPath.isEmpty(), "_emojiAssetPath() not found in Theme.qml");
    QVERIFY2(!stripFn.isEmpty(), "stripEmoji() not found in Theme.qml");
    QVERIFY2(!replaceEmoji.isEmpty(), "replaceEmojiWithImg() not found in Theme.qml");

    // Stand in for the EmojiAssets C++ singleton, seeded from the REAL asset directory in
    // the source tree. That keeps "is this emoji bundled?" answered by what actually ships,
    // so a test asserting an emoji strips cannot pass merely because a hand-written list
    // omitted it.
    QDir emojiDir(QStringLiteral(DECENZA_SOURCE_DIR) + "/resources/emoji");
    const QStringList assets = emojiDir.entryList(QStringList{"*.svg"}, QDir::Files);
    QVERIFY2(assets.size() > 3000,
             qPrintable(QStringLiteral("expected the full bundled set, found %1").arg(assets.size())));
    m_assetCount = assets.size();

    QJSValue known = m_engine.newObject();
    for (const QString& a : assets)
        known.setProperty(a.left(a.size() - 4), QJSValue(true));
    m_engine.globalObject().setProperty("__knownEmoji", known);
    m_engine.evaluate("var EmojiAssets = { has: function(k) { return __knownEmoji[k] === true } }");

    const QString program = QStringLiteral("(function(){ %1\n%2\n%3\n%4\n%5\n%6\n"
                                           "return { escapeHtml: escapeHtml,"
                                           "         replaceEmojiWithImg: replaceEmojiWithImg,"
                                           "         _emojiAssetPath: _emojiAssetPath }; })()")
                                .arg(escapeHtml, isEmoji, isEmojiPres, assetPath, replaceEmoji)
                                .arg(stripFn);

    m_theme = m_engine.evaluate(program);
    QVERIFY2(!m_theme.isError(), qPrintable(m_theme.toString()));
}

QString TestTextEscaping::call(const QString& fn, const QJSValueList& args)
{
    QJSValue f = m_theme.property(fn);
    QJSValue r = f.call(args);
    if (r.isError())
        return QStringLiteral("ERROR: ") + r.toString();
    return r.toString();
}

void TestTextEscaping::escapesAmpersandAndLessThan()
{
    QCOMPARE(call("escapeHtml", {QJSValue("Salt & Pepper")}), QStringLiteral("Salt &amp; Pepper"));
    QCOMPARE(call("escapeHtml", {QJSValue("a < b")}), QStringLiteral("a &lt; b"));
    // Ampersand must be escaped FIRST or the &lt; it produces gets double-escaped.
    QCOMPARE(call("escapeHtml", {QJSValue("<&>")}), QStringLiteral("&lt;&amp;>"));
}

// The load-bearing assertion. Escaping > breaks Markdown blockquotes.
void TestTextEscaping::doesNotEscapeGreaterThan()
{
    QCOMPARE(call("escapeHtml", {QJSValue("> quoted")}), QStringLiteral("> quoted"));
    QCOMPARE(call("escapeHtml", {QJSValue("a > b")}), QStringLiteral("a > b"));
}

void TestTextEscaping::tagInjectionIsNeutralised()
{
    // Leaving > raw does not re-open the injection hole: without a live <, there is no tag.
    QCOMPARE(call("escapeHtml", {QJSValue("<img src=x onerror=y>")}),
             QStringLiteral("&lt;img src=x onerror=y>"));
    QCOMPARE(call("escapeHtml", {QJSValue("<b>bold</b>")}),
             QStringLiteral("&lt;b>bold&lt;/b>"));
}

void TestTextEscaping::replaceEmojiEscapesByDefault()
{
    // No third argument: a new call site must be safe without opting in.
    QCOMPARE(call("replaceEmojiWithImg", {QJSValue("<b>hi</b>"), QJSValue(16)}),
             QStringLiteral("&lt;b>hi&lt;/b>"));
    // Explicit false behaves the same as omitted.
    QCOMPARE(call("replaceEmojiWithImg", {QJSValue("<b>hi</b>"), QJSValue(16), QJSValue(false)}),
             QStringLiteral("&lt;b>hi&lt;/b>"));
}

void TestTextEscaping::replaceEmojiPreservesMarkupWhenAllowed()
{
    QCOMPARE(call("replaceEmojiWithImg", {QJSValue("<b>hi</b>"), QJSValue(16), QJSValue(true)}),
             QStringLiteral("<b>hi</b>"));
}

void TestTextEscaping::emojiBecomesImgTag()
{
    // U+2615 HOT BEVERAGE -> bundled SVG, in both modes. The crash this whole change
    // exists to prevent is a colour glyph reaching the platform renderer, so the
    // rewrite must not depend on the escaping mode.
    const QString escaped = call("replaceEmojiWithImg", {QJSValue(QString::fromUtf8("☕")), QJSValue(16)});
    const QString allowed = call("replaceEmojiWithImg", {QJSValue(QString::fromUtf8("☕")), QJSValue(16), QJSValue(true)});
    QVERIFY2(escaped.contains("qrc:/emoji/2615.svg"), qPrintable(escaped));
    QVERIFY2(allowed.contains("qrc:/emoji/2615.svg"), qPrintable(allowed));
    QVERIFY2(!escaped.contains(QString::fromUtf8("☕")), "raw emoji codepoint survived the rewrite");
}

// Range checks alone miss these: "1️⃣" starts at ASCII U+0031 and "©️" is U+00A9. Both render
// from Apple Color Emoji ONLY because of the trailing U+FE0F, so both reached CoreText as
// colour glyphs — the crash path this whole change exists to close. The variation selector
// is the signal. Filenames must have FE0F stripped: 31-20e3.svg exists upstream,
// 31-fe0f-20e3.svg does not (verified against jdecked/twemoji@17.0.3).
void TestTextEscaping::variationSelectorSequencesAreRewritten()
{
    // Build the sequences explicitly from codepoints so the source file's own encoding
    // cannot quietly change what is being tested.
    const QString keycap1 = QString(QChar(0x0031)) + QChar(0xFE0F) + QChar(0x20E3);
    const QString copyright = QString(QChar(0x00A9)) + QChar(0xFE0F);
    const QString trademark = QString(QChar(0x2122)) + QChar(0xFE0F);

    const QString k = call("replaceEmojiWithImg", {QJSValue(keycap1), QJSValue(16)});
    QVERIFY2(k.contains("qrc:/emoji/31-20e3.svg"), qPrintable("keycap: " + k));
    QVERIFY2(!k.contains("fe0f"), qPrintable("FE0F must be stripped from the asset key: " + k));

    const QString c = call("replaceEmojiWithImg", {QJSValue(copyright), QJSValue(16)});
    QVERIFY2(c.contains("qrc:/emoji/a9.svg"), qPrintable("copyright: " + c));

    const QString t = call("replaceEmojiWithImg", {QJSValue(trademark), QJSValue(16)});
    QVERIFY2(t.contains("qrc:/emoji/2122.svg"), qPrintable("trademark: " + t));
}

// The variation-selector rule must be bounded: a stray U+FE0F after ordinary text must not
// drag that character into an image reference.
void TestTextEscaping::strayVariationSelectorDoesNotCaptureText()
{
    const QString letterThenVs = QString("a") + QChar(0xFE0F) + QString("bc");
    const QString out = call("replaceEmojiWithImg", {QJSValue(letterThenVs), QJSValue(16)});
    QVERIFY2(!out.contains("<img"), qPrintable("a letter must not become an image: " + out));
    QCOMPARE(out, QStringLiteral("abc"));  // stray selector dropped, text intact
}

// The broken-image bug: before this change both functions emitted qrc:/emoji/<key>.svg with
// no check that the file existed, so an emoji outside the bundled set produced an image
// reference nothing could resolve — neither drawn nor removed. Shipping ~4,000 assets does
// not fix this on its own: a codepoint from a Unicode revision newer than the pinned upstream
// still misses.
void TestTextEscaping::unbundledEmojiIsStrippedNotBroken()
{
    // U+1F322 sits INSIDE _isEmoji's 1F300-1F5FF range but upstream draws no asset for it.
    // That combination is the point: a codepoint the rewriter treats as emoji and then cannot
    // resolve is exactly what produced a broken image. 1,075 codepoints in the matched ranges
    // currently have no asset, so this is a present-day gap, not a hypothetical future one.
    //
    // An earlier version of this test used U+1FB00, which _isEmoji does not match at all — so
    // it passed whether or not the existence check existed, and proved nothing.
    const QString unbundled = QString::fromUcs4(U"\U0001F322", 1);
    QVERIFY2(call("_emojiAssetPath", {m_engine.toScriptValue(QStringList{"1f322"})}).isEmpty(),
             "an unbundled key must yield no path");

    const QString out = call("replaceEmojiWithImg",
                             {QJSValue("a" + unbundled + "b"), QJSValue(16)});
    QVERIFY2(!out.contains("<img"), qPrintable("unbundled emoji must not emit an image: " + out));
    QVERIFY2(out.contains("a") && out.contains("b"),
             qPrintable("surrounding text must survive: " + out));

    // A bundled one still resolves, so the check is not simply refusing everything.
    const QString ok = call("replaceEmojiWithImg", {QJSValue(QString::fromUtf8("☕")), QJSValue(16)});
    QVERIFY2(ok.contains("qrc:/emoji/2615.svg"), qPrintable(ok));
}

// Documents the Qt behaviour that dictates the markdown pipeline ORDER: convert
// markdown -> HTML FIRST, then inject emoji <img> (see markdownrenderer.h). A test of the
// FRAMEWORK, deliberately — the design rests on it, it is surprising, and it cost a
// production regression (release notes truncated at the first emoji) plus a longer-running
// silent one in AI replies, because a code comment asserted the opposite and nobody
// checked. If a Qt upgrade fixes this, the ordering constraint can be relaxed.
void TestTextEscaping::inlineImgTruncatesMarkdownDocument()
{
    const QString img = "<img src=\"file:///nonexistent-but-irrelevant.svg\" width=\"12\">";

    QTextDocument withImg;
    withImg.setMarkdown("hi " + img + " there\n\n- alpha\n- beta");
    const QString got = withImg.toPlainText();
    QVERIFY2(!got.contains("there"), qPrintable("Qt no longer truncates at inline <img> — "
             "the markdown->HTML-first ordering could be relaxed. Got: " + got));
    QVERIFY2(!got.contains("alpha"), qPrintable("expected list after <img> to be dropped: " + got));

    // Same content without the img survives intact — proving the img is the cause.
    QTextDocument noImg;
    noImg.setMarkdown("hi there\n\n- alpha\n- beta");
    const QString ok = noImg.toPlainText();
    QVERIFY2(ok.contains("there") && ok.contains("alpha"), qPrintable(ok));
}

QTEST_MAIN(TestTextEscaping)
#include "tst_textescaping.moc"
