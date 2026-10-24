#include <QtTest>

#include "core/stableweightcapture.h"

#include <limits>

class tst_StableWeightCapture final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void candidateMovementRestartsBaselineDwell()
    {
        StableWeightCapture capture;
        auto result = capture.update(0.0, 10.0, true, 0);
        QVERIFY(result.nextWakeMs == 1000);
        QVERIFY(!result.loadPresent);

        result = capture.update(0.6, 10.0, true, 500);
        QVERIFY(result.nextWakeMs == 1500);
        result = capture.update(0.6, 10.0, true, 1499);
        QVERIFY(!result.loadPresent);
        QVERIFY(result.nextWakeMs == 1500);

        result = capture.update(0.6, 10.0, true, 1500);
        QVERIFY(result.valid);
        QCOMPARE(result.virtualZero, 0.6);
        QVERIFY(!result.loadPresent);
        QVERIFY(!result.nextWakeMs.has_value());
    }

    void loadedWeightCapturesOnceAndMaterialChangeRearms()
    {
        StableWeightCapture capture;
        capture.update(0.0, 10.0, true, 0);
        capture.update(0.0, 10.0, true, 1000);

        auto result = capture.update(25.0, 10.0, true, 1001);
        QVERIFY(result.loadPresent);
        QCOMPARE(result.netWeight, 15.0);
        QVERIFY(result.nextWakeMs == 3501);
        result = capture.update(25.0, 10.0, true, 3500);
        QVERIFY(!result.capturedGrams.has_value());
        result = capture.update(25.0, 10.0, true, 3501);
        QVERIFY(result.capturedGrams == 15.0);
        QVERIFY(result.captured);
        QVERIFY(!result.rejectedGrams.has_value());

        result = capture.update(25.0, 10.0, true, 4000);
        QVERIFY(!result.capturedGrams.has_value());
        QVERIFY(result.captured);

        result = capture.update(31.1, 10.0, true, 4001);
        QVERIFY(!result.captured);
        QVERIFY(result.nextWakeMs == 6501);
        result = capture.update(31.1, 10.0, true, 6501);
        QVERIFY(result.capturedGrams == 21.1);
        QVERIFY(result.captured);
    }

    void rejectedLoadNotifiesOnceUntilCandidateMoves()
    {
        StableWeightCapture capture;
        capture.update(0.0, 1.0, true, 0);
        capture.update(0.0, 1.0, true, 1000);
        capture.update(5.5, 1.0, true, 1001);

        auto result = capture.update(5.5, 1.0, true, 3501);
        QVERIFY(result.rejectedGrams == 4.5);
        QVERIFY(!result.captured);
        result = capture.update(5.5, 1.0, true, 5000);
        QVERIFY(!result.rejectedGrams.has_value());

        result = capture.update(4.75, 1.0, true, 5001);
        QVERIFY(result.nextWakeMs == 7501);
        result = capture.update(4.75, 1.0, true, 7501);
        QVERIFY(result.rejectedGrams == 3.75);
    }

    void emptyBaselineDriftAndResetReadoptZero()
    {
        StableWeightCapture capture;
        capture.update(-2.0, 0.0, true, 0);
        auto result = capture.update(-2.0, 0.0, true, 1000);
        QCOMPARE(result.virtualZero, -2.0);

        result = capture.update(-1.4, 0.0, true, 1001);
        QVERIFY(result.nextWakeMs == 2001);
        result = capture.update(-1.4, 0.0, true, 2001);
        QCOMPARE(result.virtualZero, -1.4);

        result = capture.update(40.0, 0.0, true, 2002);
        QVERIFY(result.loadPresent);
        result = capture.reset(2100);
        QVERIFY(!result.captured);
        QVERIFY(!result.loadPresent);
        QCOMPARE(result.virtualZero, -1.4); // Previous zero remains diagnostic until re-adoption.
        QVERIFY(result.nextWakeMs == 3100);
        result = capture.update(40.0, 0.0, true, 3100);
        QCOMPARE(result.virtualZero, 40.0);
        QVERIFY(!result.loadPresent);
    }

    void invalidSamplesAndBackwardsTimeDoNotMutateState()
    {
        StableWeightCapture capture;
        auto result = capture.update(0.0, 5.0, true, 100);
        QVERIFY(result.valid);
        const auto invalidWeight = capture.update(std::numeric_limits<double>::quiet_NaN(),
                                                  5.0, true, 200);
        QVERIFY(!invalidWeight.valid);
        QCOMPARE(invalidWeight.virtualZero, 0.0);
        QVERIFY(!invalidWeight.nextWakeMs.has_value());

        result = capture.update(0.0, 5.0, true, 1100);
        QCOMPARE(result.virtualZero, 0.0);
        const auto backwards = capture.update(25.0, 5.0, true, 1099);
        QVERIFY(!backwards.valid);
        QVERIFY(backwards.error.contains(QStringLiteral("monotonic")));
        QCOMPARE(backwards.loadPresent, false);

        StableWeightCapture::Config invalidConfig;
        invalidConfig.maxNet = -1.0;
        QString error;
        QVERIFY(!capture.setConfig(invalidConfig, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(capture.config().maxNet, 45.0);
    }

    void deactivationClearsCaptureAndRequiresFreshBaseline()
    {
        StableWeightCapture capture;
        capture.update(0.0, 10.0, true, 0);
        capture.update(0.0, 10.0, true, 1000);
        capture.update(25.0, 10.0, true, 1001);
        const auto captured = capture.update(25.0, 10.0, true, 3501);
        QVERIFY(captured.captured);

        auto result = capture.update(25.0, 10.0, false, 3600);
        QVERIFY(!result.captured);
        QVERIFY(!result.loadPresent);
        QVERIFY(!result.nextWakeMs.has_value());
        result = capture.update(25.0, 10.0, true, 3601);
        QVERIFY(!result.loadPresent);
        QVERIFY(result.nextWakeMs == 4601);
        result = capture.update(25.0, 10.0, true, 4601);
        QCOMPARE(result.virtualZero, 25.0);
        QVERIFY(!result.loadPresent);
    }
};

QTEST_GUILESS_MAIN(tst_StableWeightCapture)

#include "tst_stableweightcapture.moc"
