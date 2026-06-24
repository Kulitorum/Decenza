#include <QtTest>
#include <QVector>

#include "profile/temperaturedisplay.h"

using namespace TemperatureDisplay;

// Text literals used by the formatter, mirrored here so the test reads clearly.
static const QString DEG   = QStringLiteral("°C");
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
};

QTEST_APPLESS_MAIN(tst_TemperatureDisplay)
#include "tst_temperaturedisplay.moc"
