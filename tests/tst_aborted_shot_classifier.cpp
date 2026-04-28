// Tests for the aborted-shot classifier (issue #899).
//
// The classifier decides whether an espresso shot "did not start" and should
// therefore be dropped from history. The fixtures below come from a dry-run
// against an 882-shot corpus; positive cases are real "did not start" shots
// and negative cases are real shots that must be preserved.

#include "controllers/abortedshotclassifier.h"

#include <QtTest/QtTest>

using decenza::isAbortedShot;
using decenza::kAbortedDurationSec;
using decenza::kAbortedYieldG;

class tst_AbortedShotClassifier : public QObject {
    Q_OBJECT

private slots:

    // Real shots from Jeff's local DB that the classifier should drop.
    void corpusPositives_data() {
        QTest::addColumn<double>("durationSec");
        QTest::addColumn<double>("finalWeightG");
        // Shot 885: canonical preinfusion abort, peak pressure 0.35 bar.
        QTest::newRow("shot 885 (preinfusion abort)") << 2.284 << 1.1;
        // Shot 17: 2.2 s, no yield reached the cup.
        QTest::newRow("shot 17 (early stop)")          << 2.225 << 0.0;
        // Shot 850: 6 s, never built pressure.
        QTest::newRow("shot 850 (no flow)")            << 6.067 << 0.2;
        // Shot 836: 6 s, near-zero yield.
        QTest::newRow("shot 836 (near zero)")          << 6.049 << 0.8;
        // Shot 1: 0.5 s of actual frame execution per phaseSummaries.
        QTest::newRow("shot 1 (extraction 0.5s)")      << 0.5   << 0.1;
    }

    void corpusPositives() {
        QFETCH(double, durationSec);
        QFETCH(double, finalWeightG);
        QVERIFY2(isAbortedShot(durationSec, finalWeightG),
                 qPrintable(QString("expected aborted for dur=%1 weight=%2")
                                .arg(durationSec).arg(finalWeightG)));
    }

    // Real shots that the classifier must NOT drop — these are diagnostically
    // valuable (long-low-yield chokes show flat pressure traces operators dial
    // against) or normal turbo-style shots.
    void corpusNegatives_data() {
        QTest::addColumn<double>("durationSec");
        QTest::addColumn<double>("finalWeightG");
        // Shot 890: 60-s choke producing 1.1 g — exactly the bad-puck-prep
        // signal we want to preserve.
        QTest::newRow("shot 890 (60s choke)")    << 59.554 << 1.1;
        // Shot 708: another long choke.
        QTest::newRow("shot 708 (57s choke)")    << 56.594 << 2.4;
        // Shot 732: 43 s, near-zero yield.
        QTest::newRow("shot 732 (43s choke)")    << 43.09  << 0.9;
        // Shot 117: 133 s of pumping for 3.8 g — extreme choke, must keep.
        QTest::newRow("shot 117 (133s choke)")   << 133.108 << 3.8;
        // Shot 868: legitimate turbo extraction, 7 s / 37 g.
        QTest::newRow("shot 868 (turbo)")        << 7.271  << 37.4;
    }

    void corpusNegatives() {
        QFETCH(double, durationSec);
        QFETCH(double, finalWeightG);
        QVERIFY2(!isAbortedShot(durationSec, finalWeightG),
                 qPrintable(QString("expected kept for dur=%1 weight=%2")
                                .arg(durationSec).arg(finalWeightG)));
    }

    // Boundary checks: both clauses use strict <, so values exactly at the
    // threshold must NOT classify as aborted.
    void boundaries() {
        QVERIFY(!isAbortedShot(kAbortedDurationSec, 0.0));   // dur exactly 10.0 → kept
        QVERIFY(!isAbortedShot(0.0, kAbortedYieldG));         // yield exactly 5.0 → kept
        QVERIFY(!isAbortedShot(kAbortedDurationSec, kAbortedYieldG));  // both exact → kept
        QVERIFY(isAbortedShot(kAbortedDurationSec - 0.001, kAbortedYieldG - 0.001));  // both just under → aborted

        // Single-clause failures — the conjunction requires BOTH.
        QVERIFY(!isAbortedShot(5.0, 10.0));   // short but real yield → kept
        QVERIFY(!isAbortedShot(30.0, 1.0));   // long but no yield → kept (diagnostic)
    }

    // The classifier itself is a pure function. The toggle that gates whether
    // a shot's verdict actually causes a drop lives in MainController; this
    // test documents that the function does not consult any global state.
    void noHiddenState() {
        // Same inputs → same output, regardless of how many times we call it.
        for (int i = 0; i < 5; ++i) {
            QVERIFY(isAbortedShot(2.0, 1.0));
            QVERIFY(!isAbortedShot(20.0, 30.0));
        }
    }
};

QTEST_GUILESS_MAIN(tst_AbortedShotClassifier)
#include "tst_aborted_shot_classifier.moc"
