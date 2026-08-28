#pragma once

#include <QList>
#include "../profile/profileframe.h"

/**
 * Minimal frame-transition record used as input to the auto-flow-cal
 * window classifier. Decoupled from `PhaseMarker` (which lives in
 * shotdatamodel.h and pulls in Qt Graphs / Quick) so the classifier
 * can be unit-tested without the full ShotDataModel stack.
 *
 * Callers in production code convert `PhaseMarker -> FrameTransition`
 * at the call site (trivial copy of `time` and `frameNumber`).
 */
struct FrameTransition {
    double time = 0.0;     // Shot-relative seconds. Must share the timebase
                           // used for windowStart / windowEnd.
    int frameNumber = -1;  // 0-based profile frame index; -1 means
                           // "before extraction / no frame yet".
};

/**
 * Result of classifying the pump-control mode active during an auto-flow-cal
 * steady window. The caller branches on `mixedMode` / `fallbackToProfileScan`
 * first; only when both are false should `isFlowProfile` and `targetFlow` be
 * used.
 */
struct AutoFlowCalClassification {
    /// Window spans both flow- and pressure-controlled frames. The caller
    /// should skip this window (no unambiguous anchor).
    bool mixedMode = false;

    /// Phase-marker data was unavailable or unusable. The caller should fall
    /// back to profile-level scanning (the pre-fix behavior) so calibration
    /// still runs on shots without marker data.
    bool fallbackToProfileScan = false;

    /// True when every frame touched by the window is flow-controlled.
    bool isFlowProfile = false;

    /// Target flow (mL/s) of the flow-controlled frame whose target is
    /// closest to the observed mean machine flow. Only valid when
    /// `isFlowProfile == true`.
    double targetFlow = 0.0;

    /// Lowest frame index observed during the window (for logging).
    int firstFrameInWindow = -1;
    /// Highest frame index observed during the window (for logging).
    int lastFrameInWindow = -1;
};

/// Flow-target threshold: frames with `flow > kAutoFlowCalMinFlowTarget` are
/// considered active flow targets, filtering out near-zero flow frames that
/// are really pressure-controlled in practice. Shared between
/// `classifyAutoFlowCalWindow()` and `MainController::computeAutoFlowCalibration()`'s
/// profile-level fallback scan via `isActiveFlowFrame()`/`pickClosestFlowTarget()`
/// below, so the two picking paths can't drift on what counts as "a flow frame".
constexpr double kAutoFlowCalMinFlowTarget = 0.1;

/// Default relative-undershoot threshold for `autoFlowCalWindowTargetCheck()`
/// below (10%). Named here, not re-typed as a literal at each call site
/// (production or test), so the production constant and the values tests
/// assert against can't silently diverge.
constexpr double kAutoFlowCalDeviationThreshold = 0.10;

/// True if `frame` counts as an active flow-controlled anchor for auto flow
/// calibration purposes: pump control is flow AND its target is above the
/// no-op threshold.
bool isActiveFlowFrame(const ProfileFrame& frame);

/**
 * Picks the flow target (mL/s) among the given frame `indices` closest to
 * `meanMachineFlow`, considering only frames where `isActiveFlowFrame()` is
 * true. Ties resolve to whichever qualifying index comes FIRST in `indices`
 * — the caller controls tie-break order by how it orders `indices` (e.g.
 * frame-index-ascending for a deterministic "lowest index wins").
 *
 * @return The picked target, or `0.0` if no frame in `indices` qualifies.
 */
double pickClosestFlowTarget(
    const QList<ProfileFrame>& steps,
    const QList<int>& indices,
    double meanMachineFlow);

/**
 * Classify the pump-control mode active during an auto-flow-cal steady
 * window using the frame-transition data recorded during the shot.
 *
 * Rationale: a hybrid profile (e.g. ASL9-3) has both pressure-controlled
 * decline frames and a flow-controlled tail frame. A profile-level scan
 * classifies it as "flow" because it has any flow frame, but the steady
 * window almost always lands in the pressure declines. Anchoring the
 * v3 formula to the flow tail's target then produces false rejections
 * ("extraction anomaly") and spurious multiplier jumps on the rare
 * window that slips through. Classifying by the frames actually touched
 * by the window routes ASL9-3 shots correctly to the v2 (pressure)
 * branch and leaves flow-only profiles (e.g. D-Flow / Q) unchanged.
 *
 * @param steps             Ordered profile frames (typically `Profile::steps()`).
 * @param transitions       Frame transitions recorded during the shot,
 *                          ordered by `time` ascending.
 * @param windowStart       Shot-relative start of the steady window (s).
 * @param windowEnd         Shot-relative end of the steady window (s).
 * @param meanMachineFlow   Mean reported flow during the window (mL/s),
 *                          used to pick the closest flow target when
 *                          multiple flow frames are touched.
 */
AutoFlowCalClassification classifyAutoFlowCalWindow(
    const QList<ProfileFrame>& steps,
    const QList<FrameTransition>& transitions,
    double windowStart,
    double windowEnd,
    double meanMachineFlow);

/**
 * Result of checking a flow-controlled window's measured flow against its
 * frame's target. `deviation` is always computed (0.0 for a non-positive
 * target); `missedTarget` additionally requires undershoot — see below.
 */
struct AutoFlowCalTargetCheck {
    /// True only when the window UNDERSHOT target by more than the caller's
    /// threshold. Never true for an overshoot, regardless of magnitude.
    bool missedTarget = false;
    /// Relative deviation |measured - target| / target. Always >= 0.
    /// Exposed so a caller that skips on `missedTarget` can also log the
    /// magnitude without recomputing the same formula a second time.
    double deviation = 0.0;
};

/**
 * Checks whether a flow-controlled window's measured mean machine flow fell
 * meaningfully short of the touched frame's target flow.
 *
 * Rationale: a flow-controlled frame can carry a pressure ceiling (e.g.
 * D-Flow, D-Flow/Q). When the puck's resistance would require exceeding that
 * ceiling to hold the frame's target flow, the DE1 caps pressure instead and
 * flow falls below target for the rest of the frame. The flow branch's formula
 * up to 2.0.4 divided by the target (`weightFlow / (targetFlow * density)`),
 * so an unattained target manufactured an ideal that measured nothing about
 * pump-model accuracy (Kulitorum/Decenza#1823). That formula is gone as of v6 —
 * both control modes now use the pump-model error — but the check still
 * matters, because that error itself varies with flow RATE. A caller should
 * SKIP a window where
 * `missedTarget` is true: it measured the pump model at a rate the profile
 * does not pour at, and a single per-profile multiplier cannot describe two
 * operating points. Measured on one DE1 at a fixed multiplier, the ratio of
 * scale weight flow to reported machine flow runs 0.76 at 1.9 mL/s and 1.13
 * at 0.72 mL/s — so a capped window and a target-met window on the same
 * profile disagree by 30-40%.
 *
 * The achieved-flow (pressure-branch) formula is NOT the answer here, though
 * it was used that way between 2.0.4 and this change: it does not remove that
 * disagreement, it flips its sign (the #1872 reporter's capped window gave
 * 0.902 via the flow branch and 1.351 via the achieved-flow branch), which
 * made his multiplier oscillate with how many of the week's shots capped.
 * See `openspec/changes/skip-off-target-flow-cal-windows/`.
 *
 * Deliberately ONE-SIDED: only undershoot (`meanMachineFlow < targetFlow`)
 * can set `missedTarget`, never overshoot. A pressure ceiling can hold flow
 * BELOW its setpoint; it has no mechanism to push flow above it, so an
 * overshoot reading has no pressure-cap explanation and the window is still
 * genuinely flow-controlled — it is measuring the pump model at an operating point
 * the profile really does reach, which is the only thing this check is for.
 *
 * @param meanMachineFlow   Mean reported flow during the window (mL/s).
 * @param targetFlow        The touched frame's target flow (mL/s). Must be > 0;
 *                          returns `{false, 0.0}` for a non-positive target
 *                          (nothing to compare against).
 * @param thresholdFraction Relative undershoot above which the window is
 *                          considered pressure-capped and skipped
 *                          (e.g. 0.10 for 10%).
 */
AutoFlowCalTargetCheck autoFlowCalWindowTargetCheck(
    double meanMachineFlow,
    double targetFlow,
    double thresholdFraction);

/**
 * The auto-flow-cal ideal for one steady window, for BOTH control modes.
 *
 *     ideal = currentFactor * weightFlow / (machineFlow * density)
 *
 * `machineFlow` is the DE1's REPORTED flow, which already carries
 * `currentFactor`. Dividing it back out recovers the model's own estimate, so
 * the result is the model's ERROR: water actually delivered over water the
 * model says was delivered. That is the value the stored multiplier is supposed
 * to equal. Note the DE1 has no flow meter — Decent removed it in favour of an
 * open-loop physics model over pump strokes — so this corrects a model, not a
 * sensor. Two properties follow, and both are asserted in
 * `tests/tst_autoflowcal.cpp`:
 *
 *  - It is a FIXED POINT: feeding it a machine already calibrated correctly
 *    returns the same multiplier, so a converged machine stops moving.
 *  - It is INVARIANT under `currentFactor` on a machine that servos its
 *    calibrated flow (raising the multiplier lowers delivered water, so
 *    `machineFlow` holds and `weightFlow` falls in proportion). Measured
 *    across three unrelated machines the stored multiplier moved 15-38% while
 *    this expression moved 4-15%.
 *
 * Flow-controlled windows used a different expression until v6,
 * `weightFlow / (targetFlow * density)`. On a window holding target that
 * equals `ideal / currentFactor`, and blending it in at the batch EMA's alpha
 * of 0.5 is Babylonian square-root iteration — so it settled on the SQUARE ROOT
 * of the pump-model error rather than the error itself, which is where
 * flow-profile machines were observed to sit. See
 * `openspec/changes/skip-off-target-flow-cal-windows/design.md`.
 *
 * No clamping, no guards: callers apply the window ratio guards and
 * `kCalibrationMin`/`kCalibrationMax` bounds themselves. Returns a non-finite
 * value if `machineFlow` or `density` is zero; the caller checks.
 */
double autoFlowCalIdeal(
    double currentFactor,
    double weightFlow,
    double machineFlow,
    double density);
