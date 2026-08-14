#pragma once

#include <QString>

/**
 * Why the frame that just ended exited.
 *
 * Extracted from MainController::onShotSample so the inference is unit-testable
 * — it feeds ShotAnalysis::detectSkipFirstFrame and analyzeFlowVsGoal's
 * limiter-tail trim, and both read it as ground truth. The vocabulary and the
 * precedence (weight -> sensor -> time -> unconfirmed) are specified by
 * openspec/specs/frame-transition-reason.
 *
 * The one non-obvious rule is the extrapolation tolerance. The DE1 evaluates
 * its own exit condition on an internal cadence roughly an order of magnitude
 * faster than the ~10 Hz BLE sample stream, so a fast-moving frame routinely
 * crosses its threshold BETWEEN two samples the app can see. Comparing the
 * transition sample against the threshold with no tolerance therefore fails to
 * confirm exactly the frames that exit fastest — a fill frame that hits its
 * pressure target in under two seconds is the normal case, not an anomaly.
 * Issue #1813: frame 0 configured `pressure_over 2.10`, transition sample
 * 2.048 bar, preceding sample 1.869 bar, 210 ms apart. The crossing happened
 * ~60 ms after the last sample the app saw; the machine did exactly what the
 * profile asked, and the app recorded `pressure_unconfirmed`, which made the
 * skip-first-frame detector badge a correct shot "First step skipped".
 *
 * The tolerance is the MEASURED per-sample change, never a constant: a reading
 * that is flat or moving away from the threshold gets none, and a transition
 * with no preceding sample in the same shot gets none either. That keeps the
 * unconfirmed branch meaningful — a genuinely skipped frame has no rise to
 * extrapolate from, so it still lands there, which is what the skip detector
 * exists to catch.
 *
 * The tolerance takes the two readings as one sample apart and does not measure
 * the gap between them, so a stalled BLE notification widens it. That direction
 * is deliberate: a stall means MORE unobserved time, so the threshold is more
 * likely to have been crossed out of sight, not less. Refusing to extrapolate
 * across a gap would fall back to `*_unconfirmed` exactly when the evidence for
 * a real crossing is strongest, and put the #1813 badge back on a shot whose
 * only fault was a dropped packet. Both readings are printed by the
 * confirmed-by-extrapolation log line in MainController, so if a stall ever
 * does produce a wrong verdict, a submitted log settles it with numbers rather
 * than a guess -- which is the point at which normalising by the real sample
 * interval would be worth its machinery.
 */
namespace FrameExit {

struct Inputs {
    // Exit configuration of the frame that just ENDED.
    bool exitIf = false;
    QString exitType;               // "pressure_over" / "pressure_under" / "flow_over" / "flow_under"
    double exitPressureOver = 0.0;
    double exitPressureUnder = 0.0;
    double exitFlowOver = 0.0;
    double exitFlowUnder = 0.0;
    double configuredSeconds = 0.0;

    // Sensor readings at the transition sample, and at the sample before it.
    double pressure = 0.0;
    double flow = 0.0;
    double prevPressure = 0.0;
    double prevFlow = 0.0;
    bool prevValid = false;         // false on the first sample of a shot

    double frameElapsedSec = 0.0;
    bool weightExit = false;        // app sent skipToNextFrame() on weight — 100% certain
};

struct Result {
    QString reason;
    // True when `reason` is a confirmed sensor exit that the transition sample
    // alone did not satisfy — the threshold was reached only by extrapolating
    // one sample forward. Logged so a submitted log distinguishes the two.
    bool extrapolated = false;
};

// A rising signal reaches `threshold` within one more sample's worth of the
// change observed between the previous sample and this one.
inline bool reachedOver(double now, double prev, bool prevValid, double threshold,
                        bool* extrapolated)
{
    if (now >= threshold) return true;
    if (!prevValid) return false;
    const double rise = now - prev;
    if (rise <= 0.0) return false;   // flat or falling: nothing to extrapolate
    if (now + rise < threshold) return false;
    if (extrapolated) *extrapolated = true;
    return true;
}

inline bool reachedUnder(double now, double prev, bool prevValid, double threshold,
                         bool* extrapolated)
{
    if (now <= threshold) return true;
    if (!prevValid) return false;
    const double fall = prev - now;
    if (fall <= 0.0) return false;
    if (now - fall > threshold) return false;
    if (extrapolated) *extrapolated = true;
    return true;
}

inline Result inferReason(const Inputs& in)
{
    Result r;

    if (in.weightExit) {
        r.reason = QStringLiteral("weight");
        return r;
    }

    if (!in.exitIf) {
        // No exit condition configured — the frame ran to its length.
        r.reason = QStringLiteral("time");
        return r;
    }

    // The `> 0` guards on the _under arms keep a zeroed/absent sensor reading
    // from confirming an exit it never saw.
    if (in.exitType == QStringLiteral("pressure_over")
        && reachedOver(in.pressure, in.prevPressure, in.prevValid,
                       in.exitPressureOver, &r.extrapolated)) {
        r.reason = QStringLiteral("pressure");
        return r;
    }
    if (in.exitType == QStringLiteral("pressure_under") && in.pressure > 0
        && reachedUnder(in.pressure, in.prevPressure, in.prevValid,
                        in.exitPressureUnder, &r.extrapolated)) {
        r.reason = QStringLiteral("pressure");
        return r;
    }
    if (in.exitType == QStringLiteral("flow_over")
        && reachedOver(in.flow, in.prevFlow, in.prevValid,
                       in.exitFlowOver, &r.extrapolated)) {
        r.reason = QStringLiteral("flow");
        return r;
    }
    if (in.exitType == QStringLiteral("flow_under") && in.flow > 0
        && reachedUnder(in.flow, in.prevFlow, in.prevValid,
                        in.exitFlowUnder, &r.extrapolated)) {
        r.reason = QStringLiteral("flow");
        return r;
    }

    r.extrapolated = false;  // no arm confirmed; nothing was extrapolated

    if (in.frameElapsedSec >= in.configuredSeconds * 0.9) {
        // Exit condition configured but time ran out first.
        r.reason = QStringLiteral("time");
        return r;
    }

    // Configured exit, threshold not reached even with the extrapolation, and
    // time did not expire — usually a real sensor exit whose crossing fell
    // further than one sample from what we saw, but a genuinely skipped frame
    // lands here too. Record it as an UNCONFIRMED sensor exit (hint from
    // exitType): displays render it like the sensor exit it probably was, the
    // grind detector's limiter-tail trim treats pressure_unconfirmed as
    // limiter engagement, and the skip-first-frame guard still refuses to
    // trust it.
    r.reason = in.exitType.contains(QStringLiteral("pressure"))
        ? QStringLiteral("pressure_unconfirmed")
        : QStringLiteral("flow_unconfirmed");
    return r;
}

}  // namespace FrameExit
