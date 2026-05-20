#include <QtTest>

#include "ble/transport/bleprioritydetector.h"

// Connection-priority backoff decision logic (#1093/#1176).
//
// BlePriorityDetector is pure and Qt-free, so the full state machine —
// including the time-dependent window-expiry path — is tested deterministically
// with a caller-supplied millisecond clock. The QtScaleBleTransport that owns
// it only forwards inputs and supplies a real monotonic clock.

class tst_ScaleBlePriority : public QObject {
    Q_OBJECT

private slots:
    void de1FaultClusterTriggersBackoff() {
        BlePriorityDetector d;
        d.armWindow(1000);
        // One below threshold: no backoff yet.
        for (int i = 0; i < BlePriorityDetector::kDe1FaultThreshold - 1; ++i)
            QVERIFY(!d.onDe1Fault(1100));
        QVERIFY(!d.backoffTriggered());
        QVERIFY(!d.skipHighPriority());
        // Crossing the threshold (still inside the window) fires exactly once.
        QVERIFY(d.onDe1Fault(1200));
        QVERIFY(d.backoffTriggered());
        QVERIFY(d.skipHighPriority());
        QVERIFY(!d.armed());
        // Latched: never fires again.
        QVERIFY(!d.onDe1Fault(1300));
        QVERIFY(!d.onScaleStall());
    }

    void isolatedFaultsFarApartNeverTrigger() {
        // Faults spaced further apart than the window each re-anchor a fresh
        // window (count resets to 1) and never accumulate to the threshold.
        BlePriorityDetector d;
        d.armWindow(0);
        const int64_t w = BlePriorityDetector::kDe1FaultWindowMs;
        for (int i = 0; i < 6; ++i) {
            QVERIFY(!d.onDe1Fault(i * (w + 1)));  // each > window past the last
            QVERIFY(!d.backoffTriggered());
        }
        QVERIFY(d.armed());  // still watching — a real cluster could still trip
        QCOMPARE(d.de1FaultCount(), 1);
        QVERIFY(!d.skipHighPriority());
    }

    void doubleFaultAtSameInstantTrips_cascadeContract() {
        // QtScaleBleTransport calls onDe1Fault(nowMs()) TWICE for a
        // "write-failed" cascade. This test pins the same-instant semantics
        // that makes it work: nowMs - m_windowStartMs == 0 is NOT > window,
        // so the second call increments (not re-anchors) and fires.
        // If the boundary ever changes to >= this test catches the regression.
        BlePriorityDetector d;
        d.armWindow(0);
        const int64_t t = 12345;
        QVERIFY(!d.onDe1Fault(t));   // first: anchors window, count=1
        QVERIFY(d.onDe1Fault(t));    // second at same instant: count=2, fires
        QVERIFY(d.backoffTriggered());
        QVERIFY(d.skipHighPriority());
    }

    void regression1238_faultsJustOutsidePriorWindowNowTrip() {
        // #1238 (P80X Android 9): scale requested HIGH; the DE1 emitted a
        // write-failed at t≈10571.6 s and a controller-error at t≈10591.7 s
        // — Δ=20.034 s, missing the prior 20 s window by 34 ms. The widened
        // 60 s window MUST capture this cluster. Calling this out as a
        // regression so any future window narrowing has to confront the log.
        BlePriorityDetector d;
        d.armWindow(0);
        QVERIFY(!d.onDe1Fault(10571649));   // fault 1: write-failed
        QVERIFY(d.onDe1Fault(10591683));    // fault 2: controller-error, Δ=20.034 s
        QVERIFY(d.backoffTriggered());
        QVERIFY(d.skipHighPriority());
    }

    void accumulatesAcrossReconnectsAntiStarvation() {
        // Regression: a flapping weak link (fault, reconnect, fault) must
        // accumulate across reconnects. armWindow() on reconnect must NOT
        // reset the cluster, or the threshold is starved on exactly the
        // hardware this targets.
        BlePriorityDetector d;
        d.armWindow(0);                     // connect
        QVERIFY(!d.onDe1Fault(100));        // fault 1 (windowStart=100)
        d.armWindow(0);                     // reconnect — must not reset count
        QVERIFY(d.onDe1Fault(5000));        // fault 2, within window → fires
        QVERIFY(d.backoffTriggered());
        QVERIFY(d.skipHighPriority());
    }

    void scaleStallBackstopTriggers() {
        BlePriorityDetector d;
        d.armWindow(5000);
        QVERIFY(d.onScaleStall());  // backstop fires immediately while armed
        QVERIFY(d.backoffTriggered());
        QVERIFY(d.skipHighPriority());
    }

    void notArmedNeverBacksOff() {
        // No scale at HIGH (never armed) — idle / no-scale / BALANCED path.
        BlePriorityDetector d;
        for (int i = 0; i < BlePriorityDetector::kDe1FaultThreshold + 3; ++i)
            QVERIFY(!d.onDe1Fault(100));
        QVERIFY(!d.onScaleStall());
        QVERIFY(!d.backoffTriggered());
        QVERIFY(!d.skipHighPriority());
    }

    void skipFlagBlocksArmingAndDetection() {
        BlePriorityDetector d;
        d.setSkipHighPriority(true);   // already backed off earlier this session
        d.armWindow(1000);             // reconnect attempt — must NOT re-arm
        QVERIFY(!d.armed());
        for (int i = 0; i < 5; ++i)
            QVERIFY(!d.onDe1Fault(1100));
        QVERIFY(!d.onScaleStall());
        QCOMPARE(d.de1FaultCount(), 0);
        QVERIFY(d.skipHighPriority());
    }

    void disarmStopsDetection() {
        BlePriorityDetector d;
        d.armWindow(0);
        d.disarm();  // e.g. scale started at BALANCED
        QVERIFY(!d.onDe1Fault(10));
        QVERIFY(!d.onScaleStall());
        QVERIFY(!d.backoffTriggered());
    }

    // --- observe mode (observe-mode change) ---

    // A stall in observe reports a would-fire but NEVER latches/disarms, and
    // keeps reporting on every subsequent independent episode.
    void observeStallRepeatsAndNeverLatches() {
        BlePriorityDetector d;
        d.armWindow(0, /*observe=*/true);
        QVERIFY(d.observing());
        for (int i = 0; i < 5; ++i) {
            QVERIFY(d.onScaleStall());          // would-fire, every time
            QVERIFY(!d.backoffTriggered());     // never latches
            QVERIFY(!d.skipHighPriority());     // never sets skip-HIGH
            QVERIFY(d.armed());                 // stays armed
        }
    }

    // DE1-fault cluster in observe: fires the would-backoff at the same
    // threshold as enforce, but does not latch and re-arms for the next cluster.
    void observeDe1ClusterRepeatsAtSameThreshold() {
        BlePriorityDetector d;
        d.armWindow(0, /*observe=*/true);
        QVERIFY(!d.onDe1Fault(100));            // fault 1 (anchors window)
        QVERIFY(d.onDe1Fault(200));             // fault 2 → would-fire
        QVERIFY(!d.backoffTriggered());
        QVERIFY(!d.skipHighPriority());
        QVERIFY(d.armed());
        // Cluster window was reset by wouldFire → a fresh cluster fires again.
        QVERIFY(!d.onDe1Fault(300));            // fault 1 of next cluster
        QVERIFY(d.onDe1Fault(400));             // fault 2 → would-fire again
        QVERIFY(!d.backoffTriggered());
    }

    // Identical trigger point: the same input sequence reaches the trigger in
    // enforce and observe; only the consequence (latch vs not) differs.
    void observeAndEnforceShareTriggerPoint() {
        BlePriorityDetector en, ob;
        en.armWindow(0);
        ob.armWindow(0, /*observe=*/true);
        QCOMPARE(en.onDe1Fault(100), false);  QCOMPARE(ob.onDe1Fault(100), false);
        QCOMPARE(en.onDe1Fault(200), true);   QCOMPARE(ob.onDe1Fault(200), true);
        QVERIFY(en.backoffTriggered());        // enforce latched
        QVERIFY(!ob.backoffTriggered());       // observe did not
    }

    // Observe ignores a pre-existing skip-HIGH latch (observe forces HIGH at
    // the transport, so detection must NOT be suppressed by a stale latch).
    void observeIgnoresSkipHighGuard() {
        BlePriorityDetector d;
        d.setSkipHighPriority(true);           // simulate a prior latch
        d.armWindow(0, /*observe=*/true);
        QVERIFY(d.armed());                    // armed despite skip-high
        QVERIFY(d.onScaleStall());             // and detecting
        QVERIFY(!d.backoffTriggered());
    }

    // The one-shot "cluster subsided without escalation" notice fires in
    // observe when a window expires with ≥1 fault below threshold, and is
    // cleared on read.
    void observeClusterSubsidedNotice() {
        BlePriorityDetector d;
        d.armWindow(0, /*observe=*/true);
        QVERIFY(!d.onDe1Fault(100));           // 1 fault, window anchored
        QVERIFY(!d.takeObserveClusterSubsided());  // not yet (window open)
        const int64_t past = 100 + BlePriorityDetector::kDe1FaultWindowMs + 1;
        QVERIFY(!d.onDe1Fault(past));          // window expired, re-anchors
        QVERIFY(d.takeObserveClusterSubsided());   // subsided notice set
        QVERIFY(!d.takeObserveClusterSubsided());   // one-shot: cleared on read
    }

    // enforce remains byte-identical: observing() is false and the latch path
    // is unchanged when armed without observe.
    void enforceUnchangedWhenNotObserving() {
        BlePriorityDetector d;
        d.armWindow(0);
        QVERIFY(!d.observing());
        QVERIFY(d.onScaleStall());
        QVERIFY(d.backoffTriggered());
        QVERIFY(d.skipHighPriority());
        QVERIFY(!d.takeObserveClusterSubsided());  // never set in enforce
    }
};

QTEST_GUILESS_MAIN(tst_ScaleBlePriority)
#include "tst_scaleblepriority.moc"
