.pragma library

// Idle pill-row fit (descriptive-recipe-names): pack a list of pill widths into
// pages that each occupy AT MOST `maxRows` rows, filling greedily left-to-right.
// Returns an array of per-page pill COUNTS — the count varies with the names'
// widths and may differ from page to page (a page of long names holds fewer).
//
// Used by RecipesItem.qml and BeansItem.qml to replace the old fixed "5 per
// page" windowing so the longer bean+type+profile recipe names never spill past
// two rows. The caller measures widths with a TextMetrics that MIRRORS
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
