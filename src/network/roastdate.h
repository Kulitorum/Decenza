#pragma once

#include <QString>
#include <QDate>
#include <QLocale>
#include <QRegularExpression>

// Normalize a stored roast-date string to ISO yyyy-MM-dd for the Visualizer
// payload. New (bean-bag) shots already store ISO, but legacy pre-bag shots
// stored the user's *display* format (e.g. US "12.15.2025"), and the
// migration-16 enjoyment re-sync ships those strings verbatim.
//
// Visualizer parses roast_date server-side with Date.strptime(user's Visualizer
// date format) and only falls back to a lenient Date.parse on failure
// (app/models/concerns/date_parseable.rb). A dot-separated localized string is
// therefore parsed correctly only when it matches the viewer's date setting,
// and is otherwise misread or dropped. An unambiguous dash-separated ISO date
// always misses every (dotted) strptime format and takes the Date.parse
// fallback, resolving identically for every user — which keeps Coffee
// Management's (roaster, name, parsed-date) bag matching correct.
//
// Header-only + pure so the uploader and tests share one definition without
// link-time coupling (mirrors BeanBaseBlob).
namespace RoastDate {

// True iff the active locale writes the month before the day (US "MM/DD").
// Used only to disambiguate a numeric date that parses validly under *both*
// orders — the same order the legacy display string was originally written in.
inline bool localePrefersMonthFirst()
{
    const QString fmt = QLocale().dateFormat(QLocale::ShortFormat);
    const int mPos = fmt.indexOf(QLatin1Char('M'));
    const int dPos = fmt.indexOf(QLatin1Char('d'));
    if (mPos < 0 || dPos < 0)
        return true;  // no order to read — default to month-first
    return mPos < dPos;
}

// Coerce `raw` to ISO yyyy-MM-dd, or return it unchanged when it isn't a date
// we can confidently parse. Handles 3-part numeric dates separated by '/', '.',
// or '-'. A 4-digit leading group is read year-first (unambiguous). Otherwise
// the trailing group must be a 4-digit year and the lead two groups are
// month/day: when only one order yields a valid calendar date we take it, and
// when both do (e.g. 01.02.2025) we fall back to the locale's component order.
inline QString toIso(const QString& raw)
{
    const QString s = raw.trimmed();
    if (s.isEmpty())
        return raw;

    static const QRegularExpression re(
        QStringLiteral("^(\\d{1,4})[\\/.\\-](\\d{1,2})[\\/.\\-](\\d{1,4})$"));
    const QRegularExpressionMatch m = re.match(s);
    if (!m.hasMatch())
        return raw;  // not a recognizable numeric date — leave it alone

    const int g1 = m.captured(1).toInt();
    const int g2 = m.captured(2).toInt();
    const int g3 = m.captured(3).toInt();

    // Year-first (yyyy-MM-dd / yyyy.MM.dd / yyyy/MM/dd) — unambiguous.
    if (m.captured(1).size() == 4) {
        const QDate d(g1, g2, g3);
        return d.isValid() ? d.toString(Qt::ISODate) : raw;
    }

    // Day/month-first: require a 4-digit trailing year to stay unambiguous.
    if (m.captured(3).size() != 4)
        return raw;
    const QDate monthFirst(g3, g1, g2);  // g1=MM, g2=DD
    const QDate dayFirst(g3, g2, g1);    // g1=DD, g2=MM

    if (monthFirst.isValid() && dayFirst.isValid())
        return (localePrefersMonthFirst() ? monthFirst : dayFirst).toString(Qt::ISODate);
    if (monthFirst.isValid())
        return monthFirst.toString(Qt::ISODate);
    if (dayFirst.isValid())
        return dayFirst.toString(Qt::ISODate);
    return raw;  // neither order is a real calendar date
}

}  // namespace RoastDate
