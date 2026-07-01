.pragma library

// ---------------------------------------------------------------------------
// Locale-aware date entry helpers.
//
// The field DISPLAYS/ACCEPTS dates in the host locale's order (US month-first,
// most of the world day-first, ISO year-first) but STORES them as ISO
// yyyy-mm-dd. The order/separator functions below are pure and take the locale
// short-format string as input (the QML caller obtains it via
// `Qt.locale().dateFormat(Locale.ShortFormat)`), which keeps them unit-testable
// without a live locale and avoids referencing the `Locale` enum inside this
// .pragma library.
// ---------------------------------------------------------------------------

function _pad2(n) { return String(n).padStart(2, '0') }

// Parse a locale short-format pattern (e.g. "M/d/yy", "dd/MM/yyyy", "yyyy/M/d")
// into an ordered array of segment tokens: subset/permutation of ["y","M","d"].
// Falls back to ISO order ["y","M","d"] when the pattern can't be parsed.
function dateOrderFromFormat(fmt) {
    if (!fmt) return ["y", "M", "d"]
    var order = []
    var seen = {}
    for (var i = 0; i < fmt.length; i++) {
        var c = fmt.charAt(i)
        var token = (c === 'y' || c === 'Y') ? "y"
                  : (c === 'M') ? "M"
                  : (c === 'd') ? "d" : ""
        if (token && !seen[token]) { seen[token] = true; order.push(token) }
    }
    return order.length === 3 ? order : ["y", "M", "d"]
}

// Extract the separator from a locale short-format pattern. Prefers a
// conventional ASCII date separator (/, -, .). Locales that separate segments
// with words (e.g. CJK "yyyy年M月d日") have no ASCII separator, so fall back to
// "-" rather than surfacing a stray ideograph between segments. Falls back to
// "-" (ISO) when nothing matches.
function dateSeparatorFromFormat(fmt) {
    if (!fmt) return "-"
    var m = /[-\/.]/.exec(fmt)
    return m ? m[0] : "-"
}

// Human-readable order for the accessible description, e.g. "month, then day,
// then year". `order` is the array from dateOrderFromFormat. `words` maps each
// token (y/M/d) to a localized word and `connector` is the localized joiner —
// both are supplied by the QML caller (which can reach TranslationManager),
// keeping this .pragma library free of translation lookups. Defaults are
// English so the function stays usable/testable without a caller.
function orderWords(order, words, connector) {
    var w = words || { y: "year", M: "month", d: "day" }
    var c = (connector !== undefined && connector !== null) ? connector : ", then "
    var out = (order && order.length === 3) ? order : ["y", "M", "d"]
    return out.map(function (k) { return w[k] }).join(c)
}

// Caret index in `text` just after the Nth digit. Lets a progressive-format
// reassignment restore the caret to the digit the user was editing instead of
// forcing it to the end of the field.
function caretForDigits(text, n) {
    if (n <= 0) return 0
    var count = 0
    for (var i = 0; i < text.length; i++) {
        var ch = text.charAt(i)
        if (ch >= '0' && ch <= '9') {
            count++
            if (count === n) return i + 1
        }
    }
    return text.length
}

// Progressive "format as you type": given the current text, strip to digits and
// re-insert the separator as each segment (year=4 digits, month/day=2) fills.
// Separators are only appended after complete segments, so nothing is injected
// ahead of a segment the user has not reached yet.
function formatAsTyped(text, order, separator) {
    var digits = (text || "").replace(/\D/g, "")
    var ord = (order && order.length === 3) ? order : ["y", "M", "d"]
    var out = ""
    var idx = 0
    for (var i = 0; i < ord.length && idx < digits.length; i++) {
        var len = ord[i] === "y" ? 4 : 2
        var seg = digits.substr(idx, len)
        if (i > 0 && out.length > 0) out += separator
        out += seg
        idx += len
        if (seg.length < len) break   // segment incomplete — no trailing separator
    }
    return out
}

// Parse a completed localized entry to ISO yyyy-mm-dd using the locale order.
// Returns "" for blank OR for anything that isn't a valid, complete date, so an
// incomplete/garbage entry never corrupts the stored value.
function localizedToIso(text, order) {
    if (!text) return ""
    var nums = (text.split(/\D+/)).filter(function (s) { return s.length > 0 })
    if (nums.length !== 3) return ""
    var ord = (order && order.length === 3) ? order : ["y", "M", "d"]
    var seg = {}
    for (var i = 0; i < 3; i++) seg[ord[i]] = parseInt(nums[i], 10)
    var y = seg["y"], mo = seg["M"], d = seg["d"]
    if (isNaN(y) || isNaN(mo) || isNaN(d)) return ""
    if (y < 100) y += 2000           // 2-digit year → 20xx
    if (y < 1900 || y > 2100) return ""
    if (mo < 1 || mo > 12) return ""
    if (d < 1 || d > 31) return ""
    return y + "-" + _pad2(mo) + "-" + _pad2(d)
}

// Render a stored ISO yyyy-mm-dd date in the locale order/separator for display.
// Returns "" for blank; returns the input unchanged if it isn't ISO-shaped.
function isoToLocalized(iso, order, separator) {
    if (!iso) return ""
    var m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(iso)
    if (!m) return iso
    var v = { y: m[1], M: m[2], d: m[3] }
    var ord = (order && order.length === 3) ? order : ["y", "M", "d"]
    return ord.map(function (k) { return v[k] }).join(separator || "-")
}

// Attempt to normalize a date string to yyyy-mm-dd format.
// Handles common wrong formats such as m/d/yyyy, mm/dd/yyyy, d/m/yyyy,
// and yyyy/mm/dd (correct digits, wrong separator).
// Returns the original string unchanged if the format cannot be determined.
function normalizeDateString(dateString) {
    if (!dateString || dateString.length === 0) return dateString

    // Already correct: yyyy-mm-dd
    if (/^\d{4}-\d{2}-\d{2}$/.test(dateString)) return dateString

    // Detect separator
    var sep = ""
    if (dateString.indexOf('/') >= 0) sep = '/'
    else if (dateString.indexOf('-') >= 0) sep = '-'
    else return dateString

    var parts = dateString.split(sep)
    if (parts.length !== 3) return dateString

    var p0 = parseInt(parts[0], 10)
    var p1 = parseInt(parts[1], 10)
    var p2 = parseInt(parts[2], 10)
    if (isNaN(p0) || isNaN(p1) || isNaN(p2)) return dateString

    var year, month, day

    if (parts[2].length === 4 || p2 > 31) {
        // Year is last: m/d/yyyy or d/m/yyyy
        year = p2
        if (p0 > 12) {
            // p0 can't be a month — must be day
            day = p0; month = p1
        } else if (p1 > 12) {
            // p1 can't be a month — must be day
            month = p0; day = p1
        } else {
            // Ambiguous — assume US order: month/day/year
            month = p0; day = p1
        }
    } else if (parts[0].length === 4 || p0 > 31) {
        // Year is first with wrong separator: yyyy/mm/dd
        year = p0; month = p1; day = p2
    } else {
        return dateString  // Cannot determine format
    }

    if (year < 1900 || year > 2100) return dateString
    if (month < 1 || month > 12) return dateString
    if (day < 1 || day > 31) return dateString

    return year + "-"
        + String(month).padStart(2, '0') + "-"
        + String(day).padStart(2, '0')
}
