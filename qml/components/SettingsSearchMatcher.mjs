// Settings search: turns index entries into display items and ranks them for a query.
// Pure: translation and availability are passed in, so tests run it in a bare QJSEngine.
import Fuse from "../third_party/fuse/fuse.mjs"

// Words this short must appear as written; fuzzing them matches nearly everything.
const EXACT_MAX_LENGTH = 3

// Fuse's default tokenizer is /[\p{L}\p{M}\p{N}_]+/gu, which QV4's regex engine silently
// matches nothing with. Lower-cased, accent-stripped input arrives here.
function tokenize(text) {
    return text.split(/[\s\-\/.,:;()!?"']+/).filter(function(t) { return t.length > 0 })
}

function fold(text) {
    return String(text || "").toLowerCase().normalize("NFD").replace(/[\u0300-\u036f]/g, "")
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
            return fuse.search(words.join(" ")).filter(function(r) {
                return exact.every(function(w) { return haystacks[r.refIndex].indexOf(w) !== -1 })
            }).map(function(r) {
                return { r: r, title: wordHits(words, titleWords[r.refIndex]),
                         keyword: wordHits(words, keywordWords[r.refIndex]) }
            }).sort(function(a, b) {
                return (b.title - a.title) || (b.keyword - a.keyword) || (a.r.score - b.r.score)
            }).map(function(x) { return x.r.item })
        }
    }
}
