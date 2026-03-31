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
                           QVector<double> frameExitWeights = {}) {
        QVector<double> learningDrips;
        QVector<double> learningFlows;
        wp.configure(targetWeight, preinfuseFrames, frameExitWeights,
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

private slots:

    void init() {
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

    void spikeDoesNotCorruptFlowRate() {
        // A rejected spike should not affect LSLR flow computation
        WeightProcessor wp;
        installFakeClock(wp);
        QSignalSpy flowSpy(&wp, &WeightProcessor::flowRatesReady);

        // Build stable 2 g/s flow
        feedRising(wp, 0.0, 2.0, 10);

        int countBefore = flowSpy.count();
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
        wp.configure(36.0, 0, empty, empty, empty, false);
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
        int firstCount = stopSpy.count();

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
};

QTEST_GUILESS_MAIN(tst_WeightProcessor)
#include "tst_weightprocessor.moc"
