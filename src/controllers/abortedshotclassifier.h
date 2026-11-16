#pragma once

#include <algorithm>

namespace decenza {

// Aborted-shot classifier — see issue #899 / openspec/specs/shot-save-filter.
//
// A shot "did not start" iff it ran under 10 s AND weighed under the larger
// of 5 g and a third of its stop target. Both clauses use strict <.
//
// The 5 g floor alone kept a 1.5 s group rinse as a 6.4 g shot against a 32 g
// target: with no cup, the water lands on the scale. The floor still decides
// shots with no weight target (stopTargetG <= 0). It was validated against an
// 882-shot corpus (5 discarded, all genuine); the one-third clause was chosen, not
// validated: low enough to keep a short shot stopped on purpose.
// Long, low-yield chokes (60 s / 1 g) are kept: their graphs are diagnostic.

inline constexpr double kAbortedDurationSec = 10.0;
inline constexpr double kAbortedYieldG = 5.0;
inline constexpr double kAbortedTargetFraction = 1.0 / 3.0;

inline bool isAbortedShot(double extractionDurationSec, double finalWeightG,
                          double stopTargetG) {
    return extractionDurationSec < kAbortedDurationSec
        && finalWeightG < std::max(kAbortedYieldG, stopTargetG * kAbortedTargetFraction);
}

struct ShotYield {
    double measuredG = 0.0;
    double recordedG = 0.0;
    bool aborted = false;
};

// With no yield the record takes the profile's target (SAW-stopped shots land
// near it); the aborted verdict never does. On the target a 0.2 s false start
// was kept as a 42 g shot (#2058). The verdict judges against stopTargetG, the
// weight this shot was actually set to stop at.
inline ShotYield resolveShotYield(double extractionDurationSec, double measuredG,
                                  double profileTargetG, double stopTargetG) {
    return {measuredG,
            measuredG <= 0.0 && profileTargetG > 0.0 ? profileTargetG : measuredG,
            isAbortedShot(extractionDurationSec, measuredG, stopTargetG)};
}

}  // namespace decenza
