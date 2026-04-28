#pragma once

namespace decenza {

// Aborted-shot classifier — see issue #899 / openspec/changes/add-discard-aborted-shots.
//
// A shot is classified as "aborted" (i.e. did not start) iff:
//   extractionDurationSec < 10.0  AND  finalWeightG < 5.0
//
// Both clauses use strict <. The thresholds were validated against an
// 882-shot corpus; 5/882 (0.57%) classify as aborted, all genuine
// "did not start" cases (no false-drops). Long, low-yield shots
// (e.g. 60 s / 1 g chokes) are deliberately kept because their graphs
// are diagnostically valuable for puck-prep tuning.
//
// Header-only so the unit test can exercise it without linking the full
// MainController dependency graph.

inline constexpr double kAbortedDurationSec = 10.0;
inline constexpr double kAbortedYieldG = 5.0;

inline bool isAbortedShot(double extractionDurationSec, double finalWeightG) {
    return extractionDurationSec < kAbortedDurationSec
        && finalWeightG < kAbortedYieldG;
}

}  // namespace decenza
