#pragma once

#include <QJsonValue>
#include <QString>

// Canonical numeric encoding for profile JSON — the ONE place the format's
// precision policy lives.
//
// Why this exists: the precision choice used to be copy-pasted at ~38 call sites
// across profile.cpp and profileframe.cpp. When the two serializers were unified,
// the Visualizer builder's display-oriented precisions came along with it, and
// target_weight silently dropped to 0 decimals against a 0.1 g editor step —
// 36.5 g became 37 g on save. Scattered magic numbers are how that happens twice.
//
// THE RULE: a field's decimal count must be >= the resolution its editor exposes.
// ProfileEditorPage uses 0.1 steps for target weight/temperature and 0.01 steps
// for pressures, flows and limiter ranges. Widen a value here before adding a
// finer control in QML, never after.
namespace ProfileJson {

inline constexpr int Temperature = 2;   // editor step 0.1
inline constexpr int Pressure    = 2;   // editor step 0.01
inline constexpr int Flow        = 2;   // editor step 0.01
inline constexpr int Seconds     = 2;   // editor step 1
inline constexpr int Volume      = 0;   // editor step 1, integer mL
inline constexpr int Weight      = 1;   // editor step 0.1 g
inline constexpr int Limiter     = 2;   // editor step 0.01
inline constexpr int TargetMass  = 1;   // target weight/volume, editor step 0.1

// Encode a number in the canonical string form used by de1app / the tablet /
// Visualizer / reaprime. Decenza's own readers stay dual-tolerant via toDouble().
inline QString enc(double v, int decimals) {
    return QString::number(v, 'f', decimals);
}

}  // namespace ProfileJson

// NOTE: the tolerant string-or-number parser profileJsonToDouble() is declared in
// profile.h and defined in profile.cpp. It is deliberately NOT re-declared here —
// two declarations of the same function is the very drift this header exists to
// prevent. Include profile.h for the reader; this header owns the WRITER policy.
