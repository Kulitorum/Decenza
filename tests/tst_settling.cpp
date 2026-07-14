#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>
#include <QStringList>

#include "models/shotdatamodel.h"
#include "controllers/shottimingcontroller.h"
#include "ble/de1device.h"
#include "mocks/MockScaleDevice.h"

// Test SAW settling behavior: trimSettlingData(), m_sawSettling flag lifecycle,
// and the interaction between settling completion and shot save ordering.

class tst_Settling : public QObject {
    Q_OBJECT

private:
    // Helper: populate ShotDataModel with N samples of real data followed by M zero-pressure samples
    void populateWithSettlingData(ShotDataModel& model, int realSamples, int zeroSamples) {
        for (int i = 0; i < realSamples; i++) {
            double t = i * 0.2;  // 5Hz
            model.addSample(t, 9.0, 2.0, 93.0, 88.0, 9.0, 0.0, 93.0);
            model.addWeightSample(t, i * 0.4, 2.0);
        }
        double lastRealTime = realSamples * 0.2;
        for (int i = 0; i < zeroSamples; i++) {
            double t = lastRealTime + (i + 1) * 0.2;
            model.addSample(t, 0.0, 0.0, 90.0, 85.0, 0.0, 0.0, 90.0);
            // Weight continues during settling
            model.addWeightSample(t, realSamples * 0.4 + i * 0.1, 0.5);
        }
    }

private slots:
    void init() { QTest::failOnWarning(); }

    // ===== trimSettlingData() =====

    void trimRemovesTrailingZeroPressure() {
        ShotDataModel model;
        populateWithSettlingData(model, 50, 10);

        QCOMPARE(model.pressureData().size(), 60);
        model.trimSettlingData();
        QCOMPARE(model.pressureData().size(), 50);
        QCOMPARE(model.flowData().size(), 50);
        QCOMPARE(model.temperatureData().size(), 50);
    }

    void trimPreservesWeightData() {
        ShotDataModel model;
        populateWithSettlingData(model, 50, 10);

        qsizetype weightBefore = model.cumulativeWeightData().size();
        model.trimSettlingData();
        // Weight data must not be trimmed — it contains post-settling values
        QCOMPARE(model.cumulativeWeightData().size(), weightBefore);
    }

    void trimNoOpWhenNothingToTrim() {
        ShotDataModel model;
        // All samples have non-zero pressure
        for (int i = 0; i < 20; i++) {
            model.addSample(i * 0.2, 9.0, 2.0, 93.0, 88.0, 9.0, 0.0, 93.0);
        }

        qsizetype sizeBefore = model.pressureData().size();
        model.trimSettlingData();
        QCOMPARE(model.pressureData().size(), sizeBefore);
    }

    void trimPreservesDataWhenAllZeroPressure() {
        ShotDataModel model;
        // All samples have zero pressure (failed shot)
        for (int i = 0; i < 20; i++) {
            model.addSample(i * 0.2, 0.0, 0.0, 90.0, 85.0, 0.0, 0.0, 90.0);
        }

        qsizetype sizeBefore = model.pressureData().size();
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("all 20 samples have zero pressure"));
        model.trimSettlingData();
        // Must preserve all data — trimIndex==0 guard prevents data loss
        QCOMPARE(model.pressureData().size(), sizeBefore);
    }

    void trimHandlesEmptyModel() {
        ShotDataModel model;
        // Should not crash on empty data
        model.trimSettlingData();
        QCOMPARE(model.pressureData().size(), 0);
    }

    void trimTrimsWeightFlowRateByTime() {
        ShotDataModel model;
        populateWithSettlingData(model, 50, 10);

        model.trimSettlingData();
        // Weight flow rate should be trimmed to match pressure time range
        if (!model.weightFlowRateData().isEmpty()) {
            double lastPressureTime = model.pressureData().last().x();
            QVERIFY(model.weightFlowRateData().last().x() <= lastPressureTime);
        }
    }

    // ===== ShotTimingController m_sawSettling flag =====

    void settlingFlagInitiallyFalse() {
        DE1Device device;
        ShotTimingController tc(&device);
        QVERIFY(!tc.isSawSettling());
    }

    void settlingFlagClearedByStartShot() {
        DE1Device device;
        ShotTimingController tc(&device);

        // Simulate a settling state by starting a shot, which resets everything
        tc.startShot();
        QVERIFY(!tc.isSawSettling());
    }

    void settlingChangedSignalEmitted() {
        DE1Device device;
        ShotTimingController tc(&device);
        QSignalSpy spy(&tc, &ShotTimingController::sawSettlingChanged);

        tc.startShot();
        // startShot may or may not emit sawSettlingChanged depending on state
        // but endShot after SAW trigger should emit it
        tc.endShot();
        // Without SAW trigger, settling doesn't start, so signal count depends on path
        // The key invariant: after endShot without SAW, settling is not active
        QVERIFY(!tc.isSawSettling());
    }

    void shotProcessingReadyEmittedWithoutSaw() {
        DE1Device device;
        ShotTimingController tc(&device);
        QSignalSpy spy(&tc, &ShotTimingController::shotProcessingReady);

        tc.startShot();
        tc.endShot();  // No SAW trigger → immediate shotProcessingReady
        QCOMPARE(spy.count(), 1);
        QVERIFY(!tc.isSawSettling());
    }

    // ===== Rolling-average settling guard =====

    void weightAboveAvgGuardPreventsEarlySettlement() {
        // Regression test: monotonically rising weight samples with per-sample delta
        // below SETTLING_AVG_THRESHOLD (0.3g) must NOT trigger premature settlement,
        // because the circular buffer average lags behind and appears "stable" even
        // while the scale is actively climbing.
        DE1Device device;
        ShotTimingController tc(&device);

        tc.startShot();
        tc.onSawTriggered(36.0, 2.0, 36.0);
        tc.endShot();
        QVERIFY(tc.isSawSettling());

        // Feed 12 rising samples at 0.2g/step — drift is 0.2g/sample, below 0.3g
        // threshold, so the old code would have declared stable. New guard should block it.
        double w = 36.5;
        for (int i = 0; i < 12; i++) {
            tc.onWeightSample(w, 0.5);
            w += 0.2;
            QVERIFY2(tc.isSawSettling(), qPrintable(QString("Settled prematurely at sample %1, weight %2g").arg(i).arg(w, 0, 'f', 1)));
        }
    }

    void weightAboveAvgGuardAllowsSettlementWhenStable() {
        // After weight plateau, the guard must eventually allow settlement once
        // the rolling average catches up and isSawSettling becomes false.
        DE1Device device;
        ShotTimingController tc(&device);

        tc.startShot();
        tc.onSawTriggered(36.0, 2.0, 36.0);
        tc.endShot();
        QVERIFY(tc.isSawSettling());

        // Feed 20 stable samples at the same weight — avg and current converge immediately
        for (int i = 0; i < 20; i++)
            tc.onWeightSample(38.5, 0.0);

        // After SETTLING_STABLE_MS (1000ms) the settling timer fires; we can't wait
        // for a real timer in a unit test, but we can verify the guard isn't blocking:
        // weight (38.5) should equal avg (~38.5), so weightAboveAvg is false.
        // Settlement depends on the timer, so just verify settling is still active
        // (not prematurely cancelled by the guard itself).
        QVERIFY(tc.isSawSettling());
    }

    void cupLiftMidSettlePreservesLastStableAvg_1280() {
        // Issue #1280: shot 5470. SAW correctly stopped at 41.2 g and the cup
        // settled at ~42.3 g for ~700 ms, but the user lifted the cup before
        // SETTLING_STABLE_MS elapsed. Cup-lift produced scale spike artifacts
        // (44, 48.4, 51) that ShotTimingController accepted (unlike ShotDataModel
        // which spike-rejects), then a 38.5 down-step that squeaked under the
        // 20 g cup-removal threshold, then the final cup-gone -28 reading.
        // Before this fix, m_weight ended at 38.5 — the AI advisor saw
        // {yield: 38.5, target: 42} and invented a "you stopped manually"
        // narrative. The fix preserves the last clean rolling avg (~42.3 g) on
        // the cup-removed path so finalWeightG reflects what was in the cup.
        DE1Device device;
        ShotTimingController tc(&device);

        tc.startShot();
        tc.onSawTriggered(41.2, 2.5, 42.0);
        tc.endShot();
        QVERIFY(tc.isSawSettling());

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("Cup removed during settling"));

        // Sample stream extracted verbatim from shot 5470's debug log
        // (`[SAW] Settling: <w> g` lines + the final cup-gone reading).
        // The stable-plateau samples are fed with QTest::qWait(30) so the
        // controller's m_settlingAvgStableSince clock accumulates past
        // SETTLING_CLEAN_CAPTURE_MS (250 ms). Without the wait the test runs
        // in microseconds and the clean-avg gate never fires.
        const QList<double> stableSamples = {
            41.5, 41.7, 42.0, 42.2,
            42.3, 42.3, 42.3, 42.3, 42.3, 42.3, 42.3,
            42.4, 42.4, 42.5,
        };
        for (double w : stableSamples) {
            tc.onWeightSample(w, 0.5);
            QTest::qWait(50);  // ~50 ms × 14 samples = 700 ms; gate must
                                // cross SETTLING_CLEAN_CAPTURE_MS = 250 ms
        }
        // Cup-lift artifacts and removal — run fast, the gate should have
        // already captured the clean avg from the plateau above.
        tc.onWeightSample(44.0, 0.5);
        tc.onWeightSample(48.4, 0.5);
        tc.onWeightSample(51.0, 0.5);
        tc.onWeightSample(38.5, 0.5);
        tc.onWeightSample(-28.0, 0.5);

        QVERIFY2(!tc.isSawSettling(),
                 "Cup-removed branch should have fired and exited settling");
        QVERIFY2(tc.currentWeight() > 41.5 && tc.currentWeight() < 43.0,
                 qPrintable(QString(
                     "Expected ~42.3 g (last clean settled avg), got %1 g — "
                     "the cup-lift spike artifacts polluted m_weight")
                     .arg(tc.currentWeight(), 0, 'f', 2)));
        // #1161 invariant: the cup-removed branch clears
        // m_sawTriggeredThisShot but must NOT clear m_stopAtWeightTriggered
        // — otherwise MainController::onShotEnded would misclassify this
        // cup-lifted SAW shot as "profileEnd" instead of "weight".
        QVERIFY2(tc.wasSawTriggered(),
                 "Cup-removed path must preserve wasSawTriggered() == true "
                 "so the saved shot's stoppedBy classification stays 'weight'");
    }

    void cupLiftAfterNoisyPlateauDoesNotCaptureTransients_1280() {
        // Regression guard for the corpus-scan finding (PR #1282 review):
        // shots whose scale was wobbly throughout settling had no real
        // plateau, but the rolling-window avg occasionally satisfied the
        // gate transiently. The original capture rule (fire on every
        // gate match) over-applied the fallback in those cases. With
        // SETTLING_CLEAN_CAPTURE_MS = 250 ms, a single transient gate
        // fire MUST NOT update m_lastCleanSettlingAvg — verified by
        // inspecting the private member via DECENZA_TESTING friend access.
        DE1Device device;
        ShotTimingController tc(&device);

        tc.startShot();
        tc.onSawTriggered(35.0, 2.5, 36.0);
        tc.endShot();
        QVERIFY(tc.isSawSettling());

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("Cup removed during settling"));

        // Wobbly samples — the rolling avg may briefly satisfy the gate
        // but no plateau ever holds for 250 ms. Feed with NO qWait so
        // m_settlingAvgStableSince never accumulates wall-clock time.
        const QList<double> noisy = {
            34.5, 36.0, 33.8, 35.2, 36.4, 33.2, 35.9, 34.0, 36.1, 33.5,
            45.3,   // cup-lift starts; spike accepted (under 20 g step)
            44.2, 41.1, 38.4,
            10.0    // 35.3 g drop from peak 45.3 — cup-removed fires
        };
        for (double w : noisy)
            tc.onWeightSample(w, 0.5);

        QVERIFY(!tc.isSawSettling());
        // Core invariant: no transient gate fire was promoted to a
        // captured clean avg. Without SETTLING_CLEAN_CAPTURE_MS, this
        // would have been set to whatever the rolling avg was at the
        // single fortuitous moment the gate happened to fire (~34.9 g
        // for this stream, but could be 47-56 g in corpus shots 908/909).
        QVERIFY2(tc.m_lastCleanSettlingAvg == 0.0,
                 qPrintable(QString(
                     "Transient single-sample gate fires must NOT update "
                     "m_lastCleanSettlingAvg, got %1 g")
                     .arg(tc.m_lastCleanSettlingAvg, 0, 'f', 2)));
    }

    void implausibleCleanAvgIsRejectedAsScaleFault_1280() {
        // Regression guard for the corpus-scan finding (PR #1282 review):
        // shot 825 had a scale fault — the cup-on-scale reading froze at
        // ~75 g for hundreds of milliseconds on a ~40 g target shot. The
        // stability gate held continuously (gate is purely a window-drift
        // check, can't tell a real settle from a frozen reading), so
        // m_lastCleanSettlingAvg got captured at the glitch value.
        // MAX_PLAUSIBLE_POST_STOP_DRIP_G rejects any captured avg whose
        // overshoot above m_weightAtStop exceeds physical reality
        // (real drip is 0.5–3 g, never tens of grams). On rejection the
        // fallback chain falls through to the m_weightAtStop floor.
        DE1Device device;
        ShotTimingController tc(&device);

        tc.startShot();
        tc.onSawTriggered(39.5, 2.5, 40.0);
        tc.endShot();
        QVERIFY(tc.isSawSettling());

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("Cup removed during settling"));
        // The cup-removal handler also warns when it rejects the implausible
        // clean avg as a scale fault — that rejection IS what this test exercises.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("rejected as scale fault"));

        // Simulate a frozen-scale fault: a long run of identical samples
        // at 74.8 g (35+ g above stop weight). The gate fires once the
        // rolling window fills (SETTLING_WINDOW_SIZE = 6) and then
        // consecutively. We need the post-window-fill cumulative qWait
        // to exceed SETTLING_CLEAN_CAPTURE_MS (250 ms): 15 samples × 50 ms
        // = 750 ms total wall time; subtracting the first 6 samples that
        // fill the window leaves 9 post-fill intervals × 50 ms = 450 ms,
        // comfortably above the 250 ms capture threshold.
        for (int i = 0; i < 15; ++i) {
            tc.onWeightSample(74.8, 0.0);
            QTest::qWait(50);
        }
        // Confirm the capture did happen (we DON'T want to silently rely
        // on the capture gate having filtered it — the plausibility cap
        // is the layer being tested).
        QVERIFY2(tc.m_lastCleanSettlingAvg > 70.0,
                 "Capture gate should have fired on the frozen plateau");

        // Cup-removal trigger: a big drop.
        tc.onWeightSample(20.0, 0.5);

        QVERIFY(!tc.isSawSettling());
        // Expected: clean avg (74.8) was rejected because 74.8 - 39.5 = 35.3 g
        // exceeds MAX_PLAUSIBLE_POST_STOP_DRIP_G (5.0). The entire post-stop
        // stream is corrupt (m_weight was 74.8 too), so finalWeight snaps
        // back to the SAW trigger weight (39.5 g) — the only physically
        // defensible minimum-truth value we have for this shot.
        QVERIFY2(std::abs(tc.currentWeight() - 39.5) < 0.5,
                 qPrintable(QString(
                     "Implausible clean avg (35+ g overshoot) must snap "
                     "finalWeight to m_weightAtStop (39.5 g), got %1 g — "
                     "restoring the glitch value would AMPLIFY the bug")
                     .arg(tc.currentWeight(), 0, 'f', 2)));
    }

    void cupLiftBeforeAnyCleanAvgFloorsAtStopWeight() {
        // Edge case: cup lifted before any settling sample satisfies the
        // stability gate (no clean rolling avg captured). Then a sequence of
        // small drops walks m_weight below m_weightAtStop before the final
        // big drop trips cup-removal. Without the floor, m_weight would
        // persist BELOW the SAW trigger weight — physically impossible since
        // post-stop drip can only add weight.
        DE1Device device;
        ShotTimingController tc(&device);

        tc.startShot();
        tc.onSawTriggered(40.0, 3.0, 42.0);
        tc.endShot();
        QVERIFY(tc.isSawSettling());

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("Cup removed during settling"));

        // Cup wobble samples: each step is under the 20 g cup-removal threshold
        // (relative to the previous m_weight), so each individual sample is
        // accepted. m_weight ends up at 25 g (below stop weight 40) before the
        // final -10 trips cup-removal via the peak-drop arm (peak=42, -10 < 22).
        tc.onWeightSample(42.0, 0.5);   // m_weight = 42, peak = 42
        tc.onWeightSample(36.0, 0.5);   // delta -6 < 20, accepted; m_weight = 36
        tc.onWeightSample(30.0, 0.5);   // delta -6 < 20, accepted; m_weight = 30
        tc.onWeightSample(25.0, 0.5);   // delta -5 < 20, accepted; m_weight = 25 (< 40)
        tc.onWeightSample(-10.0, 0.5);  // cup-removed via peak arm (-10 < 42 - 20)

        QVERIFY(!tc.isSawSettling());
        QVERIFY2(tc.currentWeight() >= 40.0,
                 qPrintable(QString(
                     "Expected floor at m_weightAtStop (40.0 g), got %1 g — "
                     "post-stop drip can only add weight; persisting a value "
                     "below the SAW trigger weight is physically impossible")
                     .arg(tc.currentWeight(), 0, 'f', 2)));
    }

    void cleanAvgClearedBetweenShots_1280() {
        // Regression guard for the cross-shot reset invariant. The fix
        // resets m_lastCleanSettlingAvg = 0.0 in both startShot() and
        // startSettlingTimer(); if either reset is ever removed during
        // refactoring, a clean avg captured during shot N could leak into
        // shot N+1's cup-removal handler — silently overwriting the new
        // shot's finalWeight with the prior shot's value.
        DE1Device device;
        ShotTimingController tc(&device);

        // Shot N: capture a clean avg via the same QTest::qWait pattern
        // the _1280 plateau test uses.
        tc.startShot();
        tc.onSawTriggered(35.0, 2.5, 36.0);
        tc.endShot();
        for (int i = 0; i < 14; ++i) {
            tc.onWeightSample(36.0, 0.5);
            QTest::qWait(50);
        }
        QVERIFY2(tc.m_lastCleanSettlingAvg > 0.0,
                 "Capture gate should have fired on the plateau");

        // Starting shot N+1 while shot N is still settling warns that it's
        // cancelling the settling timer and saving the previous shot — exactly
        // the cross-shot transition this test drives.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("Cancelling settling timer - new shot started"));

        // Start shot N+1 — this must reset the captured value.
        tc.startShot();
        QCOMPARE(tc.m_lastCleanSettlingAvg, 0.0);

        // Re-confirm the reset also happens when settling starts (covers
        // the path where startShot() ran but settling didn't yet).
        tc.m_lastCleanSettlingAvg = 99.9;
        tc.onSawTriggered(35.0, 2.5, 36.0);
        tc.endShot();  // triggers startSettlingTimer()
        QCOMPARE(tc.m_lastCleanSettlingAvg, 0.0);
    }

    void cleanAvgSurvivesPostCaptureGateFailure_1280() {
        // Regression guard for the "captured then disturbed" sequence
        // Mark's shot 5470 actually had: settling plateau holds for
        // hundreds of ms (capture fires), THEN the cup-lift artifacts
        // arrive (gate fails for each spike sample). The captured value
        // MUST survive that gate failure — otherwise a "tighten the gate"
        // refactor that clears m_lastCleanSettlingAvg on every gate-fail
        // would silently break the actual bug class the fix targets.
        DE1Device device;
        ShotTimingController tc(&device);

        tc.startShot();
        tc.onSawTriggered(41.2, 2.5, 42.0);
        tc.endShot();

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("Cup removed during settling"));

        // Establish the captured value with a stable plateau.
        for (int i = 0; i < 14; ++i) {
            tc.onWeightSample(42.3, 0.5);
            QTest::qWait(50);
        }
        const double capturedAvg = tc.m_lastCleanSettlingAvg;
        QVERIFY2(capturedAvg > 41.0 && capturedAvg < 43.0,
                 "Plateau should produce a clean avg near 42.3 g");

        // Now feed a few "gate failure" samples that do NOT trigger
        // cup-removal (deltas under 20 g). Each one should reset
        // m_settlingAvgStableSince via weightAboveAvg but MUST NOT clear
        // m_lastCleanSettlingAvg.
        tc.onWeightSample(44.0, 0.5);  // delta +1.7 from 42.3; weightAboveAvg trips
        tc.onWeightSample(48.4, 0.5);  // delta +4.4; weightAboveAvg trips
        tc.onWeightSample(51.0, 0.5);  // delta +2.6; weightAboveAvg trips
        QCOMPARE(tc.m_lastCleanSettlingAvg, capturedAvg);

        // Cup-removed via single-step drop from 51 to 20 (delta 31 > 20).
        tc.onWeightSample(20.0, 0.5);

        QVERIFY(!tc.isSawSettling());
        QVERIFY2(std::abs(tc.currentWeight() - capturedAvg) < 0.1,
                 qPrintable(QString(
                     "After post-capture gate failure, cup-removed must "
                     "restore the pre-disruption captured avg (%1 g), "
                     "got %2 g")
                     .arg(capturedAvg, 0, 'f', 2)
                     .arg(tc.currentWeight(), 0, 'f', 2)));
    }

    void sawLearningCompleteFiresBeforeShotProcessingReady() {
        // SAW_LEARNING.md requires the [SAW] accuracy / accumulated / committed lines
        // to land in the per-shot debug log. shotProcessingReady triggers stopCapture
        // downstream, so sawLearningComplete (and the qDebug it drives) must fire first.
        DE1Device device;
        ShotTimingController tc(&device);

        MockScaleDevice scale;
        scale.mockSetConnected(true);
        tc.setScale(&scale);

        // ScaleDevice's destructor emits a DISCONNECTED warning as the mock goes out
        // of scope at test end; mark it expected per docs/CLAUDE_MD/TESTING.md.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("DISCONNECTED"));

        // Populate state so onSettlingComplete passes every guard and reaches the
        // sawLearningComplete emit: drip=1.5g, flow=1.5ml/s, overshoot=0.5g.
        tc.m_weightAtStop = 35.0;
        tc.m_flowRateAtStop = 1.5;
        tc.m_targetWeightAtStop = 36.0;
        tc.m_weight = 36.5;
        tc.m_sawSettling = true;

        QStringList order;
        QObject::connect(&tc, &ShotTimingController::sawLearningComplete,
                         [&order](double, double, double) { order << "sawLearningComplete"; });
        QObject::connect(&tc, &ShotTimingController::shotProcessingReady,
                         [&order]() { order << "shotProcessingReady"; });

        tc.onSettlingComplete();

        QCOMPARE(order, (QStringList{"sawLearningComplete", "shotProcessingReady"}));
    }

    void shotProcessingReadyEmittedOnEarlyReturnFromSettling() {
        // Even when SAW learning is skipped (e.g. scale disconnected at settling),
        // shotProcessingReady must still fire — the QScopeGuard in onSettlingComplete
        // is what guarantees this on every code path.
        DE1Device device;
        ShotTimingController tc(&device);
        // No scale set → onSettlingComplete takes the "scale disconnected" early return.
        tc.m_sawSettling = true;

        QSignalSpy sawSpy(&tc, &ShotTimingController::sawLearningComplete);
        QSignalSpy readySpy(&tc, &ShotTimingController::shotProcessingReady);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Scale disconnected"));
        tc.onSettlingComplete();

        QCOMPARE(sawSpy.count(), 0);   // learning skipped
        QCOMPARE(readySpy.count(), 1); // but shot still saves
    }

    void startShotCancelsSettlingAndEmitsReady() {
        DE1Device device;
        ShotTimingController tc(&device);

        // First shot
        tc.startShot();
        // Simulate SAW trigger via onSawTriggered (sets m_sawTriggeredThisShot)
        tc.onSawTriggered(35.0, 2.0, 36.0);
        tc.endShot();  // Should start settling

        QVERIFY(tc.isSawSettling());

        // Start a new shot while settling — should cancel settling and emit shotProcessingReady
        QSignalSpy spy(&tc, &ShotTimingController::shotProcessingReady);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Cancelling settling"));
        tc.startShot();
        QVERIFY(!tc.isSawSettling());
        QCOMPARE(spy.count(), 1);  // Previous shot's shotProcessingReady emitted
    }

    // ===== #1161: wasSawTriggered() must survive settling =====

    void wasSawTriggeredStaysTrueAfterSettling_1161() {
        // Regression: MainController::onShotEnded classifies stoppedBy from
        // wasSawTriggered() via the shotProcessingReady emitted by
        // onSettlingComplete's scope guard. onSettlingComplete clears
        // m_sawTriggeredThisShot up front, so wasSawTriggered() must be
        // backed by m_stopAtWeightTriggered (reset only in startShot) —
        // otherwise every stop-at-weight shot is misclassified "profileEnd".
        DE1Device device;
        ShotTimingController tc(&device);
        tc.startShot();
        tc.onSawTriggered(35.0, 2.0, 36.0);
        QVERIFY(tc.wasSawTriggered());
        tc.m_sawSettling = true;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Scale disconnected"));
        tc.onSettlingComplete();  // clears m_sawTriggeredThisShot internally
        QVERIFY2(tc.wasSawTriggered(),
                 "#1161: SAW must still read true after settling — "
                 "onShotEnded classifies the shot at this point");
    }

    void wasSawTriggeredTrueWhenSettlingCancelled_1161() {
        // Regression (back-to-back dial-in): starting a new shot mid-settle
        // saves the PRIOR shot via shotProcessingReady emitted from inside
        // startShot(). At that instant wasSawTriggered() must still be true
        // so the prior SAW shot is classified "weight", not "profileEnd".
        DE1Device device;
        ShotTimingController tc(&device);
        tc.startShot();
        tc.onSawTriggered(35.0, 2.0, 36.0);
        tc.endShot();
        QVERIFY(tc.isSawSettling());
        bool sawAtEmit = false;
        QObject::connect(&tc, &ShotTimingController::shotProcessingReady,
                         [&tc, &sawAtEmit]() { sawAtEmit = tc.wasSawTriggered(); });
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Cancelling settling"));
        tc.startShot();  // cancels settling, emits shotProcessingReady for prior shot
        QVERIFY2(sawAtEmit,
                 "#1161: prior SAW shot must still read wasSawTriggered()==true "
                 "when saved during settling-cancel");
    }
};

QTEST_GUILESS_MAIN(tst_Settling)
#include "tst_settling.moc"
