#pragma once

#include <QHash>
#include <QString>
#include <QVector>

// A profile frame's firmware-owned exit condition (pressure/flow over/under),
// as evaluated by the DE1 itself. Built per-frame at shot start and handed to
// the StepExitArbiter so it can tell how close the live sensor is to the
// firmware's own threshold. The default {None, 0} is the inert "no firmware
// exit" value; arbitration only applies to actionable conditions.
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
    // A condition the firmware can actually act on. {None, …} and any
    // non-positive threshold (e.g. pressure_over 0) are non-actionable, so the
    // tablet treats the frame as weight-only and never arbitrates against them.
    bool isActionable() const { return kind != Kind::None && value > 0.0; }

    // Build a condition from a profile frame's firmware-exit fields. Returns
    // the inert {None, 0} when the frame has no firmware exit (exitIf false) or
    // declares an unrecognized exitType. Centralises the exitType → Kind mapping
    // so it has one definition and one test surface (vs inlined in main.cpp).
    static FrameExitCondition fromExitFields(bool exitIf, const QString& exitType,
                                             double pressureOver, double pressureUnder,
                                             double flowOver, double flowUnder);
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

    // Maximum near-threshold samples to defer before firing regardless. Firing
    // on the 3rd recorded sample spans 2 inter-sample intervals, so on the scale
    // tick (~5–10 Hz) the worst-case deferral is ~200–400 ms. Must be ≥ 3 so the
    // not-trending branch (which needs ≥ 2 recorded readings) is reachable before
    // the cap fires — otherwise the trend logic that lets us fire early is
    // bypassed. If the per-frame check ever moves to the DE1 tick this cadence
    // assumption must be revisited.
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
        // readings is the single source of truth; the deferred-sample count is
        // derived from it so the two can never drift out of sync.
        QVector<double> readings;

        void record(double sensorValue) { readings.append(sensorValue); }
        qsizetype count() const { return readings.size(); }
        // Whether the recent readings are all stepping toward the threshold.
        // A single reversal flips to "not trending". First sample (no prior)
        // assumes trending — give firmware the benefit of the doubt.
        bool isTrending(bool exitIsOver) const;
    };

    QHash<int, DeferralState> m_deferrals;
};
