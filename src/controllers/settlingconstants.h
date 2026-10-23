#pragma once

#include <algorithm>

// Post-stop settling thresholds, and the weight saved when settling is abandoned. One
// definition shared by ShotTimingController and tools/shot_eval's settle replays, so the
// offline replay cannot drift from the app.
namespace Settling {

inline constexpr int SETTLING_WINDOW_SIZE = 6;         // 6-sample circular buffer
inline constexpr double SETTLING_AVG_THRESHOLD = 0.3;  // Max avg drift to declare stable (g)
inline constexpr int SETTLING_STABLE_MS = 1000;        // How long avg must be stable (ms)
// Minimum time the stability gate must hold continuously before
// m_lastCleanSettlingAvg is captured (#1280). Filters out transient
// gate-fires during noisy/oscillating settles — a single sample whose
// window avg happens to satisfy the gate must NOT be persisted as a
// "clean" value to fall back to on cup-removal. 250 ms ≈ 3 consecutive
// samples at the typical ~100 ms scale cadence; shorter than
// SETTLING_STABLE_MS so the fallback still applies to a 700 ms plateau
// (Mark's #1280 case) without waiting for full settlement.
inline constexpr int SETTLING_CLEAN_CAPTURE_MS = 250;
// Drip limits, measured from the settle base (below). On 362 undisturbed tablet shots
// (Oct 2026) drip after settling started, divided by the flow at the stop, was p99
// 1.74 s, max 2.17 s; the largest drip was 6.85 g, from a gusher SAW caught late (4.3 g/s).
inline constexpr double MAX_PLAUSIBLE_POST_STOP_DRIP_G = 5.0;  // floor
inline constexpr double POST_STOP_DRIP_MAX_S = 3.0;
inline constexpr double POST_STOP_DRIP_CEILING_G = 12.0;       // bounds a spiked flow reading
// After the pump stops, drip only slows; a hand or a pressed scale makes the weight rise
// faster. A short flow this far above the reference is not drip. On the tablet corpus
// (Oct 2026) clean shots never went more than 2.38 g/s over it (p99 0.95), while 23 of 30
// disturbed shots passed 4. Absolute, not relative to flow: the estimator's noise is.
inline constexpr double RATE_RISE_MARGIN_GPS = 4.0;
// A disturbance must persist this many samples: one corrupted packet is not a hand.
inline constexpr int DISTURBED_SAMPLES = 2;
inline constexpr double SETTLING_ABOVE_AVG_MARGIN = 0.2; // Current weight must be within this of avg to declare stable (g)
inline constexpr int SETTLING_SILENCE_OVERRIDE_MS = 2000; // If weight unchanged for this long, declare stable regardless of avg margin
// A drop this far below the settling peak is a cup lift, never drip — drip only adds.
inline constexpr double CUP_REMOVED_DROP_G = 20.0;
// A post-stop change smaller than this does not restart the stillness clock.
inline constexpr double SETTLING_STILL_DELTA_G = 0.1;
// A settled average this far below the stop weight is still recovering, not settled.
inline constexpr double SETTLING_AVG_BELOW_STOP_G = 0.5;

// The most a reading can rise above the settle base and still be drip. Beyond it the
// reading is a hand on the cup, a pressed or drifting scale, or a frozen one (shot 825
// froze at ~75 g on a ~40 g target). The short flow at the stop has been logged at 26 and
// 60 g/s, hence the ceiling. Written so a NaN flow lands on the floor.
constexpr double maxPlausibleDripG(double flowAtStopGps)
{
    const double byFlow = flowAtStopGps * POST_STOP_DRIP_MAX_S;
    return byFlow > POST_STOP_DRIP_CEILING_G ? POST_STOP_DRIP_CEILING_G
         : byFlow > MAX_PLAUSIBLE_POST_STOP_DRIP_G ? byFlow
         : MAX_PLAUSIBLE_POST_STOP_DRIP_G;
}

// What drip is measured from: the SAW trigger weight, or the weight when settling started
// if higher. A stop the DE1 acts on late keeps pouring after the trigger, and that coffee
// is in the cup.
constexpr double settleBaseG(double stopWeightG, double settleStartWeightG)
{
    return std::max(stopWeightG, settleStartWeightG);
}

constexpr bool isOverRise(double weightG, double baseG, double flowAtStopGps)
{
    return baseG > 0.0 && weightG > baseG + maxPlausibleDripG(flowAtStopGps);
}

// The flow drip is measured against: the short flow at the SAW trigger, or at the first
// settling sample if higher -- a stop the DE1 acts on late was still pouring faster.
constexpr double settleFlowRefGps(double flowAtStopGps, double flowAtSettleStartGps)
{
    return std::max(flowAtStopGps, flowAtSettleStartGps);
}

enum class SettleDisturbance { None, OverRise, RateRise };

// One settling sample. The amount limit applies only where drip is bounded: an espresso
// puck. A filter or tea bed keeps draining standing water after the stop, which can pass
// any amount limit, but a drain only slows, so the flow test covers every beverage.
constexpr SettleDisturbance classifySettleSample(double weightG, double flowShortGps,
                                                 double baseG, double flowAtStopGps,
                                                 double flowRefGps, bool amountCheck)
{
    if (amountCheck && isOverRise(weightG, baseG, flowAtStopGps))
        return SettleDisturbance::OverRise;
    if (flowShortGps > flowRefGps + RATE_RISE_MARGIN_GPS)
        return SettleDisturbance::RateRise;
    return SettleDisturbance::None;
}

// A rejected packet still lives in the worker's regression window. If the next
// reading returns to the last accepted weight, its high flow is the spike's tail,
// not independent confirmation. Ignore that tail while readings stay in the
// recovered band. Normal flow or a fresh rise beyond the band ends recovery;
// an amount disturbance is never suppressed. Shared with the timestamped replay.
class RateRiseRecovery {
public:
    SettleDisturbance filter(SettleDisturbance candidate, double weightG,
                            double lastAcceptedG, bool pending, double pendingWeightG)
    {
        if (candidate != SettleDisturbance::RateRise) {
            m_recovering = false;
            return candidate;
        }
        if (pending && weightG < pendingWeightG - SETTLING_ABOVE_AVG_MARGIN
            && weightG <= lastAcceptedG + SETTLING_ABOVE_AVG_MARGIN) {
            m_recovering = true;
            m_ceilingG = lastAcceptedG + SETTLING_ABOVE_AVG_MARGIN;
        }
        if (m_recovering && weightG <= m_ceilingG)
            return SettleDisturbance::None;
        m_recovering = false;
        return candidate;
    }

private:
    bool m_recovering = false;
    double m_ceilingG = 0.0;
};

constexpr bool isCupLift(double weightG, double peakG)
{
    return weightG < peakG - CUP_REMOVED_DROP_G;
}

// What to save when settling is abandoned because the reading stopped being drip: the
// cup was lifted, or a disturbance was classified above. A captured clean average is
// always within the amount limit, because every sample averaged into it was checked first.
enum class AbandonedSettleSource { CleanAvg, SettleBase, LastReading };
struct AbandonedSettle {
    double weightG;
    AbandonedSettleSource source;
};
constexpr AbandonedSettle abandonedSettleWeight(double lastCleanAvgG, double baseG,
                                                double lastReadingG, bool disturbed)
{
    if (lastCleanAvgG > 0.0)
        return {lastCleanAvgG, AbandonedSettleSource::CleanAvg};
    // Drip only adds, so a reading below the base is an artifact; and a disturbance leaves
    // no reading to trust, since the one before it may be the disturbance starting.
    if (baseG > 0.0 && (disturbed || lastReadingG < baseG))
        return {baseG, AbandonedSettleSource::SettleBase};
    return {lastReadingG, AbandonedSettleSource::LastReading};
}

constexpr const char* abandonedSettleSourceText(AbandonedSettleSource source)
{
    return source == AbandonedSettleSource::CleanAvg   ? "the last steady reading"
         : source == AbandonedSettleSource::SettleBase ? "the stop weight"
                                                       : "the last reading";
}

}  // namespace Settling
