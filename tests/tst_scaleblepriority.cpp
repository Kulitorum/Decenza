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

    void de1FaultsAfterWindowDoNotTrigger() {
        BlePriorityDetector d;
        d.armWindow(0);
        // First fault arrives after the window has elapsed → ignored, disarmed.
        QVERIFY(!d.onDe1Fault(BlePriorityDetector::kDe1FaultWindowMs + 1));
        QVERIFY(!d.armed());
        QVERIFY(!d.backoffTriggered());
        // Subsequent faults stay inert (window closed, device looks capable).
        for (int i = 0; i < 5; ++i)
            QVERIFY(!d.onDe1Fault(BlePriorityDetector::kDe1FaultWindowMs + 100));
        QVERIFY(!d.skipHighPriority());
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
