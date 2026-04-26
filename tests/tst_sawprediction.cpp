#include <QtTest>
#include <QVector>
#include <QtMath>

#include "machine/sawprediction.h"

// Direct unit tests for SawPrediction::weightedDripPrediction.
//
// The σ flow-similarity constant lives in this header and is consumed by
// three sites: WeightProcessor::getExpectedDrip (live SAW threshold) and
// Settings::getExpectedDrip[For] (post-shot learning feedback). Those sites
// are exercised through their own integration tests, but the math itself
// has no direct test — issue #873 flagged the WeightProcessor copy as a
// hand-carried snapshot of the σ math with no σ-specific signal. These
// tests pin the behaviour of the kernel directly so a future regression
// (e.g. resetting σ to 1.5, or zeroing it) is caught at the unit level.

class tst_SawPrediction : public QObject {
    Q_OBJECT

private slots:

    // ===== σ constant lock-in =====

    void sigmaConstantMatchesNarrowedValue() {
        // PR #870 narrowed σ from 1.5 to 0.25 ml/s. Pin the value here so a
        // careless edit is caught even before any kernel test runs.
        QCOMPARE(SawPrediction::kFlowSimilaritySigma, 0.25);
        QCOMPARE(SawPrediction::kFlowSimilaritySigmaSq2, 0.125);
    }

    // ===== flowDiff = 0 baseline =====

    void zeroFlowDiffCollapsesToTrainingDrip() {
        // When every entry's flow matches currentFlowRate exactly, flowWeight=1
        // and the result is the recency-weighted average of the drips. With a
        // constant drip the answer is just that drip — σ is invisible here.
        QVector<double> drips = {2.0, 2.0, 2.0};
        QVector<double> flows = {1.5, 1.5, 1.5};
        const double pred = SawPrediction::weightedDripPrediction(
            drips, flows, 1.5, 10.0, 3.0);
        QVERIFY2(qAbs(pred - 2.0) < 1e-9,
                 qPrintable(QString("expected 2.0, got %1").arg(pred)));
    }

    // ===== σ-narrow attenuation =====

    void farFlowAttenuatesToNaNFallback() {
        // Single entry trained at flow=1.5, queried at flow=2.5. flowDiff=1.0,
        // flowWeight = exp(-1.0/0.125) = exp(-8) ≈ 3.4e-4. recencyWeight = 10.
        // totalWeight ≈ 3.4e-3, below the 0.01 floor → kernel returns NaN so
        // the caller falls back. This is the σ=0.25 signature: at σ=1.5
        // (regression) flowWeight ≈ 0.80 and the kernel would return 2.0.
        QVector<double> drips = {2.0};
        QVector<double> flows = {1.5};
        const double pred = SawPrediction::weightedDripPrediction(
            drips, flows, 2.5, 10.0, 3.0);
        QVERIFY2(qIsNaN(pred),
                 qPrintable(QString("expected NaN fallback, got %1").arg(pred)));
    }

    void wideFlowSpreadSeparatesPredictions() {
        // Two entries straddling the query flow. With σ=0.25 the off-flow
        // entry is attenuated to ~exp(-32) ≈ 0 and the kernel locks onto the
        // matching entry. At σ=1.5 the two would blend to ~mid-range.
        QVector<double> drips = {0.6, 1.8};
        QVector<double> flows = {1.0, 3.0};

        const double low = SawPrediction::weightedDripPrediction(
            drips, flows, 1.0, 10.0, 3.0);
        const double high = SawPrediction::weightedDripPrediction(
            drips, flows, 3.0, 10.0, 3.0);

        QVERIFY2(qAbs(low - 0.6) < 0.05,
                 qPrintable(QString("low query expected ~0.6, got %1").arg(low)));
        QVERIFY2(qAbs(high - 1.8) < 0.05,
                 qPrintable(QString("high query expected ~1.8, got %1").arg(high)));
    }

    // ===== Recency weighting =====

    void recencyWeightingFavoursNewest() {
        // Three entries at the same flow but different drips. The weighted
        // average should pull toward the newest entry (index 0) because
        // recencyMax > recencyMin.
        QVector<double> drips = {1.0, 2.0, 3.0};   // newest is 1.0
        QVector<double> flows = {1.5, 1.5, 1.5};

        const double pred = SawPrediction::weightedDripPrediction(
            drips, flows, 1.5, /*recencyMax=*/10.0, /*recencyMin=*/1.0);

        // Pure mean would be 2.0. Recency-weighted with max=10/min=1 puts
        // more weight on 1.0, so the result should sit below 2.0.
        QVERIFY2(pred < 2.0,
                 qPrintable(QString("recency should pull below mean 2.0, got %1").arg(pred)));
        QVERIFY2(pred > 1.0,
                 qPrintable(QString("recency should not collapse to newest, got %1").arg(pred)));
    }

    void uniformRecencyCollapsesToFlowWeightedMean() {
        // recencyMax == recencyMin → recency drops out and the kernel reduces
        // to a pure flow-weighted average. With identical flows and uniform
        // recency the result is the unweighted mean of the drips.
        QVector<double> drips = {1.0, 2.0, 3.0};
        QVector<double> flows = {1.5, 1.5, 1.5};
        const double pred = SawPrediction::weightedDripPrediction(
            drips, flows, 1.5, /*recencyMax=*/5.0, /*recencyMin=*/5.0);
        QVERIFY2(qAbs(pred - 2.0) < 1e-9,
                 qPrintable(QString("expected mean 2.0, got %1").arg(pred)));
    }

    // ===== Single-entry edge =====

    void singleEntryWithMatchingFlowReturnsThatDrip() {
        // count-1 == 0 → recency denominator is clamped to 1, recencyWeight =
        // recencyMax. flowWeight = 1 → result is the entry's drip (clamped).
        QVector<double> drips = {1.4};
        QVector<double> flows = {2.0};
        const double pred = SawPrediction::weightedDripPrediction(
            drips, flows, 2.0, 10.0, 3.0);
        QVERIFY2(qAbs(pred - 1.4) < 1e-9,
                 qPrintable(QString("expected 1.4, got %1").arg(pred)));
    }

    // ===== Empty input =====

    void emptyInputReturnsNaN() {
        QVector<double> drips;
        QVector<double> flows;
        const double pred = SawPrediction::weightedDripPrediction(
            drips, flows, 2.0, 10.0, 3.0);
        QVERIFY(qIsNaN(pred));
    }

    // ===== Mismatched sizes are a contract violation =====

    void mismatchedVectorSizesReturnNaN() {
        // Callers construct drips and flows in lockstep, but a refactor that
        // accidentally drops one append would be silently undefined without
        // a guard. The kernel returns NaN so the call site lands on its
        // sensor-lag fallback rather than reading past the end of `flows`.
        QVector<double> drips = {1.0, 2.0, 3.0};
        QVector<double> flows = {1.5, 1.5};   // one short
        const double pred = SawPrediction::weightedDripPrediction(
            drips, flows, 1.5, 10.0, 3.0);
        QVERIFY(qIsNaN(pred));
    }

    // ===== Output clamping =====

    void resultClampedToMinDrip() {
        QVector<double> drips = {0.1, 0.2};   // below 0.5 floor
        QVector<double> flows = {1.5, 1.5};
        const double pred = SawPrediction::weightedDripPrediction(
            drips, flows, 1.5, 10.0, 3.0);
        QCOMPARE(pred, SawPrediction::kMinDripPrediction);
    }

    void resultClampedToMaxDrip() {
        QVector<double> drips = {25.0, 30.0};   // above 20.0 ceiling
        QVector<double> flows = {1.5, 1.5};
        const double pred = SawPrediction::weightedDripPrediction(
            drips, flows, 1.5, 10.0, 3.0);
        QCOMPARE(pred, SawPrediction::kMaxDripPrediction);
    }

    // ===== totalWeight floor — all entries far from query flow =====

    void allEntriesFarFromQueryFlowReturnNaN() {
        // Multiple entries all 2 ml/s away from the query → each flowWeight ≈
        // exp(-32). Even with recencyMax=10 the totalWeight stays well under
        // the 0.01 floor.
        QVector<double> drips = {1.0, 2.0, 3.0};
        QVector<double> flows = {0.5, 0.5, 0.5};
        const double pred = SawPrediction::weightedDripPrediction(
            drips, flows, 2.5, 10.0, 3.0);
        QVERIFY2(qIsNaN(pred),
                 qPrintable(QString("expected NaN, got %1").arg(pred)));
    }
};

QTEST_GUILESS_MAIN(tst_SawPrediction)
#include "tst_sawprediction.moc"
