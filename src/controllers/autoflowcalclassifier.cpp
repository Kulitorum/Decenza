#include "autoflowcalclassifier.h"

#include <QSet>
#include <QtGlobal>
#include <algorithm>

namespace {

// Flow-target threshold: frames with `flow > 0.1` are considered active flow
// targets. Matches the historical profile-level scan, which ignored frames
// with near-zero flow targets to avoid treating no-op flow frames as anchors.
constexpr double kMinFlowTarget = 0.1;

}  // namespace

AutoFlowCalClassification classifyAutoFlowCalWindow(
    const QList<ProfileFrame>& steps,
    const QList<FrameTransition>& transitions,
    double windowStart,
    double windowEnd,
    double meanMachineFlow) {

    AutoFlowCalClassification result;

    if (transitions.isEmpty() || steps.isEmpty()) {
        result.fallbackToProfileScan = true;
        return result;
    }

    // Frame index active at `t`: the frameNumber of the latest transition
    // whose time <= t. Transitions are chronological, so a forward walk
    // works. Returns -1 if `t` predates the first transition.
    auto frameAtTime = [&](double t) -> int {
        int frameIndex = -1;
        for (const auto& tr : transitions) {
            if (tr.time <= t) {
                frameIndex = tr.frameNumber;
            } else {
                break;
            }
        }
        return frameIndex;
    };

    // Collect every frame index active during [windowStart, windowEnd]:
    // - The frame active at windowStart (from frameAtTime).
    // - Any frames entered strictly after windowStart and no later than
    //   windowEnd (picked up by scanning transitions in the window interval).
    QSet<int> framesInWindow;
    int startFrame = frameAtTime(windowStart);
    if (startFrame >= 0) {
        framesInWindow.insert(startFrame);
    }
    for (const auto& tr : transitions) {
        if (tr.time > windowStart && tr.time <= windowEnd && tr.frameNumber >= 0) {
            framesInWindow.insert(tr.frameNumber);
        }
    }

    if (framesInWindow.isEmpty()) {
        result.fallbackToProfileScan = true;
        return result;
    }

    result.firstFrameInWindow = *std::min_element(framesInWindow.constBegin(),
                                                  framesInWindow.constEnd());
    result.lastFrameInWindow = *std::max_element(framesInWindow.constBegin(),
                                                 framesInWindow.constEnd());

    // Classify every frame touched by the window.
    bool anyFlow = false;
    bool anyPressure = false;
    for (int idx : framesInWindow) {
        if (idx < 0 || idx >= steps.size()) {
            // An out-of-range frame index from the transition stream means
            // the marker data doesn't match the current profile (e.g. profile
            // was swapped mid-shot). Fall back to profile-level scan for
            // safety rather than guessing.
            result.fallbackToProfileScan = true;
            return result;
        }
        const auto& frame = steps[idx];
        if (frame.isFlowControl() && frame.flow > kMinFlowTarget) {
            anyFlow = true;
        } else {
            anyPressure = true;
        }
    }

    if (anyFlow && anyPressure) {
        result.mixedMode = true;
        return result;
    }

    if (anyFlow) {
        result.isFlowProfile = true;
        // Pick the flow target closest to the observed mean machine flow.
        // Preserves the historical multi-target handling (e.g. profiles that
        // step between two flow rates) without extra bookkeeping.
        double bestDist = 1e9;
        for (int idx : framesInWindow) {
            const auto& frame = steps[idx];
            if (frame.isFlowControl() && frame.flow > kMinFlowTarget) {
                double dist = qAbs(frame.flow - meanMachineFlow);
                if (dist < bestDist) {
                    bestDist = dist;
                    result.targetFlow = frame.flow;
                }
            }
        }
    } else {
        result.isFlowProfile = false;
    }

    return result;
}
