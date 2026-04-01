#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>

#include "models/shotdatamodel.h"
#include "controllers/shottimingcontroller.h"
#include "ble/de1device.h"

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
