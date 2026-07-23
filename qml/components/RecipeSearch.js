.pragma library

// Tokenized matching for the Recipes page search field — the single source of
// truth for the in-app path (RecipesPage.qml). The query is split into tokens on
// whitespace, the characters `-`, `/` and `.` are DELETED from both the query and
// the searchable text, and a recipe matches only when EVERY token is found (as a
// substring) somewhere in its combined text.
//
// This is what lets "Yirg Df" match a Yirgacheffe recipe on a "D-Flow / Q" profile:
// the two tokens land in different fields (coffee and profile), which the prior
// single contiguous-substring match (`hay.indexOf(query)`) could never do; and
// deleting punctuation collapses "D-Flow" to "dflow", so the abbreviation "df"
// matches it — matching the way users think of the profile ("Dflow", one word).
//
// This is the multi-word, cross-field idea behind Shot History's search
// (formatFtsQuery in shothistorystorage_queries.cpp, which likewise tokenizes and
// ANDs), but DELIBERATELY more forgiving: FTS splits "D-Flow" into the tokens
// "d"/"flow" and prefix-matches, so History would NOT match a bare "df"; deleting
// punctuation here does, because the reported query is exactly "Yirg Df".
//
// The ShotServer web /recipes page (shotserver_recipes.cpp, matchesFilter) carries
// a behaviorally identical copy in embedded JS; keep the two in sync. Guarded by
// tests/tst_recipesearch.cpp, which evaluates THIS file rather than a copy.

// Lower-case and DELETE `-` `/` `.` so an abbreviation like "df" matches "D-Flow"
// and tokens can span a punctuation boundary. Whitespace is preserved as the token
// separator, so " / " (spaces around a slash) still splits its neighbours.
function normalize(s) {
    return String(s || "").toLowerCase().replace(/[-\/.]/g, "")
}

// The normalized, non-empty tokens of a query. An empty/whitespace query yields
// [], which the caller treats as "match everything".
function tokenize(query) {
    return normalize(query).split(/\s+/).filter(function(t) { return t.length > 0 })
}

// Combined searchable text for one recipe: name + roaster + coffee + profile + the
// drink-type label. The label is passed in rather than derived here, because its
// source is surface-specific — the in-app page derives+localizes it via DrinkType,
// the web /recipes page uses its own English map — but the FIELD LIST lives here so
// "which fields are searched" is one place, testable without loading the page. The
// web page mirrors this same five-field set inline (shotserver_recipes.cpp).
function buildHaystack(r, drinkLabel) {
    return (r.name || "") + " " + (r.roasterName || "") + " "
         + (r.coffeeName || "") + " " + (r.profileTitle || "") + " "
         + (drinkLabel || "")
}

// True iff every token appears in the normalized haystack. tokens is normally the
// output of tokenize(); an empty tokens array matches everything. Each token is
// re-normalized here (idempotent for tokenize() output) so a caller that passes a
// raw, un-normalized token — e.g. "d-flow" — still matches the collapsed haystack
// instead of silently never matching. A token that normalizes to "" imposes no
// constraint (indexOf("") is 0), which is the right "no-op token" behavior.
function matches(haystack, tokens) {
    if (!tokens || tokens.length === 0)
        return true
    var hay = normalize(haystack)
    for (var i = 0; i < tokens.length; ++i) {
        if (hay.indexOf(normalize(tokens[i])) === -1)
            return false
    }
    return true
}
