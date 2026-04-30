#pragma once

#include "ai/shotanalysis.h"

#include <QString>
#include <cmath>

// Projection from the typed ShotAnalysis::DetectorResults struct onto the
// five boolean quality-badge columns persisted in the `shots` DB table and
// surfaced via QualityBadges.qml. Single source of truth for the badge↔struct
// mapping — used by both save-time computation (saveShotData) and the
// recompute-on-load path (loadShotRecordStatic) so the cascade definition
// lives in exactly one place. See docs/SHOT_REVIEW.md §4 for the full
// mapping table.
//
// Mapping notes:
//   - channelingDetected fires ONLY on Sustained severity. Transient
//     channeling surfaces as a "Transient channel at Xs" caution line in the
//     dialog and as channelingSeverity == "transient" in MCP, but the
//     boolean badge stays false (matches PR #922's invariant).
//   - grindIssueDetected fires on chokedPuck OR yieldOvershoot OR
//     |flowDelta| > FLOW_DEVIATION_THRESHOLD, mirroring ShotAnalysis::
//     detectGrindIssue's semantics.
//   - tempUnstable already encodes its gates inside analyzeShot
//     (tempStabilityChecked && !tempIntentionalStepping && avgDev > threshold),
//     so no caller-side conjuncts are needed.
//   - pourTruncated and skipFirstFrame are 1:1 with their struct fields.
//
// Header-only by design: lets unit tests (`tst_shotanalysis`'s
// `badgeProjection_*` methods) call the projection directly without linking
// the full storage TU.

namespace decenza {

// Bag of the five boolean badge values, returned by deriveBadgesFromAnalysis.
struct BadgeFlags {
    bool pourTruncatedDetected = false;
    bool channelingDetected = false;
    bool temperatureUnstable = false;
    bool grindIssueDetected = false;
    bool skipFirstFrameDetected = false;
};

// Compute the projection. Pure function; no side effects.
inline BadgeFlags deriveBadgesFromAnalysis(const ShotAnalysis::DetectorResults& d)
{
    BadgeFlags flags;
    flags.pourTruncatedDetected = d.pourTruncated;
    flags.skipFirstFrameDetected = d.skipFirstFrame;
    flags.channelingDetected = (d.channelingSeverity == QStringLiteral("sustained"));
    flags.temperatureUnstable = d.tempUnstable;
    flags.grindIssueDetected = d.grindHasData
        && (d.grindChokedPuck
            || d.grindYieldOvershoot
            || std::abs(d.grindFlowDeltaMlPerSec) > ShotAnalysis::FLOW_DEVIATION_THRESHOLD);
    return flags;
}

// Convenience: apply the projection directly to a target whose field names
// match the BadgeFlags shape (ShotSaveData and ShotRecord both qualify).
// Templated so save and load paths share one call site each without an
// explicit conversion step.
template <typename T>
inline void applyBadgesToTarget(T& target, const ShotAnalysis::DetectorResults& d)
{
    const auto flags = deriveBadgesFromAnalysis(d);
    target.pourTruncatedDetected = flags.pourTruncatedDetected;
    target.skipFirstFrameDetected = flags.skipFirstFrameDetected;
    target.channelingDetected = flags.channelingDetected;
    target.temperatureUnstable = flags.temperatureUnstable;
    target.grindIssueDetected = flags.grindIssueDetected;
}

} // namespace decenza
