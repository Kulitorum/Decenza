#pragma once

#include <QList>
#include "../profile/profileframe.h"

/**
 * Minimal frame-transition record used as input to the auto-flow-cal
 * window classifier. Decoupled from `PhaseMarker` (which lives in
 * shotdatamodel.h and pulls in Qt Charts / Quick) so the classifier
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
