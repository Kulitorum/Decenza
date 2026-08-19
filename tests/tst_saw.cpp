#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>

#include "machine/weightprocessor.h"

// Test SAW (stop-at-weight) logic in WeightProcessor across profile types.
// WeightProcessor has a clean public interface — no friend access needed.
//
// Uses an injectable fake clock to avoid real-time waits (~38s → <1s).

class tst_SAW : public QObject {
    Q_OBJECT

private:
    qint64 m_fakeClock = 1000000;  // Arbitrary start (1,000,000 ms)

    void installFakeClock(WeightProcessor& wp) {
        wp.setWallClock([this]() { return m_fakeClock; });
    }

    // Helper: feed N weight samples at regular intervals to build LSLR history
    void feedSamples(WeightProcessor& wp, double startWeight, double flowRate,
                     int count, int intervalMs = 200) {
        for (int i = 0; i < count; i++) {
            double w = startWeight + flowRate * (i * intervalMs / 1000.0);
            wp.processWeight(w);
            m_fakeClock += intervalMs;
        }
    }

    // Helper: configure for espresso with typical values
    void configureEspresso(WeightProcessor& wp, double targetWeight, int preinfuseFrames) {
        QVector<double> frameExitWeights;  // No per-frame exits for simplicity
        QVector<double> learningDrips;     // No learning data — use fallback
        QVector<double> learningFlows;
        wp.configure(targetWeight, preinfuseFrames, frameExitWeights, {},
                     learningDrips, learningFlows, false, 0.38);
    }

private slots:

    void init() { QTest::failOnWarning();
        m_fakeClock = 1000000;  // Reset for each test
    }

    // ===== SAW does NOT trigger in first 5 seconds =====

    void sawIgnoresFirst5Seconds() {
        WeightProcessor wp;
        installFakeClock(wp);
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
            m_fakeClock += 200;
        }

        QCOMPARE(spy.count(), 0);  // Should NOT trigger in first 5s
    }

    // ===== SAW triggers after 5 seconds =====

    void sawTriggersAfter5Seconds() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);

        QSignalSpy spy(&wp, &WeightProcessor::stopNow);

        // Advance past the 5-second guard
        m_fakeClock += 5500;

        // Then feed rising weight samples to build LSLR
        for (int i = 0; i < 30; i++) {
            double w = 30.0 + i * 0.5;  // Rising from 30g at 0.5g per sample
            wp.processWeight(w);
            m_fakeClock += 200;
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
        installFakeClock(wp);
        configureEspresso(wp, 36.0, preinfuseFrames);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);  // Still in preinfusion

        QSignalSpy spy(&wp, &WeightProcessor::stopNow);

        // Advance past 5s guard, feed rising weight during preinfusion
        m_fakeClock += 5500;
        for (int i = 0; i < 15; i++) {
            double w = 30.0 + i * 0.5;
            wp.processWeight(w);
            m_fakeClock += 200;
        }

        QCOMPARE(spy.count(), 0);  // Should NOT trigger — still in preinfusion frames

        // Now advance past preinfusion and feed rising weight
        wp.setCurrentFrame(preinfuseFrames);

        for (int i = 0; i < 15; i++) {
            double w = 30.0 + i * 0.5;
            wp.processWeight(w);
            m_fakeClock += 200;
        }

        QVERIFY(spy.count() >= 1);  // NOW should trigger
    }

    // ===== SAW does not trigger when targetWeight == 0 =====

    void sawDisabledWhenTargetZero() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 0.0, 0);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);

        QSignalSpy spy(&wp, &WeightProcessor::stopNow);

        m_fakeClock += 5500;
        for (int i = 0; i < 10; i++) {
            wp.processWeight(50.0);
            m_fakeClock += 200;
        }

        QCOMPARE(spy.count(), 0);
    }

    // ===== SAW requires flow rate >= 0.5 =====

    void sawRequiresValidFlow() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);

        QSignalSpy spy(&wp, &WeightProcessor::stopNow);

        // Advance past 5s guard, then feed constant weight (0 flow rate)
        m_fakeClock += 5500;
        for (int i = 0; i < 10; i++) {
            wp.processWeight(40.0);  // Constant weight = 0 flow
            m_fakeClock += 200;
        }

        QCOMPARE(spy.count(), 0);  // Flow too low (LSLR ≈ 0)
    }

    // ===== Per-frame weight exit fires =====

    void perFrameWeightExit() {
        WeightProcessor wp;
        installFakeClock(wp);
        QVector<double> frameExits = {0.0, 0.2, 0.0};  // Frame 1 exits at 0.2g
        QVector<double> learningDrips, learningFlows;
        wp.configure(36.0, 2, frameExits, {}, learningDrips, learningFlows, false, 0.38);
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
        installFakeClock(wp);
        QVector<double> frameExits = {0.0, 0.2, 0.0};
        QVector<double> learningDrips, learningFlows;
        wp.configure(36.0, 2, frameExits, {}, learningDrips, learningFlows, false, 0.38);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(1);

        QSignalSpy spy(&wp, &WeightProcessor::skipFrame);

        wp.processWeight(0.3);
        m_fakeClock += 200;
        wp.processWeight(0.5);  // Still in frame 1, already triggered

        QCOMPARE(spy.count(), 1);  // Only fires once
    }

    // ===== SAW with preinfuseFrameCount == 0 fires immediately (after 5s) =====

    void sawZeroPreinfuseFrames() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);  // No preinfusion
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);  // Frame 0 >= preinfuseFrameCount(0) → past preinfusion

        QSignalSpy spy(&wp, &WeightProcessor::stopNow);

        // Advance past 5s, feed rising weight
        m_fakeClock += 5500;
        for (int i = 0; i < 20; i++) {
            double w = 30.0 + i * 1.0;  // Rising from 30 to 49
            wp.processWeight(w);
            m_fakeClock += 200;
        }

        QVERIFY(spy.count() >= 1);
    }
    // ===== SAW emits sawTriggered with correct data =====

    void sawTriggeredCarriesData() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);

        QSignalSpy stopSpy(&wp, &WeightProcessor::stopNow);
        QSignalSpy sawSpy(&wp, &WeightProcessor::sawTriggered);

        // Advance past 5s, feed rising weight
        m_fakeClock += 5500;
        for (int i = 0; i < 30; i++) {
            double w = 30.0 + i * 0.5;
            wp.processWeight(w);
            m_fakeClock += 200;
        }

        QVERIFY(stopSpy.count() >= 1);
        QVERIFY(sawSpy.count() >= 1);

        // Verify sawTriggered carries: weightAtStop, flowRateAtStop, targetWeight
        QList<QVariant> args = sawSpy.first();
        double weightAtStop = args.at(0).toDouble();
        double flowAtStop = args.at(1).toDouble();
        double target = args.at(2).toDouble();

        QVERIFY(weightAtStop >= 30.0);    // Should be a reasonable weight
        QVERIFY(flowAtStop >= 0.5);       // Flow must be valid (>= 0.5 guard)
        QCOMPARE(target, 36.0);           // Target passed through correctly
    }

    // ===== Untared cup detection =====

    void untaredCupDetected() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);

        QSignalSpy cupSpy(&wp, &WeightProcessor::untaredCupDetected);
        QSignalSpy stopSpy(&wp, &WeightProcessor::stopNow);

        // Feed weight > 50g within first 3 seconds, persisting across enough
        // consecutive samples (UNTARED_CUP_CONFIRM_SAMPLES = 4 — event-based, not
        // a timer) so the popup fires.
        for (int i = 0; i < 3; i++) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 80"));
            wp.processWeight(80.0);
            QCOMPARE(cupSpy.count(), 0);  // Not confirmed yet
            m_fakeClock += 150;
        }
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 80"));
        wp.processWeight(80.0);  // 4th consecutive sample confirms

        QCOMPARE(cupSpy.count(), 1);  // Should detect untared cup once persisted
        QCOMPARE(stopSpy.count(), 0); // Should NOT trigger SAW stop
    }

    // ===== A stale sample that resolves before confirmation does not fire (#1837) =====

    void untaredCupTransientStaleSampleDoesNotFire() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);

        QSignalSpy cupSpy(&wp, &WeightProcessor::untaredCupDetected);

        // Hot Water leaves e.g. 139.5g on the scale; Espresso re-tares, but the
        // scale's zeroed BLE notification lands after extraction has already
        // started, so the stale weight reads for a few arrivals before the real
        // zero arrives. Matches #1837's log: the worst of the three logged
        // incidents saw 3 consecutive stale samples before the real zero — one
        // short of the 4-sample confirmation. That must not show the popup.
        for (int i = 0; i < 3; i++) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 139.5"));
            wp.processWeight(139.5);
            m_fakeClock += 100;
        }
        // The real zero also trips the unrelated spike-rejection guard (a 139.5g
        // drop in one sample), matching #1837's log — not what this test covers.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Spike rejected"));
        wp.processWeight(0.0);  // Real tare-confirmed zero arrives

        QCOMPARE(cupSpy.count(), 0);  // Never reached the 4-sample confirmation
    }

    // ===== A stale reading that keeps recurring after a spike-rejected real zero
    // never fires, even across many alternating cycles (#1838 review finding) =====

    void untaredCupAlternatingReadingsNeverConfirm() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);

        QSignalSpy cupSpy(&wp, &WeightProcessor::untaredCupDetected);

        // Some scales alternate a loaded reading with a near-zero one (documented
        // at weightprocessor.cpp:103-109, "e.g. 200, 0, 200, 0"). Each real zero
        // is a big-enough step to get spike-rejected (so m_lastRawWeight is never
        // updated to it), and the next stale-cached high reading is then accepted
        // normally — so a naive streak counter that only resets on a qualifying
        // "low" sample can be revived by that stale reading forever. The
        // spike-reject path must itself break the streak. 10 cycles, one
        // qualifying sample per cycle — never enough in a row to confirm.
        for (int i = 0; i < 10; i++) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 139.5"));
            wp.processWeight(139.5);
            m_fakeClock += 100;
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Spike rejected"));
            wp.processWeight(0.0);
            m_fakeClock += 100;
        }

        QCOMPARE(cupSpy.count(), 0);  // Popup never fires — the streak keeps resetting
    }

    // ===== A streak starting just before the 3s window still confirms, even if
    // confirmation lands just after it (#1838 review finding) =====

    void untaredCupStreakStartingNearWindowBoundaryStillFires() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);

        QSignalSpy cupSpy(&wp, &WeightProcessor::untaredCupDetected);

        // First qualifying sample lands just inside the 3s window; later
        // confirming samples land just past it. A streak already under way must
        // be allowed to finish confirming, or a late-starting genuine untared cup
        // is silently swallowed instead of showing the popup.
        m_fakeClock += 2850;  // extractionTime = 2.85s — still < 3.0s
        for (int i = 0; i < 3; i++) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 80"));
            wp.processWeight(80.0);
            m_fakeClock += 100;  // 2.85, 2.95, 3.05, 3.15s across the loop + final call
        }
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 80"));
        wp.processWeight(80.0);  // extractionTime = 3.15s — past the window, but streak in progress

        QCOMPARE(cupSpy.count(), 1);  // Confirmed streak fires even though it crossed 3.0s
    }

    // ===== A retare mid-extraction re-arms the popup for a later, genuinely new
    // untared-cup condition (#1838 review finding) =====

    void untaredCupReArmsAfterRetare() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);

        QSignalSpy cupSpy(&wp, &WeightProcessor::untaredCupDetected);

        // First untared-cup condition confirms and fires once.
        for (int i = 0; i < 4; i++) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 90"));
            wp.processWeight(90.0);
            m_fakeClock += 100;
        }
        QCOMPARE(cupSpy.count(), 1);

        // A cup-placed-during-preheat retare mid-extraction (MachineState::
        // flowBeforeAutoTare) resets extraction timing and the tare state.
        wp.resetForRetare();
        wp.markExtractionStart();
        wp.setTareComplete(true);

        // A genuinely new untared-cup condition after the retare must be able to
        // fire the popup again, not stay silenced by the first one's latch.
        for (int i = 0; i < 4; i++) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 90"));
            wp.processWeight(90.0);
            m_fakeClock += 100;
        }
        QCOMPARE(cupSpy.count(), 2);
    }

    // ===== Untared cup does NOT fire after 3 seconds =====

    void untaredCupNotAfter3Seconds() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        wp.startExtraction();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);

        QSignalSpy cupSpy(&wp, &WeightProcessor::untaredCupDetected);

        // Advance past 3 seconds, then feed high weight
        m_fakeClock += 3500;
        wp.processWeight(80.0);
        m_fakeClock += 200;
        wp.processWeight(80.0);

        QCOMPARE(cupSpy.count(), 0);  // Too late for untared cup detection
    }
};

QTEST_GUILESS_MAIN(tst_SAW)
#include "tst_saw.moc"
