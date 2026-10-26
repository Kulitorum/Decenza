#include <QtTest>

#include "core/stableweightcapture.h"

#include <limits>

namespace {
constexpr auto Off = StableWeightCapture::Arming::Off;
constexpr auto Tracking = StableWeightCapture::Arming::Tracking;
constexpr auto Armed = StableWeightCapture::Arming::Armed;
} // namespace

class tst_StableWeightCapture final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void candidateMovementRestartsBaselineDwell()
    {
        StableWeightCapture capture;
        auto result = capture.update(0.0, 10.0, Armed, 0);
        QVERIFY(result.nextWakeMs == 1000);
        QVERIFY(!result.loadPresent);

        result = capture.update(0.6, 10.0, Armed, 500);
        QVERIFY(result.nextWakeMs == 1500);
        result = capture.update(0.6, 10.0, Armed, 1499);
        QVERIFY(!result.loadPresent);
        QVERIFY(result.nextWakeMs == 1500);

        result = capture.update(0.6, 10.0, Armed, 1500);
        QVERIFY(result.valid);
        QCOMPARE(result.virtualZero, 0.6);
        QVERIFY(!result.loadPresent);
        QVERIFY(!result.nextWakeMs.has_value());
    }

    void loadedWeightCapturesOnceAndMaterialChangeRearms()
    {
        StableWeightCapture capture;
        capture.update(0.0, 10.0, Armed, 0);
        capture.update(0.0, 10.0, Armed, 1000);

        auto result = capture.update(25.0, 10.0, Armed, 1001);
        QVERIFY(result.loadPresent);
        QCOMPARE(result.netWeight, 15.0);
        QVERIFY(result.nextWakeMs == 3501);
        result = capture.update(25.0, 10.0, Armed, 3500);
        QVERIFY(!result.capturedGrams.has_value());
        result = capture.update(25.0, 10.0, Armed, 3501);
        QVERIFY(result.capturedGrams == 15.0);
        QVERIFY(result.captured);
        QVERIFY(!result.rejectedGrams.has_value());

        result = capture.update(25.0, 10.0, Armed, 4000);
        QVERIFY(!result.capturedGrams.has_value());
        QVERIFY(result.captured);

        result = capture.update(31.1, 10.0, Armed, 4001);
        QVERIFY(!result.captured);
        QVERIFY(result.nextWakeMs == 6501);
        result = capture.update(31.1, 10.0, Armed, 6501);
        QVERIFY(result.capturedGrams == 21.1);
        QVERIFY(result.captured);
    }

    void rejectedLoadNotifiesOnceUntilCandidateMoves()
    {
        StableWeightCapture capture;
        capture.update(0.0, 1.0, Armed, 0);
        capture.update(0.0, 1.0, Armed, 1000);
        capture.update(5.5, 1.0, Armed, 1001);

        auto result = capture.update(5.5, 1.0, Armed, 3501);
        QVERIFY(result.rejectedGrams == 4.5);
        QVERIFY(!result.captured);
        result = capture.update(5.5, 1.0, Armed, 5000);
        QVERIFY(!result.rejectedGrams.has_value());

        result = capture.update(4.75, 1.0, Armed, 5001);
        QVERIFY(result.nextWakeMs == 7501);
        result = capture.update(4.75, 1.0, Armed, 7501);
        QVERIFY(result.rejectedGrams == 3.75);
    }

    void emptyBaselineDriftReadoptsZero()
    {
        StableWeightCapture capture;
        capture.update(-2.0, 0.0, Armed, 0);
        auto result = capture.update(-2.0, 0.0, Armed, 1000);
        QCOMPARE(result.virtualZero, -2.0);

        result = capture.update(-1.4, 0.0, Armed, 1001);
        QVERIFY(result.nextWakeMs == 2001);
        result = capture.update(-1.4, 0.0, Armed, 2001);
        QCOMPARE(result.virtualZero, -1.4);
    }

    // #2058's log: a 150 g pitcher of milk tared on a scale that reads -450 g empty,
    // one pre-tare sample still in flight before the scale reports its new zero.
    void tareMovesTheEmptyReadingWithTheZero_data()
    {
        QTest::addColumn<double>("afterTare");
        QTest::addColumn<double>("zero");
        QTest::addColumn<bool>("loadPresent");
        QTest::newRow("tare lands") << 0.0 << -150.0 << true;
        QTest::newRow("pitcher lifted before the tare lands") << -450.0 << -450.0 << false;
    }

    void tareMovesTheEmptyReadingWithTheZero()
    {
        QFETCH(double, afterTare);
        QFETCH(double, zero);
        QFETCH(bool, loadPresent);
        StableWeightCapture capture;
        StableWeightCapture::Config milk;
        milk.minNet = 50.0;
        milk.maxNet = 1500.0;
        QVERIFY(capture.setConfig(milk));
        capture.update(-450.0, 50.0, Armed, 0);
        capture.update(-450.0, 50.0, Armed, 1000);
        capture.update(-300.0, 50.0, Armed, 1001);

        capture.tare(1050);
        QVERIFY(capture.tarePending());
        auto result = capture.update(-300.2, 50.0, Armed, 1100);
        QVERIFY(capture.tarePending());
        QCOMPARE(result.netWeight, 100.0 - 0.2);

        result = capture.update(afterTare, 50.0, Armed, 1200);
        QVERIFY(!capture.tarePending());
        if (loadPresent) {
            QCOMPARE(result.virtualZero, zero);
            QCOMPARE(result.netWeight, 100.0);
            // The tare did not restart the 2.5 s dwell begun at 1001.
            result = capture.update(afterTare, 50.0, Armed, 3501);
            QVERIFY(result.capturedGrams == 100.0);
        } else {
            // Unconfirmed: nothing kept, the empty reading is re-learned.
            QVERIFY(!result.seeded);
            result = capture.update(afterTare, 50.0, Armed, 2200);
            QCOMPARE(result.virtualZero, zero);
            QVERIFY(!result.loadPresent);
        }
    }

    // The scale dropped the tare: its reading never moved. The pitcher placed
    // afterwards is measured from the empty reading, not learned as it.
    void tareThatNeverLandsKeepsTheEmptyReading()
    {
        StableWeightCapture capture;
        capture.update(-450.0, 50.0, Armed, 0);
        capture.update(-450.0, 50.0, Armed, 1000);
        capture.tare(1100);
        capture.update(-450.0, 50.0, Armed, 7100);
        QVERIFY(!capture.tarePending());
        const auto result = capture.update(-300.0, 50.0, Armed, 7200);
        QCOMPARE(result.virtualZero, -450.0);
        QCOMPARE(result.netWeight, 100.0);
    }

    // A zero whose first sample arrives after the window still lands.
    void lateTareZeroStillLands()
    {
        StableWeightCapture capture;
        capture.update(-450.0, 50.0, Armed, 0);
        capture.update(-450.0, 50.0, Armed, 1000);
        capture.update(-300.0, 50.0, Armed, 1001);
        capture.tare(1050);
        const auto result = capture.update(0.0, 50.0, Armed, 7100);
        QVERIFY(!capture.tarePending());
        QCOMPARE(result.netWeight, 100.0);
    }

    // A pre-tare reading within 1 g of the tared zero still confirms the tare.
    void tareOfASmallReadingLandsAtZero()
    {
        StableWeightCapture capture;
        capture.update(1.1, 50.0, Armed, 0);
        capture.update(1.1, 50.0, Armed, 1000);
        capture.tare(1050);
        capture.update(0.1, 50.0, Armed, 1100);
        QVERIFY(!capture.tarePending());
        // The tared empty scale settles at 0.1 and is re-learned there.
        const auto result = capture.update(150.1, 50.0, Armed, 1200);
        QCOMPARE(result.netWeight, 100.0);
    }

    void tareOfAZeroReadingKeepsTheEmptyReading()
    {
        StableWeightCapture capture;
        capture.update(0.3, 50.0, Armed, 0);
        capture.update(0.3, 50.0, Armed, 1000);
        capture.tare(1050);
        QVERIFY(!capture.tarePending());
        const auto result = capture.update(150.3, 50.0, Armed, 1100);
        QCOMPARE(result.virtualZero, 0.3);
        QCOMPARE(result.netWeight, 100.0);
    }

    void invalidSamplesAndBackwardsTimeDoNotMutateState()
    {
        StableWeightCapture capture;
        auto result = capture.update(0.0, 5.0, Armed, 100);
        QVERIFY(result.valid);
        const auto invalidWeight = capture.update(std::numeric_limits<double>::quiet_NaN(),
                                                  5.0, Armed, 200);
        QVERIFY(!invalidWeight.valid);
        QCOMPARE(invalidWeight.virtualZero, 0.0);
        QVERIFY(!invalidWeight.nextWakeMs.has_value());

        result = capture.update(0.0, 5.0, Armed, 1100);
        QCOMPARE(result.virtualZero, 0.0);
        const auto backwards = capture.update(25.0, 5.0, Armed, 1099);
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

    void offClearsCaptureAndRequiresFreshBaseline()
    {
        StableWeightCapture capture;
        capture.update(0.0, 10.0, Armed, 0);
        capture.update(0.0, 10.0, Armed, 1000);
        capture.update(25.0, 10.0, Armed, 1001);
        const auto captured = capture.update(25.0, 10.0, Armed, 3501);
        QVERIFY(captured.captured);

        auto result = capture.update(25.0, 10.0, Off, 3600);
        QVERIFY(!result.captured);
        QVERIFY(!result.loadPresent);
        QVERIFY(!result.nextWakeMs.has_value());
        result = capture.update(25.0, 10.0, Armed, 3601);
        QVERIFY(!result.loadPresent);
        QVERIFY(result.nextWakeMs == 4601);
        result = capture.update(25.0, 10.0, Armed, 4601);
        QCOMPARE(result.virtualZero, 25.0);
        QVERIFY(!result.loadPresent);
    }

    // #2058: an un-zeroed scale (-450 g empty) with the load placed before capture is
    // armed. Tracking learns the empty reading but never captures; arming measures the
    // load from it after a fresh dwell instead of adopting the load as the zero.
    void trackingLearnsEmptyReadingSoAnEarlierLoadIsMeasuredWhenArmed()
    {
        StableWeightCapture capture;
        capture.update(-450.0, 10.0, Tracking, 0);
        auto result = capture.update(-450.0, 10.0, Tracking, 1000);
        QVERIFY(result.seeded);
        QCOMPARE(result.virtualZero, -450.0);

        result = capture.update(-425.0, 10.0, Tracking, 1001);
        QVERIFY(result.loadPresent);
        QCOMPARE(result.netWeight, 15.0);
        QVERIFY(!result.nextWakeMs.has_value());
        result = capture.update(-425.0, 10.0, Tracking, 9000);
        QVERIFY(!result.capturedGrams.has_value());
        QVERIFY(!result.rejectedGrams.has_value());

        result = capture.update(-425.0, 10.0, Armed, 9001);
        QCOMPARE(result.virtualZero, -450.0);
        QVERIFY(result.nextWakeMs == 11501);
        result = capture.update(-425.0, 10.0, Armed, 11501);
        QVERIFY(result.capturedGrams == 15.0);

        // Disarming drops the latch, so re-arming over the same load captures again.
        capture.update(-425.0, 10.0, Tracking, 11600);
        result = capture.update(-425.0, 10.0, Armed, 11601);
        QVERIFY(!result.captured);
        result = capture.update(-425.0, 10.0, Armed, 14101);
        QVERIFY(result.capturedGrams == 15.0);
    }
};

QTEST_GUILESS_MAIN(tst_StableWeightCapture)

#include "tst_stableweightcapture.moc"
