#include <QtTest>
#include <QVector>

#include "profile/temperaturedisplay.h"

using namespace TemperatureDisplay;

// Text literals used by the formatter, mirrored here so the test reads clearly.
static const QString DEG   = QStringLiteral("°C");
static const QString DEGF  = QStringLiteral("°F");
static const QString SEP   = QStringLiteral(" · ");
static const QString ELLIP = QStringLiteral("…");

class tst_TemperatureDisplay : public QObject {
    Q_OBJECT

private slots:
    // ----- distinctCount -----
    void distinctCount_data() {
        QTest::addColumn<QVector<double>>("temps");
        QTest::addColumn<int>("expected");
        QTest::newRow("empty") << QVector<double>{} << 0;
        QTest::newRow("one") << QVector<double>{90} << 1;
        QTest::newRow("uniform") << QVector<double>{90, 90, 90} << 1;
        QTest::newRow("two") << QVector<double>{88, 93} << 2;
        QTest::newRow("two-repeated") << QVector<double>{88, 88, 93, 93} << 2;
        QTest::newRow("three") << QVector<double>{84, 79, 52} << 3;
        QTest::newRow("tolerance-inside") << QVector<double>{88.0, 88.02} << 1;   // within 0.05 → 1
        QTest::newRow("tolerance-outside") << QVector<double>{88.0, 88.06} << 2;  // beyond 0.05 → 2
    }
    void distinctCount() {
        QFETCH(QVector<double>, temps);
        QFETCH(int, expected);
        QCOMPARE(TemperatureDisplay::distinctCount(temps), expected);
    }

    // ----- N=1: single value, override shown as a delta tag (NOT a recomputed temp) -----
    void singleNoOverride() {
        QCOMPARE(format({90, 90, 90}, 90, false, 0), QStringLiteral("90") + DEG);
    }
    void singleWithOverride() {
        // all frames 90, override 91 → "90°C +1°" (base temp + offset, not "90 → 91")
        QCOMPARE(format({90, 90, 90}, 90, true, 91),
                 QStringLiteral("90") + DEG + QStringLiteral(" +1°"));
    }

    // ----- N=2: spaced mid-dot list -----
    void twoNoOverride() {
        QCOMPARE(format({88, 93}, 88, false, 0),
                 QStringLiteral("88") + SEP + QStringLiteral("93") + DEG);
    }
    void twoWithOverride() {
        // anchor 88, override 90 → delta +2 → base temps + "+2°" (NOT 90, 95)
        QCOMPARE(format({88, 93}, 88, true, 90),
                 QStringLiteral("88") + SEP + QStringLiteral("93") + DEG + QStringLiteral(" +2°"));
    }

    // ----- N>=3: first…last ellipsis, trajectory order -----
    void threeNoOverride() {
        QCOMPARE(format({84, 79, 52}, 84, false, 0),
                 QStringLiteral("84") + ELLIP + QStringLiteral("52") + DEG);
    }
    void threeWithOverride() {
        // anchor 84, override 85 → delta +1 → "84…52°C +1°" (base endpoints + offset)
        QCOMPARE(format({84, 79, 52}, 84, true, 85),
                 QStringLiteral("84") + ELLIP + QStringLiteral("52") + DEG + QStringLiteral(" +1°"));
    }

    // ----- negative delta -----
    void negativeDeltaTag() {
        // anchor 90, override 88 → delta -2, two distinct temps → "90 · 85°C -2°"
        QCOMPARE(format({90, 85}, 90, true, 88),
                 QStringLiteral("90") + SEP + QStringLiteral("85") + DEG + QStringLiteral(" -2°"));
    }

    // ----- zero delta: override equals anchor → no tag -----
    void zeroDeltaNoTag() {
        QCOMPARE(format({88, 93}, 88, true, 88),
                 QStringLiteral("88") + SEP + QStringLiteral("93") + DEG);
    }

    // ----- sub-threshold delta (|delta| < 0.05) is suppressed -----
    void subThresholdDeltaNoTag() {
        QCOMPARE(format({90, 90}, 90, true, 90.04),
                 QStringLiteral("90") + DEG);
    }

    // ----- branch keys on DISTINCT count, not frame count -----
    void twoDistinctAmongThreeFrames() {
        // three frames, two distinct → N=2 mid-dot list (not the N≥3 ellipsis)
        QCOMPARE(format({88, 88, 93}, 88, false, 0),
                 QStringLiteral("88") + SEP + QStringLiteral("93") + DEG);
    }

    // ----- ramp-up-then-down edge case (documented simplification) -----
    void rampHidesPeak() {
        // 88→92→90: ellipsis shows first…last (88…90), peak 92 not shown
        QCOMPARE(format({88, 92, 90}, 88, false, 0),
                 QStringLiteral("88") + ELLIP + QStringLiteral("90") + DEG);
    }

    // ----- empty frames fall back to anchor -----
    void emptyFallsBackToAnchor() {
        QCOMPARE(format({}, 93, false, 0), QStringLiteral("93") + DEG);
        QCOMPARE(format({}, 93, true, 94),
                 QStringLiteral("93") + DEG + QStringLiteral(" +1°"));
    }

    // ----- fractional temps keep .5, drop trailing .0 -----
    void fractionalFormatting() {
        QCOMPARE(format({88.5, 93}, 88.5, false, 0),
                 QStringLiteral("88.5") + SEP + QStringLiteral("93") + DEG);
    }

    // ===== Fahrenheit conversion (fahrenheit = true) =====
    // Absolute values convert ×9/5+32; the delta tag scales ×9/5 (no +32 shift);
    // the sub-threshold suppression stays keyed in Celsius. These guard the classic
    // C/F trap where a refactor funnelling absolute + delta through one converter
    // would render "+33.8°" instead of "+1.8°".
    void fahrenheitSingleAbsolute() {
        // 93°C → 199.4°F
        QCOMPARE(format({93, 93}, 93, false, 0, true), QStringLiteral("199.4") + DEGF);
    }
    void fahrenheitDeltaScalesNotShifts() {
        // 90°C → 194°F; delta +1°C → +1.8°F (scaled, not +33.8°)
        QCOMPARE(format({90, 90, 90}, 90, true, 91, true),
                 QStringLiteral("194") + DEGF + QStringLiteral(" +1.8°"));
    }
    void fahrenheitNegativeDelta() {
        // 90,85 → 194,185; delta -2°C → -3.6°F
        QCOMPARE(format({90, 85}, 90, true, 88, true),
                 QStringLiteral("194") + SEP + QStringLiteral("185") + DEGF + QStringLiteral(" -3.6°"));
    }
    void fahrenheitTwoListConvertsBoth() {
        // 88,93 → 190.4,199.4
        QCOMPARE(format({88, 93}, 88, false, 0, true),
                 QStringLiteral("190.4") + SEP + QStringLiteral("199.4") + DEGF);
    }
    void fahrenheitEllipsisConvertsBothEndpoints() {
        // 84…52 → 183.2…125.6
        QCOMPARE(format({84, 79, 52}, 84, false, 0, true),
                 QStringLiteral("183.2") + ELLIP + QStringLiteral("125.6") + DEGF);
    }
    void fahrenheitEmptyFallsBackToAnchor() {
        // empty → anchor 93 → 199.4°F; delta +1°C → +1.8°F
        QCOMPARE(format({}, 93, true, 94, true),
                 QStringLiteral("199.4") + DEGF + QStringLiteral(" +1.8°"));
    }
    void fahrenheitSuppressionGateStaysCelsius() {
        // 0.04°C delta (= 0.072°F) is below the Celsius suppression threshold → no tag,
        // even though 0.072 would exceed a naive 0.05°F gate.
        QCOMPARE(format({90, 90}, 90, true, 90.04, true), QStringLiteral("194") + DEGF);
    }
    void fahrenheitDefaultArgMatchesFalse() {
        // the fahrenheit=false default must behave exactly like explicit false
        QCOMPARE(format({88, 93}, 88, true, 90),
                 format({88, 93}, 88, true, 90, false));
    }
};

QTEST_APPLESS_MAIN(tst_TemperatureDisplay)
#include "tst_temperaturedisplay.moc"
