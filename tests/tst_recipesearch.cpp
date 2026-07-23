// Guards the Recipes-page search matcher on BOTH surfaces that implement it:
//
//   1. The in-app path — qml/components/RecipeSearch.js (normalize/tokenize/matches),
//      used by RecipesPage.filterAndSort.
//   2. The ShotServer web /recipes path — the normalizeSearch/matchesFilter functions
//      embedded in generateRecipesPage() (src/network/shotserver_recipes.cpp).
//
// The two are separate implementations (QML JS vs browser JS served as a string) that
// must stay behaviorally in sync. Both are loaded from their REAL shipping source
// (read from DECENZA_SOURCE_DIR, evaluated in a QJSEngine) rather than a C++ copy, so
// this file guards the actual code and FAILS if either surface drifts — same approach
// as tst_textescaping.
//
// The bug this exists for: typing "Yirg Df" on the Recipes page found nothing, even
// though the coffee is a Yirgacheffe and the profile is "D-Flow / Q". The old matcher
// tested the whole query as one contiguous substring (`hay.indexOf(query)`), so a
// cross-field, multi-token query could never match, and "df" could not reach "D-Flow".
// The fix tokenizes the query and DELETES `-` `/` `.` (collapsing "D-Flow" to "dflow"),
// requiring every token to be found (AND).

#include <QtTest>
#include <QJSEngine>
#include <QFile>

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
    void matchesToleratesUnnormalizedTokens();

    // Web matcher (shotserver_recipes.cpp) — must agree with the in-app matcher
    void webMatcherAgreesOnSharedCases();
    void webSearchesDrinkType();

private:
    QJSEngine m_engine;
    QJSValue m_lib;   // RecipeSearch.js
    QJSValue m_web;   // extracted web matcher
    bool match(const QString& haystack, const QString& query);
    // Web matcher: builds a recipe {name, profileTitle, roaster, coffee, drinkType}
    // and returns matchesFilter against the tokenized query.
    bool webMatch(const QString& name, const QString& profileTitle, const QString& roaster,
                  const QString& coffee, const QString& drinkType, const QString& query);
};

// Pull a `function <name>(...) { ... }` block out of source by brace matching. Fails
// (returns empty) if the name is absent or appears more than once, so an ambiguous
// extraction is caught loudly by the caller rather than testing the wrong text.
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
    // --- In-app matcher: RecipeSearch.js (strip the QML `.pragma library` line) ---
    QString js = readSource("/qml/components/RecipeSearch.js");
    QVERIFY2(!js.isEmpty(), "could not read RecipeSearch.js");
    js.remove(QRegularExpression("^\\s*\\.pragma\\s+library\\s*$",
                                 QRegularExpression::MultilineOption));
    const QString libProgram =
        QStringLiteral("(function(){ %1\n return { tokenize: tokenize, matches: matches,"
                       "                           normalize: normalize }; })()").arg(js);
    m_lib = m_engine.evaluate(libProgram);
    QVERIFY2(!m_lib.isError(), qPrintable(m_lib.toString()));
    QVERIFY2(m_lib.property("matches").isCallable(), "matches() not found in RecipeSearch.js");
    QVERIFY2(m_lib.property("tokenize").isCallable(), "tokenize() not found in RecipeSearch.js");

    // --- Web matcher: extracted from generateRecipesPage() in shotserver_recipes.cpp ---
    const QString cpp = readSource("/src/network/shotserver_recipes.cpp");
    QVERIFY2(!cpp.isEmpty(), "could not read shotserver_recipes.cpp");
    const QString normalizeSearch = extractFunction(cpp, "normalizeSearch");
    const QString matchesFilter = extractFunction(cpp, "matchesFilter");
    QVERIFY2(!normalizeSearch.isEmpty(), "normalizeSearch() not found (or ambiguous) in shotserver_recipes.cpp");
    QVERIFY2(!matchesFilter.isEmpty(), "matchesFilter() not found (or ambiguous) in shotserver_recipes.cpp");

    // matchesFilter references drinkLabel(); stub it to echo the raw drink type so the
    // field's INCLUSION is exercised without pulling in the DRINK_LABELS map.
    const QString webProgram =
        QStringLiteral("(function(){ var drinkLabel = function(t){ return t || ''; };\n"
                       "  %1\n  %2\n"
                       "  return { normalizeSearch: normalizeSearch, webMatch:"
                       "    function(name, profileTitle, roaster, coffee, drinkType, q) {"
                       "      var r = { name: name, profileTitle: profileTitle, roasterName: roaster,"
                       "                coffeeName: coffee, drinkType: drinkType };"
                       "      var tokens = normalizeSearch(q).split(/\\s+/).filter(Boolean);"
                       "      return matchesFilter(r, tokens);"
                       "    } }; })()").arg(normalizeSearch, matchesFilter);
    m_web = m_engine.evaluate(webProgram);
    QVERIFY2(!m_web.isError(), qPrintable(m_web.toString()));
    QVERIFY2(m_web.property("webMatch").isCallable(), "web matcher failed to build");
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

QTEST_MAIN(TestRecipeSearch)
#include "tst_recipesearch.moc"
