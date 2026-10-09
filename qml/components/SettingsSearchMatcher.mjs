// Settings search: turns index entries into display items and ranks them for a query.
// Pure: translation and availability are passed in, so tests run it in a bare QJSEngine.
import Fuse, { stripDiacritics } from "../third_party/fuse/fuse.mjs"

// Words this short must appear as written; fuzzing them matches nearly everything.
const EXACT_MAX_LENGTH = 3
// A query with a good match (a title/keyword word hit, or a Fuse score under STRONG_SCORE) drops
// results with no word hit scoring over NOISE_SCORE: "retain" also matched "Screensaver Settings"
// at 0.63. A query with no good match (the typo "farenheit", ~0.55) keeps its fuzzy results.
const STRONG_SCORE = 0.3
const NOISE_SCORE = 0.35

// Both Fuse and wordHits split with this, so a word means the same thing to each.
function tokenize(text) {
    return text.split(/[\s\-\/.,:;()!?"']+/).filter(function(t) { return t.length > 0 })
}

// Folded as Fuse's ignoreDiacritics folds, so the exact-word filter agrees with Fuse's match.
function fold(text) {
    return stripDiacritics(String(text).toLowerCase())
}

// entries: SettingsSearchEntries.js `entries`. translate(key, fallback) and isAvailable(condition)
// come from the caller; an entry needs every one of its conditions.
export function buildItems(entries, translate, isAvailable) {
    const items = []
    for (const e of entries) {
        if (!e.availability.every(isAvailable))
            continue
        items.push({
            tabId: e.tabId || "", cardId: e.cardId || "", externalRoute: e.externalRoute || "",
            kind: e.kind,
            title: translate(e.key, e.fallback),
            fallbackTitle: e.fallback,
            cardTitle: e.cardKey ? translate(e.cardKey, e.cardFallback) : "",
            description: e.descKey ? translate(e.descKey, e.descFallback) : "",
            keywords: e.keywords
        })
    }
    return items
}

export function createMatcher(items) {
    const fuse = new Fuse(items, {
        keys: [
            { name: "title", weight: 0.45 },
            { name: "keywords", weight: 0.25 },
            { name: "cardTitle", weight: 0.12 },
            { name: "description", weight: 0.10 },
            { name: "fallbackTitle", weight: 0.08 }
        ],
        includeScore: true,
        ignoreDiacritics: true,
        ignoreLocation: true,
        threshold: 0.35,
        useTokenSearch: true,
        tokenMatch: "all",
        tokenize: tokenize
    })
    const haystacks = items.map(function(it) {
        return fold([it.title, it.fallbackTitle, it.cardTitle, it.description].concat(it.keywords).join(" "))
    })
    const titleWords = items.map(function(it) { return tokenize(fold(it.title + " " + it.fallbackTitle)) })
    const keywordWords = items.map(function(it) { return tokenize(fold(it.keywords.join(" "))) })

    // Query words that are (2) or start (1) a word of the item. Fuse alone ranks "ai" inside
    // "maintenance" level with the title "AI Provider", and "cle" in "clear" level with "Clé".
    function wordHits(words, targets) {
        return words.reduce(function(sum, w) {
            if (targets.indexOf(w) !== -1)
                return sum + 2
            return sum + (targets.some(function(t) { return t.indexOf(w) === 0 }) ? 1 : 0)
        }, 0)
    }

    return {
        // Best match first. An empty query returns every item in index order.
        search: function(query) {
            const words = tokenize(fold(query))
            if (words.length === 0)
                return items.slice()
            const exact = words.filter(function(w) { return w.length <= EXACT_MAX_LENGTH })
            const ranked = fuse.search(words.join(" ")).filter(function(r) {
                return exact.every(function(w) { return haystacks[r.refIndex].indexOf(w) !== -1 })
            }).map(function(r) {
                return { r: r, title: wordHits(words, titleWords[r.refIndex]),
                         keyword: wordHits(words, keywordWords[r.refIndex]) }
            })
            const strong = ranked.some(function(x) { return x.title + x.keyword > 0 || x.r.score < STRONG_SCORE })
            return ranked.filter(function(x) {
                return !strong || x.title + x.keyword > 0 || x.r.score <= NOISE_SCORE
            }).sort(function(a, b) {
                return (b.title - a.title) || (b.keyword - a.keyword) || (a.r.score - b.r.score)
            }).map(function(x) { return x.r.item })
        }
    }
}
