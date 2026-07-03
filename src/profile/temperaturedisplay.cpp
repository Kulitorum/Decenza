#include "temperaturedisplay.h"

#include <QtGlobal>
#include <cmath>

namespace {

// Format a temperature dropping a trailing ".0" so 88 → "88" but 88.5 → "88.5".
QString num(double v) {
    QString s = QString::number(std::round(v * 10.0) / 10.0, 'f', 1);
    if (s.endsWith(QLatin1String(".0")))
        s.chop(2);
    return s;
}

// Signed delta tag, e.g. "+1°" / "-1.5°".
QString deltaTag(double delta) {
    const QString sign = delta < 0 ? QStringLiteral("-") : QStringLiteral("+");
    return sign + num(std::fabs(delta)) + QStringLiteral("°"); // °
}

const QString kListSep = QStringLiteral(" · ");   // separates the two distinct temps; a SPACED mid-dot (a comma reads as a decimal point, a tight "·" / slash read as a decimal / fraction)
const QString kEllip   = QStringLiteral("…");     // …

} // namespace

namespace TemperatureDisplay {

int distinctCount(const QVector<double>& stepTemps) {
    QVector<double> seen;
    for (double t : stepTemps) {
        bool found = false;
        for (double s : seen) {
            if (std::fabs(s - t) < 0.05) { found = true; break; }
        }
        if (!found)
            seen.append(t);
    }
    return static_cast<int>(seen.size());
}

QString format(const QVector<double>& stepTemps, double anchorTemp,
               bool hasOverride, double overrideTemp, bool fahrenheit) {
    // Display-unit conversion: absolute temps shift origin (×9/5 +32); a delta/offset
    // scales only (×9/5). Storage is always Celsius; this is display-only.
    const auto disp = [fahrenheit](double c) { return fahrenheit ? (c * 9.0 / 5.0 + 32.0) : c; };
    const auto dispDelta = [fahrenheit](double d) { return fahrenheit ? (d * 9.0 / 5.0) : d; };
    const QString suffix = fahrenheit ? QStringLiteral("°F") : QStringLiteral("°C");

    const double delta = hasOverride ? (overrideTemp - anchorTemp) : 0.0;
    // Show the override as the OFFSET that will be applied to every step (e.g.
    // "+1°"), not as recomputed per-step values. This expresses "all steps +1°"
    // directly and never recomputes/misrepresents a "first temp". Suppressed when
    // the delta rounds to zero.
    const QString tag = (hasOverride && std::fabs(delta) >= 0.05)
        ? QStringLiteral(" ") + deltaTag(dispDelta(delta)) : QString();

    // The base is the profile's own temperature(s), unshifted.
    if (stepTemps.isEmpty())
        return num(disp(anchorTemp)) + suffix + tag;

    // Distinct values in first-seen (trajectory) order.
    QVector<double> distinct;
    for (double t : stepTemps) {
        bool found = false;
        for (double s : distinct) {
            if (std::fabs(s - t) < 0.05) { found = true; break; }
        }
        if (!found)
            distinct.append(t);
    }

    QString base;
    if (distinct.size() <= 1)
        base = num(disp(stepTemps.first())) + suffix;                                       // N=1: single value
    else if (distinct.size() == 2)
        base = num(disp(distinct[0])) + kListSep + num(disp(distinct[1])) + suffix;         // N=2: spaced mid-dot list
    else
        base = num(disp(stepTemps.first())) + kEllip + num(disp(stepTemps.last())) + suffix; // N>=3: first…last
    return base + tag;
}

} // namespace TemperatureDisplay
