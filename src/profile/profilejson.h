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
inline constexpr int Volume      = 1;   // editor step 1, but keep 1dp: ProfileFrame::
                                        // toTclList() writes volume with 1dp, and an
                                        // imported frame carrying e.g. 22.5 mL must not
                                        // truncate to 22 just because our editor is
                                        // integer-only. The two writers must agree.
inline constexpr int Weight      = 1;   // editor step 0.1 g
inline constexpr int Limiter     = 2;   // editor step 0.01
inline constexpr int TargetMass  = 1;   // target_weight editor step 0.1; target_volume is
                                        // integer-stepped but shares this (1dp covers both)
inline constexpr int TankTemp    = 1;   // tank target, editor step 1 °C

// Encode a number in the canonical string form used by de1app / the tablet /
// Visualizer / Decaid. Decenza's own readers stay dual-tolerant via toDouble().
inline QString enc(double v, int decimals) {
    // Clamp a negative zero to zero. Imported profiles carry values like -5.7e-15
    // (seen in a de1app cleaning profile), which format as "-0.00" — valid to any
    // parser but confusing in a diff, and it reappears on every re-import unless
    // fixed at the encoder rather than in the data file.
    if (v > -0.5e-9 && v < 0.0) v = 0.0;
    return QString::number(v, 'f', decimals);
}

}  // namespace ProfileJson

// NOTE: the tolerant string-or-number parser profileJsonToDouble() is declared in
// profile.h and defined in profile.cpp. It is deliberately NOT re-declared here —
// two declarations of the same function is the very drift this header exists to
// prevent. Include profile.h for the reader; this header owns the WRITER policy.
