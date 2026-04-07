.pragma library

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
