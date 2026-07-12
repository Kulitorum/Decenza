#pragma once

#include <QObject>
#include <QString>
#include <QVector>

// Adaptive rendering of a profile's temperature(s) for the shot-plan widget and
// the Brew Settings dialog. A brew temperature override shifts EVERY frame by a
// uniform delta (preserving the curve shape), so a single number misrepresents a
// multi-temperature profile. These pure functions show the profile's own
// temperature(s) plus the OFFSET that will be applied to every step (e.g. "+1°"),
// keyed on the count of distinct frame temperatures (N):
//   N = 1  → single value          "90°C"
//   N = 2  → spaced mid-dot list   "90 · 88°C"
//   N ≥ 3  → first…last ellipsis    "84…52°C"   (trajectory order, not sorted)
// When an override is active a signed delta tag is appended ("90 · 88°C +1°"),
// expressing "all steps +1°" directly rather than recomputing per-step values.
//
// The `fahrenheit` argument converts the values and swaps the unit symbol to °F
// (the numeric examples above are the default Celsius rendering).
// Logic lives here (not in QML) so it is unit-testable; the output carries no
// translatable words (only numbers, the °C/°F unit symbol, separators and the
// delta tag), so the callers compose any surrounding labels.
namespace TemperatureDisplay {

// --- Unit-conversion primitives (display/entry only; storage stays Celsius) ---
// An absolute temperature shifts origin (×9/5 +32); a DELTA/offset scales only
// (+4°C = +7.2°F). Single source of the conversion math: format() below and the
// QML Theme helpers (via TemperatureDisplayBridge) both delegate here.
double cToDisplay(double celsius, bool fahrenheit);
double displayToC(double display, bool fahrenheit);
double cDeltaToDisplay(double deltaCelsius, bool fahrenheit);
double displayToCDelta(double deltaDisplay, bool fahrenheit);
QString unitSuffix(bool fahrenheit);

// Number of distinct temperatures among the frames (within 0.05°C tolerance).
int distinctCount(const QVector<double>& stepTemps);

// Render the temperature segment.
//   stepTemps   – per-frame temperatures in frame order (may be empty)
//   anchorTemp  – the profile's espressoTemperature; the override delta is
//                 (overrideTemp - anchorTemp), and it is the fallback value when
//                 stepTemps is empty
//   hasOverride – whether a brew temperature override is active
//   overrideTemp– the active override value (ignored when !hasOverride)
//   fahrenheit  – when true the (Celsius) inputs are converted to °F for display
//                 (absolute ×9/5+32, delta ×9/5) and the unit symbol becomes °F
//   baselineShiftC – added to every displayed frame temperature (Celsius). Used to
//                 show a recipe's OWN temps as the baseline: a recipe applies a
//                 uniform delta to every frame, so the shot plan shows the shifted
//                 temps (e.g. "81 · 91°C") rather than "profile + delta". Pass the
//                 recipe temp as anchorTemp too so the tag measures deviation FROM
//                 the recipe (0 at the recipe baseline). Defaults to 0 = no shift.
QString format(const QVector<double>& stepTemps, double anchorTemp,
               bool hasOverride, double overrideTemp, bool fahrenheit = false,
               double baselineShiftC = 0.0);

} // namespace TemperatureDisplay

// Thin QML bridge for the conversion primitives above, registered as the
// "TemperatureDisplay" context property so qml/Theme.qml delegates to the same
// unit-tested math instead of mirroring it in JS. `fahrenheit` is an explicit
// argument (not read from Settings here) because the QML binding engine cannot
// track property reads inside C++ methods — the Theme wrappers read
// Settings.app.temperatureUnit in JS so bindings re-evaluate on unit toggle.
class TemperatureDisplayBridge : public QObject {
    Q_OBJECT
public:
    explicit TemperatureDisplayBridge(QObject* parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE double cToDisplay(double celsius, bool fahrenheit) const;
    Q_INVOKABLE double displayToC(double display, bool fahrenheit) const;
    Q_INVOKABLE double cDeltaToDisplay(double deltaCelsius, bool fahrenheit) const;
    Q_INVOKABLE double displayToCDelta(double deltaDisplay, bool fahrenheit) const;
    Q_INVOKABLE QString unitSuffix(bool fahrenheit) const;
};
