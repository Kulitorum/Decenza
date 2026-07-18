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

private:
    QJSEngine m_engine;
    QJSValue m_theme;
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
    const QString replaceEmoji = extract("replaceEmojiWithImg");
    QVERIFY2(!escapeHtml.isEmpty(), "escapeHtml() not found in Theme.qml");
    QVERIFY2(!isEmoji.isEmpty(), "_isEmoji() not found in Theme.qml");
    QVERIFY2(!replaceEmoji.isEmpty(), "replaceEmojiWithImg() not found in Theme.qml");

    const QString program = QStringLiteral("(function(){ %1\n%2\n%3\n"
                                           "return { escapeHtml: escapeHtml,"
                                           "         replaceEmojiWithImg: replaceEmojiWithImg }; })()")
                                .arg(escapeHtml, isEmoji, replaceEmoji);

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

QTEST_MAIN(TestTextEscaping)
#include "tst_textescaping.moc"
