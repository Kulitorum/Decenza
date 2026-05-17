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
};

QTEST_GUILESS_MAIN(tst_ScaleBlePriority)
#include "tst_scaleblepriority.moc"
