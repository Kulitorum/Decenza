.pragma library

// Tokenized matching for the Recipes and Beans page search fields — the single
// source of truth for the in-app path (RecipesPage.qml, BeanInfoPage.qml). The query is split into tokens on
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

// Keys that are not text a user would search for: identifiers, links, sync and
// cache bookkeeping, and the raw blob (searched parsed, below).
var _bagSkipKeys = ["id", "kind", "beanBaseId", "beanBaseData", "equipmentId",
    "visualizerBagId", "visualizerRoasterId", "visualizerSeen", "visualizerSyncPending",
    "link", "source", "canonicalRoasterId", "visualizerCanonicalId",
    "linkChecked", "linkDead", "aiPageSearched"]

// Every text value a bag holds: its own fields (roaster, coffee, roast level,
// notes, dates, ...) and each value in its bean-details blob, nested ones
// included. The web /beans page carries a behaviorally identical bagHaystack
// (shotserver_bags.cpp); tests/tst_recipesearch.cpp checks the two agree.
function buildBagHaystack(bag) {
    var parts = []
    function collect(o) {
        for (var key in o) {
            if (_bagSkipKeys.indexOf(key) !== -1)
                continue
            var v = o[key]
            if (typeof v === "string")
                parts.push(v)
            else if (v && typeof v === "object")
                collect(v)
        }
    }
    collect(bag || ({}))
    try {
        collect(JSON.parse((bag && bag.beanBaseData) || "{}"))
    } catch (e) {
        // An unreadable blob contributes nothing; the bag's own fields still match.
    }
    return parts.join(" ")
}

// A sorted copy of list by keyOf(item), which returns a number or a lower-cased
// string. Blank keys (0 or "") go last in both directions, so a never-used or
// undated item never floats to the top; ties break by id, as Array.sort is not
// guaranteed stable.
function sortedCopy(list, keyOf, direction) {
    var asc = (direction !== "DESC")
    function blank(k) { return (typeof k === "number") ? (k <= 0) : (String(k).length === 0) }
    return list.slice().sort(function(a, b) {
        var ka = keyOf(a), kb = keyOf(b)
        var ba = blank(ka), bb = blank(kb)
        if (ba !== bb) return ba ? 1 : -1
        var cmp = (typeof ka === "number") ? (ka - kb) : String(ka).localeCompare(String(kb))
        if (cmp === 0) cmp = (Number(a.id) || 0) - (Number(b.id) || 0)
        return asc ? cmp : -cmp
    })
}
