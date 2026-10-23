#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>
#include <QStringList>

#include "models/shotdatamodel.h"
#include "controllers/shottimingcontroller.h"
#include "ble/de1device.h"
#include "mocks/MockScaleDevice.h"
#include "profile/profile.h"

// Test SAW settling behavior: trimSettlingData(), m_sawSettling flag lifecycle,
// and the interaction between settling completion and shot save ordering.

class tst_Settling : public QObject {
    Q_OBJECT

private:
    // Helper: populate ShotDataModel with N samples of real data followed by M zero-pressure samples
    void populateWithSettlingData(ShotDataModel& model, int realSamples, int zeroSamples) {
        for (int i = 0; i < realSamples; i++) {
            double t = i * 0.2;  // 5Hz
            model.addSample(t, 9.0, 2.0, 93.0, 88.0, 9.0, 0.0, 93.0, 94.0);
            model.addWeightSample(t, i * 0.4, 2.0);
        }
        double lastRealTime = realSamples * 0.2;
        for (int i = 0; i < zeroSamples; i++) {
            double t = lastRealTime + (i + 1) * 0.2;
            model.addSample(t, 0.0, 0.0, 90.0, 85.0, 0.0, 0.0, 90.0, 91.0);
            // Weight continues during settling
            model.addWeightSample(t, realSamples * 0.4 + i * 0.1, 0.5);
        }
    }

    // The loop below waits on a CONDITION rather than a sample budget because
    // QTest::qWait waits AT LEAST its argument. It "always pauses for the full
    // timeout" by design, and overshoots it in practice: it loops
    // processEvents(AllEvents, deadline) then qSleep(min(10ms, remaining)) until
    // the deadline HAS expired, so a slow handler or the 10 ms sleep granularity
    // pushes each call past its nominal wait
    // (qtbase/src/corelib/kernel/qtestsupport_core.cpp:137 for the documented
    // guarantee, :167-180 for the loop). Three tests once hand-rolled
    // `for (i < 14) { sample; qWait(50) }` and budgeted ~750 ms, and under the
    // parallel ASan/UBSan suite the 15-iteration one measured 1015 ms.
    //
    // Feed a plateau centred on `grams` until the clean-avg capture gate fires,
    // leaving settling still IN PROGRESS so the caller can drive what happens next.
    //
    // The samples alternate +/-0.1 g rather than repeating one value, and that is
    // load-bearing in both directions:
    //   - It keeps the caller's path reachable. Byte-identical samples leave
    //     `delta` at 0, so onWeightSample's FAST path accumulates stillness across
    //     the whole loop and correctly completes settling once it crosses
    //     SETTLING_STABLE_MS (1000 ms) -- which a loaded machine reaches before the
    //     loop ends. Alternating exceeds the 0.1 g change threshold on every
    //     sample, so that clock never accumulates. Do not "simplify" this back to
    //     one value.
    //     The jitter takes the FAST path off the wall clock, and only that path.
    //     `m_settlingAvgStableSince` keeps running across jittered samples, so the
    //     ROLLING path still completes at SETTLING_STABLE_MS and the loop's real
    //     bound is the capture gate firing first. Both gates run off the SAME
    //     origin, `m_settlingAvgStableSince`: capture at SETTLING_CLEAN_CAPTURE_MS
    //     (250 ms), completion at SETTLING_STABLE_MS (1000 ms), so the margin is 4x
    //     however many samples the loop took to arm the gate. The 40-sample cap is
    //     a runaway stop, not the bound; it is deliberately past 1000 ms of qWait so
    //     that a genuinely stuck gate fails on the QVERIFY2 below rather than
    //     looping forever.
    //   - It stays inside the rolling-average gate: +/-0.1 g is under
    //     SETTLING_ABOVE_AVG_MARGIN (0.2 g), so no sample ever reads as above the
    //     window mean, and the window mean itself is `grams` with drift near zero.
    // A real scale jitters; a run of identical readings was the unrealistic part.
    void feedPlateauUntilCaptured(ShotTimingController& tc, double grams, double flow) {
        // Exits on the capture gate: SETTLING_WINDOW_SIZE (6) samples to arm the
        // rolling path, then SETTLING_CLEAN_CAPTURE_MS (250 ms) of continuous gate,
        // so ~11 samples at 50 ms each. That 250 ms is wall clock, so the sample
        // count needed rises with qWait overshoot; the 40 cap is the runaway stop.
        for (int i = 0; i < 40 && tc.m_lastCleanSettlingAvg <= 0.0; ++i) {
            tc.onWeightSample(grams + (i % 2 ? 0.1 : -0.1), flow);
            QTest::qWait(50);
        }
        QVERIFY2(tc.m_lastCleanSettlingAvg > 0.0,
                 "Capture gate never fired on the plateau");
        QVERIFY2(tc.isSawSettling(),
                 "Settling completed inside the plateau loop, so the path under test "
                 "never ran. Two clocks can do this: the fast path's stillness clock "
                 "accumulating across samples that changed by 0.2 g (the jitter above "
                 "exists to prevent exactly that), or the rolling path's "
                 "m_settlingAvgStableSince crossing SETTLING_STABLE_MS because qWait "
                 "overshot and the capture gate did not fire first. Check which.");
    }

private slots:
    void init() { QTest::failOnWarning(); }

    void postSaveDiagnosticsKeepYieldAndLearningUnchanged_data() {
        QTest::addColumn<double>("laterG");
        QTest::addColumn<bool>("reportsRise");
        QTest::newRow("gradual-rise-from-log") << 37.0 << true;
        QTest::newRow("late-disturbance-from-log") << 46.0 << true;
        QTest::newRow("small-wobble") << 36.5 << false;
        QTest::newRow("cup-removed") << 0.0 << false;
    }

    void postSaveDiagnosticsKeepYieldAndLearningUnchanged() {
        QFETCH(double, laterG);
        QFETCH(bool, reportsRise);
        DE1Device device;
        ShotTimingController tc(&device);
        tc.m_weight = 36.3;
        tc.m_appliedZeroOffsetG = 0;
        tc.m_postSettle = ShotTimingController::PostSettleTrace{
            36.3, -0.6, QDateTime::currentMSecsSinceEpoch(), 1, QString()};
        tc.m_postSettle->lastG = 36.3;
        QSignalSpy ready(&tc, &ShotTimingController::shotProcessingReady);
        QSignalSpy learning(&tc, &ShotTimingController::sawLearningComplete);
        if (reportsRise)
            QTest::ignoreMessage(QtInfoMsg, QRegularExpression(
                "\\[SAW\\]\\[Timing\\] Post-save weight changed:.*savedG=36.3.*savedYieldUnchanged=true learningUnchanged=true"));
        tc.onWeightSample(laterG - 0.6, 0, 0);
        if (tc.m_postSettle)
            QCOMPARE(tc.m_postSettle->maxRiseG >= 0.5, reportsRise);
        tc.flushPostSettleTrace();
        QCOMPARE(tc.currentWeight(), 36.3);
        QCOMPARE(ready.count(), 0);
        QCOMPARE(learning.count(), 0);
    }

    void sensorTimeUsesExtractionOriginAndIncludesSettlingOnly() {
        DE1Device device;
        ShotTimingController tc(&device);
        tc.startShot();
        QVERIFY(!tc.sensorTime(QDateTime::currentMSecsSinceEpoch()).has_value());
        QSignalSpy started(&tc, &ShotTimingController::extractionClockStarted);
        ShotSample sample;
        tc.onShotSample(sample, 9.0, 2.0, 93.0, 2, false);
        QCOMPARE(started.count(), 1);
        const qint64 origin = tc.m_displayTimeBase;
        QCOMPARE(tc.sensorTime(origin + 425).value(), 0.425);
        QVERIFY(!tc.sensorTime(origin - 1).has_value());
        tc.onShotSample(sample, 9.0, 2.0, 93.0, 3, false);
        QCOMPARE(started.count(), 1);
        tc.m_shotActive = false;
        tc.m_sawSettling = true;
        QCOMPARE(tc.sensorTime(origin + 31000).value(), 31.0);
        tc.m_sawSettling = false;
        QVERIFY(!tc.sensorTime(origin + 32000).has_value());
        tc.startShot();
        QVERIFY(!tc.sensorTime(origin + 33000).has_value());
    }

    // ===== trimSettlingData() =====

    void trimRemovesTrailingZeroPressure() {
        ShotDataModel model;
        populateWithSettlingData(model, 50, 10);

        QCOMPARE(model.pressureData().size(), 60);
        model.trimSettlingData();
        QCOMPARE(model.pressureData().size(), 50);
        QCOMPARE(model.flowData().size(), 50);
        QCOMPARE(model.temperatureData().size(), 50);
        // The goal series trim on a time cutoff rather than an index, so they
        // desync silently: a missed trim leaves the goal lines trailing past
        // every other series into the settling region.
        QCOMPARE(model.temperatureGoalData().size(), 50);
        QCOMPARE(model.temperatureMixGoalData().size(), 50);
    }

    // stopTime() is SAW-only, and silently so. It is written by exactly one
    // caller — markStopAt(), wired to WeightProcessor::stopNow — so a shot that
    // ends any other way (manual stop, profile end, volume stop, or SAW blocked
    // by an oscillating scale) finishes with a full data set and stopTime still
    // at its -1 sentinel. Nothing about the getter's name says that, and the
    // Home Screen widget was wired to it as if it meant "how long the shot
    // ran": every non-SAW shot was rejected as non-finalized and the last-shot
    // tile stopped updating (#1658). Consumers wanting a duration must take the
    // finalized value from MainController::shotPersisted instead.
    void stopTimeStaysSentinelWithoutSaw() {
        ShotDataModel model;
        populateWithSettlingData(model, 50, 10);

        // A real shot by every other measure...
        QVERIFY(model.rawTime() > 0.0);
        QVERIFY(!model.cumulativeWeightData().isEmpty());
        // ...yet this is what a widget reading stopTime() would have been handed.
        QCOMPARE(model.stopTime(), -1.0);

        // And it only becomes a duration once the SAW path marks it.
        model.markStopAt(10.0);
        QCOMPARE(model.stopTime(), 10.0);
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
            model.addSample(i * 0.2, 9.0, 2.0, 93.0, 88.0, 9.0, 0.0, 93.0, 94.0);
        }

        qsizetype sizeBefore = model.pressureData().size();
        model.trimSettlingData();
        QCOMPARE(model.pressureData().size(), sizeBefore);
    }

    void trimPreservesDataWhenAllZeroPressure() {
        ShotDataModel model;
        // All samples have zero pressure (failed shot)
        for (int i = 0; i < 20; i++) {
            model.addSample(i * 0.2, 0.0, 0.0, 90.0, 85.0, 0.0, 0.0, 90.0, 91.0);
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

        QTest::ignoreMessage(QtInfoMsg,
            QRegularExpression("Cup removed during settling"));

        // Sample stream extracted verbatim from shot 5470's debug log
        // (`[SAW] Settling: <w> g` lines + the final cup-gone reading).
        // The stable-plateau samples are fed with QTest::qWait(50) so the
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
        // The plateau above is fed on a fixed qWait budget, and its tail accumulates
        // real stillness: the run starts at 42.2, not at the seven 42.3s, because
        // `qAbs(42.3 - 42.2)` is 0.0999... and falls under the 0.1 change threshold.
        // Eight samples, so seven intervals, so ~350 ms. If those overshoot far
        // enough to cross SETTLING_STABLE_MS, the fast path completes settling before
        // the artifacts arrive and this test passes for the WRONG reason: the
        // !isSawSettling() below would hold because settling finished normally, and
        // currentWeight() would be 42.3, inside the band it asserts. Fail loudly here
        // instead, so an overshoot reads as an overshoot rather than as a pass.
        QVERIFY2(tc.isSawSettling(),
                 "Settling completed on the plateau, so the cup-removed path under "
                 "test never ran -- the qWait budget above overshot");
        QVERIFY2(tc.m_lastCleanSettlingAvg > 0.0,
                 "Capture gate never fired on the plateau, so the fallback this test "
                 "exercises has nothing to fall back to");

        // Cup-lift artifacts and removal — run fast, the gate should have already
        // captured the clean avg from the plateau above. 51.0 is past the 48.7 g
        // over-rise limit (41.2 + 3 s x 2.5 g/s) but alone, so it is dropped; the
        // 38.5 down-step below the stop weight then precedes the lift, which must
        // restore the clean avg rather than floor at the stop weight.
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
        // #1161 invariant: the abandoned-settle path clears
        // m_sawTriggeredThisShot but must NOT clear m_stopAtWeightTriggered
        // — otherwise MainController::onShotEnded would misclassify this
        // cup-lifted SAW shot as "profileEnd" instead of "weight".
        QVERIFY2(tc.wasSawTriggered(),
                 "Abandoned-settle path must preserve wasSawTriggered() == true "
                 "so the saved shot's stoppedBy classification stays 'weight'");
    }

    void cupLiftAfterNoisyPlateauDoesNotCaptureTransients_1280() {
        // Regression guard for the corpus-scan finding (PR Kulitorum/Decenza#1282 review):
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

        QTest::ignoreMessage(QtInfoMsg,
            QRegularExpression("more than drip can add"));

        // Wobbly samples — the rolling avg may briefly satisfy the gate
        // but no plateau ever holds for 250 ms. Feed with NO qWait so
        // m_settlingAvgStableSince never accumulates wall-clock time.
        const QList<double> noisy = {
            34.5, 36.0, 33.8, 35.2, 36.4, 33.2, 35.9, 34.0, 36.1, 33.5,
            45.3,   // cup-lift starts: past what drip can add, dropped on its own
            44.2,   // second in a row: settling is abandoned
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

    void frozenHighReadingSavesStopWeight_1280() {
        // Corpus shot 825: the reading froze at ~75 g on a ~40 g target. It used to
        // be captured as a "clean" average and rejected later; now it is rejected
        // on arrival. With no steady reading yet, the reading BEFORE it is not
        // trusted either -- it may be the disturbance starting -- so the stop weight
        // is saved even though that earlier reading (40.5 g) is above it.
        DE1Device device;
        ShotTimingController tc(&device);

        tc.startShot();
        tc.onSawTriggered(39.5, 2.5, 40.0);
        tc.endShot();
        QVERIFY(tc.isSawSettling());

        QTest::ignoreMessage(QtInfoMsg,
            QRegularExpression("more than drip can add"));

        tc.onWeightSample(40.5, 0.5);
        tc.onWeightSample(74.8, 0.5);
        tc.onWeightSample(74.8, 0.5);

        QVERIFY(!tc.isSawSettling());
        QCOMPARE(tc.m_lastCleanSettlingAvg, 0.0);
        QVERIFY2(std::abs(tc.currentWeight() - 39.5) < 0.05,
                 qPrintable(QString("Expected the stop weight (39.5 g), got %1 g")
                                .arg(tc.currentWeight(), 0, 'f', 2)));
    }

    void fastStopDripIsNotAnOverRise() {
        // A fast stop drips more: 3 s x 8.5 g/s, capped at 12 g. This 7 g drip passes
        // the 5 g floor on two samples, so a limit that ignored flow would cut it short.
        DE1Device device;
        ShotTimingController tc(&device);

        tc.startShot();
        tc.onSawTriggered(30.0, 8.5, 36.0);
        tc.endShot();

        for (double w = 31.0; w <= 37.0; w += 1.0)
            tc.onWeightSample(w, 8.0);

        QVERIFY2(tc.isSawSettling(),
                 "A 7 g drip after an 8.5 g/s stop was treated as more than drip can add");
    }

    void spikedStopFlowDoesNotDisableOverRise() {
        // The short flow at the stop has been logged at 60 g/s. Uncapped, that allows
        // 180 g of "drip" and the check never fires; the 12 g ceiling keeps it alive.
        DE1Device device;
        ShotTimingController tc(&device);

        tc.startShot();
        tc.onSawTriggered(35.0, 60.0, 36.0);
        tc.endShot();

        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("more than drip can add"));

        tc.onWeightSample(48.0, 0.5);
        tc.onWeightSample(48.0, 0.5);

        QVERIFY2(!tc.isSawSettling(), "A 13 g rise passed as drip on a spiked flow reading");
    }

    void lateStopMeasuresDripFromWhereSettlingStarted() {
        // The DE1 acted on the stop late: SAW fired at 30 g, but the pump ran on to 38 g
        // before the shot ended. That coffee is real, so drip is measured from 38 g, not
        // from the trigger -- from the trigger, 39 and 40 g would be past the 6 g limit.
        DE1Device device;
        ShotTimingController tc(&device);

        tc.startShot();
        tc.onSawTriggered(30.0, 2.0, 36.0);
        tc.m_weight = 38.0;  // the pour that continued until endShot()
        tc.endShot();

        for (double w = 39.0; w <= 42.0; w += 1.0)
            tc.onWeightSample(w, 0.5);

        QVERIFY2(tc.isSawSettling(),
                 "Coffee poured before the pump stopped was treated as a disturbance");
    }

    void loneSpikeIsDroppedNotActedOn() {
        // One corrupted packet far above the drip is not a hand on the cup: it must
        // neither end settling nor become the current weight.
        DE1Device device;
        ShotTimingController tc(&device);

        tc.startShot();
        tc.onSawTriggered(35.0, 2.0, 36.0);
        tc.endShot();

        tc.onWeightSample(36.0, 0.5);
        tc.onWeightSample(47.0, 0.5);
        tc.onWeightSample(36.2, 0.5);

        QVERIFY2(tc.isSawSettling(), "A single spike ended settling");
        QCOMPARE(tc.currentWeight(), 36.2);
        QCOMPARE(tc.m_settlingPeakWeight, 36.2);
    }

    void overRiseStillPendingWhenSettlingEndsCounts() {
        // A reading past the limit, then the scale goes quiet (corpus shot 1016: 2.3 s
        // with no sample). Completing on the reading before it would save a value the
        // disturbance had already moved, so the pending reading counts as the over-rise.
        DE1Device device;
        ShotTimingController tc(&device);

        tc.startShot();
        tc.onSawTriggered(35.0, 2.0, 36.0);
        tc.endShot();

        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("more than drip can add"));

        tc.onWeightSample(36.0, 0.5);
        tc.onWeightSample(47.0, 0.5);
        tc.m_lastWeightChangeTime -= Settling::SETTLING_SILENCE_OVERRIDE_MS + 100;
        tc.onDisplayTimerTick();

        QVERIFY(!tc.isSawSettling());
        QCOMPARE(tc.currentWeight(), 35.0);
    }

    void fastRiseWithinTheAmountLimitIsCaught() {
        // Corpus shot 793: the scale wobbled and climbed fast but stayed inside the amount
        // limit, so only the flow test sees it. Weight arriving at 6-7.5 g/s after a
        // 1.5 g/s stop is not drip: drip only slows once the pump stops.
        DE1Device device;
        ShotTimingController tc(&device);

        tc.startShot();
        tc.onSawTriggered(38.9, 1.5, 40.0);
        tc.endShot();

        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("faster than drip can run"));

        feedPlateauUntilCaptured(tc, 39.6, 0.5);
        QVERIFY(!QTest::currentTestFailed());
        const double captured = tc.m_lastCleanSettlingAvg;

        tc.onWeightSample(40.5, 0.5, 6.0);
        tc.onWeightSample(41.5, 0.5, 7.5);  // both inside the 43.9 g amount limit

        QVERIFY2(!tc.isSawSettling(), "A rise faster than drip can run did not end settling");
        QCOMPARE(tc.currentWeight(), captured);
    }

    void filterDrainIsNotCutShort() {
        // A filter bed keeps draining standing water after the stop, past any amount
        // limit an espresso puck's drip fits (200 g + 12 g here). The drain slows, so the
        // flow test leaves it alone too, and settling waits for it as before.
        DE1Device device;
        ShotTimingController tc(&device);
        Profile filter;
        filter.setBeverageType(QStringLiteral("filter"));
        tc.setCurrentProfile(&filter);

        tc.startShot();
        tc.onSawTriggered(200.0, 6.0, 210.0);
        tc.endShot();

        const QList<QPair<double, double>> drain = {
            {205.0, 5.0}, {210.0, 4.0}, {215.0, 3.0}, {219.0, 2.0}, {222.0, 1.0}};
        for (const auto& [weight, flow] : drain)
            tc.onWeightSample(weight, flow, flow);

        QVERIFY2(tc.isSawSettling(), "A filter bed draining 22 g after the stop was cut short");
    }

    void lateGusherFlowIsNotADisturbance() {
        // Corpus shot 939: SAW caught a gusher late, at 4.3 g/s, and the pour was running
        // at 7.5 g/s by the time the pump stopped. Measured from the trigger's flow, its
        // real drip would read as a disturbance; measured from settle start it does not.
        DE1Device device;
        ShotTimingController tc(&device);

        tc.startShot();
        tc.onSawTriggered(44.6, 4.3, 36.0);
        tc.endShot();

        tc.onWeightSample(45.9, 7.5, 7.5);  // first settling sample
        tc.onWeightSample(47.3, 8.5, 9.0);
        tc.onWeightSample(48.9, 8.5, 9.2);

        QVERIFY2(tc.isSawSettling(), "A late-caught gusher's drip was treated as a disturbance");
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

        QTest::ignoreMessage(QtInfoMsg,
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

        // Shot N: capture a clean avg via the same plateau helper the _1280
        // test uses.
        tc.startShot();
        tc.onSawTriggered(35.0, 2.5, 36.0);
        tc.endShot();
        feedPlateauUntilCaptured(tc, 36.0, 0.5);
        // QVERIFY2 inside a non-slot helper returns from the HELPER, not the test, so
        // without this the test would keep asserting against state the helper just
        // reported as bad -- and the failure it printed would be buried under the
        // downstream one. That is the same diagnosis problem the helper exists to fix.
        QVERIFY(!QTest::currentTestFailed());

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

    // A sample that MOVES the scale must never be recorded as the settled weight,
    // no matter how long the scale was still before it.
    //
    // Not named for #1280: that report's symptom is attributed, in
    // shottimingcontroller.cpp, to the cup-removed path. This came from a suite
    // failure instead. The 1.7 g step below is an INCREASE, which the cup-removed
    // gate never claims at any size — and see the note in onWeightSample's
    // `delta >= 0.1` branch (around the `stableMs = 0` reset, NOT at the gate
    // itself) for why a real lift can reach this branch too.
    //
    // onWeightSample samples the stillness duration BEFORE accounting for the
    // current reading, then the fast path completes settling at that reading. So a
    // disturbing sample arriving after a second of stillness used to log "stable
    // for 1007 ms" and settle at the disturbed value -- the stillness measured
    // belonged to the samples before it, not to it.
    void movingSampleDoesNotSettleOnStaleStillness() {
        DE1Device device;
        ShotTimingController tc(&device);
        tc.startShot();
        tc.onSawTriggered(41.2, 2.5, 42.0);
        tc.endShot();

        // Two identical readings: the scale is still, so the fast path's stillness
        // clock is running and pinned to the first of them.
        tc.onWeightSample(42.3, 0.5);
        tc.onWeightSample(42.3, 0.5);

        // Age that stillness past SETTLING_STABLE_MS. Reaching back into the clock
        // rather than sleeping keeps this deterministic and off the suite's wall
        // clock; it is the same state a scale that sat still for 1.1 s produces.
        tc.m_lastWeightChangeTime -= 1100;

        // The disturbance: +1.7 g. Only three samples are in the window, so the
        // rolling path cannot fire either -- if settling completes here, it did so on the
        // stale stillness, at 44.0 g.
        tc.onWeightSample(44.0, 0.5);

        QVERIFY2(tc.isSawSettling(),
                 "A sample 1.7 g away from the last reading completed settling, so it "
                 "was measured as still using the run of samples BEFORE it");

        // ...and the fast path must still WORK. Without this half, `stableMs = 0`
        // could be made unconditional -- killing the fast path outright, leaving SAW
        // to settle only via the rolling path or the 10 s timeout -- and every
        // assertion in this file would still pass, because they all assert that
        // settling did NOT complete. Age the run that STARTED at 44.0 g, then feed a
        // byte-identical sample so `delta` is 0 and the stillness stands.
        // Completing settling reaches onSettlingComplete()'s no-physical-scale guard,
        // and init()'s failOnWarning() would take that as a failure.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("No physical scale at settling"));
        tc.m_lastWeightChangeTime -= 1100;
        tc.onWeightSample(44.0, 0.5);

        QVERIFY2(!tc.isSawSettling(),
                 "A second of real stillness at the new weight did not complete "
                 "settling, so the fast path is dead");
        QCOMPARE(tc.currentWeight(), 44.0);
    }

    void cupLiftDetectedOnShotThatNeverReached20g() {
        // Cup-removal used to require the weight to have EXCEEDED 20 g before it
        // looked at the drop, so a lift on a ristretto went undetected and settling
        // finished at the negative reading. 15 g target, so nothing here clears the
        // old gate; -28.0 is what shot 5470 logged when its cup came off, and being
        // the cup's tared-out mass it does not scale down with the target.
        DE1Device device;
        ShotTimingController tc(&device);

        tc.startShot();
        tc.onSawTriggered(15.0, 2.5, 15.5);
        tc.endShot();
        QVERIFY(tc.isSawSettling());

        QSignalSpy learnSpy(&tc, &ShotTimingController::sawLearningComplete);

        QTest::ignoreMessage(QtInfoMsg,
            QRegularExpression("Cup removed during settling"));

        feedPlateauUntilCaptured(tc, 15.2, 0.5);

        tc.onWeightSample(-28.0, 0.5);

        QVERIFY2(!tc.isSawSettling(),
                 "Cup-removed never fired on a sub-20 g shot, so settling ran on "
                 "with a negative reading");
        QVERIFY2(tc.currentWeight() > 14.0 && tc.currentWeight() < 16.5,
                 qPrintable(QString("Expected ~15.2 g (the clean settled avg), got "
                                    "%1 g").arg(tc.currentWeight(), 0, 'f', 2)));
        // A cup lift teaches the predictor nothing, so learning must be skipped --
        // asserted on the signal, since wasSawTriggered() below cannot observe it.
        QCOMPARE(learnSpy.count(), 0);
        // #1161 invariant: the cup-removed branch clears m_sawTriggeredThisShot but
        // must NOT clear m_stopAtWeightTriggered, which is what wasSawTriggered()
        // reads -- otherwise MainController::onShotEnded misclassifies this as
        // "profileEnd" instead of "weight".
        QVERIFY2(tc.wasSawTriggered(),
                 "Cup-removed path must preserve wasSawTriggered() == true");
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

        QTest::ignoreMessage(QtInfoMsg,
            QRegularExpression("Cup removed during settling"));

        // Establish the captured value with a stable plateau.
        feedPlateauUntilCaptured(tc, 42.3, 0.5);
        // QVERIFY2 inside a non-slot helper returns from the HELPER, not the test, so
        // without this the test would keep asserting against state the helper just
        // reported as bad -- and the failure it printed would be buried under the
        // downstream one. That is the same diagnosis problem the helper exists to fix.
        QVERIFY(!QTest::currentTestFailed());
        const double capturedAvg = tc.m_lastCleanSettlingAvg;
        QVERIFY2(capturedAvg > 41.0 && capturedAvg < 43.0,
                 "Plateau should produce a clean avg near 42.3 g");

        // Now feed "gate failure" samples that end nothing: each stays under the
        // 48.7 g over-rise limit (41.2 + 3 s x 2.5 g/s). Each one should reset
        // m_settlingAvgStableSince but MUST NOT clear m_lastCleanSettlingAvg.
        tc.onWeightSample(44.0, 0.5);  // delta +1.7 from 42.3; weightAboveAvg trips
        tc.onWeightSample(45.5, 0.5);
        tc.onWeightSample(46.0, 0.5);
        QCOMPARE(tc.m_lastCleanSettlingAvg, capturedAvg);

        // Cup-removed: 46.0 -> 20.0 is a 26 g drop below the peak (> CUP_REMOVED_DROP_G).
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

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("No physical scale at settling"));
        tc.onSettlingComplete();

        QCOMPARE(sawSpy.count(), 0);   // learning skipped
        QCOMPARE(readySpy.count(), 1); // but shot still saves
    }

    void flowScaleServesSawButNeverTrainsIt() {
        // A virtual scale is permanently isConnected(), so the connection check alone
        // lets it through. Without the isFlowScale() clause a scale-less shot trains a
        // physical scale's pool from a flow-integral ESTIMATE — and a biased one: the
        // gravity drip off the puck lands after the pour ends, which is exactly when
        // FlowScale goes silent, so its drip is systematically low and drags the saved
        // scale's learned model down. SAW then stops late and overshoots once the real
        // scale is reconnected. See the reasoning at the guard itself.
        //
        // State below is byte-for-byte the passing case in
        // sawLearningCompleteFiresBeforeShotProcessingReady above, so the ONLY
        // difference is isFlowScale() — deleting that clause flips this test red
        // while the positive test stays green. (Note the fixture's drip is 1.5 g, not
        // zero; the guard does not depend on the value, only on the source.)
        DE1Device device;
        ShotTimingController tc(&device);

        MockScaleDevice scale;
        scale.mockSetConnected(true);
        scale.setIsFlowScale(true);
        tc.setScale(&scale);

        tc.m_weightAtStop = 35.0;
        tc.m_flowRateAtStop = 1.5;
        tc.m_targetWeightAtStop = 36.0;
        tc.m_weight = 36.5;
        tc.m_sawSettling = true;

        QSignalSpy sawSpy(&tc, &ShotTimingController::sawLearningComplete);
        QSignalSpy readySpy(&tc, &ShotTimingController::shotProcessingReady);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("No physical scale at settling"));
        tc.onSettlingComplete();

        QCOMPARE(sawSpy.count(), 0);    // never trains the physical scale's pool
        QCOMPARE(readySpy.count(), 1);  // but the scale-less shot still saves
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
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("No physical scale at settling"));
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
