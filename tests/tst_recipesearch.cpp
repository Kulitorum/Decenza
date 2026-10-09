// Guards list search and sort on BOTH surfaces that implement them:
//
//   1. The app: qml/components/RecipeSearch.js, used by RecipesPage and BeanInfoPage.
//   2. The ShotServer web pages: normalizeSearch, tokenizeSearch and sortedCopy in
//      webtemplates/management_js.h, matchesFilter in shotserver_recipes.cpp and
//      bagHaystack in shotserver_bags.cpp.
//
// Both are loaded from their REAL shipping source (read from DECENZA_SOURCE_DIR,
// evaluated in a QJSEngine), so a drift on either surface fails here.
//
// The bug this exists for: "Yirg Df" found nothing on Recipes although the coffee is
// a Yirgacheffe and the profile "D-Flow / Q". The match now tokenizes the query and
// DELETES `-` `/` `.` ("D-Flow" -> "dflow"), requiring every token (AND).
//
// Settings search lives here too, for the same reason: it is shipping JS
// (qml/components/SettingsSearchMatcher.mjs over the vendored Fuse.js, and the generated
// qml/components/SettingsSearchEntries.js) evaluated in a QJSEngine.

#include <QtTest>
#include <QJSEngine>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

class TestRecipeSearch : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }
    void initTestCase();

    // In-app matcher (RecipeSearch.js)
    void crossFieldQueryMatches();
    void tokenSpanningPunctuation();
    void allTokensRequired();
    void singleTokenStillMatches();
    void emptyQueryMatchesEverything();
    void caseInsensitive();
    void collapsesInternalWhitespace();
    void matchesToleratesUnnormalizedTokens();
    void matchesNoOpTokenIsIgnored();

    // In-app haystack assembly (RecipeSearch.buildHaystack, as RecipesPage uses it)
    void appHaystackSearchesAllFields();

    // Web matcher (shotserver_recipes.cpp) — must agree with the in-app matcher
    void webMatcherAgreesOnSharedCases();
    void webSearchesDrinkType();

    // Beans page: every text value a bag holds, never its ids or links; app and web agree
    void bagSearchCoversAllTextAndAgreesWithWeb();
    void bagSearchSkipsBookkeepingAtEveryDepth();

    // Recipes and Beans order: blanks last both ways, ties by id; app and web agree
    void sortedCopyBlanksLastAndAgreesWithWeb();

    // Settings search: every query the old hand-written index answered still finds its card
    void settingsSearchSnapshotStillFound();
    // Settings search ranking, typo/accent tolerance, AND, and availability filtering
    void settingsSearch_data();
    void settingsSearch();

private:
    QJSEngine m_engine;
    QJSValue m_lib;   // RecipeSearch.js
    QJSValue m_web;   // extracted web matcher
    QJSValue m_webBagHaystack;   // bagHaystack from shotserver_bags.cpp
    QJSValue m_webSortedCopy;    // sortedCopy from management_js.h
    QJSValue m_settingsMatcher;  // SettingsSearchMatcher.mjs
    QJSValue m_settingsEntries;  // SettingsSearchEntries.js `entries`
    QJSValue m_settingsLegacy;   // SettingsSearchIndex.js getSearchEntries (migration bridge)
    // Matcher over the current index, with `available` conditions and a key -> text map
    // standing in for TranslationManager.
    QJSValue settingsMatcher(const QStringList& available, const QVariantMap& translations = {});
    // Both surfaces' haystacks for one bag, after checking they are identical.
    QString bagHaystack(const QJSValue& bag, const QString& kindLabel);
    bool match(const QString& haystack, const QString& query);
    // Web matcher: builds a recipe {name, profileTitle, roaster, coffee, drinkType}
    // and returns matchesFilter against the tokenized query.
    bool webMatch(const QString& name, const QString& profileTitle, const QString& roaster,
                  const QString& coffee, const QString& drinkType, const QString& query);
};

// Pull a `function <name>(...) { ... }` block out of source by brace matching. Fails
// (returns empty) if the name is absent or appears more than once, so an ambiguous
// extraction is caught loudly by the caller rather than testing the wrong text.
//
// Constraint: this counts raw `{`/`}` with no awareness of strings, regex, or object
// literals, so the extracted web functions must keep their BODIES brace-free (no
// `${...}`, `/\s{2,}/`, or inline `{}`). A stray brace inside a string could truncate
// the block into something that still parses; if these functions grow braces, switch
// to a real tokenizer or assert the extracted text ends at the true function end.
static QString extractFunction(const QString& src, const QString& name)
{
    const QString needle = "function " + name + "(";
    const qsizetype start = src.indexOf(needle);
    if (start < 0) return QString();
    if (src.indexOf(needle, start + 1) >= 0) return QString();  // ambiguous
    int depth = 0;
    qsizetype i = src.indexOf('{', start);
    if (i < 0) return QString();
    for (; i < src.size(); ++i) {
        if (src[i] == '{') ++depth;
        else if (src[i] == '}') {
            if (--depth == 0) return src.mid(start, i - start + 1);
        }
    }
    return QString();
}

static QString readSource(const QString& relPath)
{
    QFile f(QStringLiteral(DECENZA_SOURCE_DIR) + relPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

void TestRecipeSearch::initTestCase()
{
    // --- Settings search: the ES module (and the vendored Fuse.js it imports) ---
    m_settingsMatcher = m_engine.importModule(
        QStringLiteral(DECENZA_SOURCE_DIR "/qml/components/SettingsSearchMatcher.mjs"));
    QVERIFY2(!m_settingsMatcher.isError(), qPrintable(m_settingsMatcher.toString()));
    QVERIFY2(m_settingsMatcher.property("createMatcher").isCallable(), "createMatcher() not exported");
    for (const auto& [rel, expr, out] : {
             std::tuple{"/qml/components/SettingsSearchEntries.js", "entries", &m_settingsEntries},
             std::tuple{"/qml/components/SettingsSearchIndex.js", "getSearchEntries", &m_settingsLegacy}}) {
        QString src = readSource(QLatin1String(rel));
        QVERIFY2(!src.isEmpty(), rel);
        src.remove(QRegularExpression("^\\s*\\.pragma\\s+library\\s*$",
                                      QRegularExpression::MultilineOption));
        *out = m_engine.evaluate(QStringLiteral("(function(){ %1\n return %2; })()").arg(src, QLatin1String(expr)));
        QVERIFY2(!out->isError() && !out->isUndefined(), rel);
    }

    // --- In-app matcher: RecipeSearch.js (strip the QML `.pragma library` line) ---
    QString js = readSource("/qml/components/RecipeSearch.js");
    QVERIFY2(!js.isEmpty(), "could not read RecipeSearch.js");
    js.remove(QRegularExpression("^\\s*\\.pragma\\s+library\\s*$",
                                 QRegularExpression::MultilineOption));
    const QString libProgram =
        QStringLiteral("(function(){ %1\n return { tokenize: tokenize, matches: matches,"
                       "     normalize: normalize, buildHaystack: buildHaystack,"
                       "     buildBagHaystack: buildBagHaystack, bagSkipKeys: _bagSkipKeys,"
                       "     sortedCopy: sortedCopy }; })()").arg(js);
    m_lib = m_engine.evaluate(libProgram);
    QVERIFY2(!m_lib.isError(), qPrintable(m_lib.toString()));
    QVERIFY2(m_lib.property("matches").isCallable(), "matches() not found in RecipeSearch.js");
    QVERIFY2(m_lib.property("tokenize").isCallable(), "tokenize() not found in RecipeSearch.js");
    QVERIFY2(m_lib.property("buildHaystack").isCallable(), "buildHaystack() not found in RecipeSearch.js");

    // --- Web matcher: extracted from generateRecipesPage() in shotserver_recipes.cpp ---
    const QString cpp = readSource("/src/network/shotserver_recipes.cpp");
    QVERIFY2(!cpp.isEmpty(), "could not read shotserver_recipes.cpp");
    // The tokenizer is shared by the web /recipes and /beans pages (WEB_JS_MANAGEMENT).
    const QString sharedJs = readSource("/src/network/webtemplates/management_js.h");
    QVERIFY2(!sharedJs.isEmpty(), "could not read management_js.h");
    const QString normalizeSearch = extractFunction(sharedJs, "normalizeSearch");
    const QString tokenizeSearch = extractFunction(sharedJs, "tokenizeSearch");
    const QString matchesFilter = extractFunction(cpp, "matchesFilter");
    QVERIFY2(!normalizeSearch.isEmpty(), "normalizeSearch() not found (or ambiguous) in management_js.h");
    QVERIFY2(!tokenizeSearch.isEmpty(), "tokenizeSearch() not found (or ambiguous) in management_js.h");
    QVERIFY2(!matchesFilter.isEmpty(), "matchesFilter() not found (or ambiguous) in shotserver_recipes.cpp");

    // matchesFilter references drinkLabel(); stub it to echo the raw drink type so the
    // field's INCLUSION is exercised without pulling in the DRINK_LABELS map. webMatch
    // routes through the REAL tokenizeSearch used by render(), so the query-tokenization
    // step is guarded too, not re-implemented here.
    const QString webProgram =
        QStringLiteral("(function(){ var drinkLabel = function(t){ return t || ''; };\n"
                       "  %1\n  %2\n  %3\n"
                       "  return { normalizeSearch: normalizeSearch, tokenizeSearch: tokenizeSearch,"
                       "    webMatch:"
                       "    function(name, profileTitle, roaster, coffee, drinkType, q) {"
                       "      var r = { name: name, profileTitle: profileTitle, roasterName: roaster,"
                       "                coffeeName: coffee, drinkType: drinkType };"
                       "      return matchesFilter(r, tokenizeSearch(q));"
                       "    } }; })()").arg(normalizeSearch, tokenizeSearch, matchesFilter);
    m_web = m_engine.evaluate(webProgram);
    QVERIFY2(!m_web.isError(), qPrintable(m_web.toString()));
    QVERIFY2(m_web.property("webMatch").isCallable(), "web matcher failed to build");

    const QString bagHaystack = extractFunction(readSource("/src/network/shotserver_bags.cpp"), "bagHaystack");
    QVERIFY2(!bagHaystack.isEmpty(), "bagHaystack() not found (or ambiguous) in shotserver_bags.cpp");
    m_webBagHaystack = m_engine.evaluate(QStringLiteral("(function(){ %1\n return bagHaystack; })()").arg(bagHaystack));
    QVERIFY2(m_webBagHaystack.isCallable(), qPrintable(m_webBagHaystack.toString()));

    const QString sortedCopy = extractFunction(sharedJs, "sortedCopy");
    QVERIFY2(!sortedCopy.isEmpty(), "sortedCopy() not found (or ambiguous) in management_js.h");
    m_webSortedCopy = m_engine.evaluate(QStringLiteral("(function(){ %1\n return sortedCopy; })()").arg(sortedCopy));
    QVERIFY2(m_webSortedCopy.isCallable(), qPrintable(m_webSortedCopy.toString()));
}

QString TestRecipeSearch::bagHaystack(const QJSValue& bag, const QString& kindLabel)
{
    const QJSValue app = m_lib.property("buildBagHaystack").call({bag, QJSValue(kindLabel)});
    const QJSValue web = m_webBagHaystack.call({bag, QJSValue(kindLabel)});
    if (app.isError() || web.isError()) {
        qWarning("haystack threw: app %s, web %s", qPrintable(app.toString()), qPrintable(web.toString()));
        return QString();
    }
    if (app.toString() != web.toString()) {
        qWarning("app and web haystacks differ:\n  app: %s\n  web: %s",
                 qPrintable(app.toString()), qPrintable(web.toString()));
        return QString();
    }
    return app.toString();
}

// The full pipeline as the in-app page uses it: tokenize the query, then match.
bool TestRecipeSearch::match(const QString& haystack, const QString& query)
{
    QJSValue tokens = m_lib.property("tokenize").call({QJSValue(query)});
    QJSValue r = m_lib.property("matches").call({QJSValue(haystack), tokens});
    Q_ASSERT(!r.isError());
    return r.toBool();
}

bool TestRecipeSearch::webMatch(const QString& name, const QString& profileTitle,
                                const QString& roaster, const QString& coffee,
                                const QString& drinkType, const QString& query)
{
    QJSValue r = m_web.property("webMatch").call({QJSValue(name), QJSValue(profileTitle),
                                                  QJSValue(roaster), QJSValue(coffee),
                                                  QJSValue(drinkType), QJSValue(query)});
    Q_ASSERT(!r.isError());
    return r.toBool();
}

// The reported bug: two tokens, each in a different field (coffee vs profile).
void TestRecipeSearch::crossFieldQueryMatches()
{
    // haystack mirrors name + roaster + coffee + profileTitle
    const QString hay = "Morning cup  Ethiopia Yirgacheffe  D-Flow / Q";
    QVERIFY2(match(hay, "Yirg Df"), "cross-field two-token query must match");
    QVERIFY2(match(hay, "yirgacheffe q"), "coffee token + profile token must match");
    QVERIFY2(match(hay, "df yirg"), "token order must not matter");
}

// A lone token that only lines up once punctuation is deleted.
void TestRecipeSearch::tokenSpanningPunctuation()
{
    QVERIFY2(match("D-Flow / Q", "df"), "'df' must match 'D-Flow' after deletion");
    QVERIFY2(match("D-Flow / Q", "d flow"), "'d flow' must match 'D-Flow'");
    QVERIFY2(match("D-Flow / Q", "d-flow"), "the literal 'd-flow' must still match");
}

// Every token must be found; one miss hides the recipe.
void TestRecipeSearch::allTokensRequired()
{
    const QString hay = "Ethiopia Yirgacheffe  D-Flow";
    QVERIFY2(!match(hay, "Yirg Pressure"),
             "a token matching no field must exclude the recipe");
    QVERIFY2(!match(hay, "Colombia"), "a non-matching single token must not match");
}

void TestRecipeSearch::singleTokenStillMatches()
{
    QVERIFY2(match("Ethiopia Yirgacheffe  D-Flow", "yirg"), "single token still matches");
    QVERIFY2(!match("Ethiopia Yirgacheffe  D-Flow", "kenya"), "single non-match excluded");
}

void TestRecipeSearch::emptyQueryMatchesEverything()
{
    QVERIFY2(match("anything", ""), "empty query matches");
    QVERIFY2(match("anything", "   "), "whitespace-only query matches");
    QVERIFY2(match("anything", " - / . "), "punctuation-only query yields no tokens => matches");
}

void TestRecipeSearch::caseInsensitive()
{
    QVERIFY2(match("Ethiopia YIRGACHEFFE d-flow", "yirg DF"),
             "matching must ignore case on both sides");
}

// Any run of whitespace (spaces, tabs) splits tokens; leading/trailing whitespace is
// dropped rather than producing empty tokens.
void TestRecipeSearch::collapsesInternalWhitespace()
{
    const QString hay = "Ethiopia Yirgacheffe  D-Flow";
    QVERIFY2(match(hay, "yirg\t df"), "a tab between tokens still splits them");
    QVERIFY2(match(hay, "  yirg   df  "), "leading/trailing/repeated whitespace is harmless");
}

// The app builds its haystack via RecipeSearch.buildHaystack (name + roaster + coffee
// + profile + drink label). Guards that all five fields are searchable — the app path
// the web test cannot reach, and the one field (drink label) sourced differently here.
void TestRecipeSearch::appHaystackSearchesAllFields()
{
    QJSValue r = m_engine.newObject();
    r.setProperty("name", QJSValue(QStringLiteral("Morning cup")));
    r.setProperty("roasterName", QJSValue(QStringLiteral("Prodigal")));
    r.setProperty("coffeeName", QJSValue(QStringLiteral("Ethiopia Yirgacheffe")));
    r.setProperty("profileTitle", QJSValue(QStringLiteral("D-Flow / Q")));
    const QString hay =
        m_lib.property("buildHaystack").call({r, QJSValue(QStringLiteral("Latte"))}).toString();

    QVERIFY2(match(hay, "morning"), "name field must be searchable");
    QVERIFY2(match(hay, "prodigal"), "roaster field must be searchable");
    QVERIFY2(match(hay, "yirg"), "coffee field must be searchable");
    QVERIFY2(match(hay, "df"), "profile field must be searchable (df -> D-Flow)");
    QVERIFY2(match(hay, "latte"), "drink-label field must be searchable");
    QVERIFY2(match(hay, "yirg latte"), "cross-field: coffee token + drink-label token");
    QVERIFY2(!match(hay, "kenya"), "a token present in no field must exclude the recipe");
}

// #5 robustness: matches() must tolerate a raw, un-normalized token (e.g. one still
// carrying a hyphen) rather than silently never matching.
void TestRecipeSearch::matchesToleratesUnnormalizedTokens()
{
    QJSValue rawTokens = m_engine.newArray(1);
    rawTokens.setProperty(0, QJSValue(QStringLiteral("d-flow")));   // NOT run through tokenize()
    QJSValue r = m_lib.property("matches").call({QJSValue(QStringLiteral("D-Flow / Q")), rawTokens});
    QVERIFY2(!r.isError(), qPrintable(r.toString()));
    QVERIFY2(r.toBool(), "matches() must normalize raw tokens so 'd-flow' still matches 'D-Flow'");
}

// A raw token that normalizes to "" (e.g. "-") must impose NO constraint rather than
// excluding everything — matches() relies on indexOf("") === 0. tokenize() filters
// these out before matches() sees them, so exercise matches() directly.
void TestRecipeSearch::matchesNoOpTokenIsIgnored()
{
    QJSValue toks = m_engine.newArray(2);
    toks.setProperty(0, QJSValue(QStringLiteral("yirg")));
    toks.setProperty(1, QJSValue(QStringLiteral("-")));   // normalizes to ""
    QJSValue r = m_lib.property("matches").call({QJSValue(QStringLiteral("Yirgacheffe")), toks});
    QVERIFY2(!r.isError(), qPrintable(r.toString()));
    QVERIFY2(r.toBool(), "a token that normalizes to empty must not exclude the recipe");
}

// The web matcher must reach the same verdicts as the in-app matcher on the shared
// cases — if the embedded JS drifts from RecipeSearch.js, this fails.
void TestRecipeSearch::webMatcherAgreesOnSharedCases()
{
    // (name, profile, roaster, coffee, drinkType, query, expected)
    QVERIFY2(webMatch("Morning cup", "D-Flow / Q", "", "Ethiopia Yirgacheffe", "espresso", "Yirg Df"),
             "web: cross-field 'Yirg Df' must match");
    QVERIFY2(webMatch("", "D-Flow / Q", "", "", "espresso", "df"),
             "web: 'df' must match 'D-Flow'");
    QVERIFY2(!webMatch("", "D-Flow", "", "Ethiopia Yirgacheffe", "espresso", "Yirg Pressure"),
             "web: a token matching no field must exclude the recipe");
    QVERIFY2(webMatch("anything", "", "", "", "", " - / . "),
             "web: punctuation-only query matches everything");
    QVERIFY2(webMatch("", "d-flow", "", "YIRGACHEFFE", "espresso", "yirg DF"),
             "web: matching must ignore case");
}

// #1 / more-fields: the web haystack includes the drink-type label, so a drink word narrows.
void TestRecipeSearch::webSearchesDrinkType()
{
    QVERIFY2(webMatch("House milk", "Cremina", "", "Ethiopia", "latte", "latte"),
             "web: a drink-type token must match via the drink label field");
    QVERIFY2(!webMatch("House milk", "Cremina", "", "Ethiopia", "espresso", "latte"),
             "web: a drink-type token must NOT match a recipe of a different type");
}

// A bag as the inventory hands it over: its own fields, and a details blob with a
// nested canonical snapshot, a link and ids. Every text value is searchable, and its
// kind through the label; identifiers, links and enums are not.
void TestRecipeSearch::bagSearchCoversAllTextAndAgreesWithWeb()
{
    const QJSValue bag = m_engine.evaluate(QStringLiteral(
        "({ id: 7, kind: 'tea', roasterName: 'Saka', coffeeName: 'Gran Bar', notes: 'for milk drinks',"
        "   visualizerBagId: 'abc-123', yieldMode: 'none',"
        "   beanBaseData: JSON.stringify({ region: 'Minas Gerais', canonical: { producer: 'Saka Caffe' },"
        "                                  link: 'https://example.com/gran-bar', id: 'canon-99' }) })"));
    QVERIFY(!bag.isError());
    const QString hay = bagHaystack(bag, QStringLiteral("Tea"));
    QVERIFY(!hay.isEmpty());
    for (const char* q : {"saka gran", "milk", "minas", "caffe", "tea"})
        QVERIFY2(match(hay, q), q);
    for (const char* q : {"example", "canon", "abc", "none"})
        QVERIFY2(!match(hay, q), q);

    // An unreadable blob costs only its own details, never the bag (or the list).
    const QJSValue corrupt = m_engine.evaluate(QStringLiteral(
        "({ id: 8, roasterName: 'Saka', beanBaseData: '{not json' })"));
    const QString corruptHay = bagHaystack(corrupt, QStringLiteral("Coffee"));
    QVERIFY(match(corruptHay, "saka coffee"));
}

// Every key the app skips, placed on the bag, on its blob and one level inside the
// blob, with a value no field could otherwise hold: none of it may be searchable on
// either surface. Built from the app's own list, so a key dropped from the web's copy
// fails here too.
void TestRecipeSearch::bagSearchSkipsBookkeepingAtEveryDepth()
{
    const QJSValue build = m_engine.evaluate(QStringLiteral(
        "(function(keys) { var blob = { canonical: {} }, bag = { roasterName: 'Saka' };"
        "  keys.forEach(function(k) { bag[k] = blob[k] = blob.canonical[k] = 'zq' + k; });"
        "  bag.beanBaseData = JSON.stringify(blob); return bag; })"));
    const QJSValue bag = build.call({m_lib.property("bagSkipKeys")});
    QVERIFY(!bag.isError());
    const QString hay = bagHaystack(bag, QString());
    QVERIFY(match(hay, "saka"));
    QVERIFY2(!hay.contains(QStringLiteral("zq")), qPrintable(hay));
}

void TestRecipeSearch::sortedCopyBlanksLastAndAgreesWithWeb()
{
    struct Case { const char* list; const char* direction; const char* ids; };
    const Case cases[] = {
        {"[{id:1,k:0},{id:2,k:5},{id:3,k:9},{id:4,k:5}]", "ASC",  "2,4,3,1"},
        {"[{id:1,k:0},{id:2,k:5},{id:3,k:9},{id:4,k:5}]", "DESC", "3,4,2,1"},
        {"[{id:1,k:''},{id:2,k:'b'},{id:3,k:'a'}]",       "ASC",  "3,2,1"},
        {"[{id:1,k:''},{id:2,k:'b'},{id:3,k:'a'}]",       "DESC", "2,3,1"},
        // Anything but ASC is descending, as Shot History's query reads it.
        {"[{id:1,k:1},{id:2,k:2}]",                       "",     "2,1"},
    };
    const QJSValue keyOf = m_engine.evaluate(QStringLiteral("(function(x) { return x.k; })"));
    const QJSValue ids = m_engine.evaluate(QStringLiteral(
        "(function(l) { return l.map(function(x) { return x.id; }).join(','); })"));
    for (const Case& c : cases) {
        for (const QJSValue& fn : {m_lib.property("sortedCopy"), m_webSortedCopy}) {
            const QJSValue list = m_engine.evaluate(QStringLiteral("(%1)").arg(QLatin1String(c.list)));
            const QString before = ids.call({list}).toString();
            const QJSValue sorted = fn.call({list, keyOf, QJSValue(QLatin1String(c.direction))});
            QVERIFY2(!sorted.isError(), qPrintable(sorted.toString()));
            QCOMPARE(ids.call({sorted}).toString(), QLatin1String(c.ids));
            QCOMPARE(ids.call({list}).toString(), before);   // a copy: the page's own list keeps its order
        }
    }
}

QJSValue TestRecipeSearch::settingsMatcher(const QStringList& available, const QVariantMap& translations)
{
    const QJSValue tr = m_engine.evaluate(QStringLiteral(
        "(function(map) { return function(key, fallback) { return map[key] || fallback } })")).call(
        {m_engine.toScriptValue(translations)});
    const QJSValue identity = m_engine.evaluate(QStringLiteral("(function(k, f) { return f })"));
    const QJSValue isAvailable = m_engine.evaluate(QStringLiteral(
        "(function(list) { return function(c) { return !c || list.indexOf(c) !== -1 } })")).call(
        {m_engine.toScriptValue(available)});
    const QJSValue items = m_settingsMatcher.property("buildItems").call(
        {m_settingsEntries, m_settingsLegacy.call({tr}), m_settingsLegacy.call({identity}), tr, isAvailable});
    const QJSValue matcher = m_settingsMatcher.property("createMatcher").call({items});
    if (items.isError() || matcher.isError())
        qWarning() << "settings matcher:" << items.toString() << matcher.toString();
    return matcher;
}

// Results as "tabId/cardId" (or the external route), best first.
static QStringList settingsTargets(const QJSValue& results)
{
    QStringList out;
    const int n = results.property("length").toInt();
    for (int i = 0; i < n; ++i) {
        const QJSValue r = results.property(i);
        const QString route = r.property("externalRoute").toString();
        out << (route.isEmpty() ? r.property("tabId").toString() + "/" + r.property("cardId").toString()
                                : route);
    }
    return out;
}

void TestRecipeSearch::settingsSearchSnapshotStillFound()
{
    const QJsonArray entries = QJsonDocument::fromJson(
        readSource("/tests/data/settings_search_snapshot.json").toUtf8()).object().value("entries").toArray();
    QVERIFY(entries.size() > 50);
    // Migration bridge: cards the old index never had and no tab has converted yet. Each is
    // removed as its tab becomes SettingsCards; empty when the migration completes.
    const QStringList notYetIndexed = {"calibration/sensorCalibration", "calibration/steamHealth"};
    const QJSValue matcher = settingsMatcher({"android", "simulator", "debug"});
    for (const QJsonValue& v : entries) {
        const QJsonObject e = v.toObject();
        const QString target = e.contains("externalRoute") ? e.value("externalRoute").toString()
            : e.value("tabId").toString() + "/" + e.value("cardId").toString();
        if (notYetIndexed.contains(target))
            continue;
        QStringList queries{e.value("title").toString()};
        for (const QJsonValue& k : e.value("keywords").toArray())
            queries << k.toString();
        for (const QString& q : queries) {
            const QStringList got = settingsTargets(matcher.property("search").call({q}));
            QVERIFY2(got.contains(target), qPrintable(QString("\"%1\" no longer finds %2").arg(q, target)));
        }
    }
}

void TestRecipeSearch::settingsSearch_data()
{
    QTest::addColumn<QString>("query");
    QTest::addColumn<QStringList>("available");
    QTest::addColumn<QVariantMap>("translations");
    QTest::addColumn<QString>("first");     // expected best result, or "" for none
    QTest::addColumn<QString>("absent");    // a target that must not appear, or ""

    const QStringList all{"android", "simulator", "debug"};
    QTest::newRow("typo fahrenheit") << "farenheit" << all << QVariantMap() << "machine/temperatureUnit" << "";
    QTest::newRow("typo celsius") << "celcius" << all << QVariantMap() << "machine/temperatureUnit" << "";
    QTest::newRow("title beats keyword") << "backup" << all << QVariantMap() << "historyData/dailyBackup" << "";
    QTest::newRow("short word ranks its title") << "ai" << all << QVariantMap() << "ai/aiProvider" << "";
    QTest::newRow("every word must match") << "factory xyzzy" << all << QVariantMap() << "" << "";
    QTest::newRow("accents and AND, translated")
        << "unite temperature" << all
        << QVariantMap{{"settings.options.temperatureUnit", QString::fromUtf8("Unité de température")}}
        << "machine/temperatureUnit" << "";
    QTest::newRow("accent folded in a short exact word")
        << "cle" << all << QVariantMap{{"settings.ai.section.provider", QString::fromUtf8("Clé API")}}
        << "ai/aiProvider" << "";
    QTest::newRow("English keyword in German")
        << "bluetooth" << all << QVariantMap{{"settings.bluetooth.machine", "Maschine"}}
        << "connections/machineConnection" << "";
    QTest::newRow("android-only hidden elsewhere") << "launcher" << QStringList{"simulator"} << QVariantMap()
        << "" << "machine/launcherMode";
    QTest::newRow("android-only shown on android") << "launcher" << all << QVariantMap()
        << "machine/launcherMode" << "";
    QTest::newRow("simulator compiled out") << "simulation" << QStringList{"android"} << QVariantMap()
        << "" << "machine/simulationMode";
}

void TestRecipeSearch::settingsSearch()
{
    QFETCH(QString, query);
    QFETCH(QStringList, available);
    QFETCH(QVariantMap, translations);
    QFETCH(QString, first);
    QFETCH(QString, absent);
    const QStringList got = settingsTargets(settingsMatcher(available, translations).property("search").call({query}));
    if (first.isEmpty() && absent.isEmpty())
        QVERIFY2(got.isEmpty(), qPrintable(got.join(", ")));
    if (!first.isEmpty())
        QVERIFY2(!got.isEmpty() && got.first() == first, qPrintable(got.join(", ")));
    if (!absent.isEmpty())
        QVERIFY2(!got.contains(absent), qPrintable(got.join(", ")));
}

QTEST_MAIN(TestRecipeSearch)
#include "tst_recipesearch.moc"
