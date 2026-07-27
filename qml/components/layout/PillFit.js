.pragma library

// Idle pill-row fit (descriptive-recipe-names): pack a list of pill widths into
// pages that each occupy AT MOST `maxRows` rows, filling greedily left-to-right.
// Returns an array of per-page pill COUNTS — the count varies with the names'
// widths and may differ from page to page (a page of long names holds fewer).
//
// Used by every idle pill row — RecipesItem, BeansItem, EspressoItem,
// EquipmentItem, FlushItem, HotWaterItem, and IdlePage's center-zone loaders —
// replacing three prior behaviors (recipes/beans: fixed 5 per page; equipment:
// fixed cap of 5; profiles/flush/hot-water: unbounded wrap) so the longer
// bean+type+profile names never spill past two rows. The caller measures widths
// with FontMetrics.advanceWidth() using a font that MIRRORS
// PresetPillRow's pill metrics (font 16 bold, pillPadding, pillSpacing, icon) —
// keep those in sync with PresetPillRow.qml; only the width formula is mirrored,
// never the available width itself (the caller reads the row's real
// effectiveMaxWidth), so drift can at worst cost one extra row, never data.
//
//   widths:     array of measured pill widths (text + icon + padding), in order
//   spacing:    horizontal gap between pills in a row (PresetPillRow.pillSpacing)
//   availWidth: usable width of the pill area (row effectiveMaxWidth minus any
//               reserved arrow gutter)
//   maxRows:    row cap per page (2 for the idle widgets)
function packPageSizes(widths, spacing, availWidth, maxRows) {
    var n = widths.length
    if (n === 0)
        return [0]
    // Width unknown yet (pre-layout): one page, avoids a transient arrow flash.
    if (!(availWidth > 0))
        return [n]

    var pages = []
    var i = 0
    while (i < n) {
        var rowsUsed = 1
        var rowW = 0        // width of the current row so far
        var count = 0       // pills placed on this page
        while (i + count < n) {
            var w = widths[i + count]
            if (rowW === 0) {
                // First pill of a row always goes on it, even if it alone
                // exceeds availWidth (nothing narrower to do — it gets its row).
                rowW = w
                count++
            } else if (rowW + spacing + w <= availWidth) {
                rowW += spacing + w
                count++
            } else if (rowsUsed < maxRows) {
                // Wrap to the next row within this page.
                rowsUsed++
                rowW = w
                count++
            } else {
                // Page is full (maxRows rows used) — this pill starts the next page.
                break
            }
        }
        if (count === 0)   // safety: always make progress
            count = 1
        pages.push(count)
        i += count
    }
    return pages
}

// Keep the pill order the user is currently looking at (#1673).
//
// Every inventory query is ordered by last_used DESC, and activating a pill
// touches last_used — so the next inventoryReady re-sorts the list and the row
// REPACKS, moving pills (and re-wrapping the two rows) under the user's finger.
// Activation itself emits no change signal, but the writes that settle right
// after an apply do, which is why it only happens sometimes.
//
// While the row is open, callers pass the list they are showing as `prev`: rows
// already on screen keep their positions, genuinely new rows are appended, and
// removed rows drop out. The caller lifts the freeze on close (a fresh
// requestInventory then adopts the real MRU order).
//
//   prev:  the list currently displayed (may be empty on first load)
//   next:  the freshly loaded list, in true MRU order
//   idKey: the identity field to match rows on ("id")
function keepOrder(prev, next, idKey) {
    if (!prev || prev.length === 0 || !next || next.length === 0)
        return next
    var position = ({})
    for (var p = 0; p < prev.length; ++p) {
        var prevId = prev[p][idKey]
        if (prevId !== undefined)
            position[prevId] = p
    }
    var known = []
    var added = []
    for (var n = 0; n < next.length; ++n) {
        var id = next[n][idKey]
        if (id !== undefined && position[id] !== undefined)
            known.push({ order: position[id], row: next[n] })
        else
            added.push(next[n])   // new since the row opened — append, don't shuffle
    }
    known.sort(function(a, b) { return a.order - b.order })
    var out = []
    for (var k = 0; k < known.length; ++k)
        out.push(known[k].row)
    return out.concat(added)
}
