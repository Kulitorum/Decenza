#include <QtTest>

#include "usb/serialstall.h"

// The USB serial stall detector's decision, pinned.
//
// This exists because the thing being tested is an ABSENCE in the failure case
// and a single line in the success case, which is the shape nothing notices when
// it breaks. A USB DE1 whose port opens and whose subscribes are written but
// which never notifies produces "Port opened", maybe "Machine info", then
// silence: no WARN, and no disconnect, because the port never closed. Before this
// detector the only evidence of that state was the per-frame RX line — ~600 DEBUG
// lines a shot — which was deleted for volume. If the replacement regresses, the
// symptom is that a broken machine looks like a healthy one, and no test failing
// is exactly what a reader would take as proof it still works.
//
// The detector is tested rather than SerialTransport because the transport is
// not reachable: open() needs a real port, write() early-returns unless
// m_connected, and the production threshold is a minute. See serialstall.h.
//
// This class replaced a free `shouldWarn(bool, bool, qint64, qint64)` function
// whose four parameters were mutually convertible — every argument transposed
// still compiled, and swapping the two durations turned it into a detector that
// warns on the first write instead of never warning at all. Encapsulating the
// state as SerialStall::Detector removed the call site that could get the order
// wrong, and made the arm/disarm/noteInbound lifecycle testable for the first
// time — the free function's tests could only ever pass four loose values in,
// never exercise open/close/resume as a sequence.
class tst_SerialStall : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void warnsOnlyPastTheThreshold_data() {
        QTest::addColumn<qint64>("elapsedMs");
        QTest::addColumn<bool>("expected");

        // Strictly greater than, so a gap exactly equal to the threshold is not yet
        // a stall. Pinned because flipping this to >= is a silent one-character
        // change that makes the detector fire a beat early on every boundary.
        QTest::newRow("well under")     << qint64(0)     << false;
        QTest::newRow("just under")     << qint64(999)   << false;
        QTest::newRow("exactly at")     << qint64(1000)  << false;
        QTest::newRow("just over")      << qint64(1001)  << true;
        QTest::newRow("far over")       << qint64(90000) << true;
    }

    void warnsOnlyPastTheThreshold() {
        QFETCH(qint64, elapsedMs);
        QFETCH(bool, expected);
        SerialStall::Detector d(/*thresholdMs=*/1000);
        d.arm(0);
        QCOMPARE(d.shouldWarn(elapsedMs), expected);
    }

    // The latch. Writes are frequent — DE1Device polls continuously while
    // connected — so an unlatched check would emit a warning per write for as long
    // as the machine stayed silent. That is the flat-repeat pattern the change this
    // belongs to exists to remove, and it would be worst on the one fault the
    // detector was added to report.
    void latchSuppressesRepeats() {
        SerialStall::Detector d(1000);
        d.arm(0);
        QVERIFY(d.shouldWarn(90000));
        QVERIFY(!d.shouldWarn(90000));
        // Still latched on a LATER write too, not just the immediately next one.
        QVERIFY(!d.shouldWarn(200000));
    }

    // Before arm(), there is no port to be stale — the lifecycle case the old
    // free-function test could only fake with a bool, not actually exercise.
    // A never-armed detector must not warn regardless of how large `nowMs` is.
    void neverArmedNeverWarns() {
        SerialStall::Detector d(1000);
        QVERIFY(!d.shouldWarn(90000));
        QVERIFY(!d.shouldWarn(std::numeric_limits<qint64>::max()));
    }

    // disarm() must clear both the arm and any latched warning, so a genuinely
    // NEW stall after a reconnect is not swallowed by the previous connection's
    // latch, and the closed port itself never reads as stale.
    void disarmClearsBothArmAndLatch() {
        SerialStall::Detector d(1000);
        d.arm(0);
        QVERIFY(d.shouldWarn(90000));   // latch a warning
        d.disarm();
        QVERIFY(!d.shouldWarn(90001));  // disarmed: no warning even one tick later
        d.arm(90001);
        QVERIFY(!d.shouldWarn(90500));  // re-armed: fresh clock, well under threshold
        QVERIFY(d.shouldWarn(91002));   // ...and warns again once past it
    }

    // noteInbound() both refreshes the clock (so a write right after resumes the
    // full threshold, not the stale one) and reports whether THIS line is the one
    // that ended a stall — exactly once, matching the "Inbound traffic resumed"
    // line SerialTransport logs only on that transition.
    void noteInboundReportsResumeOnlyOnce() {
        SerialStall::Detector d(1000);
        d.arm(0);
        QVERIFY(d.shouldWarn(90000));       // stalled and warned
        QVERIFY(d.noteInbound(90001));      // this line ends the stall — reported
        QVERIFY(!d.noteInbound(90500));     // the next line is unremarkable
        QVERIFY(!d.shouldWarn(90600));      // clock reset by noteInbound; not stale
    }
};

QTEST_MAIN(tst_SerialStall)
#include "tst_serialstall.moc"
