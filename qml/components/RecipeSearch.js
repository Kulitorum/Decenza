.pragma library

// Search and sort for the Recipes and Beans pages (RecipesPage.qml, BeanInfoPage.qml).
// A query splits on whitespace, `-` `/` `.` are DELETED from query and text, and
// an item matches when EVERY token is a substring of its text: so "Yirg Df" finds
// a Yirgacheffe recipe on a "D-Flow / Q" profile ("D-Flow" collapses to "dflow").
// Shot History's FTS search also ANDs tokens, but would not match a bare "df".
//
// The web /recipes and /beans pages carry the same logic in embedded JS
// (normalizeSearch, tokenizeSearch and sortedCopy in webtemplates/management_js.h;
// matchesFilter in shotserver_recipes.cpp; bagHaystack in shotserver_bags.cpp).
// tests/tst_recipesearch.cpp evaluates both sources and checks they agree.

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

// Searchable text for one recipe. The drink label is passed in because each
// surface derives it its own way (the app localizes it via DrinkType).
function buildHaystack(r, drinkLabel) {
    return (r.name || "") + " " + (r.roasterName || "") + " "
         + (r.coffeeName || "") + " " + (r.profileTitle || "") + " "
         + (drinkLabel || "")
}

// True when every token appears in the normalized haystack; no tokens matches
// everything. Tokens are re-normalized, so a raw "d-flow" still matches.
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

// Keys that are not text a user would search for, skipped at any depth:
// identifiers, links, enums, sync and cache bookkeeping, and the raw blob
// (searched parsed, below). `kind` is searched through its label instead.
var _bagSkipKeys = ["id", "kind", "beanBaseId", "beanBaseData", "equipmentId",
    "visualizerBagId", "visualizerRoasterId", "visualizerSeen", "visualizerSyncPending",
    "link", "source", "canonicalRoasterId", "visualizerCanonicalId",
    "linkChecked", "linkDead", "aiPageSearched", "yieldMode"]

// Every text value a bag holds, its details blob (nested values included) and
// kindLabel ("Tea" / "Coffee", localized by the caller), so "tea" finds every
// bag of tea.
function buildBagHaystack(bag, kindLabel) {
    var parts = [kindLabel || ""]
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
    var blob = ({})
    try {
        blob = JSON.parse((bag && bag.beanBaseData) || "{}")
    } catch (e) {
        // Unreadable: the bag's own fields still match. BagCard logs it.
    }
    collect(blob)
    return parts.join(" ")
}

// A sorted copy of list by keyOf(item), a number or a lower-cased string. Blank
// keys (0 or "") go last in both directions; ties break by id, as Array.sort is
// not guaranteed stable. Anything but "ASC" sorts descending, as Shot History's
// query does.
function sortedCopy(list, keyOf, direction) {
    var asc = (direction === "ASC")
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
