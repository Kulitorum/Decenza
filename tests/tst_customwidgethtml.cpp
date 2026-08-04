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
#include "core/settings_network.h"

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
    void webCatalogOmitsActionsTheWebCannotAuthor();
    void actionContextsAreDrawnFromTheKnownVocabulary();
    void neitherEditorHardCodesItsOwnActionList();
    void historyFilterKeysAreUnderstoodByTheStorageLayer();
    void gestureDestinationsAreDeclaredExactlyOnce();
    void everyGestureCapableTypeRoutesThroughTheSharedHelper();

private:
    QJSEngine m_engine;
    QJSValue m_jsSegmentsToHtml;
    QString viaJs(const QVariantList &segments);
    static QString readSource(const QString &relativePath);
};

// EVERY source path this file inspects, in one place. These tests read shipping
// source because there is no QML engine here, and that coupling is what made eight
// of them fail across one refactor — not because behaviour broke, but because a
// file moved. One constant per file means a move is one edit, and the "this test is
// now blind" guards below turn a bad path into a loud failure rather than a pass.
namespace SrcPath {
const auto kLayoutActions   = QStringLiteral("/qml/components/layout/LayoutActions.qml");
const auto kCustomItem      = QStringLiteral("/qml/components/layout/items/CustomItem.qml");
const auto kCustomEditor    = QStringLiteral("/qml/components/layout/CustomEditorPopup.qml");
const auto kReadoutOptions  = QStringLiteral("/qml/components/layout/ReadoutOptionsPopup.qml");
const auto kItemDelegate    = QStringLiteral("/qml/components/layout/LayoutItemDelegate.qml");
const auto kShotHistoryPage = QStringLiteral("/qml/pages/ShotHistoryPage.qml");
const auto kWebLayout       = QStringLiteral("/src/network/shotserver_layout.cpp");
const auto kStorageQueries  = QStringLiteral("/src/history/shothistorystorage_queries.cpp");
inline QString widgetItem(const QString &name) {
    return QStringLiteral("/qml/components/layout/items/%1.qml").arg(name);
}
}


// Lift the JS segmentsToHtml() out of the raw string literal it is served from. Brace-matched
// rather than regex'd, so a nested `}` inside the function body cannot truncate it.
void TestCustomWidgetHtml::initTestCase()
{
    const QString src = readSource(SrcPath::kWebLayout);
    QVERIFY2(!src.isEmpty(), "could not read shotserver_layout.cpp");

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

// Run the SHIPPING dispatch — LayoutActions.execute() lifted out of the QML and
// executed in a QJSEngine against recording stubs — and assert every action the
// catalog offers actually reaches a handler.
//
// This replaces a scrape that sliced the function into `category === "..."` branches
// and looked for `case "x":` text. That scrape asserted the SHAPE of the code, so it
// broke whenever the code moved and would have passed on a `case` that called the
// wrong thing. Running the real function asserts the BEHAVIOUR instead: a renamed
// branch, a reordered switch, or a rewritten dispatch all still pass as long as
// every action does something, and an action that silently does nothing fails.
//
// The stubs are recorders, not fakes: every singleton the dispatch touches becomes a
// Proxy that logs `Name.member(...)` and returns another recorder, so any call shape
// works without the test knowing what a given action is supposed to do.
static const char *kRecorderPreamble = R"JS(
    var __calls = [];
    // Some arms are guarded on a NUMBER ("is there a saved shot?", "is a recipe
    // active?"). A bare recorder fails those comparisons and the arm looks dead, so
    // these read as a plausible positive value.
    var __numeric = ["lastSavedShotId", "activeRecipeId", "activeBagId", "chargingMode",
                     "selectedFavoriteProfile"];
    function __rec(name) {
        return new Proxy(function(){}, {
            get: function(t, prop) {
                if (prop === Symbol.toPrimitive || prop === "toString") return function(){ return name; };
                if (__numeric.indexOf(String(prop)) >= 0) return 1;
                return __rec(name + "." + String(prop));
            },
            // An arm can WRITE rather than call (chargingMode = ...), so a set is
            // just as much "this action did something" as a call is.
            set: function(t, prop, value) { __calls.push(name + "." + String(prop) + "="); return true; },
            apply: function(t, self, args) { __calls.push(name); return __rec(name + "()"); },
            has: function() { return true; }
        });
    }
    var AppShell = __rec("AppShell");
    var MainController = __rec("MainController");
    var ProfileManager = __rec("ProfileManager");
    var DE1Device = __rec("DE1Device");
    var ScaleDevice = __rec("ScaleDevice");
    var MachineState = __rec("MachineState");
    var BLEManager = __rec("BLEManager");
    var BatteryManager = __rec("BatteryManager");
    var TranslationManager = __rec("TranslationManager");
    var Settings = __rec("Settings");
    var Qt = __rec("Qt");
    var console = { warn: function(m){ __calls.push("WARN:" + m); }, log: function(){} };
)JS";

void TestCustomWidgetHtml::everyCatalogActionHasADispatchArm()
{
    const QString src = readSource(SrcPath::kLayoutActions);
    QVERIFY2(!src.isEmpty(), "could not read LayoutActions.qml");

    // Lift execute() and the two helpers it calls, brace-matched like segmentsToHtml.
    QString fns;
    for (const QString &name : { QStringLiteral("function _warn("),
                                 QStringLiteral("function execute(") }) {
        const qsizetype start = src.indexOf(name);
        QVERIFY2(start >= 0, qPrintable(QStringLiteral("%1 not found in LayoutActions.qml — "
                                                       "this test is now blind").arg(name)));
        int depth = 0;
        for (qsizetype i = src.indexOf('{', start); i < src.size(); ++i) {
            if (src[i] == '{') ++depth;
            else if (src[i] == '}' && --depth == 0) {
                fns += src.mid(start, i - start + 1) + QStringLiteral("\n");
                break;
            }
        }
    }
    QVERIFY2(fns.contains(QStringLiteral("function execute(")), "could not brace-match execute()");
    // The history-filter helpers execute() calls; lifted so navigate:history* run.
    for (const QString &h : { QStringLiteral("_recipeHistoryFilter"), QStringLiteral("_beanHistoryFilter"),
                              QStringLiteral("_bagHistoryFilter"), QStringLiteral("_profileHistoryFilter") })
        fns += QStringLiteral("function %1() { return null; }\n").arg(h);

    QJSEngine eng;
    const QJSValue setup = eng.evaluate(QString::fromUtf8(kRecorderPreamble)
                                        + fns
                                        + QStringLiteral("(function(a){ __calls = []; execute(a, {}); return __calls.join('|'); })"));
    QVERIFY2(!setup.isError(), qPrintable(setup.toString()));
    QVERIFY(setup.isCallable());

    const QVariantList catalog = SettingsNetwork::layoutActionCatalog();
    QVERIFY2(catalog.size() > 35, "layoutActionCatalog() is not returning the table");

    QStringList dead;
    for (const QVariant &v : catalog) {
        const QVariantMap a = v.toMap();
        const QString id = a.value(QStringLiteral("id")).toString();
        // A parameterized action is never stored bare; it dispatches as
        // `command:loadProfile:<file>`, so that is what we exercise.
        const QString probe = a.value(QStringLiteral("expandsToSubmenu")).toBool()
            ? id + QStringLiteral(":some_profile.json") : id;
        const QJSValue out = setup.call({ probe });
        QVERIFY2(!out.isError(), qPrintable(out.toString()));
        const QString calls = out.toString();
        // Match the WORDS, not a "WARN:unknown" prefix: _warn prepends the
        // subsystem name, so the recorded line is "WARN:LayoutActions: unknown ...".
        // Anchoring on the prefix made this check unreachable — proven by breaking a
        // dispatch arm and watching the test still pass.
        if (calls.isEmpty() || calls.contains(QStringLiteral("unknown"))
                || calls.contains(QStringLiteral("malformed")))
            dead << id + QStringLiteral(" -> ") + (calls.isEmpty() ? QStringLiteral("(nothing)") : calls);
    }
    QVERIFY2(dead.isEmpty(),
             qPrintable(QStringLiteral("catalog actions that reach no handler — they would be "
                                       "offered in an editor and do nothing when tapped: ")
                        + dead.join(QStringLiteral("; "))));
}

// A parameterized action cannot be authored by a surface that has no way to
// supply the parameter: the web editor's pickAction() stores the bare id, which
// CustomItem then rejects at dispatch. Centralizing the catalog is what put such
// an action within that editor's reach, so the exclusion has to hold.
void TestCustomWidgetHtml::webCatalogOmitsActionsTheWebCannotAuthor()
{
    const QJsonObject json = SettingsNetwork::layoutActionCatalogJson();
    const QJsonArray actions = json.value(QStringLiteral("actions")).toArray();
    const QJsonObject labels = json.value(QStringLiteral("labels")).toObject();

    QVERIFY2(actions.size() > 35, "the web action list is far shorter than the table");
    // Labels cover EVERY id, offered or not — that is what keeps a stored legacy
    // action from rendering as its raw string.
    QVERIFY2(labels.size() > actions.size(),
             "the label map should cover more ids than the picker offers (legacy aliases)");
    QVERIFY2(labels.contains(QStringLiteral("command:scanDE1")),
             "the legacy alias lost its label; a stored layout will show a raw id");

    QStringList offered;
    for (const QJsonValue &v : actions)
        offered << v.toObject().value(QStringLiteral("id")).toString();

    QVERIFY2(!offered.contains(QStringLiteral("command:loadProfile")),
             "the web picker is offering a parameterized action it cannot expand; picking it "
             "stores a bare id that CustomItem rejects");
    QVERIFY2(!offered.contains(QStringLiteral("command:scanDE1")),
             "a non-picker alias is being offered");
    // The in-app picker DOES offer it — the two surfaces differ deliberately, and
    // asserting only the absence above would also pass if the action vanished.
    QStringList inApp;
    for (const QVariant &v : SettingsNetwork::layoutActionCatalog())
        inApp << v.toMap().value(QStringLiteral("id")).toString();
    QVERIFY2(inApp.contains(QStringLiteral("command:loadProfile")),
             "the in-app picker lost Load Profile");
}

// `contexts` is a space-separated string in the table, so a typo ("idel") or an
// empty value costs nothing at compile time and silently removes the action from
// every picker — invisible to whoever typed it, since a stored layout keeps
// dispatching it correctly.
void TestCustomWidgetHtml::actionContextsAreDrawnFromTheKnownVocabulary()
{
    static const QSet<QString> kKnown{
        QStringLiteral("idle"), QStringLiteral("espresso"), QStringLiteral("steam"),
        QStringLiteral("hotwater"), QStringLiteral("flush"), QStringLiteral("all") };

    const QVariantList catalog = SettingsNetwork::layoutActionCatalog();
    QVERIFY(catalog.size() > 35);

    QStringList problems;
    QSet<QString> seenIds;
    for (const QVariant &v : catalog) {
        const QVariantMap a = v.toMap();
        const QString id = a.value(QStringLiteral("id")).toString();
        if (seenIds.contains(id))
            problems << id + QStringLiteral(" (duplicate id)");
        seenIds.insert(id);

        const QStringList ctx = a.value(QStringLiteral("contexts")).toStringList();
        if (ctx.isEmpty()) {
            problems << id + QStringLiteral(" (no contexts — unreachable in every picker)");
            continue;
        }
        for (const QString &c : ctx) {
            if (!kKnown.contains(c))
                problems << id + QStringLiteral(" (unknown context '%1')").arg(c);
        }
    }
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral(", "))));
}

// The two editors must READ the catalog, not carry their own copies. They each
// did carry one, and the copies drifted by sixteen entries before anyone
// noticed — nothing failed when they diverged, which is why they did. If a
// hand-written list reappears in either file, this fails rather than waiting for
// a user to notice an action missing from one surface.
void TestCustomWidgetHtml::neitherEditorHardCodesItsOwnActionList()
{
    const QString webSrc = readSource(SrcPath::kWebLayout);
    QVERIFY2(!webSrc.isEmpty(), "could not read shotserver_layout.cpp");
    // The CONSUMPTION sites, not just the name: a bare `contains("LAYOUT_...")`
    // is satisfied by the C++ injection line alone, so the catalog could be
    // injected into the page and then ignored while this still passed. The page
    // normalizes the injected object into `CATALOG` once, so that is what the
    // two readers derive from.
    QVERIFY2(webSrc.contains(QStringLiteral("var CATALOG = ")),
             "the web editor no longer normalizes the injected catalog");
    QVERIFY2(webSrc.contains(QStringLiteral("CATALOG.actions")),
             "the web editor's ACTIONS no longer derives from the injected catalog");
    QVERIFY2(webSrc.contains(QStringLiteral("CATALOG.labels")),
             "the web editor's label lookup no longer derives from the injected catalog");
    // The community-library filter was the third hand-written copy; it is now
    // built from the same catalog.
    QVERIFY2(webSrc.contains(QStringLiteral("fillCommActionFilter")),
             "the community action filter no longer derives from the catalog");

    const QString popupSrc = readSource(SrcPath::kCustomEditor);
    QVERIFY2(!popupSrc.isEmpty(), "could not read CustomEditorPopup.qml");
    // The catalog read moved into the LayoutActions singleton when the built-in
    // widgets' options popup needed the same list — a second "read the catalog,
    // filter by context, translate" was the copy about to be written. So the
    // in-app editors must consume the singleton, and the singleton the catalog.
    const QString actionsSrc = readSource(
        SrcPath::kLayoutActions);
    QVERIFY2(!actionsSrc.isEmpty(), "could not read LayoutActions.qml");
    QVERIFY2(actionsSrc.contains(QStringLiteral("layoutActionCatalog()")),
             "LayoutActions no longer consumes SettingsNetwork::layoutActionCatalog()");
    QVERIFY2(popupSrc.contains(QStringLiteral("LayoutActions.picker")),
             "the Custom widget editor no longer gets its action list from LayoutActions");
    const QString readoutSrc = readSource(SrcPath::kReadoutOptions);
    QVERIFY2(!readoutSrc.isEmpty(), "could not read ReadoutOptionsPopup.qml");
    QVERIFY2(readoutSrc.contains(QStringLiteral("LayoutActions.picker")),
             "the built-in widgets' options popup no longer gets its action list from "
             "LayoutActions — it has grown a second copy");

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
    // Same shape in an HTML <option> list — how the community filter's copy was
    // written, and the form the two regexes above cannot see.
    static const QRegularExpression optionRe(
        QStringLiteral("<option value=\"(?:navigate|command):[A-Za-z0-9]+\">"));
    QVERIFY2(!optionRe.match(webSrc).hasMatch(),
             "shotserver_layout.cpp has re-grown a hand-written <option> action list");
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
    // The helpers moved with the dispatch into the LayoutActions singleton.
    const QString itemSrc = readSource(SrcPath::kLayoutActions);
    QVERIFY2(!itemSrc.isEmpty(), "could not read LayoutActions.qml");

    // Each helper is `function _xHistoryFilter() { ... }` — take the whole run of
    // them rather than brace-matching four times.
    const qsizetype from = itemSrc.indexOf(QStringLiteral("function _recipeHistoryFilter("));
    const qsizetype to = itemSrc.indexOf(QStringLiteral("function pickerActions("));
    QVERIFY2(from >= 0 && to > from,
             "the _*HistoryFilter helpers moved or were renamed; this test is now blind");
    const QString helpers = itemSrc.mid(from, to - from);

    // Every helper must be INSIDE that range. A fifth one added below
    // executeActionString would otherwise contribute no keys and be checked by
    // nothing, while the key-count floor below still passed on the existing four.
    QCOMPARE(helpers.count(QStringLiteral("HistoryFilter()")),
             itemSrc.count(QStringLiteral("HistoryFilter()")) - 4);  // 4 call sites in the switch

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
    // By NAME, not by count. A floor only defends against losing keys, and the
    // direction that matters here is the opposite one — a key added or renamed
    // while the existing seven stay put would sail past `size() >= 6`.
    for (const QString &expected : { QStringLiteral("recipeId"), QStringLiteral("recipeName"),
                                     QStringLiteral("beanBrand"), QStringLiteral("beanType"),
                                     QStringLiteral("bagId"), QStringLiteral("bagLabel"),
                                     QStringLiteral("profileName") }) {
        QVERIFY2(keys.contains(expected),
                 qPrintable(QStringLiteral("filter key '%1' is no longer produced by the helpers — "
                                           "renamed, or the scan stopped seeing it").arg(expected)));
    }

    const QString storageSrc =
        readSource(SrcPath::kStorageQueries);
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

    // The page forwards initialFilter WHOLESALE to the query now, excluding only
    // the banner labels. So the check is the mirror of what it was: those two
    // names must be IN the exclusion list. If one fell out, recipeName in
    // particular is a real query term in parseFilterMap (the `recipe:` keyword's
    // substring match), so forwarding it would silently widen an exact id filter
    // into a name search — a wrong result set, not an error.
    //
    // Read from the exclusion list specifically rather than the whole file: both
    // names legitimately appear elsewhere in it, where filterByRecipe and the
    // banner CONSTRUCT or display a filter.
    const QString pageSrc = readSource(SrcPath::kShotHistoryPage);
    QVERIFY2(!pageSrc.isEmpty(), "could not read ShotHistoryPage.qml");
    const qsizetype at = pageSrc.indexOf(QStringLiteral("bannerOnlyKeys = ["));
    QVERIFY2(at >= 0, "bannerOnlyKeys not found in ShotHistoryPage.qml — the merge changed shape "
                      "and this test is now blind");
    const QString list = pageSrc.mid(at, pageSrc.indexOf(QLatin1Char(']'), at) - at);
    for (const QString &k : kBannerOnly) {
        QVERIFY2(list.contains(QStringLiteral("\"%1\"").arg(k)),
                 qPrintable(QStringLiteral("%1 is missing from bannerOnlyKeys — it is a display "
                                           "label and would now be forwarded to the query").arg(k)));
    }
}


// A widget's page destination must be declared in ONE place: the C++ reservation table.
// It used to be stated three times per widget — compileToCustom's longPressAction, the
// dedicated item's goToX(), and the table — three copies free to drift with nothing
// failing if they did. This is the gate that keeps them collapsed.
void TestCustomWidgetHtml::gestureDestinationsAreDeclaredExactlyOnce()
{
    // Every type that accepts gesture overrides is declared, and the one-slot types
    // resolve to a real catalog action.
    QStringList labelIds;
    for (const QVariant &v : SettingsNetwork::layoutActionCatalog())
        labelIds << v.toMap().value(QStringLiteral("id")).toString();
    const QVariantMap labels = SettingsNetwork::layoutActionLabels();

    static const QStringList kOneSlot{
        QStringLiteral("recipes"), QStringLiteral("beans"), QStringLiteral("steam"),
        QStringLiteral("hotwater"), QStringLiteral("flush"), QStringLiteral("espresso"),
        QStringLiteral("equipment") };
    static const QStringList kTwoSlot{
        QStringLiteral("history"), QStringLiteral("autofavorites"), QStringLiteral("settings") };

    for (const QString &t : kOneSlot) {
        QVERIFY2(SettingsNetwork::typeSupportsGestureOverrides(t), qPrintable(t));
        const QString reserved = SettingsNetwork::gestureReservedActionForType(t);
        QVERIFY2(!reserved.isEmpty(),
                 qPrintable(QStringLiteral("%1 reserves no destination; its page would become "
                                           "unreachable once a gesture is overridden").arg(t)));
        QVERIFY2(labels.contains(reserved),
                 qPrintable(QStringLiteral("%1 reserves '%2', which is not a catalog action")
                            .arg(t, reserved)));
    }
    for (const QString &t : kTwoSlot) {
        QVERIFY2(SettingsNetwork::typeSupportsGestureOverrides(t), qPrintable(t));
        QVERIFY2(SettingsNetwork::gestureReservedActionForType(t).isEmpty(),
                 qPrintable(QStringLiteral("%1 opens its page on TAP, so it must reserve nothing "
                                           "and leave both gestures free").arg(t)));
    }

    // No surface may re-state a destination. compileToCustom must not hand-write a
    // gesture action for a type that reserves one, and no dedicated item may call its
    // own navigation from a gesture handler.
    const QString delegateSrc =
        readSource(SrcPath::kItemDelegate);
    QVERIFY2(!delegateSrc.isEmpty(), "could not read LayoutItemDelegate.qml");
    for (const QString &t : kOneSlot) {
        const QString reserved = SettingsNetwork::gestureReservedActionForType(t);
        QVERIFY2(!delegateSrc.contains(QStringLiteral("longPressAction: \"%1\"").arg(reserved))
                     && !delegateSrc.contains(QStringLiteral("doubleclickAction: \"%1\"").arg(reserved)),
                 qPrintable(QStringLiteral("LayoutItemDelegate re-states %1's destination '%2'; it "
                                           "must come from the reservation table").arg(t, reserved)));
    }
}

// Ten widgets, two render formats. A file whose gesture handler still calls its own
// navigation directly ignores the user's override in that format only — visible nowhere
// except by using that widget in that zone.
void TestCustomWidgetHtml::everyGestureCapableTypeRoutesThroughTheSharedHelper()
{
    static const QStringList kFiles{
        QStringLiteral("RecipesItem"), QStringLiteral("BeansItem"), QStringLiteral("SteamItem"),
        QStringLiteral("HotWaterItem"), QStringLiteral("FlushItem"), QStringLiteral("EspressoItem"),
        QStringLiteral("EquipmentItem"), QStringLiteral("HistoryItem"),
        QStringLiteral("AutoFavoritesItem"), QStringLiteral("SettingsItem") };

    QStringList problems;
    for (const QString &f : kFiles) {
        const QString src = readSource(SrcPath::widgetItem(f));
        if (src.isEmpty()) { problems << f + QStringLiteral(" (unreadable)"); continue; }
        for (const QString &handler : { QStringLiteral("onAccessibleLongPressed"),
                                        QStringLiteral("onAccessibleDoubleClicked") }) {
            const qsizetype at = src.indexOf(handler);
            if (at < 0) { problems << f + QStringLiteral(" (no %1)").arg(handler); continue; }
            const QString line = src.mid(at, src.indexOf(QLatin1Char('\n'), at) - at);
            if (!line.contains(QStringLiteral("LayoutActions.runGesture")))
                problems << QStringLiteral("%1.%2 does not route through LayoutActions")
                                .arg(f, handler);
        }
    }
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral("; "))));

    // Double-click is ALWAYS supported, on every widget and in both render
    // formats. An action can be assigned to it at any time, and a widget that
    // only starts listening once one is stored is a widget whose gesture set
    // changes shape under the user. CustomItem used to gate on
    // `doubleclickAction !== ""`, which left the compiled form deaf until
    // configured; this pins the uniform behaviour so it cannot creep back.
    QStringList hardcoded;
    for (const QString &f : kFiles + QStringList{ QStringLiteral("CustomItem") }) {
        const QString src = readSource(SrcPath::widgetItem(f));
        if (src.isEmpty()) { hardcoded << f + QStringLiteral(" (unreadable)"); continue; }
        qsizetype at = 0;
        while ((at = src.indexOf(QStringLiteral("supportDoubleClick:"), at)) >= 0) {
            const QString line = src.mid(at, src.indexOf(QLatin1Char('\n'), at) - at);
            if (!line.contains(QStringLiteral("true")))
                hardcoded << f + QStringLiteral(": ") + line.trimmed();
            at += 1;
        }
    }
    QVERIFY2(hardcoded.isEmpty(),
             qPrintable(QStringLiteral("supportDoubleClick is conditional; it must be "
                                       "unconditionally true so a double-click action can be "
                                       "added at any time: ")
                        + hardcoded.join(QStringLiteral("; "))));

    // And the dispatch itself stays in one place: CustomItem must delegate, not carry a
    // second copy of the switch.
    const QString custom = readSource(SrcPath::kCustomItem);
    QVERIFY2(custom.contains(QStringLiteral("LayoutActions.execute")),
             "CustomItem no longer delegates to the shared dispatch");
    QVERIFY2(!custom.contains(QStringLiteral("category === \"navigate\"")),
             "CustomItem has re-grown its own action dispatch");
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
