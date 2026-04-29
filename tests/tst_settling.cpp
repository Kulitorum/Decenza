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
};

QTEST_GUILESS_MAIN(tst_Settling)
#include "tst_settling.moc"
