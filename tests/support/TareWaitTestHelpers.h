#pragma once

#include "machine/weightprocessor.h"

// Bringing a WeightProcessor to a known point in its TARE WAIT — the window between
// startExtraction() arming the wait and the scale's zero being seen to land. Shared by
// tst_saw and tst_weightprocessor, which both need it and previously carried
// byte-identical copies of all of it.
//
// The wait is worth a helper because getting into it is not one call: flow can start
// before the app's own tare lands, so markExtractionStart() grants a bounded run of
// arrivals that this class refuses to judge, and a test that does not feed those
// arrivals spends the grace on its own samples and measures something else.
namespace TareWait {

// WeightProcessor::kTareGraceSamplesAfterFlow, which is private. Mirrored rather than
// befriended: a test that read the constant out of the class could not fail when the
// constant moved, and this is the only statement of what the grace is worth in arrivals.
inline constexpr int kGraceArrivals = 6;

inline void feed(WeightProcessor& wp, double weight, int count, qint64& clock,
                 int intervalMs = 100)
{
    for (int i = 0; i < count; i++) {
        wp.processWeight(weight);
        clock += intervalMs;
    }
}

// "Flow has started and the scale is tared" — the state a real shot reaches before any
// weight accumulates. The two near-zero samples are the point, not scaffolding: they are
// what ends the tare wait, so the samples a test feeds afterwards are judged.
inline void armExtraction(WeightProcessor& wp, qint64& clock)
{
    wp.startExtraction();
    feed(wp, 0.0, 2, clock);  // kTareLandedConfirmations near-zero samples
    wp.markExtractionStart();
    wp.setTareComplete(true);
}

// The grace alone, for a wait re-armed without a fresh startExtraction() — a
// mid-extraction retare, say. Silent by construction, which QTest::failOnWarning()
// turns into an assertion.
inline void burnGrace(WeightProcessor& wp, double cupWeight, qint64& clock,
                      int intervalMs = 100)
{
    feed(wp, cupWeight, kGraceArrivals, clock, intervalMs);
}

// A shot whose scale is NEVER tared — the untared-cup case. No zero arrives, so the
// wait ends on its grace instead, and every arrival after that is judged. Feeds the
// cup's own reading, which is what a real untared cup sends throughout.
inline void armExtractionUntared(WeightProcessor& wp, double cupWeight, qint64& clock,
                                 int intervalMs = 100)
{
    wp.startExtraction();
    wp.markExtractionStart();
    wp.setTareComplete(true);
    burnGrace(wp, cupWeight, clock, intervalMs);
}

}  // namespace TareWait
