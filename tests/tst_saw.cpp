#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>

#include "machine/weightprocessor.h"
#include "support/TareWaitTestHelpers.h"

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
        TareWait::armExtraction(wp, m_fakeClock);
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
        TareWait::armExtraction(wp, m_fakeClock);
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
        TareWait::armExtraction(wp, m_fakeClock);
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
        TareWait::armExtraction(wp, m_fakeClock);
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
        TareWait::armExtraction(wp, m_fakeClock);
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
        TareWait::armExtraction(wp, m_fakeClock);
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
        TareWait::armExtraction(wp, m_fakeClock);
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
        TareWait::armExtraction(wp, m_fakeClock);
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
        TareWait::armExtraction(wp, m_fakeClock);
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
        TareWait::armExtractionUntared(wp, 80.0, m_fakeClock);  // grace spent, no zero ever arrived
        wp.setCurrentFrame(0);

        QSignalSpy cupSpy(&wp, &WeightProcessor::untaredCupDetected);
        QSignalSpy stopSpy(&wp, &WeightProcessor::stopNow);

        // Past the grace the reading is judged, and the cup is the verdict. The popup
        // still needs it to repeat (UNTARED_CUP_CONFIRM_SAMPLES = 2 — one corrupt
        // packet must not accuse the user), so the first warned sample does not fire.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 80"));
        wp.processWeight(80.0);
        QCOMPARE(cupSpy.count(), 0);  // Not confirmed yet
        m_fakeClock += 150;

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 80"));
        wp.processWeight(80.0);  // second consecutive sample confirms

        QCOMPARE(cupSpy.count(), 1);  // Should detect untared cup once persisted
        QCOMPARE(stopSpy.count(), 0); // Should NOT trigger SAW stop
    }

    // ===== A stale sample that resolves before confirmation does not fire (#1837) =====

    void untaredCupTransientStaleSampleDoesNotFire() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        wp.startExtraction();
        wp.markExtractionStart();  // DE1 starts flow; the app's tare is still in flight
        wp.setTareComplete(true);
        wp.setCurrentFrame(0);

        QSignalSpy cupSpy(&wp, &WeightProcessor::untaredCupDetected);

        // Hot Water leaves e.g. 139.5g on the scale; Espresso re-tares, but the
        // scale's zeroed BLE notification lands after extraction has already started,
        // so the stale weight reads for a few arrivals before the real zero arrives.
        // Matches #1837's log: the worst of the three logged incidents saw 3
        // consecutive stale samples before the real zero.
        //
        // NOTHING here may warn, and init()'s failOnWarning() enforces it. That is the
        // whole fix: these samples are pre-tare, so calling them an untared cup was a
        // wrong verdict, and the drop to zero is the app's own tare rather than a
        // spike. Both used to be reported, twice over, on every shot poured into a cup
        // the previous pour had left heavy.
        for (int i = 0; i < 3; i++) {
            wp.processWeight(139.5);
            m_fakeClock += 100;
        }
        wp.processWeight(0.0);  // Real zero arrives — held, one packet is not proof
        m_fakeClock += 100;
        wp.processWeight(0.0);  // Confirmed: the tare landed

        QCOMPARE(cupSpy.count(), 0);
    }

    // ===== A stale reading that keeps recurring after a spike-rejected real zero
    // never fires, even across many alternating cycles (#1838 review finding) =====

    void untaredCupAlternatingReadingsNeverConfirm() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        TareWait::armExtractionUntared(wp, 139.5, m_fakeClock);  // grace spent on the stale reading
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
        TareWait::armExtractionUntared(wp, 80.0, m_fakeClock);  // grace spent, no zero ever arrived
        wp.setCurrentFrame(0);

        QSignalSpy cupSpy(&wp, &WeightProcessor::untaredCupDetected);

        // First qualifying sample lands just inside the 3s window; the confirming one
        // lands just past it. A streak already under way must be allowed to finish
        // confirming, or a late-starting genuine untared cup is silently swallowed
        // instead of showing the popup.
        // 2.95 s past the window's anchor, which is the LAST GRANTED arrival, not flow
        // start — burnGrace leaves the clock one interval past it.
        m_fakeClock += 2950 - 100;

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 80"));
        wp.processWeight(80.0);  // 2.95s — starts the streak just inside the window
        m_fakeClock += 100;

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 80"));
        wp.processWeight(80.0);  // 3.05s — past the window, but the streak is in progress

        QCOMPARE(cupSpy.count(), 1);  // Confirmed streak fires even though it crossed 3.0s
    }

    // ===== A retare mid-extraction re-arms the popup for a later, genuinely new
    // untared-cup condition (#1838 review finding) =====

    void untaredCupReArmsAfterRetare() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        TareWait::armExtractionUntared(wp, 90.0, m_fakeClock);  // grace spent, no zero ever arrived
        wp.setCurrentFrame(0);

        QSignalSpy cupSpy(&wp, &WeightProcessor::untaredCupDetected);

        // First untared-cup condition confirms and fires once.
        for (int i = 0; i < 2; i++) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 90"));
            wp.processWeight(90.0);
            m_fakeClock += 100;
        }
        QCOMPARE(cupSpy.count(), 1);

        // A retare resets extraction timing and the tare state — including a fresh
        // tare wait, so the grace has to be spent again. A state-machine exercise: the
        // preheat retare this mirrors (MachineState::flowBeforeAutoTare) is gated on a
        // pre-flow substate, so production reaches this state before flow, not after.
        wp.resetForRetare();
        wp.markExtractionStart();
        wp.setTareComplete(true);
        TareWait::burnGrace(wp, 90.0, m_fakeClock);

        // A genuinely new untared-cup condition after the retare must be able to
        // fire the popup again, not stay silenced by the first one's latch.
        for (int i = 0; i < 2; i++) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 90"));
            wp.processWeight(90.0);
            m_fakeClock += 100;
        }
        QCOMPARE(cupSpy.count(), 2);
    }

    // ===== The detection window survives a slow scale (2 Hz) =====

    void untaredCupStillDetectedOnASlowScale() {
        // The grace is bounded in ARRIVALS and the detection window in SECONDS, so on a
        // 2 Hz scale six granted arrivals are 3.0 s — the whole window, spent before a
        // single sample could be judged. Measured from flow start, a genuinely untared
        // cup would then never warn and never raise the popup, and only on the slowest
        // scales, which is the shape of bug that reaches users and not tests. The window
        // is anchored to the end of the tare wait for exactly this reason.
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);

        TareWait::armExtractionUntared(wp, 80.0, m_fakeClock, /*intervalMs*/ 500);
        wp.setCurrentFrame(0);
        QSignalSpy cupSpy(&wp, &WeightProcessor::untaredCupDetected);

        // 3.0 s into extraction already — every one of those arrivals was pre-tare.
        for (int i = 0; i < 2; i++) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 80"));
            wp.processWeight(80.0);
            m_fakeClock += 500;
        }

        QCOMPARE(cupSpy.count(), 1);
    }

    // ===== The window cannot drift into the pour, in either direction =====

    void untaredCupWindowDoesNotReopenAfterAStallPastGrace() {
        // The window anchor is the LAST GRANTED arrival, not the arrival that ends the
        // wait. Those differ whenever the feed goes quiet in between, and anchoring on
        // the latter would restart a 3 s window wherever the feed happened to resume.
        // An earlier revision of this fix did exactly that.
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);

        TareWait::armExtractionUntared(wp, 80.0, m_fakeClock);
        wp.setCurrentFrame(0);
        QSignalSpy cupSpy(&wp, &WeightProcessor::untaredCupDetected);

        m_fakeClock += 5000;  // feed stalls right after the last granted arrival
        wp.processWeight(80.0);
        m_fakeClock += 500;
        wp.processWeight(80.0);

        QCOMPARE(cupSpy.count(), 0);  // window closed where judging became possible
    }

    void untaredCupWindowIsClampedAgainstALateAnchor() {
        // Late is the dangerous direction: a >50 g reading deep into a pour is real
        // coffee on a large target, and the verdict LATCHES — one false positive
        // freezes the streak, which holds the window open and makes every later heavy
        // sample return ahead of the SAW stop. Stop-at-weight would be off for the rest
        // of the shot. The anchor is clamped so the window cannot reach that far.
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 60.0, 0);  // target above the 50 g sanity bar
        wp.setCurrentFrame(0);

        // Six granted arrivals dragged out over 8 s by a limping feed. The arithmetic is
        // the test: unclamped the anchor lands at 8.0 s and the samples below fall 1.6 s
        // and 2.1 s into its window, so the popup fires on real coffee. Clamped, the
        // anchor is pinned at 4.0 s, its window shuts at 7.0 s, and both samples are
        // outside it. Get this spacing wrong in either direction and the test passes
        // whether or not the clamp exists — the first draft of it did.
        TareWait::armExtractionUntared(wp, 55.0, m_fakeClock, /*intervalMs*/ 1600);
        QSignalSpy cupSpy(&wp, &WeightProcessor::untaredCupDetected);

        wp.processWeight(55.0);
        m_fakeClock += 500;
        wp.processWeight(55.0);

        QCOMPARE(cupSpy.count(), 0);
    }

    void untaredCupWindowAnchorsToFlowStartAfterALongPreheat() {
        // The tare normally lands during preheat, well before flow. The anchor is then
        // OLDER than flow start and qMax must discard it — without that the window
        // would already read as expired at flow start, on every ordinary shot.
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);

        // No setCurrentFrame() here: it is what drives the scale-feed stall check, and a
        // 30 s preheat with no samples is a stall by that check's reckoning. Unrelated
        // subsystem, and the sanity check does not read the frame.
        TareWait::armExtraction(wp, m_fakeClock, /*preheatGapMs*/ 30000);
        QSignalSpy cupSpy(&wp, &WeightProcessor::untaredCupDetected);

        for (int i = 0; i < 2; i++) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sanity check: weight 80"));
            wp.processWeight(80.0);
            m_fakeClock += 100;
        }

        QCOMPARE(cupSpy.count(), 1);
    }

    // ===== Untared cup does NOT fire after 3 seconds =====

    void untaredCupNotAfter3Seconds() {
        WeightProcessor wp;
        installFakeClock(wp);
        configureEspresso(wp, 36.0, 0);
        TareWait::armExtraction(wp, m_fakeClock);
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
