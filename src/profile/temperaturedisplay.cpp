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

double cToDisplay(double celsius, bool fahrenheit) {
    return fahrenheit ? (celsius * 9.0 / 5.0 + 32.0) : celsius;
}

double displayToC(double display, bool fahrenheit) {
    return fahrenheit ? ((display - 32.0) * 5.0 / 9.0) : display;
}

double cDeltaToDisplay(double deltaCelsius, bool fahrenheit) {
    return fahrenheit ? (deltaCelsius * 9.0 / 5.0) : deltaCelsius;
}

double displayToCDelta(double deltaDisplay, bool fahrenheit) {
    return fahrenheit ? (deltaDisplay * 5.0 / 9.0) : deltaDisplay;
}

QString unitSuffix(bool fahrenheit) {
    return fahrenheit ? QStringLiteral("°F") : QStringLiteral("°C");
}

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
    const auto disp = [fahrenheit](double c) { return cToDisplay(c, fahrenheit); };
    const auto dispDelta = [fahrenheit](double d) { return cDeltaToDisplay(d, fahrenheit); };
    const QString suffix = unitSuffix(fahrenheit);

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

double TemperatureDisplayBridge::cToDisplay(double celsius, bool fahrenheit) const {
    return TemperatureDisplay::cToDisplay(celsius, fahrenheit);
}

double TemperatureDisplayBridge::displayToC(double display, bool fahrenheit) const {
    return TemperatureDisplay::displayToC(display, fahrenheit);
}

double TemperatureDisplayBridge::cDeltaToDisplay(double deltaCelsius, bool fahrenheit) const {
    return TemperatureDisplay::cDeltaToDisplay(deltaCelsius, fahrenheit);
}

double TemperatureDisplayBridge::displayToCDelta(double deltaDisplay, bool fahrenheit) const {
    return TemperatureDisplay::displayToCDelta(deltaDisplay, fahrenheit);
}

QString TemperatureDisplayBridge::unitSuffix(bool fahrenheit) const {
    return TemperatureDisplay::unitSuffix(fahrenheit);
}
