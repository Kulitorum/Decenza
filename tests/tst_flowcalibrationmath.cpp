#include <QtTest>
#include <limits>

#include "../src/models/flowcalibrationmath.h"

class tst_FlowCalibrationMath : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void multiplierBoundsAreFiniteAndInclusive()
    {
        QVERIFY(FlowCalibrationMath::validMultiplier(0.35));
        QVERIFY(FlowCalibrationMath::validMultiplier(3.0));
        QVERIFY(FlowCalibrationMath::validMultiplier(1.0));
        QVERIFY(!FlowCalibrationMath::validMultiplier(0.349));
        QVERIFY(!FlowCalibrationMath::validMultiplier(3.001));
        QVERIFY(!FlowCalibrationMath::validMultiplier(std::numeric_limits<double>::quiet_NaN()));
        QVERIFY(!FlowCalibrationMath::validMultiplier(std::numeric_limits<double>::infinity()));
    }

    void saveGuardRequiresCurrentRevisionAndValidMultiplier()
    {
        QVERIFY(FlowCalibrationMath::validSaveRequest(1.25, 9, 9));
        QVERIFY(!FlowCalibrationMath::validSaveRequest(1.25, 8, 9));
        QVERIFY(!FlowCalibrationMath::validSaveRequest(0.1, 9, 9));
        QVERIFY(!FlowCalibrationMath::validSaveRequest(
            std::numeric_limits<double>::quiet_NaN(), 9, 9));
    }

    void replayUsesRecordedMultiplierAndLegacySentinel()
    {
        const QVector<QPointF> original{{0.0, 2.0}, {1.0, 4.0}};
        const auto replay = FlowCalibrationMath::replay(original, 2.0, 1.5);
        QCOMPARE(replay.size(), 2);
        QCOMPARE(replay.at(0), QPointF(0.0, 1.5));
        QCOMPARE(replay.at(1), QPointF(1.0, 3.0));

        // Unknown recorded multiplier (zero sentinel) uses the original 1.0 basis.
        const auto legacy = FlowCalibrationMath::replay(original, 0.0, 1.5);
        QCOMPARE(legacy.at(0), QPointF(0.0, 3.0));
        QCOMPARE(legacy.at(1), QPointF(1.0, 6.0));
    }

    void replayClampsPreviewToCanonicalRange()
    {
        const QVector<QPointF> original{{1.0, 10.0}};
        QCOMPARE(FlowCalibrationMath::replay(original, 1.0, -2.0).first().y(), 3.5);
        QCOMPARE(FlowCalibrationMath::replay(original, 1.0, 99.0).first().y(), 30.0);
    }
};

QTEST_APPLESS_MAIN(tst_FlowCalibrationMath)
#include "tst_flowcalibrationmath.moc"
