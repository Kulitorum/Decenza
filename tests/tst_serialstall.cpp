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
// The predicate is tested rather than SerialTransport because the transport is
// not reachable: open() needs a real port, write() early-returns unless
// m_connected, and the production threshold is a minute. See serialstall.h.
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
        QCOMPARE(SerialStall::shouldWarn(/*alreadyWarned=*/false, /*livenessValid=*/true,
                                         elapsedMs, /*thresholdMs=*/1000),
                 expected);
    }

    // The latch. Writes are frequent — DE1Device polls continuously while
    // connected — so an unlatched check would emit a warning per write for as long
    // as the machine stayed silent. That is the flat-repeat pattern the change this
    // belongs to exists to remove, and it would be worst on the one fault the
    // detector was added to report.
    void latchSuppressesRepeats() {
        QVERIFY(SerialStall::shouldWarn(false, true, 90000, 1000));
        QVERIFY(!SerialStall::shouldWarn(true, true, 90000, 1000));
    }

    // An invalid liveness timer means no port has been open yet, or it has closed.
    // Without this gate a default-constructed QElapsedTimer reads as an unbounded
    // stall, so simply constructing a SerialTransport — or closing one — would warn
    // that a machine had gone silent when there was never a machine.
    void invalidLivenessNeverWarns() {
        QVERIFY(!SerialStall::shouldWarn(false, /*livenessValid=*/false, 90000, 1000));
        // Not rescued by an absurd elapsed value either.
        QVERIFY(!SerialStall::shouldWarn(false, false, std::numeric_limits<qint64>::max(), 1000));
    }
};

QTEST_MAIN(tst_SerialStall)
#include "tst_serialstall.moc"
