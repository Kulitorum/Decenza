#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>

#include "machine/weightprocessor.h"

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
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
    }

private slots:

    void init() { QTest::failOnWarning();
        m_fakeClock = 1000000;  // Reset for each test
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
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
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
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
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
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
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
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
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
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
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
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);

        QSignalSpy skipSpy(&wp, &WeightProcessor::skipFrame);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 99"));
        wp.processWeight(99.0);
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
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);

        QSignalSpy cupSpy(&wp, &WeightProcessor::untaredCupDetected);

        // Weight > 50g immediately — triggers sanity check warning
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 55"));
        wp.processWeight(55.0);
        m_fakeClock += 200;

        QVERIFY(cupSpy.count() >= 1);
    }

    void untaredCupNotDetectedLate() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);

        QSignalSpy cupSpy(&wp, &WeightProcessor::untaredCupDetected);

        // Advance past 3s detection window
        m_fakeClock += 3500;

        wp.processWeight(55.0);
        m_fakeClock += 200;

        QCOMPARE(cupSpy.count(), 0);
    }

    void untaredCupNotDetectedBelowThreshold() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);

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
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
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
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
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
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
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
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
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
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
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
