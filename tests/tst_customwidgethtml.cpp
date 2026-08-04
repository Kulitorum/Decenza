// Differential guard on the TWO implementations of segmentsToHtml().
//
// A custom layout widget can be authored in the app (CustomEditorPopup -> DocumentFormatter,
// src/core/documentformatter.cpp) or in the web layout editor (the JS served from
// src/network/shotserver_layout.cpp). Both compile the same segment schema to the same stored
// `content` string, so the two must agree byte for byte — CLAUDE.md's rule that the app and
// ShotServer surfaces stay in sync. Neither had any test, and they had already drifted: the
// C++ side escapes the double quote via QString::toHtmlEscaped() (qstring.cpp:10129) and the
// JS side did not.
//
// The JS is run out of shipping source, not reimplemented here. A copy would drift and then
// assert that the copy is correct, which is worse than no test — same reasoning as
// tst_textescaping.cpp, whose extraction approach this follows.
//
// Also pins DocumentFormatter's own colour round trip, which had no coverage at all. The
// distinction it guards: NO stored colour is the "Default" state that makes a widget follow
// the theme, and an explicit colour — including black — is stored as itself. Black used to
// double as the sentinel, so choosing black in the editor gave back theme-coloured text.

#include <QtTest>
#include <QJSEngine>
#include <QFile>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextCharFormat>

#include "core/documentformatter.h"

class TestCustomWidgetHtml : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }
    void initTestCase();

    void bothImplementationsAgree_data();
    void bothImplementationsAgree();

    void roundTripPreservesExplicitColourAndSize();
    void colourIsDroppedOnlyWhenUnset();
    void explicitBlackIsStored();
    void clearColorReturnsToDefault();
    void clearColorLeavesOtherRunsAlone();

    void everyCatalogActionHasADispatchArm();
    void neitherEditorHardCodesItsOwnActionList();
    void historyFilterKeysAreUnderstoodByTheStorageLayer();

private:
    QJSEngine m_engine;
    QJSValue m_jsSegmentsToHtml;
    QString viaJs(const QVariantList &segments);
    static QString readSource(const QString &relativePath);
};

// Lift the JS segmentsToHtml() out of the raw string literal it is served from. Brace-matched
// rather than regex'd, so a nested `}` inside the function body cannot truncate it.
void TestCustomWidgetHtml::initTestCase()
{
    QFile f(QStringLiteral(DECENZA_SOURCE_DIR) + "/src/network/shotserver_layout.cpp");
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(f.errorString()));
    const QString src = QString::fromUtf8(f.readAll());

    const QString needle = QStringLiteral("function segmentsToHtml(");
    const qsizetype start = src.indexOf(needle);
    QVERIFY2(start >= 0, "function segmentsToHtml( not found in shotserver_layout.cpp — the "
                         "web editor's copy moved or was renamed; this test is now blind");
    QVERIFY2(src.indexOf(needle, start + 1) < 0,
             "more than one segmentsToHtml( in shotserver_layout.cpp — cannot tell which one "
             "the web editor serves");

    QString body;
    int depth = 0;
    for (qsizetype i = src.indexOf('{', start); i < src.size(); ++i) {
        if (src[i] == '{') ++depth;
        else if (src[i] == '}' && --depth == 0) {
            body = src.mid(start, i - start + 1);
            break;
        }
    }
    QVERIFY2(!body.isEmpty(), "could not brace-match the JS segmentsToHtml body");

    const QJSValue result = m_engine.evaluate(QStringLiteral("(function(){ %1 return segmentsToHtml; })()").arg(body));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    m_jsSegmentsToHtml = result;
    QVERIFY(m_jsSegmentsToHtml.isCallable());
}

QString TestCustomWidgetHtml::readSource(const QString &relativePath)
{
    QFile f(QStringLiteral(DECENZA_SOURCE_DIR) + relativePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

// Centralizing the action catalog (layout-action-catalog) moved the picker's
// list away from the code that dispatches it. That buys one declaration and
// costs the adjacency that used to make a missing dispatch arm obvious: an entry
// can now be offered in both editors, saved onto a widget, and do nothing when
// tapped, with no warning anywhere until a user reports a dead button.
//
// So: every id the catalog offers must have a matching `case` in
// CustomItem.executeActionString(). Read out of shipping source rather than
// linked, which keeps this target's link set unchanged (the catalog lives in
// settings_network.cpp, which drags in the whole Settings facade).
void TestCustomWidgetHtml::everyCatalogActionHasADispatchArm()
{
    const QString catalogSrc = readSource(QStringLiteral("/src/core/settings_network.cpp"));
    QVERIFY2(!catalogSrc.isEmpty(), "could not read settings_network.cpp");

    const qsizetype tableStart = catalogSrc.indexOf(QStringLiteral("kActions = {"));
    QVERIFY2(tableStart >= 0, "layoutActionTable's kActions initializer not found — the catalog "
                              "moved or was renamed; this test is now blind");
    const qsizetype tableEnd = catalogSrc.indexOf(QStringLiteral("};"), tableStart);
    QVERIFY(tableEnd > tableStart);
    const QString table = catalogSrc.mid(tableStart, tableEnd - tableStart);

    // { "navigate:historyBag", "customaction...", ... }  →  navigate / historyBag
    static const QRegularExpression entryRe(
        QStringLiteral("\\{\\s*\"(navigate|command):([A-Za-z0-9]+)\""));
    QStringList ids;
    auto it = entryRe.globalMatch(table);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        ids << m.captured(1) + QLatin1Char(':') + m.captured(2);
    }
    // A regex that silently matched nothing would make this test pass forever.
    QVERIFY2(ids.size() > 20, qPrintable(QStringLiteral("only %1 catalog entries parsed — the "
             "table's formatting changed and this test is no longer reading it").arg(ids.size())));

    const QString itemSrc = readSource(QStringLiteral("/qml/components/layout/items/CustomItem.qml"));
    QVERIFY2(!itemSrc.isEmpty(), "could not read CustomItem.qml");

    QStringList missing;
    for (const QString &id : ids) {
        const QString target = id.section(QLatin1Char(':'), 1);
        // executeActionString() splits `category:target` and switches on the
        // target within the category's branch, so the arm is `case "target":`.
        // One action is PARAMETERIZED instead — `command:loadProfile:<file>` is
        // matched by prefix before the switch, so it has no case of its own and
        // the bare id is never stored. Accept either shape.
        if (itemSrc.contains(QStringLiteral("case \"%1\":").arg(target)))
            continue;
        if (itemSrc.contains(QStringLiteral("indexOf(\"%1:\") === 0").arg(target)))
            continue;
        missing << id;
    }
    QVERIFY2(missing.isEmpty(),
             qPrintable(QStringLiteral("catalog actions with no dispatch arm in CustomItem.qml "
                                       "(they would be offered in both editors and do nothing): ")
                        + missing.join(QStringLiteral(", "))));
}

// The two editors must READ the catalog, not carry their own copies. They each
// did carry one, and the copies drifted by sixteen entries before anyone
// noticed — nothing failed when they diverged, which is why they did. If a
// hand-written list reappears in either file, this fails rather than waiting for
// a user to notice an action missing from one surface.
void TestCustomWidgetHtml::neitherEditorHardCodesItsOwnActionList()
{
    const QString webSrc = readSource(QStringLiteral("/src/network/shotserver_layout.cpp"));
    QVERIFY2(!webSrc.isEmpty(), "could not read shotserver_layout.cpp");
    QVERIFY2(webSrc.contains(QStringLiteral("LAYOUT_ACTIONS")),
             "the web editor no longer consumes the injected LAYOUT_ACTIONS catalog");

    const QString popupSrc = readSource(
        QStringLiteral("/qml/components/layout/CustomEditorPopup.qml"));
    QVERIFY2(!popupSrc.isEmpty(), "could not read CustomEditorPopup.qml");
    QVERIFY2(popupSrc.contains(QStringLiteral("layoutActionCatalog()")),
             "the in-app picker no longer consumes SettingsNetwork::layoutActionCatalog()");

    // An action id written as a literal ALONGSIDE a label is the copy shape.
    // Bare ids are fine and expected — CustomItem's dispatch, the compiled
    // action buttons in LayoutItemDelegate, `command:loadProfile:<file>`
    // assembly — so match only id-with-label pairs.
    static const QRegularExpression pairRe(
        QStringLiteral("\"(?:navigate|command):[A-Za-z0-9]+\"\\s*,\\s*label\\s*:"));
    QVERIFY2(!pairRe.match(webSrc).hasMatch(),
             "shotserver_layout.cpp has re-grown a hand-written {id, label} action list");
    static const QRegularExpression qmlPairRe(
        QStringLiteral("id:\\s*\"(?:navigate|command):[A-Za-z0-9]+\"\\s*,\\s*label\\s*:"));
    QVERIFY2(!qmlPairRe.match(popupSrc).hasMatch(),
             "CustomEditorPopup.qml has re-grown a hand-written {id, label} action list");
}

// The Custom widget's four History actions hand ShotHistoryPage an
// `initialFilter` map, which is forwarded to the storage layer's
// parseFilterMap(). A key the storage does not read is not an error anywhere: it
// is dropped in silence and the user gets an UNFILTERED list that looks like a
// filtered one. `bagID` for `bagId` would do it, and so would a key that was
// renamed on the C++ side alone.
//
// So every key those four helpers put in an initialFilter must either be read by
// parseFilterMap or be one of the two the page deliberately keeps for the banner
// and never sends to the query. Source-level for the same reason as the tests
// above: exercising the helpers needs Settings, MainController and ProfileManager
// live.
void TestCustomWidgetHtml::historyFilterKeysAreUnderstoodByTheStorageLayer()
{
    const QString itemSrc = readSource(QStringLiteral("/qml/components/layout/items/CustomItem.qml"));
    QVERIFY2(!itemSrc.isEmpty(), "could not read CustomItem.qml");

    // Each helper is `function _xHistoryFilter() { ... }` — take the whole run of
    // them rather than brace-matching four times.
    const qsizetype from = itemSrc.indexOf(QStringLiteral("function _recipeHistoryFilter("));
    const qsizetype to = itemSrc.indexOf(QStringLiteral("function executeActionString("));
    QVERIFY2(from >= 0 && to > from,
             "the _*HistoryFilter helpers moved or were renamed; this test is now blind");
    const QString helpers = itemSrc.mid(from, to - from);

    // Keys assigned into a filter object, in either shape the helpers use:
    // `f.beanBrand = ...` and `{ bagId: id, bagLabel: label }`.
    QSet<QString> keys;
    static const QRegularExpression dotRe(QStringLiteral("\\bf\\.([A-Za-z][A-Za-z0-9]*)\\s*="));
    auto dotIt = dotRe.globalMatch(helpers);
    while (dotIt.hasNext())
        keys.insert(dotIt.next().captured(1));
    static const QRegularExpression literalRe(
        QStringLiteral("[\\{,]\\s*([A-Za-z][A-Za-z0-9]*)\\s*:"));
    const qsizetype litFrom = helpers.indexOf(QStringLiteral("initialFilter:"));
    QVERIFY(litFrom >= 0);
    auto litIt = literalRe.globalMatch(helpers, litFrom);
    while (litIt.hasNext()) {
        const QString k = litIt.next().captured(1);
        if (k != QLatin1String("initialFilter"))
            keys.insert(k);
    }
    QVERIFY2(keys.size() >= 6, qPrintable(QStringLiteral("only %1 filter keys parsed — the helpers' "
             "formatting changed and this test is no longer reading them").arg(keys.size())));

    const QString storageSrc =
        readSource(QStringLiteral("/src/history/shothistorystorage_queries.cpp"));
    QVERIFY2(!storageSrc.isEmpty(), "could not read shothistorystorage_queries.cpp");

    // Read by ShotHistoryPage for the banner label and deliberately never
    // forwarded to the query — see the numericFields/filterFields loops there.
    static const QStringList kBannerOnly{ QStringLiteral("recipeName"), QStringLiteral("bagLabel") };

    QStringList unknown;
    for (const QString &k : keys) {
        if (kBannerOnly.contains(k))
            continue;
        if (storageSrc.contains(QStringLiteral("filterMap.value(\"%1\"").arg(k)))
            continue;
        unknown << k;
    }
    QVERIFY2(unknown.isEmpty(),
             qPrintable(QStringLiteral("initialFilter keys the storage layer never reads — these "
                                       "are dropped silently and yield an UNFILTERED list: ")
                        + unknown.join(QStringLiteral(", "))));

    // The banner-only keys must stay OUT of the page's two passthrough arrays.
    // Checked against those arrays specifically, not the whole file: both names
    // legitimately appear elsewhere in it, where filterByRecipe/filterByBag
    // CONSTRUCT a filter. Adding one to a passthrough list would send a display
    // label to the query, where it would be matched as a term.
    const QString pageSrc = readSource(QStringLiteral("/qml/pages/ShotHistoryPage.qml"));
    QVERIFY2(!pageSrc.isEmpty(), "could not read ShotHistoryPage.qml");
    for (const QString &arrayName : { QStringLiteral("filterFields"), QStringLiteral("numericFields") }) {
        const qsizetype at = pageSrc.indexOf(arrayName + QStringLiteral(" = ["));
        QVERIFY2(at >= 0, qPrintable(QStringLiteral("%1 array not found in ShotHistoryPage.qml — "
                                                    "this test is now blind").arg(arrayName)));
        const QString list = pageSrc.mid(at, pageSrc.indexOf(QLatin1Char(']'), at) - at);
        for (const QString &k : kBannerOnly) {
            QVERIFY2(!list.contains(QStringLiteral("\"%1\"").arg(k)),
                     qPrintable(QStringLiteral("%1 is in the %2 passthrough array — it is a banner "
                                               "label, never a query term").arg(k, arrayName)));
        }
    }
}

QString TestCustomWidgetHtml::viaJs(const QVariantList &segments)
{
    QJSValue arg = m_engine.toScriptValue(segments);
    QJSValue out = m_jsSegmentsToHtml.call(QJSValueList{arg});
    if (out.isError())
        return QStringLiteral("<JS ERROR: ") + out.toString() + QStringLiteral(">");
    return out.toString();
}

void TestCustomWidgetHtml::bothImplementationsAgree_data()
{
    QTest::addColumn<QVariantList>("segments");

    auto seg = [](const QString &text, const QVariantMap &extra = {}) {
        QVariantMap m = extra;
        m[QStringLiteral("text")] = text;
        return QVariant(m);
    };

    QTest::newRow("plain") << QVariantList{ seg(QStringLiteral("Hello")) };
    QTest::newRow("bold") << QVariantList{ seg(QStringLiteral("Hello"), {{QStringLiteral("bold"), true}}) };
    QTest::newRow("italic") << QVariantList{ seg(QStringLiteral("Hello"), {{QStringLiteral("italic"), true}}) };
    QTest::newRow("colour") << QVariantList{ seg(QStringLiteral("Hello"), {{QStringLiteral("color"), QStringLiteral("#00cc6d")}}) };
    QTest::newRow("size") << QVariantList{ seg(QStringLiteral("Hello"), {{QStringLiteral("size"), 28}}) };
    QTest::newRow("colour+size") << QVariantList{
        seg(QStringLiteral("Hello"), {{QStringLiteral("color"), QStringLiteral("#00cc6d")},
                                      {QStringLiteral("size"), 48}}) };
    QTest::newRow("newline") << QVariantList{ seg(QStringLiteral("a")), seg(QStringLiteral("\n")), seg(QStringLiteral("b")) };
    QTest::newRow("empty-skipped") << QVariantList{ seg(QString()), seg(QStringLiteral("x")) };
    QTest::newRow("ampersand") << QVariantList{ seg(QStringLiteral("Tea & Coffee")) };
    QTest::newRow("angle-brackets") << QVariantList{ seg(QStringLiteral("a <b> c")) };
    // The drift this test was written for: legal unescaped in element content, so both render
    // the same, but the two surfaces stored different bytes for the same widget.
    QTest::newRow("double-quote") << QVariantList{ seg(QStringLiteral("say \"hi\"")) };
    QTest::newRow("quote-inside-span") << QVariantList{
        seg(QStringLiteral("say \"hi\""), {{QStringLiteral("color"), QStringLiteral("#ff0000")}}) };
    QTest::newRow("token") << QVariantList{ seg(QStringLiteral("%STATE%")) };
    QTest::newRow("emoji") << QVariantList{ seg(QStringLiteral("café ☕")) };
    QTest::newRow("all-at-once") << QVariantList{
        seg(QStringLiteral("A&\"<"), {{QStringLiteral("bold"), true},
                                      {QStringLiteral("italic"), true},
                                      {QStringLiteral("color"), QStringLiteral("#123456")},
                                      {QStringLiteral("size"), 12}}) };
}

void TestCustomWidgetHtml::bothImplementationsAgree()
{
    QFETCH(QVariantList, segments);

    const QString cpp = DocumentFormatter::segmentsToHtml(segments);
    const QString js = viaJs(segments);

    QCOMPARE(js, cpp);
}

// A colour and size chosen in the editor survive document -> segments -> HTML. This is the
// path the custom-widget styling actually travels when saved.
void TestCustomWidgetHtml::roundTripPreservesExplicitColourAndSize()
{
    QTextDocument doc;
    QTextCursor cur(&doc);
    QTextCharFormat fmt;
    fmt.setForeground(QColor(QStringLiteral("#00cc6d")));
    fmt.setProperty(QTextFormat::FontPixelSize, 28);
    cur.insertText(QStringLiteral("Styled"), fmt);

    DocumentFormatter f;
    f.setTextDocumentForTesting(&doc);
    const QVariantList segments = f.toSegments();

    QCOMPARE(segments.size(), 1);
    const QVariantMap seg = segments.first().toMap();
    QCOMPARE(seg.value(QStringLiteral("text")).toString(), QStringLiteral("Styled"));
    QCOMPARE(seg.value(QStringLiteral("color")).toString(), QStringLiteral("#00cc6d"));
    QCOMPARE(seg.value(QStringLiteral("size")).toInt(), 28);

    const QString html = DocumentFormatter::segmentsToHtml(segments);
    QVERIFY2(html.contains(QStringLiteral("color:#00cc6d")), qPrintable(html));
    QVERIFY2(html.contains(QStringLiteral("font-size:28px")), qPrintable(html));
}

// The two states must be distinguishable, because they mean opposite things:
//
//   - NO stored colour is the "Default" state: the text follows the widget's theme colour.
//     That absence is load-bearing — emitting a colour here would pin every custom widget
//     and break dark mode.
//   - An explicitly chosen colour is stored, INCLUDING black. Black used to double as the
//     sentinel above, so picking black in the editor gave back theme-coloured text, which on
//     a dark theme is the opposite of what was asked for.
void TestCustomWidgetHtml::colourIsDroppedOnlyWhenUnset()
{
    QTextDocument doc;
    QTextCursor cur(&doc);
    cur.insertText(QStringLiteral("Unstyled"));

    DocumentFormatter f;
    f.setTextDocumentForTesting(&doc);
    QVariantList segments = f.toSegments();
    QCOMPARE(segments.size(), 1);
    QVERIFY2(!segments.first().toMap().contains(QStringLiteral("color")),
             "unstyled text must carry no colour, or every custom widget stops following "
             "the theme");
}

void TestCustomWidgetHtml::explicitBlackIsStored()
{
    QTextDocument doc;
    QTextCursor cur(&doc);
    QTextCharFormat fmt;
    fmt.setForeground(QColor(Qt::black));
    cur.insertText(QStringLiteral("Deliberate black"), fmt);

    DocumentFormatter f;
    f.setTextDocumentForTesting(&doc);
    const QVariantList segments = f.toSegments();
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments.first().toMap().value(QStringLiteral("color")).toString(),
             QStringLiteral("#000000"));
}

// "Default" is a distinct action, not a shade — mergeCharFormat cannot remove a property, so
// clearColorOnRange rewrites the affected fragments' formats.
void TestCustomWidgetHtml::clearColorReturnsToDefault()
{
    QTextDocument doc;
    QTextCursor cur(&doc);
    QTextCharFormat fmt;
    fmt.setForeground(QColor(QStringLiteral("#ff0000")));
    cur.insertText(QStringLiteral("Red"), fmt);

    DocumentFormatter f;
    f.setTextDocumentForTesting(&doc);
    QCOMPARE(f.toSegments().first().toMap().value(QStringLiteral("color")).toString(),
             QStringLiteral("#ff0000"));

    f.clearColorOnRange(0, 3);

    const QVariantList segments = f.toSegments();
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments.first().toMap().value(QStringLiteral("text")).toString(),
             QStringLiteral("Red"));
    QVERIFY2(!segments.first().toMap().contains(QStringLiteral("color")),
             "clearColorOnRange must remove the colour, not set one");
}

// Clearing one run must not touch its neighbours.
void TestCustomWidgetHtml::clearColorLeavesOtherRunsAlone()
{
    QTextDocument doc;
    QTextCursor cur(&doc);
    QTextCharFormat red;
    red.setForeground(QColor(QStringLiteral("#ff0000")));
    QTextCharFormat green;
    green.setForeground(QColor(QStringLiteral("#00ff00")));
    cur.insertText(QStringLiteral("AAA"), red);
    cur.insertText(QStringLiteral("BBB"), green);

    DocumentFormatter f;
    f.setTextDocumentForTesting(&doc);
    f.clearColorOnRange(0, 3);

    const QVariantList segments = f.toSegments();
    QCOMPARE(segments.size(), 2);
    QVERIFY2(!segments.at(0).toMap().contains(QStringLiteral("color")), "first run should be Default");
    QCOMPARE(segments.at(1).toMap().value(QStringLiteral("color")).toString(),
             QStringLiteral("#00ff00"));
}

QTEST_MAIN(TestCustomWidgetHtml)
#include "tst_customwidgethtml.moc"
