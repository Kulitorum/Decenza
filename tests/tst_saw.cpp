#include <QtTest>
#include <QSignalSpy>

#include "machine/weightprocessor.h"

// Test SAW (stop-at-weight) logic in WeightProcessor across profile types.
// WeightProcessor has a clean public interface — no friend access needed.

class tst_SAW : public QObject {
    Q_OBJECT

private:
    // Helper: feed N weight samples at regular intervals to build LSLR history
    void feedSamples(WeightProcessor& wp, double startWeight, double flowRate,
                     int count, int intervalMs = 200) {
        for (int i = 0; i < count; i++) {
            double w = startWeight + flowRate * (i * intervalMs / 1000.0);
            wp.processWeight(w);
            QTest::qWait(intervalMs);
        }
    }

    // Helper: configure for espresso with typical values
    void configureEspresso(WeightProcessor& wp, double targetWeight, int preinfuseFrames) {
        QVector<double> frameExitWeights;  // No per-frame exits for simplicity
        QVector<double> learningDrips;     // No learning data — use fallback
        QVector<double> learningFlows;
        wp.configure(targetWeight, preinfuseFrames, frameExitWeights,
                     learningDrips, learningFlows, false, 0.38);
    }

private slots:

    // ===== SAW does NOT trigger in first 5 seconds =====

    void sawIgnoresFirst5Seconds() {
        WeightProcessor wp;
        configureEspresso(wp, 36.0, 0);  // 0 preinfuse frames
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);  // Past preinfusion (0 >= 0)

        QSignalSpy spy(&wp, &WeightProcessor::stopNow);

        // Feed weight samples that exceed target, but within 5 seconds
        // At 200ms intervals, 20 samples = 4 seconds
        for (int i = 0; i < 20; i++) {
            wp.processWeight(40.0);  // Way above 36g target
            QTest::qWait(200);
        }

        QCOMPARE(spy.count(), 0);  // Should NOT trigger in first 5s
    }

    // ===== SAW triggers after 5 seconds =====

    void sawTriggersAfter5Seconds() {
        WeightProcessor wp;
        configureEspresso(wp, 36.0, 0);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);

        QSignalSpy spy(&wp, &WeightProcessor::stopNow);

        // Wait past the 5-second guard first
        QTest::qWait(5500);

        // Then feed rising weight samples to build LSLR
        for (int i = 0; i < 30; i++) {
            double w = 30.0 + i * 0.5;  // Rising from 30g at 0.5g per sample
            wp.processWeight(w);
            QTest::qWait(200);
        }

        QVERIFY(spy.count() >= 1);  // Should have triggered after weight + flow met threshold
    }

    // ===== SAW waits for preinfusion frame guard =====

    void sawWaitsForPreinfusion_data() {
        QTest::addColumn<int>("preinfuseFrames");
        QTest::newRow("2 preinfuse frames") << 2;
        QTest::newRow("3 preinfuse frames") << 3;
    }

    void sawWaitsForPreinfusion() {
        QFETCH(int, preinfuseFrames);
        WeightProcessor wp;
        configureEspresso(wp, 36.0, preinfuseFrames);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);  // Still in preinfusion

        QSignalSpy spy(&wp, &WeightProcessor::stopNow);

        // Wait past 5s guard, feed rising weight during preinfusion
        QTest::qWait(5500);
        for (int i = 0; i < 15; i++) {
            double w = 30.0 + i * 0.5;
            wp.processWeight(w);
            QTest::qWait(200);
        }

        QCOMPARE(spy.count(), 0);  // Should NOT trigger — still in preinfusion frames

        // Now advance past preinfusion and feed rising weight
        wp.setCurrentFrame(preinfuseFrames);

        for (int i = 0; i < 15; i++) {
            double w = 30.0 + i * 0.5;
            wp.processWeight(w);
            QTest::qWait(200);
        }

        QVERIFY(spy.count() >= 1);  // NOW should trigger
    }

    // ===== SAW does not trigger when targetWeight == 0 =====

    void sawDisabledWhenTargetZero() {
        WeightProcessor wp;
        configureEspresso(wp, 0.0, 0);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);

        QSignalSpy spy(&wp, &WeightProcessor::stopNow);

        QTest::qWait(5500);
        for (int i = 0; i < 10; i++) {
            wp.processWeight(50.0);
            QTest::qWait(200);
        }

        QCOMPARE(spy.count(), 0);
    }

    // ===== SAW requires flow rate >= 0.5 =====

    void sawRequiresValidFlow() {
        WeightProcessor wp;
        configureEspresso(wp, 36.0, 0);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);

        QSignalSpy spy(&wp, &WeightProcessor::stopNow);

        // Wait past 5s guard, then feed constant weight (0 flow rate)
        QTest::qWait(5500);
        for (int i = 0; i < 10; i++) {
            wp.processWeight(40.0);  // Constant weight = 0 flow
            QTest::qWait(200);
        }

        QCOMPARE(spy.count(), 0);  // Flow too low (LSLR ≈ 0)
    }

    // ===== Per-frame weight exit fires =====

    void perFrameWeightExit() {
        WeightProcessor wp;
        QVector<double> frameExits = {0.0, 0.2, 0.0};  // Frame 1 exits at 0.2g
        QVector<double> learningDrips, learningFlows;
        wp.configure(36.0, 2, frameExits, learningDrips, learningFlows, false, 0.38);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(1);  // In frame 1 which has 0.2g exit

        QSignalSpy spy(&wp, &WeightProcessor::skipFrame);

        wp.processWeight(0.3);  // Above 0.2g exit weight

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 1);  // Should skip frame 1
    }

    // ===== Per-frame weight exit fires only once per frame =====

    void perFrameWeightExitOnlyOnce() {
        WeightProcessor wp;
        QVector<double> frameExits = {0.0, 0.2, 0.0};
        QVector<double> learningDrips, learningFlows;
        wp.configure(36.0, 2, frameExits, learningDrips, learningFlows, false, 0.38);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(1);

        QSignalSpy spy(&wp, &WeightProcessor::skipFrame);

        wp.processWeight(0.3);
        wp.processWeight(0.5);  // Still in frame 1, already triggered

        QCOMPARE(spy.count(), 1);  // Only fires once
    }

    // ===== SAW with preinfuseFrameCount == 0 fires immediately (after 5s) =====

    void sawZeroPreinfuseFrames() {
        WeightProcessor wp;
        configureEspresso(wp, 36.0, 0);  // No preinfusion
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);  // Frame 0 >= preinfuseFrameCount(0) → past preinfusion

        QSignalSpy spy(&wp, &WeightProcessor::stopNow);

        // Wait past 5s, feed rising weight
        QTest::qWait(5500);
        for (int i = 0; i < 20; i++) {
            double w = 30.0 + i * 1.0;  // Rising from 30 to 49
            wp.processWeight(w);
            QTest::qWait(200);
        }

        QVERIFY(spy.count() >= 1);
    }
};

QTEST_GUILESS_MAIN(tst_SAW)
#include "tst_saw.moc"
