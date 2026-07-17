#pragma once

#include <QString>

// A yield target as a spec, not a number (openspec change add-yield-ratio-
// anchor): one value plus an explicit mode discriminator, so "both an
// absolute and a ratio" is structurally unrepresentable. The mode strings
// are the stored vocabulary (recipes.yield_mode / coffee_bags.yield_mode /
// shots.yield_mode and the QSettings session anchor):
//
//   none     — no yield of its own; the next rung of the resolution ladder
//              (recipe -> bag -> profile target_weight) answers.
//   absolute — value is a gram target.
//   ratio    — value is a multiplier of the dose (2.0 renders as "1:2").
//
// The anchor is whichever of {ratio, yield} was LAST WRITTEN — writing a
// ratio IS setting the mode. Resolution to grams happens before
// MachineState::setTargetWeight; nothing downstream of it ever sees a ratio.
namespace YieldSpec {

inline QString modeNone()     { return QStringLiteral("none"); }
inline QString modeAbsolute() { return QStringLiteral("absolute"); }
inline QString modeRatio()    { return QStringLiteral("ratio"); }

// Stored rows can carry NULL/empty (pre-conversion imports) — read those as
// "none". Unknown junk from a wire surface also lands on "none" so it can
// never arm an anchor.
inline QString normalizedMode(const QString& mode) {
    if (mode == modeAbsolute() || mode == modeRatio())
        return mode;
    return modeNone();
}

inline bool isSet(const QString& mode) {
    return mode == modeAbsolute() || mode == modeRatio();
}

// The single ratio bound, enforced in C++ at every write boundary (design.md
// Open Question 5 — previously three inconsistent bounds, none in C++).
inline double clampRatio(double r) {
    return qBound(0.5, r, 6.0);
}

// Resolve a spec to grams against a dose. `fallbackG` answers for mode
// "none" (the next ladder rung, typically the profile's target_weight).
// A ratio with no usable dose resolves to the fallback too — a 0 g stop
// target must never reach the machine (see mcptools_control's dose-less
// machine_start_espresso).
inline double resolveGrams(const QString& mode, double value, double doseG, double fallbackG) {
    if (mode == modeAbsolute() && value > 0)
        return value;
    if (mode == modeRatio() && value > 0 && doseG > 0)
        return value * doseG;
    return fallbackG;
}

} // namespace YieldSpec
