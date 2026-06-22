#pragma once

#include <QHash>
#include <QVector>

// A profile frame's firmware-owned exit condition (pressure/flow over/under),
// as evaluated by the DE1 itself. Built per-frame at shot start and handed to
// the StepExitArbiter so it can tell how close the live sensor is to the
// firmware's own threshold.
struct FrameExitCondition {
    enum class Kind { None, PressureOver, PressureUnder, FlowOver, FlowUnder };
    Kind kind = Kind::None;
    double value = 0.0;

    bool isPressure() const {
        return kind == Kind::PressureOver || kind == Kind::PressureUnder;
    }
    bool isOver() const {
        return kind == Kind::PressureOver || kind == Kind::FlowOver;
    }
};

// Decides whether the tablet should send `skipToNextFrame` (a blind, relative
// SkipToNext) on a *mixed* frame — one carrying both a tablet weight exit and a
// firmware exit — or defer to let the firmware own the transition.
//
// The race it prevents: SkipToNext advances whatever frame the DE1 is currently
// in. If the firmware's own exit fires in the BLE round-trip window between the
// tablet deciding to skip and the command landing, the firmware advances the
// frame AND the late tablet skip advances it again — a double frame-advance that
// truncates short profiles. See docs/CLAUDE_MD/RECIPE_PROFILES.md.
//
// Consulted only when the weight threshold is already met on a frame that also
// has a firmware exit. Owned per-shot by WeightProcessor:
//   - reset() at shot start
//   - onFrameAdvanced() when the machine-reported profileFrame changes
class StepExitArbiter {
public:
    enum class Verdict {
        Fire,   // send SkipToNext now
        Defer,  // wait — firmware exit may fire on its own
    };

    // Maximum samples to defer before firing regardless. The per-frame weight
    // check runs on the scale tick (~5–10 Hz), so 3 samples ≈ 300–600 ms of
    // deferral. Must be ≥ 3 so the not-trending branch (which needs ≥ 2 recorded
    // readings) is reachable before the cap fires — otherwise the trend logic
    // that lets us fire early is bypassed. If the per-frame check ever moves to
    // the DE1 tick this cadence assumption must be revisited.
    static constexpr int kMaxDeferralSamples = 3;

    // Proximity window as a fraction of the exit threshold, with an absolute
    // floor so low-threshold exits still get a meaningful window. Calibrated to
    // DE1 sensor noise: wide enough to catch a genuine firmware approach, narrow
    // enough not to stall low-threshold steps.
    static constexpr double kPressureProximityFraction = 0.20;
    static constexpr double kFlowProximityFraction = 0.25;
    static constexpr double kPressureProximityMinimum = 0.3;  // bar
    static constexpr double kFlowProximityMinimum = 0.2;      // ml/s

    StepExitArbiter() = default;

    // Evaluate whether to fire or defer the tablet skip for a mixed frame.
    // currentPressure/currentFlow are the latest cached sensor readings.
    Verdict evaluate(int profileFrame, const FrameExitCondition& exit,
                     double currentPressure, double currentFlow);

    // The machine advanced to newFrame: drop deferral state for frames the
    // firmware has passed (it never revisits them).
    void onFrameAdvanced(int newFrame);

    // Clear all state. Call at shot start.
    void reset();

private:
    struct DeferralState {
        int sampleCount = 0;
        QVector<double> readings;

        void record(double sensorValue) {
            readings.append(sensorValue);
            sampleCount++;
        }
        // Whether the recent readings are all stepping toward the threshold.
        // A single reversal flips to "not trending". First sample (no prior)
        // assumes trending — give firmware the benefit of the doubt.
        bool isTrending(bool exitIsOver) const;
    };

    QHash<int, DeferralState> m_deferrals;
};
