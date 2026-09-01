#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>

#include "machine/weightprocessor.h"
#include "support/TareWaitTestHelpers.h"

// Test WeightProcessor edge cases: LSLR flow estimation, oscillation recovery,
// per-frame weight exit, untared cup detection, and processWeight state guards.
//
// Complements tst_saw.cpp (which tests SAW gate logic).
// These tests focus on mathematical correctness and state transitions.
//
// Uses an injectable fake clock to avoid real-time waits (~39s → <1s).
//
// de1app reference: proc check_if_should_stop_espresso (de1plus/de1_comms.tcl)

class tst_WeightProcessor : public QObject {
    Q_OBJECT

private:
    qint64 m_fakeClock = 1000000;  // Arbitrary start (1,000,000 ms)

    void installFakeClock(WeightProcessor& wp) {
        wp.setWallClock([this]() { return m_fakeClock; });
    }

    void configureEspresso(WeightProcessor& wp, double targetWeight, int preinfuseFrames,
                           QVector<double> frameExitWeights = {},
                           QVector<FrameExitCondition> frameExitConditions = {}) {
        QVector<double> learningDrips;
        QVector<double> learningFlows;
        wp.configure(targetWeight, preinfuseFrames, frameExitWeights, frameExitConditions,
                     learningDrips, learningFlows, false, 0.38);
    }

    // Feed rising weight samples to build valid LSLR history
    void feedRising(WeightProcessor& wp, double startWeight, double flowRate,
                    int count, int intervalMs = 200) {
        for (int i = 0; i < count; i++) {
            double w = startWeight + flowRate * (i * intervalMs / 1000.0);
            wp.processWeight(w);
            m_fakeClock += intervalMs;
        }
    }

    // Feed constant weight to build LSLR with zero slope
    void feedConstant(WeightProcessor& wp, double weight, int count, int intervalMs = 200) {
        for (int i = 0; i < count; i++) {
            wp.processWeight(weight);
            m_fakeClock += intervalMs;
        }
    }

    // Arm a single mixed frame (frame 0) and bring the worker to the point where
    // the per-frame weight check runs. Frame 1 has no weight exit.
    void armMixedFrame(WeightProcessor& wp, double exitWeight,
                       FrameExitCondition fw) {
        QVector<double> weights = {exitWeight, 0.0};
        QVector<FrameExitCondition> conds = {fw, {}};
        configureEspresso(wp, 0, 0, weights, conds);   // no SAW target
        TareWait::armExtraction(wp, m_fakeClock);
    }

    // Arm a shot whose scale sits at `zero` when flow starts. The sample COUNT is the
    // point: markExtractionStart() only adopts an offset once the tare has been
    // observed to land, which takes kTareLandedConfirmations near-zero samples. Written
    // once so raising that constant does not quietly stop these tests from arming --
    // the two that expect NO correction would still pass, for the wrong reason.
    void armWithPreShotZero(WeightProcessor& wp, double zero, int samples = 2) {
        wp.startExtraction();
        feedConstant(wp, zero, samples, 100);
        wp.markExtractionStart();
        wp.setTareComplete(true);
    }

private slots:

    void init() { QTest::failOnWarning();
        m_fakeClock = 1000000;  // Reset for each test
    }

    // ==========================================
    // Pre-shot zero correction
    // ==========================================
    //
    // A tare leaves a small residual and the zero keeps creeping through preheat --
    // measured at -0.1 to -0.5 g across six shots. It is a BIAS, not noise: every
    // reading that shot is low by the same amount, so stop-at-weight stops late by it
    // and the saved final weight is short by it, always in the same direction.

    void preShotZeroIsSubtractedFromEveryWeight() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy spy(&wp, &WeightProcessor::flowRatesReady);

        armWithPreShotZero(wp, -0.4);  // the drifted zero, as it stands at flow start

        // A cup that really holds 36.0 g reads 35.6 on a zero sitting 0.4 g low.
        // What reaches SAW and the saved final weight must be the true 36.0.
        wp.processWeight(35.6);
        QVERIFY(!spy.isEmpty());
        QCOMPARE(spy.last().at(0).toDouble(), 36.0);
    }

    void preShotZeroIgnoresAReadingTooLargeToBeAZero() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy spy(&wp, &WeightProcessor::flowRatesReady);

        // 5 g is not a drifted zero -- it is something sitting on the platter. Only
        // the bound rules it out: it is well under the 50 g the untared-cup detector
        // reacts to, so nothing else in processWeight() would catch it. Subtracting
        // it would under-report the yield by 5 g and stop the shot 5 g heavy.
        armWithPreShotZero(wp, 5.0);

        wp.processWeight(36.0);
        QVERIFY(!spy.isEmpty());
        QCOMPARE(spy.last().at(0).toDouble(), 36.0);
    }

    void preShotZeroIsNotCapturedWhenTheTareWasNeverObserved() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy spy(&wp, &WeightProcessor::flowRatesReady);

        // ONE near-zero sample: below kTareLandedConfirmations, so the tare is never
        // confirmed to have landed. -1.8 g sits inside the 2 g bound, so only the
        // observed-tare gate can reject it -- if it were adopted, every weight this
        // shot would be 1.8 g heavy and the shot would stop 1.8 g light.
        armWithPreShotZero(wp, -1.8, 1);

        wp.processWeight(36.0);
        QVERIFY(!spy.isEmpty());
        QCOMPARE(spy.last().at(0).toDouble(), 36.0);
    }

    void retareRecapturesThePreShotZero() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy spy(&wp, &WeightProcessor::flowRatesReady);

        armWithPreShotZero(wp, -0.4);

        // A re-tare moves the zero, so the old offset must not survive it.
        wp.resetForRetare();
        feedConstant(wp, 0.9, 2, 100);
        wp.markExtractionStart();
        wp.setTareComplete(true);

        wp.processWeight(36.9);        // 0.9 offset now, not -0.4
        QVERIFY(!spy.isEmpty());
        QCOMPARE(spy.last().at(0).toDouble(), 36.0);
    }

    void theShotsZeroIsRetiredWhenTheShotIsSaved() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy offsets(&wp, &WeightProcessor::preShotZeroOffsetChanged);

        armWithPreShotZero(wp, -0.4);
        QVERIFY(!offsets.isEmpty());
        QCOMPARE(offsets.last().at(0).toDouble(), -0.4);

        wp.stopExtraction();
        // The signal is the ONLY channel to the surfaces that mirror the offset --
        // live readout, MQTT, MCP, widget. An offset dropped without notifying leaves
        // them subtracting this shot's number from the idle scale until app restart,
        // which no tare can undo (the scale zeroes, the app keeps subtracting).
        wp.clearPreShotZeroOffset();
        QCOMPARE(offsets.last().at(0).toDouble(), 0.0);
    }

    // ==========================================
    // LSLR flow estimation
    // ==========================================

    void constantWeightGivesZeroFlow() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy spy(&wp, &WeightProcessor::flowRatesReady);

        feedConstant(wp, 10.0, 8);

        QVERIFY(spy.count() >= 6);
        // Last flow rate should be near 0
        auto lastArgs = spy.last();
        double flowRate = lastArgs.at(1).toDouble();
        QVERIFY2(flowRate < 0.1,
                 qPrintable(QString("Constant weight should give ~0 flow, got %1").arg(flowRate)));
    }

    void risingWeightGivesPositiveFlow() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy spy(&wp, &WeightProcessor::flowRatesReady);

        // 2 g/s for 1.5 seconds at 200ms intervals
        feedRising(wp, 0.0, 2.0, 8);

        QVERIFY(spy.count() >= 6);
        auto lastArgs = spy.last();
        double flowRate = lastArgs.at(1).toDouble();
        // Should be approximately 2.0 g/s (within tolerance for LSLR window fill)
        QVERIFY2(flowRate > 1.0 && flowRate < 3.5,
                 qPrintable(QString("Rising 2g/s should give ~2.0 flow, got %1").arg(flowRate)));
    }

    // A bursty transport must not blind the short-flow estimate.
    //
    // Regression for the Half Decent WiFi scale: frames arrive several to a read,
    // so the de-jitter interval — calibrated from non-batched gaps only — learned
    // the INTER-BURST gap (~490 ms) instead of the 100 ms cadence. Synthetic
    // timestamps then outran wall-clock, hit the lead cap, and every later frame in
    // a burst was pinned to the same clamped value. computeLSLR's fill gate saw a
    // span far under 65% of the window and returned 0.0, so flowRateShort read
    // exactly 0.00 while the cup was filling at 2 g/s and SAW could not trigger.
    //
    // Asserted through flowRatesReady rather than by reaching into m_weightSamples:
    // a wrong short-flow on a rising feed IS the user-visible defect, and a test on
    // the timestamps alone would keep passing if the gate arithmetic changed
    // underneath it.
    //
    // The assertion is ACCURACY, deliberately, and the first version of this test
    // was wrong for want of that. It asserted only "not zero, roughly 1-3.5 g/s"
    // and PASSED against the unfixed code, because this burst shape does not drive
    // flowRateShort to zero — it drives it to 1.22 g/s on a true 2.00 g/s pour, the
    // inflated interval stretching the apparent spacing. A 40% under-read is not a
    // milder version of the bug: SAW's threshold is target minus expectedDrip(flow),
    // so under-read flow under-predicts drip, raises the threshold and stops LATE,
    // which is the overshoot actually measured on the device. Bounds are ±10% —
    // wide enough for LSLR window fill, far too tight for 1.22.
    void burstyDeliveryKeepsShortFlowValid() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy spy(&wp, &WeightProcessor::flowRatesReady);

        // 10 Hz scale delivered as 5-frame bursts every 500 ms, 2 g/s, ~4 s.
        feedBursty(wp, 2.0, /*cadenceMs=*/100, /*framesPerBurst=*/5, /*bursts=*/8);
        // The last burst only — the first second is the calibration window, where a
        // wrong estimate is expected and self-corrects.
        assertShortFlowNear(spy, 2.0, 5, "5-frame burst at 10 Hz");
    }

    // Feed `bursts` groups of `framesPerBurst` arrivals, each group delivered within
    // a couple of ms and then silent for the rest of its period. Weight tracks each
    // frame's TRUE sample instant, not its arrival, which is the whole point: a
    // correct de-jitter reconstructs the former from the latter.
    void feedBursty(WeightProcessor& wp, double flowRate, int cadenceMs,
                    int framesPerBurst, int bursts, int stallBeforeBurst = -1,
                    int stallMs = 0) {
        int frame = 0;
        for (int b = 0; b < bursts; ++b) {
            if (b == stallBeforeBurst) m_fakeClock += stallMs;
            for (int f = 0; f < framesPerBurst; ++f) {
                wp.processWeight(flowRate * (frame * cadenceMs / 1000.0));
                m_fakeClock += 2;  // batched: under the 20 ms threshold
                ++frame;
            }
            m_fakeClock += framesPerBurst * (cadenceMs - 2);
        }
    }

    void assertShortFlowNear(QSignalSpy& spy, double expected, int lastN,
                             const char* what) {
        QVERIFY(spy.count() > lastN);
        for (qsizetype i = spy.count() - lastN; i < spy.count(); ++i) {
            const double f = spy.at(i).at(2).toDouble();
            // 10%, matching what the callers' comments claim. It was 20% here while
            // a comment above asserted 10% — the looser bound still failed the
            // 1.22 g/s regression, so nothing was caught, but the guarantee a
            // reader took from the comment was twice what the code delivered.
            QVERIFY2(qAbs(f - expected) < 0.1 * expected,
                     qPrintable(QString("%1: sample %2 read %3 g/s, expected ~%4")
                                    .arg(what).arg(i).arg(f).arg(expected)));
        }
    }

    // A burst longer than the synthetic lead allowance must not silently break.
    //
    // The lead allowance was a flat 1000 ms at one point in this fix, which made it
    // an unstated cap on burst size: a burst of N frames needs (N-1) intervals of
    // lead, so 10 Hz was exact to 11 frames and then collapsed to 0.00 g/s at 14 —
    // the identical symptom the fix exists to remove. Sizing the allowance from the
    // burst in hand is what removes the cliff, and this is the case that has to
    // fail if anyone reinstates a constant.
    void longBurstDoesNotCapTheLeadAllowance() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy spy(&wp, &WeightProcessor::flowRatesReady);

        feedBursty(wp, 2.0, 100, 14, 12);
        assertShortFlowNear(spy, 2.0, 14, "14-frame burst at 10 Hz");
    }

    // A hiccup shorter than a reconnect must not poison the cadence estimate.
    //
    // kScaleStaleMs and kReconnectGapMs are both 2000, so a 1.5 s gap is by
    // definition neither a stall nor a reconnect, and an averaging estimator folded
    // it in as ordinary spacing: 145 ms learned for a 100 ms feed, and ~1 s of pour
    // reading 1.38-1.53 g/s. Taking the median of recent windows discards the
    // contaminated one instead.
    void subReconnectHiccupDoesNotPoisonTheInterval() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy spy(&wp, &WeightProcessor::flowRatesReady);

        feedBursty(wp, 2.0, 100, 5, 30, /*stallBeforeBurst=*/10, /*stallMs=*/1500);
        // Well past the hiccup: the estimate must have shrugged it off entirely,
        // not merely recovered eventually.
        assertShortFlowNear(spy, 2.0, 40, "10 Hz feed after a 1.5 s hiccup");
    }

    // A reconnect-sized gap must not leave the cadence estimate serving a
    // measurement it just discarded.
    //
    // The reconnect branch clears the ring's fill count, and that count doubles as
    // the index bound for the median — so it only means anything while the
    // write index is also back at zero. Clearing one and not the other left the
    // next window writing at a stale index: the median read the pre-reconnect value
    // it had been told to drop and never read the fresh one, for two windows, at
    // the exact moment a feed came back. Both fields now move through
    // resetRateCalibration().
    //
    // Drives the case the other tests cannot: a >2 s silence (kReconnectGapMs),
    // then a genuinely different cadence. The old estimate is 5x too small for the
    // new feed, which spaces timestamps far too tightly and collapses the LSLR
    // span — so a stale estimate shows up as short flow going wrong, not merely as
    // an internal field being off.
    void reconnectGapDoesNotServeAStaleCadence() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy spy(&wp, &WeightProcessor::flowRatesReady);

        // The returning feed must be BURSTY, and that is the whole design of this
        // test rather than a detail. A stale interval is only ever CONSULTED on the
        // batched branch: a non-bursty feed takes ts = wallClock on every sample and
        // never reads the estimate at all, so an evenly-paced feed after the gap
        // passes whether or not the ring is desynced. The first version of this test
        // did exactly that and passed against the bug it was written for.
        //
        // 10 Hz before, 5 Hz after: the stale estimate is then half the true cadence,
        // which spaces a burst's timestamps at 100 ms for samples 200 ms apart and
        // reads as roughly double the real flow.
        feedBursty(wp, 2.0, /*cadenceMs=*/100, /*framesPerBurst=*/5, /*bursts=*/6);
        m_fakeClock += 2500;  // > kReconnectGapMs
        const qsizetype afterReconnect = spy.count();
        // Five bursts, and the count is load-bearing rather than arbitrary: the
        // desync costs TWO extra rate windows, so the two versions differ only
        // for about two seconds after the gap. Feed long enough and the buggy
        // ring converges too and the test goes quiet — at 8 bursts both read
        // 2.00. Simulated across this window, 4-6 bursts separate them cleanly
        // (buggy 1.43/1.43/0.00 against fixed 2.00/2.00/2.00).
        feedBursty(wp, 2.0, /*cadenceMs=*/200, /*framesPerBurst=*/3, /*bursts=*/5);

        QVERIFY(spy.count() > afterReconnect + 10);
        assertShortFlowNear(spy, 2.0, 3, "5 Hz bursty feed after a >2 s reconnect gap");
    }

    // A transport stall must not latch the cadence estimate onto the catch-up burst.
    // A stall queues samples rather than dropping them, so the backlog lands in one
    // window measuring half the true interval — see processWeight().
    //
    // The stall gives a 1500 ms inter-arrival gap (1400 plus the trailing step), under
    // kReconnectGapMs (2000, weightprocessor.cpp:245): calibration is NOT reset, so the
    // latch path is live. Above it the estimator starts over and the fixture proves
    // nothing.
    void stallCatchUpDoesNotLatchTheCadenceEstimateLow() {
        WeightProcessor wp;
        installFakeClock(wp);

        constexpr int kCadenceMs = 100;
        constexpr double kFlow = 2.0;
        qint64 sentMs = 0;

        // Weight follows the time the sample was TAKEN, not the time it arrived —
        // the queued samples carry the weights they were measured at.
        auto send = [&](qint64 arrivalStepMs) {
            wp.processWeight(kFlow * sentMs / 1000.0);
            sentMs += kCadenceMs;
            m_fakeClock += arrivalStepMs;
        };

        for (int i = 0; i < 50; ++i) send(kCadenceMs);   // 5 s even: commits 100 ms
        QCOMPARE(wp.estimatedIntervalMsForTesting(), 100);

        m_fakeClock += 1400;                             // transport stalls
        for (int i = 0; i < 14; ++i) send(2);            // backlog released together

        // The latching window closes on the 10th sample of the resumed feed, so assert
        // at a DEFINED point rather than min-tracking a loop whose length is load-
        // bearing but unstated: at 8 samples the catch-up window never closes and the
        // test would pass while discriminating nothing.
        for (int i = 0; i < 12; ++i) send(kCadenceMs);   // feed resumes at 10 Hz
        const int committed = wp.estimatedIntervalMsForTesting();

        QVERIFY2(committed >= 90,
                 qPrintable(QString("cadence estimate fell to %1 ms after a 1.5 s "
                                    "transport stall on a true 100 ms feed").arg(committed)));
    }

    // KNOWN DEFECT, held here on purpose: jittered burst timing over-reads flow.
    //
    // This test FAILS by design (QEXPECT_FAIL). It reproduces a real defect that
    // has no fix yet, so that the defect is a running, measured thing rather than a
    // note someone has to rediscover.
    //
    // A 10 Hz feed delivered in bursts whose PERIOD jitters +-60 ms does not read a
    // steady flow at all. It OSCILLATES within each burst — measured across one
    // burst on a true 2.00 g/s feed:
    //
    //     2.25  2.92  2.92  2.25  0.04
    //
    // The last sample of every burst is near zero.
    //
    // WHAT THIS FIXTURE IS NOT: the Half Decent WiFi scale. Field measurement over
    // four shots put that feed at 3-8% batched with a deepest burst of 6, i.e.
    // almost entirely evenly paced, and blind samples at 0-3 per shot out of ~270.
    // This fixture feeds 100% batched, five-frame bursts back to back — far more
    // aggressive than any transport measured here. An earlier version of this
    // comment claimed the oscillation meant stop-at-weight was going blind "on a
    // regular beat" in the field; the diagnostics added alongside it say otherwise,
    // and this note is the correction.
    //
    // So read the test as arithmetic, not as a report from the machine: it holds a
    // genuine weakness in how a heavily-batched feed is fitted, against the day a
    // transport delivers like that. What the real scale does instead is deliver
    // irregularly — gaps followed by tight runs — which is a different problem the
    // 65%-fill rule handles badly and which this fixture does not model.
    //
    // What it is NOT: a choice of averaging statistic. The estimate is a
    // median-of-three, switched from minimum-of-three to fix the stall latch (a
    // different defect — see processWeight()), and measured at MATCHING sample
    // positions the median gives 2.16 2.96 2.96 2.16 0.04 against the minimum's
    // 2.25 2.92 2.92 2.25 0.04: the same shape, the same near-zero.
    // Recorded because the wrong comparison was convincing enough to ship.
    //
    // Do not compare statistics on a single sample from this fixture. Any one frame
    // is a different number, so any two runs can be made to say anything.
    //
    // Bursty on purpose: the cadence estimate only reaches the timestamps through
    // the batched branch, so an evenly-paced feed cannot exercise this at all.
    //
    // If this ever XPASSes, the underlying behaviour changed — work out why before
    // deleting the QEXPECT_FAIL.
    void cadenceEstimateDoesNotTrackTheLowTail() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy spy(&wp, &WeightProcessor::flowRatesReady);

        constexpr int kCadenceMs = 100;
        constexpr double kFlow = 2.0;
        constexpr int kFramesPerBurst = 5;
        const int jitter[] = {-60, 40, -30, 55, -45, 25, 60, -50};
        const qint64 t0 = m_fakeClock;
        for (int b = 0; b < 24; ++b) {
            for (int f = 0; f < kFramesPerBurst; ++f) {
                // Weight follows the WALL CLOCK, not the frame index. Deriving it
                // from the index looks equivalent and is not once the burst period
                // jitters: it delivers a fixed 1.0 g per burst over a varying real
                // interval, i.e. 1.8-2.27 g/s, and the test then measures the
                // fixture instead of the estimator. It read 1.75 g/s that way under
                // BOTH statistics, which is what exposed the mistake.
                wp.processWeight(kFlow * (m_fakeClock - t0) / 1000.0);
                m_fakeClock += 2;
            }
            m_fakeClock += kFramesPerBurst * (kCadenceMs - 2) + jitter[b % 8];
        }

        // Asserted inline rather than through assertShortFlowNear: that helper's
        // first check is a QVERIFY on the sample count, which PASSES, and
        // QEXPECT_FAIL binds to the very next check — so routing through the helper
        // marks the count check as an unexpected pass and never reaches the flow.
        // Asserts the SPREAD across one burst, not a single sample, because the
        // defect is an oscillation and any one frame of it is a different number.
        // Reading a single sample is how the first version of this test produced a
        // false comparison between two statistics.
        QVERIFY(spy.count() > kFramesPerBurst);
        double lo = 1e9, hi = -1e9;
        for (qsizetype i = spy.count() - kFramesPerBurst; i < spy.count(); ++i) {
            const double f = spy.at(i).at(2).toDouble();
            lo = qMin(lo, f);
            hi = qMax(hi, f);
        }
        QEXPECT_FAIL("", "Jittered burst timing makes short flow oscillate within "
                         "each burst (measured 0.04-2.96 g/s on a true 2.00, with the "
                         "last sample of every burst near zero). Open. NOT a choice "
                         "of averaging statistic — the minimum measures the same. See "
                         "the comment above this test.", Abort);
        QVERIFY2(hi - lo < 0.2 * kFlow,
                 qPrintable(QString("short flow swung %1 to %2 g/s within one burst "
                                    "on a steady %3 g/s feed").arg(lo).arg(hi).arg(kFlow)));
    }

    void negativeWeightClampedToZero() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy spy(&wp, &WeightProcessor::flowRatesReady);

        // Decreasing weight (dripping off scale)
        for (int i = 0; i < 8; i++) {
            wp.processWeight(20.0 - i * 2.0);
            m_fakeClock += 200;
        }

        QVERIFY(spy.count() >= 6);
        auto lastArgs = spy.last();
        double flowRate = lastArgs.at(1).toDouble();
        // LSLR clamps negative slope to 0
        QVERIFY2(flowRate >= 0.0,
                 qPrintable(QString("Negative slope should clamp to 0, got %1").arg(flowRate)));
    }

    // ==========================================
    // Oscillation recovery
    // ==========================================

    // An espresso cycle aborted BEFORE flow must disarm the worker. It is
    // armed by startExtraction() at cycle start (during preheat), but
    // stopExtraction() hangs off shotEnded, which only fires once flow has
    // STARTED — so an abort during preheat used to leave SAW live against the
    // dead shot's target, re-checking every weight sample that arrived until
    // the next shot happened to re-arm it. Put a cup on the scale after
    // aborting and SAW could fire at an idle machine. endShotCycle() is the
    // pair of startExtraction and fires on every cycle exit.
    void abortedCycleDisarmsSaw() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        wp.startExtraction();          // cycle start (preheat) — SAW armed
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);

        QSignalSpy stopSpy(&wp, &WeightProcessor::stopNow);

        // The cycle exits without ever reaching the target — an abort.
        wp.endShotCycle();

        // Weight now climbs past the dead shot's 36 g target (a cup set down
        // on the scale). Nothing should fire: no shot is running. This is the
        // SAME feed as normalShotStillStopsAtTarget, which asserts it fires
        // when armed — so the only difference here is endShotCycle(), and this
        // cannot pass by simply never reaching the threshold.
        m_fakeClock += 5500;
        feedRising(wp, 30.0, 2.0, 20);   // crosses the 35.5 g stop threshold

        QCOMPARE(stopSpy.count(), 0);
    }

    // The disarm must not break the normal path: a shot that actually flows
    // still stops at its target.
    void normalShotStillStopsAtTarget() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        TareWait::armExtraction(wp, m_fakeClock);
        wp.setCurrentFrame(0);

        QSignalSpy stopSpy(&wp, &WeightProcessor::stopNow);

        // Identical feed to abortedCycleDisarmsSaw. This is the CONTROL: it
        // proves that feed actually crosses the stop threshold, so the sibling
        // test's "count == 0" means disarmed rather than never-triggered.
        // Without it that test passed vacuously — the feed stopped at 33.6 g.
        m_fakeClock += 5500;
        feedRising(wp, 30.0, 2.0, 20);
        QCOMPARE(stopSpy.count(), 1);
    }

    void oscillationBlocksSaw() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        TareWait::armExtraction(wp, m_fakeClock);
        wp.setCurrentFrame(0);

        QSignalSpy stopSpy(&wp, &WeightProcessor::stopNow);

        // Advance past 5s guard
        m_fakeClock += 5500;

        // Build valid flow, get close to target
        feedRising(wp, 30.0, 2.0, 5);

        // Weight drops to -5g (scale tare reset) — triggers oscillation warning
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Scale oscillation detected"));
        wp.processWeight(-6.0);
        m_fakeClock += 200;

        // Now feed weight above target — should NOT trigger SAW
        // because oscillation detection blocked tare
        feedConstant(wp, 40.0, 5);

        QCOMPARE(stopSpy.count(), 0);
    }

    void oscillationRecoveryAfterSettling() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        TareWait::armExtraction(wp, m_fakeClock);
        wp.setCurrentFrame(0);

        QSignalSpy stopSpy(&wp, &WeightProcessor::stopNow);

        // Advance past 5s
        m_fakeClock += 5500;

        // Trigger oscillation
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Scale oscillation detected"));
        wp.processWeight(-6.0);
        m_fakeClock += 200;

        // Settle near zero (exactly 3 readings to trigger recovery — no extra
        // samples that would re-arm the rate filter at 0.5g baseline)
        feedConstant(wp, 0.5, 3);

        // Now build valid flow and exceed target — should trigger SAW
        // (rate filter was disarmed by settle, so the jump to 30g is accepted)
        feedRising(wp, 30.0, 2.0, 20);

        QVERIFY2(stopSpy.count() >= 1,
                 "SAW should trigger after oscillation recovery + valid flow");
    }

    // ==========================================
    // Per-frame weight exit
    // ==========================================

    void perFrameExitFires() {
        WeightProcessor wp;
        installFakeClock(wp);
        QVector<double> frameExits = {0.0, 5.0, 0.0};  // Frame 1 exits at 5g
        configureEspresso(wp, 0, 0, frameExits);  // No SAW target
        TareWait::armExtraction(wp, m_fakeClock);
        wp.setCurrentFrame(1);

        QSignalSpy skipSpy(&wp, &WeightProcessor::skipFrame);

        // Feed weight above exit threshold
        wp.processWeight(6.0);
        m_fakeClock += 200;

        QVERIFY(skipSpy.count() >= 1);
        QCOMPARE(skipSpy.first().at(0).toInt(), 1);  // Frame 1
    }

    void perFrameExitOnlyOnce() {
        WeightProcessor wp;
        installFakeClock(wp);
        QVector<double> frameExits = {5.0};
        configureEspresso(wp, 0, 0, frameExits);
        TareWait::armExtraction(wp, m_fakeClock);
        wp.setCurrentFrame(0);

        QSignalSpy skipSpy(&wp, &WeightProcessor::skipFrame);

        // Feed weight above threshold multiple times
        for (int i = 0; i < 5; i++) {
            wp.processWeight(10.0);
            m_fakeClock += 200;
        }

        QCOMPARE(skipSpy.count(), 1);  // Only once
    }

    void perFrameExitDisabledWhenZero() {
        WeightProcessor wp;
        installFakeClock(wp);
        QVector<double> frameExits = {0.0};  // Disabled
        configureEspresso(wp, 0, 0, frameExits);
        TareWait::armExtraction(wp, m_fakeClock);
        wp.setCurrentFrame(0);

        QSignalSpy skipSpy(&wp, &WeightProcessor::skipFrame);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 99"));
        wp.processWeight(99.0);  // tare landed by armExtraction, so this is judged
        m_fakeClock += 200;

        QCOMPARE(skipSpy.count(), 0);  // Disabled, no skip
    }

    // ==========================================
    // Step-exit arbiter (mixed weight + firmware exit frames)
    // ==========================================

    // Firmware far from its threshold → fire immediately, no deferral.
    void mixedFrameFiresWhenFirmwareFar() {
        WeightProcessor wp;
        installFakeClock(wp);
        armMixedFrame(wp, 1.0, {FrameExitCondition::Kind::PressureOver, 9.0});

        QSignalSpy skipSpy(&wp, &WeightProcessor::skipFrame);
        wp.setCurrentFrame(0, /*pressure*/ 2.0, /*flow*/ 0.0);  // far from 9 bar
        wp.processWeight(2.0);                                  // weight ≥ 1g

        QCOMPARE(skipSpy.count(), 1);
        QCOMPARE(skipSpy.first().at(0).toInt(), 0);
    }

    // Firmware near and trending toward its threshold → defer until the cap.
    void mixedFrameDefersWhenNearTrending() {
        WeightProcessor wp;
        installFakeClock(wp);
        armMixedFrame(wp, 1.0, {FrameExitCondition::Kind::PressureOver, 2.0});

        QSignalSpy skipSpy(&wp, &WeightProcessor::skipFrame);

        // window = max(0.20*2, 0.3) = 0.4 bar; all readings within and rising.
        const double rising[] = {1.7, 1.8, 1.9};
        for (int i = 0; i < 3; i++) {
            wp.setCurrentFrame(0, rising[i], 0.0);
            wp.processWeight(2.0);
            m_fakeClock += 200;
            if (i < StepExitArbiter::kMaxDeferralSamples - 1)
                QCOMPARE(skipSpy.count(), 0);  // still deferring
        }
        // Cap (kMaxDeferralSamples=3) reached on the 3rd sample → fire once.
        QCOMPARE(skipSpy.count(), 1);
    }

    // Firmware near but NOT trending → fire early (before the cap).
    void mixedFrameFiresWhenNearNotTrending() {
        WeightProcessor wp;
        installFakeClock(wp);
        armMixedFrame(wp, 1.0, {FrameExitCondition::Kind::PressureOver, 2.0});

        QSignalSpy skipSpy(&wp, &WeightProcessor::skipFrame);

        wp.setCurrentFrame(0, 1.9, 0.0);   // near (distance 0.1)
        wp.processWeight(2.0);
        QCOMPARE(skipSpy.count(), 0);       // first sample defers
        m_fakeClock += 200;

        wp.setCurrentFrame(0, 1.7, 0.0);   // near but falling → not trending
        wp.processWeight(2.0);
        QCOMPARE(skipSpy.count(), 1);       // fires before the cap
    }

    // The core race guard: firmware advances the frame while the tablet is
    // deferring → the tablet must NOT send a (now stale) skip for the old frame.
    void firmwareAdvanceDuringDeferralNoDoubleSkip() {
        WeightProcessor wp;
        installFakeClock(wp);
        armMixedFrame(wp, 1.0, {FrameExitCondition::Kind::PressureOver, 2.0});

        QSignalSpy skipSpy(&wp, &WeightProcessor::skipFrame);

        // Defer on frame 0 (near + trending).
        wp.setCurrentFrame(0, 1.7, 0.0);
        wp.processWeight(2.0);
        m_fakeClock += 200;
        wp.setCurrentFrame(0, 1.8, 0.0);
        wp.processWeight(2.0);
        m_fakeClock += 200;
        QCOMPARE(skipSpy.count(), 0);

        // Firmware fires its own pressure exit: frame advances to 1.
        wp.setCurrentFrame(1, 1.5, 0.0);
        wp.processWeight(2.0);

        // Frame 1 has no weight exit → nothing skipped. Crucially, no skip was
        // ever sent for frame 0: the firmware owned that transition.
        QCOMPARE(skipSpy.count(), 0);
    }

    // Regression for the imported "soup" profile: fill frame has
    // pressure_over 2.0 + weight 1.0; 1 g is reached as pressure trends through
    // 2 bar. Before the arbiter this double-skipped the fill frame, collapsing
    // the 2-frame profile. The tablet must defer and let firmware own the exit.
    void soupProfileNoDoubleSkip() {
        WeightProcessor wp;
        installFakeClock(wp);
        armMixedFrame(wp, 1.0, {FrameExitCondition::Kind::PressureOver, 2.0});

        QSignalSpy skipSpy(&wp, &WeightProcessor::skipFrame);

        // Weight already past 1 g while pressure ramps toward 2 bar (trending) —
        // the tablet defers rather than racing the firmware. Firmware fires
        // within a sample or two (well before the deferral cap).
        const double ramp[] = {1.6, 1.85};
        for (double p : ramp) {
            wp.setCurrentFrame(0, p, 0.0);
            wp.processWeight(1.5);
            m_fakeClock += 200;
        }
        QCOMPARE(skipSpy.count(), 0);  // deferred, no tablet skip yet

        // Firmware crosses 2.0 bar and advances fill → hold on its own.
        wp.setCurrentFrame(1, 2.1, 0.0);
        wp.processWeight(1.5);

        // No tablet skip on the fill frame → exactly one (firmware) advance.
        QCOMPARE(skipSpy.count(), 0);
    }

    // Direct StepExitArbiter unit checks (proximity, trend, pruning).
    void arbiterProximityAndTrend() {
        StepExitArbiter a;
        const FrameExitCondition over9{FrameExitCondition::Kind::PressureOver, 9.0};

        // Far from threshold → Fire.
        QCOMPARE(a.evaluate(0, over9, /*p*/ 2.0, /*f*/ 0.0),
                 StepExitArbiter::Verdict::Fire);

        // Near threshold, first sample → Defer (benefit of the doubt).
        const FrameExitCondition over2{FrameExitCondition::Kind::PressureOver, 2.0};
        QCOMPARE(a.evaluate(1, over2, 1.9, 0.0),
                 StepExitArbiter::Verdict::Defer);
        // Second sample falling → not trending → Fire.
        QCOMPARE(a.evaluate(1, over2, 1.7, 0.0),
                 StepExitArbiter::Verdict::Fire);
    }

    void arbiterOnFrameAdvancedPrunes() {
        StepExitArbiter a;
        const FrameExitCondition over2{FrameExitCondition::Kind::PressureOver, 2.0};

        // Build two deferral samples on frame 0 (rising → trending → Defer).
        QCOMPARE(a.evaluate(0, over2, 1.7, 0.0), StepExitArbiter::Verdict::Defer);
        QCOMPARE(a.evaluate(0, over2, 1.8, 0.0), StepExitArbiter::Verdict::Defer);

        // Machine leaves frame 0 then (hypothetically) re-enters: state must be
        // pruned, so the next evaluate is a fresh first sample → Defer, not the
        // cap-fire that a count of 3 would have produced.
        a.onFrameAdvanced(1);
        QCOMPARE(a.evaluate(0, over2, 1.9, 0.0), StepExitArbiter::Verdict::Defer);
    }

    // Flow exits must read the flow sensor (not pressure) and use the flow window.
    void arbiterUsesFlowSensorForFlowExit() {
        StepExitArbiter a;
        const FrameExitCondition flowOver{FrameExitCondition::Kind::FlowOver, 2.5};
        // Pressure is deliberately at/near a pressure threshold; only flow matters.
        // flow far from 2.5 → Fire.
        QCOMPARE(a.evaluate(0, flowOver, /*p*/ 9.0, /*f*/ 0.5),
                 StepExitArbiter::Verdict::Fire);
        // flow near 2.5 (window max(0.25*2.5,0.2)=0.625; distance 0.2) → Defer.
        QCOMPARE(a.evaluate(1, flowOver, /*p*/ 0.0, /*f*/ 2.3),
                 StepExitArbiter::Verdict::Defer);
    }

    // "under" exits trend toward the threshold by FALLING, not rising.
    void arbiterTrendForUnderExit() {
        StepExitArbiter a;
        const FrameExitCondition flowUnder{FrameExitCondition::Kind::FlowUnder, 1.0};
        // window = max(0.25*1.0,0.2)=0.25; readings near 1.0 and falling → Defer.
        QCOMPARE(a.evaluate(0, flowUnder, 0.0, 1.2), StepExitArbiter::Verdict::Defer);
        QCOMPARE(a.evaluate(0, flowUnder, 0.0, 1.1), StepExitArbiter::Verdict::Defer);

        // pressure_under, rising away from threshold → not trending → Fire.
        const FrameExitCondition pUnder{FrameExitCondition::Kind::PressureUnder, 1.0};
        QCOMPARE(a.evaluate(2, pUnder, 1.1, 0.0), StepExitArbiter::Verdict::Defer);  // first sample
        QCOMPARE(a.evaluate(2, pUnder, 1.2, 0.0), StepExitArbiter::Verdict::Fire);   // rose away
    }

    // Non-actionable firmware exit (value ≤ 0, or Kind::None) → fire as weight-only.
    void arbiterNonActionableFires() {
        StepExitArbiter a;
        QCOMPARE(a.evaluate(0, {FrameExitCondition::Kind::PressureOver, 0.0}, 0.0, 0.0),
                 StepExitArbiter::Verdict::Fire);
        QCOMPARE(a.evaluate(0, FrameExitCondition{}, 5.0, 5.0),  // Kind::None
                 StepExitArbiter::Verdict::Fire);
    }

    // The absolute proximity floor governs low-threshold exits (not the fraction).
    void arbiterProximityFloorOnLowThreshold() {
        StepExitArbiter a;
        // value 0.5: 0.20*0.5 = 0.1, but the 0.3 bar floor widens the window.
        // A reading 0.25 out is "far" under the fraction alone but "near" with
        // the floor → Defer, proving the floor is in effect.
        const FrameExitCondition over{FrameExitCondition::Kind::PressureOver, 0.5};
        QCOMPARE(a.evaluate(0, over, /*p*/ 0.25, 0.0),
                 StepExitArbiter::Verdict::Defer);
    }

    // FrameExitCondition::fromExitFields maps each exitType to the right Kind/value.
    void frameExitConditionMapping() {
        using K = FrameExitCondition::Kind;
        auto c = FrameExitCondition::fromExitFields(true, "pressure_over", 2.0, 0, 6, 0);
        QCOMPARE(int(c.kind), int(K::PressureOver)); QCOMPARE(c.value, 2.0);
        c = FrameExitCondition::fromExitFields(true, "pressure_under", 0, 1.5, 6, 0);
        QCOMPARE(int(c.kind), int(K::PressureUnder)); QCOMPARE(c.value, 1.5);
        c = FrameExitCondition::fromExitFields(true, "flow_over", 0, 0, 2.5, 0);
        QCOMPARE(int(c.kind), int(K::FlowOver)); QCOMPARE(c.value, 2.5);
        c = FrameExitCondition::fromExitFields(true, "flow_under", 0, 0, 0, 1.0);
        QCOMPARE(int(c.kind), int(K::FlowUnder)); QCOMPARE(c.value, 1.0);

        // exitIf false → no firmware exit regardless of fields.
        c = FrameExitCondition::fromExitFields(false, "pressure_over", 2.0, 0, 0, 0);
        QCOMPARE(int(c.kind), int(K::None));
        QVERIFY(!c.isActionable());

        // "weight" is app-side → None, no warning expected.
        c = FrameExitCondition::fromExitFields(true, "weight", 0, 0, 0, 0);
        QCOMPARE(int(c.kind), int(K::None));

        // Unrecognized exitType with exitIf set → None + a warning (guard disabled).
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("unrecognized exitType"));
        c = FrameExitCondition::fromExitFields(true, "bogus", 1, 1, 1, 1);
        QCOMPARE(int(c.kind), int(K::None));
    }

    // ==========================================
    // Untared cup detection
    // ==========================================

    void untaredCupDetectedEarly() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        TareWait::armExtractionUntared(wp, 55.0, m_fakeClock);  // grace spent, no zero ever arrived

        QSignalSpy cupSpy(&wp, &WeightProcessor::untaredCupDetected);

        // Past the grace, weight > 50 g is judged and warned about. The popup is
        // debounced against a single corrupt packet: UNTARED_CUP_CONFIRM_SAMPLES
        // (2, event-based — see weightprocessor.cpp) consecutive readings.
        for (int i = 0; i < 2; i++) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 55"));
            wp.processWeight(55.0);
            m_fakeClock += 100;
        }

        QVERIFY(cupSpy.count() >= 1);
    }

    void untaredCupNotDetectedLate() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        TareWait::armExtractionUntared(wp, 55.0, m_fakeClock);  // grace spent, no zero ever arrived

        QSignalSpy cupSpy(&wp, &WeightProcessor::untaredCupDetected);

        // Advance past 3s detection window. Arming with the cup's own reading matters
        // here: a test that spent the grace on nothing would pass on the grace rather
        // than on the window it is named for.
        m_fakeClock += 3500;

        wp.processWeight(55.0);
        m_fakeClock += 200;

        QCOMPARE(cupSpy.count(), 0);
    }

    void untaredCupNotDetectedBelowThreshold() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        TareWait::armExtractionUntared(wp, 49.0, m_fakeClock);  // below threshold throughout, grace spent

        QSignalSpy cupSpy(&wp, &WeightProcessor::untaredCupDetected);

        wp.processWeight(49.0);  // Below 50g threshold
        m_fakeClock += 200;

        QCOMPARE(cupSpy.count(), 0);
    }

    // ==========================================
    // Spike rejection (issue #610)
    // ==========================================

    void singleSpikeRejectedByRateFilter() {
        // Reproduces issue #610: Felicita sends 1649g instead of ~10g
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 42.0, 0);
        TareWait::armExtraction(wp, m_fakeClock);
        wp.setCurrentFrame(2);

        QSignalSpy stopSpy(&wp, &WeightProcessor::stopNow);

        m_fakeClock += 5500;

        // Build normal flow at ~2 g/s up to ~10g
        feedRising(wp, 0.0, 2.0, 8);  // 0→3.2g over 1.6s

        // Inject a single corrupt reading (1649g) — should be rejected
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Spike rejected.*1649"));
        wp.processWeight(1649.0);
        m_fakeClock += 200;

        // Continue normal flow — SAW should NOT have triggered
        feedRising(wp, 4.0, 2.0, 5);

        QCOMPARE(stopSpy.count(), 0);
    }

    void consecutiveRejectionsAutoReset() {
        // After 3 consecutive rejections, the filter accepts the new baseline.
        // This handles legitimate shifts during extraction (e.g. scale reconnect).
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy flowSpy(&wp, &WeightProcessor::flowRatesReady);
        wp.startExtraction();  // Spike filter only active during extraction

        // Establish baseline at ~10g
        feedRising(wp, 8.0, 2.0, 5);
        QVERIFY(flowSpy.count() >= 4);

        // Inject 3 readings at 500g — first 2 rejected, 3rd accepted as new baseline
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Spike rejected.*500"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Spike rejected.*500"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Spike filter reset"));
        qsizetype countBefore = flowSpy.count();
        wp.processWeight(500.0); m_fakeClock += 200;
        wp.processWeight(500.0); m_fakeClock += 200;
        wp.processWeight(500.0); m_fakeClock += 200;

        // 3rd reading should have been accepted — flowRatesReady emitted
        QCOMPARE(flowSpy.count(), countBefore + 1);
    }

    void spikeDoesNotCorruptFlowRate() {
        // A rejected spike should not affect LSLR flow computation
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy flowSpy(&wp, &WeightProcessor::flowRatesReady);
        wp.startExtraction();  // Spike filter only active during extraction

        // Build stable 2 g/s flow
        feedRising(wp, 0.0, 2.0, 10);

        qsizetype countBefore = flowSpy.count();
        double flowBefore = flowSpy.last().at(2).toDouble();  // flowRateShort

        // Inject spike — rejected, no new signal emitted
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Spike rejected"));
        wp.processWeight(500.0);
        m_fakeClock += 200;

        QCOMPARE(flowSpy.count(), countBefore);  // No signal from rejected sample

        // Next normal sample should still show ~2 g/s flow
        wp.processWeight(4.2);
        m_fakeClock += 200;

        double flowAfter = flowSpy.last().at(2).toDouble();
        QVERIFY2(qAbs(flowAfter - flowBefore) < 1.5,
                 qPrintable(QString("Flow should be stable after spike rejection: before=%1 after=%2")
                            .arg(flowBefore).arg(flowAfter)));
    }

    void tareStepIsNotASpike() {
        // The drop from a loaded portafilter to zero is the app's own tare, not BLE
        // corruption. startExtraction() clears the baseline, but the tare is async at
        // the scale: pre-tare samples keep arriving and one of them re-establishes the
        // baseline, so the step to zero used to read as a >100 g spike. Observed every
        // shot on-device: "Spike rejected: weight= 0 last= 364.6", twice.
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy flowSpy(&wp, &WeightProcessor::flowRatesReady);

        wp.startExtraction();  // Preheat begins; tare command sent by the caller

        // Scale is still reporting the loaded portafilter — these land AFTER
        // startExtraction() cleared the baseline, which is what made the old filter
        // latch onto a pre-tare value.
        wp.processWeight(364.6); m_fakeClock += 200;
        wp.processWeight(364.6); m_fakeClock += 200;
        const qsizetype countBeforeTare = flowSpy.count();

        // First near-zero sample is HELD, not believed — one packet is not proof.
        wp.processWeight(0.0); m_fakeClock += 200;
        QCOMPARE(flowSpy.count(), countBeforeTare);

        // Second confirms it. No warning may be emitted — init() calls
        // QTest::failOnWarning(), so absence is enforced, not assumed.
        wp.processWeight(0.0); m_fakeClock += 200;
        QCOMPARE(flowSpy.count(), countBeforeTare + 1);  // accepted, not rejected
    }

    void tareLandingAfterFlowStartIsNotASpike() {
        // The Sep 1 2026 field log, exactly: a 141.1 g cup left by the previous hot-water
        // pour, the app's tare in flight, and the DE1 starting flow 47 ms before the
        // zeroed sample arrived. Ending the tare wait AT flow start closed the spike
        // filter's tare exemption just before the step it exists for, so the app's own
        // zero was rejected three times and then escape-hatched, and the same stale
        // samples were reported as a cup the user had forgotten to tare.
        //
        // Nothing here may warn — init() calls QTest::failOnWarning(), so the three
        // WARN families that log carried are asserted absent, not assumed.
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        QSignalSpy landed(&wp, &WeightProcessor::tareLanded);

        wp.startExtraction();
        wp.processWeight(141.1);  m_fakeClock += 100;  // cup from the previous pour
        wp.markExtractionStart();                      // flow starts, tare still travelling
        wp.setTareComplete(true);

        wp.processWeight(141.1);  m_fakeClock += 100;  // still the old zero
        wp.processWeight(0.0);    m_fakeClock += 100;  // the real zero: held, not rejected
        QCOMPARE(landed.count(), 0);                   // one packet is not proof

        wp.processWeight(0.0);
        QCOMPARE(landed.count(), 1);  // confirmed — consumers can re-anchor on it
    }

    void perFrameExitWaitsForTheTareThenReturns() {
        // The wait the fix holds open is BOUNDED, and this is why it has to be: a cup
        // that never reads near zero would otherwise keep the per-frame weight exit
        // switched off for the whole shot.
        WeightProcessor wp;
        installFakeClock(wp);
        QVector<double> frameExits = {30.0};
        configureEspresso(wp, 0, 0, frameExits);  // no SAW target
        wp.startExtraction();
        wp.markExtractionStart();  // flow starts with the tare still in flight
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);

        QSignalSpy skipSpy(&wp, &WeightProcessor::skipFrame);

        // A pre-tare reading clears any plausible exitWeight, so acting on one would
        // skip a frame on a number we have already decided not to trust.
        TareWait::burnGrace(wp, 40.0, m_fakeClock);  // every granted arrival, none of them judged
        QCOMPARE(skipSpy.count(), 0);

        // The next arrival ends the wait, and is judged by the exit it just unblocked.
        wp.processWeight(40.0);
        QCOMPARE(skipSpy.count(), 1);
    }

    void tareLandsWithoutABigStep() {
        // The common case, and the one the >100 g branch never sees: the scale was
        // already at (or returned to) about zero, so the tare arrives as an ordinary
        // sample. The exemption must still be consumed — otherwise it stays armed all
        // preheat, keeping the per-frame weight exit gated off and handing a free pass
        // to a genuine large drop later.
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy flowSpy(&wp, &WeightProcessor::flowRatesReady);

        wp.startExtraction();
        // Realistic post-tare residual, not exactly 0 — the threshold is a band, not
        // an equality. Two of them, because one is never enough.
        wp.processWeight(0.4);  m_fakeClock += 200;
        wp.processWeight(0.3);  m_fakeClock += 200;
        // Climb in sub-100 g steps so nothing here is itself a spike.
        wp.processWeight(60.0);  m_fakeClock += 200;
        wp.processWeight(120.0); m_fakeClock += 200;
        wp.processWeight(150.0); m_fakeClock += 200;
        const qsizetype countBefore = flowSpy.count();

        // Exemption already spent by the 0.4 g sample, so this is a spike.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Spike rejected: weight=0\\.00 "));
        wp.processWeight(0.0); m_fakeClock += 200;

        QCOMPARE(flowSpy.count(), countBefore);
    }

    void tareExemptionIsDirectional() {
        // The exemption is for a step DOWN to near zero, nothing else. #610 corruption
        // travels upward (1649 g instead of 10 g), so an upward spike must still be
        // rejected even while the tare is outstanding — otherwise the hold-off would be
        // a window with no filter at all.
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy flowSpy(&wp, &WeightProcessor::flowRatesReady);

        wp.startExtraction();
        wp.processWeight(364.6); m_fakeClock += 200;  // pre-tare baseline, tare still pending
        const qsizetype countBefore = flowSpy.count();

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Spike rejected.*1649"));
        wp.processWeight(1649.0); m_fakeClock += 200;

        QCOMPARE(flowSpy.count(), countBefore);  // rejected despite awaiting the tare

        // ...and DOWN is not enough either — the destination has to be near zero.
        // A large downward step that lands somewhere else is a cup lift or garbage,
        // not the tare we are waiting for.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Spike rejected: weight=250\\.00 "));
        wp.processWeight(250.0); m_fakeClock += 200;
        QCOMPARE(flowSpy.count(), countBefore);
    }

    void tareExemptionIsConsumedOnce() {
        // Once the tare has landed the exemption is spent: a second large drop to zero
        // is a cup lift or a corrupt packet, not a tare, and must be filtered.
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy flowSpy(&wp, &WeightProcessor::flowRatesReady);

        wp.startExtraction();
        wp.processWeight(364.6); m_fakeClock += 200;
        wp.processWeight(0.0);   m_fakeClock += 200;  // held
        wp.processWeight(0.0);   m_fakeClock += 200;  // confirmed — exemption consumed

        // Climb to a large-drink weight in steps under the 100 g spike threshold —
        // a single jump to 150 g would itself be rejected as a spike, which is the
        // filter working, not the thing under test.
        wp.processWeight(60.0);  m_fakeClock += 200;
        wp.processWeight(120.0); m_fakeClock += 200;
        wp.processWeight(150.0); m_fakeClock += 200;
        const qsizetype countBefore = flowSpy.count();

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Spike rejected: weight=0\\.00 "));
        wp.processWeight(0.0); m_fakeClock += 200;

        QCOMPARE(flowSpy.count(), countBefore);  // rejected — no second free pass
    }

    void flowStartConsumesTareExemption() {
        // Backstop for a scale that never reads near zero (an untared cup left on the
        // platter): the tare wait outlives flow start by a bounded run of arrivals, and
        // once THAT is spent there is no tare still to come, so a later drop to zero
        // must be filtered rather than mistaken for one.
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy flowSpy(&wp, &WeightProcessor::flowRatesReady);

        wp.startExtraction();
        wp.processWeight(150.0); m_fakeClock += 200;  // never passes through zero
        wp.markExtractionStart();                     // flow begins; grace starts here
        feedRising(wp, 150.0, 2.0, TareWait::kGraceArrivals);   // every granted arrival, no zero
        const qsizetype countBefore = flowSpy.count();

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Spike rejected: weight=0\\.00 "));
        wp.processWeight(0.0); m_fakeClock += 200;

        QCOMPARE(flowSpy.count(), countBefore);
    }

    void preTareWeightDoesNotTriggerFrameExit() {
        // The per-frame weight exit compares an ABSOLUTE weight to a threshold, and a
        // pre-tare reading (loaded portafilter, ~364 g) clears any plausible exitWeight
        // outright. Acting on it would skip a frame on a number the app has already
        // decided not to trust — the zero point is mid-move. Unlike the SAW stop, this
        // branch has no extractionElapsed guard, so it carries its own !m_awaitingTare.
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy skipSpy(&wp, &WeightProcessor::skipFrame);

        QVector<double> weights = {20.0, 0.0};  // frame 0 exits at 20 g
        QVector<FrameExitCondition> conds = {{}, {}};
        configureEspresso(wp, 0, 0, weights, conds);
        wp.startExtraction();
        // Mirrors production ordering: setTareComplete(true) rides in the SAME queued
        // invocation as startExtraction() (see the comment at main.cpp's
        // espressoCycleStarted handler), so it is already true while the scale is still
        // reporting pre-tare weights. That is exactly why this window needs its own
        // gate — the m_tareComplete check at the top of processWeight() does not cover it.
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);

        // Scale still reporting the loaded portafilter — way past the 20 g exit.
        wp.processWeight(364.6); m_fakeClock += 200;
        wp.processWeight(364.6); m_fakeClock += 200;
        QCOMPARE(skipSpy.count(), 0);  // must NOT skip on an untared weight

        // Tare lands (confirmed), then a genuine 25 g crosses the exit for real.
        wp.processWeight(0.0);  m_fakeClock += 200;
        wp.processWeight(0.0);  m_fakeClock += 200;
        QCOMPARE(skipSpy.count(), 0);
        wp.processWeight(25.0); m_fakeClock += 200;
        QCOMPARE(skipSpy.count(), 1);  // ...and the gate does not block a real exit
    }

    void singleSpuriousZeroDoesNotOpenFrameExit() {
        // Regression for the hole the confirmation requirement closes. Zero is a
        // demonstrated spurious reading on this hardware. Believing one would consume
        // the exemption, leaving the REAL pre-tare weights to be rejected three times
        // and then escape-hatched — at which point the per-frame exit is open and a
        // 364 g portafilter reading clears any plausible exit weight, firing exactly
        // the spurious skipFrame the gate exists to prevent.
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy skipSpy(&wp, &WeightProcessor::skipFrame);

        QVector<double> weights = {20.0, 0.0};  // frame 0 exits at 20 g
        QVector<FrameExitCondition> conds = {{}, {}};
        configureEspresso(wp, 0, 0, weights, conds);
        wp.startExtraction();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);

        wp.processWeight(364.6); m_fakeClock += 200;  // real pre-tare baseline
        wp.processWeight(0.0);   m_fakeClock += 200;  // ONE spurious zero — held
        wp.processWeight(364.6); m_fakeClock += 200;  // real weight resumes
        wp.processWeight(364.6); m_fakeClock += 200;

        QCOMPARE(skipSpy.count(), 0);  // gate must still be shut

        // The real tare, confirmed, then a genuine crossing.
        wp.processWeight(0.0);  m_fakeClock += 200;
        wp.processWeight(0.0);  m_fakeClock += 200;
        wp.processWeight(25.0); m_fakeClock += 200;
        QCOMPARE(skipSpy.count(), 1);
    }

    void retareStepIsNotASpike() {
        // resetForRetare() moves the zero point mid-preheat for exactly the same
        // reason, so it re-arms the same one-shot exemption.
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy flowSpy(&wp, &WeightProcessor::flowRatesReady);

        wp.startExtraction();
        wp.processWeight(0.0);   m_fakeClock += 200;  // first tare landed
        feedRising(wp, 0.0, 2.0, 3);

        QTest::ignoreMessage(QtDebugMsg, QRegularExpression("Reset for auto-retare"));
        wp.resetForRetare();

        wp.processWeight(364.6); m_fakeClock += 200;  // scale still pre-retare
        const qsizetype countBefore = flowSpy.count();
        wp.processWeight(0.0);   m_fakeClock += 200;  // held
        wp.processWeight(0.0);   m_fakeClock += 200;  // retare confirmed

        QCOMPARE(flowSpy.count(), countBefore + 1);
    }

    void spikeBypassedWhenInactive() {
        // Outside extraction, 100g+ jumps should NOT be rejected — they're
        // legitimate events (cup placement/removal, tare drift).
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy flowSpy(&wp, &WeightProcessor::flowRatesReady);
        // Do NOT call startExtraction() — stay inactive

        // Feed a baseline
        wp.processWeight(0.0); m_fakeClock += 200;
        QCOMPARE(flowSpy.count(), 1);

        // 200g jump — would be rejected during extraction, but passes when inactive
        wp.processWeight(200.0); m_fakeClock += 200;
        QCOMPARE(flowSpy.count(), 2);  // Signal emitted, not rejected

        // Jump back to 0 — also passes
        wp.processWeight(0.0); m_fakeClock += 200;
        QCOMPARE(flowSpy.count(), 3);
    }

    // ==========================================
    // State guards
    // ==========================================

    void processWeightBeforeStartNoCrash() {
        WeightProcessor wp;
        installFakeClock(wp);
        // Should not crash when called before startExtraction
        wp.processWeight(10.0);
        m_fakeClock += 100;
        wp.processWeight(20.0);
        // If we get here, no crash
    }

    void processWeightAfterStopNoSignals() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        TareWait::armExtraction(wp, m_fakeClock);
        wp.setCurrentFrame(0);

        // Stop extraction
        wp.stopExtraction();

        QSignalSpy stopSpy(&wp, &WeightProcessor::stopNow);
        QSignalSpy skipSpy(&wp, &WeightProcessor::skipFrame);

        // Advance past 5s and feed weight above target
        m_fakeClock += 5500;
        feedRising(wp, 30.0, 2.0, 10);

        QCOMPARE(stopSpy.count(), 0);  // No SAW after stop
        QCOMPARE(skipSpy.count(), 0);  // No frame skip after stop
    }

    void configureWithEmptyFrameExits() {
        WeightProcessor wp;
        installFakeClock(wp);
        QVector<double> empty;
        wp.configure(36.0, 0, empty, {}, empty, empty, false);
        TareWait::armExtraction(wp, m_fakeClock);
        wp.setCurrentFrame(0);

        // Process weight with frame index beyond empty vector — should not crash
        wp.processWeight(10.0);
        m_fakeClock += 200;
    }

    // ==========================================
    // SAW fires only once
    // ==========================================

    void sawFiresOnlyOnce() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        TareWait::armExtraction(wp, m_fakeClock);
        wp.setCurrentFrame(0);

        QSignalSpy stopSpy(&wp, &WeightProcessor::stopNow);

        // Advance past 5s
        m_fakeClock += 5500;

        // Feed weight above target with valid flow for a long time
        feedRising(wp, 30.0, 2.0, 30);

        // SAW should fire exactly once (m_stopTriggered guard)
        QVERIFY(stopSpy.count() >= 1);
        qsizetype firstCount = stopSpy.count();

        // Continue feeding — count should not increase
        feedRising(wp, 50.0, 2.0, 10);
        QCOMPARE(stopSpy.count(), firstCount);
    }

    // ==========================================
    // SAW blocked during preinfusion
    // ==========================================

    void sawBlockedDuringPreinfusion() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 2);  // 2 preinfuse frames
        TareWait::armExtraction(wp, m_fakeClock);
        wp.setCurrentFrame(1);  // Still in preinfusion (1 < 2)

        QSignalSpy stopSpy(&wp, &WeightProcessor::stopNow);

        m_fakeClock += 5500;
        feedRising(wp, 30.0, 2.0, 15);

        QCOMPARE(stopSpy.count(), 0);  // Blocked during preinfusion
    }

    // ==========================================
    // flowRatesReady always emits (even when not extracting)
    // ==========================================

    void flowRatesReadyAlwaysEmits() {
        WeightProcessor wp;
        installFakeClock(wp);
        // No startExtraction — just feed raw weights
        QSignalSpy flowSpy(&wp, &WeightProcessor::flowRatesReady);

        feedConstant(wp, 5.0, 6);

        QVERIFY2(flowSpy.count() >= 5,
                 qPrintable(QString("flowRatesReady should emit for each processWeight, got %1")
                            .arg(flowSpy.count())));
    }

    // ==========================================
    // SAW requires tare complete
    // ==========================================

    void sawBlockedWithoutTare() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        wp.startExtraction();
        wp.markExtractionStart();
        // NOTE: NOT calling setTareComplete(true)
        wp.setCurrentFrame(0);

        QSignalSpy stopSpy(&wp, &WeightProcessor::stopNow);

        m_fakeClock += 5500;
        feedRising(wp, 30.0, 2.0, 15);

        QCOMPARE(stopSpy.count(), 0);  // Blocked without tare
    }

    // ==========================================
    // Scale-feed-liveness gate (BLE connection-priority backstop,
    // #1093/#1176): extraction + pre-shot preheat window + idle guard.
    // kScaleStaleMs = 2000 (private); tests use 3000 (> stale) / 400 (< stale).
    // ==========================================

    void stallDuringPreheatTriggers() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy stallSpy(&wp, &WeightProcessor::scaleFeedStalled);

        // Pre-shot warm-up: cycle active (preheat) + tare complete + the scale
        // was streaming (one processed sample establishes m_lastWallClockMs).
        wp.setShotCycleActive(true);
        wp.setTareComplete(true);
        wp.processWeight(0.0);

        // Feed dies; the DE1 shot-sample cadence keeps ticking during preheat.
        m_fakeClock += 3000;  // > kScaleStaleMs
        // The detector emits an intentional diagnostic qWarning (D6); expect
        // it so Autotest does not flag the (passing) test as "with warnings",
        // and so the test also asserts the log line fired.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Scale feed stalled")));
        wp.setCurrentFrame(0);

        QCOMPARE(stallSpy.count(), 1);  // caught DURING preheat, before the pour
    }

    void quietIdleScaleNoCycleDoesNotTrigger() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy stallSpy(&wp, &WeightProcessor::scaleFeedStalled);

        // Connected scale, quiet, but NO espresso cycle in progress: a
        // legitimately idle scale must never be treated as a fault.
        wp.processWeight(0.0);
        m_fakeClock += 30000;  // very stale, but gate is closed
        wp.setCurrentFrame(0);

        QCOMPARE(stallSpy.count(), 0);
    }

    // Regression net for the in-shot extraction path. The gate is
    // `(m_active || m_preheatActive) && m_tareComplete`; this pins the
    // `m_active` branch so the live-pour backstop can't silently regress
    // while the preheat test stays green.
    void stallDuringActiveExtractionTriggers() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy stallSpy(&wp, &WeightProcessor::scaleFeedStalled);

        wp.startExtraction();        // m_active = true
        wp.setTareComplete(true);
        wp.processWeight(0.0);       // establishes m_lastWallClockMs

        m_fakeClock += 3000;         // > kScaleStaleMs, feed dead
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Scale feed stalled")));
        wp.setCurrentFrame(0);       // DE1 cadence keeps ticking

        QCOMPARE(stallSpy.count(), 1);
        // The gap carried to observe mode must be the real silent duration
        // (now − last good sample), not anchored to "now". 3000 ms advanced.
        QCOMPARE(stallSpy.first().at(0).toLongLong(), qint64(3000));
    }

    // Safety invariant: preheat active but tare NOT complete must NOT fire —
    // the espresso cycle enters EspressoPreheating before the cup is placed /
    // tared, exactly the window a scale legitimately isn't streaming.
    void preheatWithoutTareCompleteDoesNotTrigger() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy stallSpy(&wp, &WeightProcessor::scaleFeedStalled);

        wp.setShotCycleActive(true); // preheat, but NO setTareComplete(true)
        wp.processWeight(0.0);
        m_fakeClock += 3000;
        wp.setCurrentFrame(0);

        QCOMPARE(stallSpy.count(), 0);  // tare gate keeps shotContext closed
    }

    // --- scaleFeedResumed recovery edge (observe-mode change) ---

    // After a stall, the first genuine sample emits scaleFeedResumed exactly
    // once, carrying the silent gap; a second sample does NOT re-emit.
    void feedResumeEmitsOnceWithGapAfterStall() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy stallSpy(&wp, &WeightProcessor::scaleFeedStalled);
        QSignalSpy resumeSpy(&wp, &WeightProcessor::scaleFeedResumed);

        wp.startExtraction();
        wp.setTareComplete(true);
        wp.processWeight(0.0);       // last good sample @ start clock

        m_fakeClock += 3000;         // > kScaleStaleMs of silence
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Scale feed stalled")));
        wp.setCurrentFrame(0);       // DE1 tick detects the stall
        QCOMPARE(stallSpy.count(), 1);
        QCOMPARE(resumeSpy.count(), 0);  // not recovered yet

        wp.processWeight(0.5);       // genuine sample → recovery edge
        QCOMPARE(resumeSpy.count(), 1);
        QCOMPARE(resumeSpy.first().at(0).toLongLong(), qint64(3000));

        m_fakeClock += 250;          // non-batched gap (avoids the unrelated
                                     // de-jitter "batched before calibration"
                                     // diagnostic — orthogonal to recovery)
        wp.processWeight(0.6);       // edge already consumed → no re-emit
        QCOMPARE(resumeSpy.count(), 1);
    }

    // No stall ⇒ no resume signal (it is strictly a 1→0 edge).
    void feedResumeNotEmittedWithoutPriorStall() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy resumeSpy(&wp, &WeightProcessor::scaleFeedResumed);

        wp.startExtraction();
        wp.setTareComplete(true);
        feedRising(wp, 0.0, 2.0, 10);   // healthy continuous feed

        QCOMPARE(resumeSpy.count(), 0);
    }

    // Recovery is observation-only: a stall→resume cycle must not spuriously
    // drive SAW (stopNow) or frame-exit (skipFrame).
    void resumeDoesNotAlterSawOrFrameExit() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        QSignalSpy stopSpy(&wp, &WeightProcessor::stopNow);
        QSignalSpy skipSpy(&wp, &WeightProcessor::skipFrame);

        wp.startExtraction();
        wp.setTareComplete(true);
        wp.processWeight(1.0);
        m_fakeClock += 3000;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Scale feed stalled")));
        wp.setCurrentFrame(0);
        wp.processWeight(1.2);          // recovery edge

        QCOMPARE(stopSpy.count(), 0);   // far below 36 g target — no SAW
        QCOMPARE(skipSpy.count(), 0);   // no frame-exit weights configured
    }

    // --- suspected → confirmed stall (epoch-scope-and-stall-confirm) ---

    // Suspected fires at kScaleStaleMs; confirmed only after
    // kScaleStallConfirmMs of CONTINUED silence; each fires exactly once.
    void suspectedThenConfirmedOnSustainedStall() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy stall(&wp, &WeightProcessor::scaleFeedStalled);
        QSignalSpy confirm(&wp, &WeightProcessor::scaleFeedStallConfirmed);

        wp.startExtraction();
        wp.setTareComplete(true);
        wp.processWeight(0.0);                 // last good sample @ T0

        m_fakeClock += 2500;                   // > kScaleStaleMs, < confirm
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Scale feed stalled")));
        wp.setCurrentFrame(0);                 // SUSPECTED
        QCOMPARE(stall.count(), 1);
        QCOMPARE(confirm.count(), 0);          // not confirmed yet

        m_fakeClock += 4000;                   // total 6500 > kScaleStallConfirmMs
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("CONFIRMED")));
        wp.setCurrentFrame(0);                 // CONFIRMED
        QCOMPARE(confirm.count(), 1);
        QCOMPARE(confirm.first().at(0).toLongLong(), qint64(6500));

        wp.setCurrentFrame(0);                 // still stalled → no re-emit
        QCOMPARE(stall.count(), 1);
        QCOMPARE(confirm.count(), 1);
    }

    // A stall that self-recovers before the confirm threshold NEVER confirms
    // (this is the false-positive shape enforce must not latch on); a later
    // independent stall re-arms suspected→confirmed cleanly.
    void recoveryBeforeConfirmCancelsThenReArms() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy confirm(&wp, &WeightProcessor::scaleFeedStallConfirmed);
        QSignalSpy resume(&wp, &WeightProcessor::scaleFeedResumed);

        wp.startExtraction();
        wp.setTareComplete(true);
        wp.processWeight(0.0);

        m_fakeClock += 2500;                   // suspected
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Scale feed stalled")));
        wp.setCurrentFrame(0);
        QCOMPARE(confirm.count(), 0);

        m_fakeClock += 1000;                   // total 3500, still < confirm
        wp.processWeight(0.5);                 // genuine sample → recovery
        QCOMPARE(resume.count(), 1);
        QCOMPARE(confirm.count(), 0);          // cancelled — never confirmed

        // Independent later stall must re-arm and be able to confirm.
        m_fakeClock += 2500;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Scale feed stalled")));
        wp.setCurrentFrame(0);                 // re-SUSPECTED (fresh episode)
        QCOMPARE(confirm.count(), 0);
        m_fakeClock += 6000;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("CONFIRMED")));
        wp.setCurrentFrame(0);                 // CONFIRMED on the new episode
        QCOMPARE(confirm.count(), 1);
    }

    // Confirmation is pure observation — a full suspected→confirmed cycle
    // must not drive SAW (stopNow) or frame-exit (skipFrame).
    void confirmDoesNotAlterSawOrFrameExit() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        QSignalSpy stopSpy(&wp, &WeightProcessor::stopNow);
        QSignalSpy skipSpy(&wp, &WeightProcessor::skipFrame);

        wp.startExtraction();
        wp.setTareComplete(true);
        wp.processWeight(1.0);
        m_fakeClock += 2500;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Scale feed stalled")));
        wp.setCurrentFrame(0);                 // suspected
        m_fakeClock += 5000;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("CONFIRMED")));
        wp.setCurrentFrame(0);                 // confirmed

        QCOMPARE(stopSpy.count(), 0);
        QCOMPARE(skipSpy.count(), 0);
    }

    // Regression for the PR #1220 review bug: a rejected SPIKE packet during
    // a stall advances m_lastWallClockMs. If CONFIRM measured the gap from
    // m_lastWallClockMs it would be reset to ~0 by the spike and a genuinely
    // dead feed that emits periodic garbage (#1176/#610 overlap) would never
    // confirm → never back off. CONFIRM must measure from the frozen
    // m_feedStallStartMs and therefore stay spike-immune.
    void confirmSurvivesSpikeRejectionDuringStall() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy confirm(&wp, &WeightProcessor::scaleFeedStallConfirmed);

        wp.startExtraction();
        wp.setTareComplete(true);
        wp.processWeight(1.0);                 // T0, last good sample

        m_fakeClock += 2500;                   // > kScaleStaleMs
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Scale feed stalled")));
        wp.setCurrentFrame(0);                 // SUSPECTED; m_feedStallStartMs := T0
        QCOMPARE(confirm.count(), 0);

        m_fakeClock += 3000;                   // T0+5500
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Spike rejected")));
        wp.processWeight(200.0);               // |200-1|>100 → rejected; advances
                                               // m_lastWallClockMs to T0+5500,
                                               // NOT a genuine sample (no recovery)
        QCOMPARE(confirm.count(), 0);

        m_fakeClock += 1000;                   // T0+6500: 6500 from frozen start
                                               // ≥ 6000, but only 1000 from the
                                               // spike-advanced m_lastWallClockMs
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("CONFIRMED")));
        wp.setCurrentFrame(0);                 // must STILL confirm (spike-immune)
        QCOMPARE(confirm.count(), 1);
        QCOMPARE(confirm.first().at(0).toLongLong(), qint64(6500));
    }

    // Confirmation must work in the pre-shot preheat context too (the spec's
    // confirmed-stall scenario says "extraction/preheat"; only the suspected
    // arm had preheat coverage before).
    void confirmWorksInPreheatContext() {
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy confirm(&wp, &WeightProcessor::scaleFeedStallConfirmed);

        wp.setShotCycleActive(true);           // EspressoPreheating (not m_active)
        wp.setTareComplete(true);
        wp.processWeight(0.0);

        m_fakeClock += 2500;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Scale feed stalled")));
        wp.setCurrentFrame(0);                 // SUSPECTED in preheat
        QCOMPARE(confirm.count(), 0);

        m_fakeClock += 4000;                   // total 6500 ≥ kScaleStallConfirmMs
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("CONFIRMED")));
        wp.setCurrentFrame(0);
        QCOMPARE(confirm.count(), 1);
    }
};

QTEST_GUILESS_MAIN(tst_WeightProcessor)
#include "tst_weightprocessor.moc"
