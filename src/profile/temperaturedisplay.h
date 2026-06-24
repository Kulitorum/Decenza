#pragma once

#include <QString>
#include <QVector>

// Adaptive rendering of a profile's temperature(s) for the shot-plan widget and
// the Brew Settings dialog. A brew temperature override shifts EVERY frame by a
// uniform delta (preserving the curve shape), so a single number misrepresents a
// multi-temperature profile. These pure functions show the profile's own
// temperature(s) plus the OFFSET that will be applied to every step (e.g. "+1°"),
// keyed on the count of distinct frame temperatures (N):
//   N = 1  → single value         "90°C"
//   N = 2  → comma list           "90, 88°C"
//   N ≥ 3  → first…last ellipsis   "84…52°C"   (trajectory order, not sorted)
// When an override is active a signed delta tag is appended ("90, 88°C +1°"),
// expressing "all steps +1°" directly rather than recomputing per-step values.
//
// Logic lives here (not in QML) so it is unit-testable; the output carries no
// translatable words (only numbers, °C, separators and the delta tag), so the
// callers compose any surrounding labels.
namespace TemperatureDisplay {

// Number of distinct temperatures among the frames (within 0.05°C tolerance).
int distinctCount(const QVector<double>& stepTemps);

// Render the temperature segment.
//   stepTemps   – per-frame temperatures in frame order (may be empty)
//   anchorTemp  – the profile's espressoTemperature; the override delta is
//                 (overrideTemp - anchorTemp), and it is the fallback value when
//                 stepTemps is empty
//   hasOverride – whether a brew temperature override is active
//   overrideTemp– the active override value (ignored when !hasOverride)
QString format(const QVector<double>& stepTemps, double anchorTemp,
               bool hasOverride, double overrideTemp);

} // namespace TemperatureDisplay
