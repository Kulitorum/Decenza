#include <QtTest>

#include "core/logcollapse.h"

// LogCollapse decides whether a repeating log line is worth printing. Three periodic sources share
// it (the MMR keepalive, the memory sampler, the ShotServer request log), which is exactly why the
// rules are pinned here: a change that makes it slightly more eager to suppress would go unnoticed
// in the app and quietly cost the next log reader the line they needed.
//
// The clock is a parameter, not a QElapsedTimer, so every case below is exact and instant.
class tst_LogCollapse : public QObject
{
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    // First sight of a key always speaks, with nothing attributed to it.
    void firstCallAlwaysLogs()
    {
        LogCollapse c(60'000);
        int suppressed = -1;
        QVERIFY(c.shouldLog("k", "text", 0, &suppressed));
        QCOMPARE(suppressed, 0);
    }

    // The whole point: identical repeats inside the window stay silent, and the count of what was
    // swallowed rides along on the next line that does print.
    void identicalRepeatsCollapseAndReportCount()
    {
        LogCollapse c(60'000);
        int suppressed = -1;
        QVERIFY(c.shouldLog("k", "same", 0, &suppressed));

        for (int i = 1; i <= 5; ++i)
            QVERIFY(!c.shouldLog("k", "same", i * 1'000, &suppressed));

        // Window elapsed — speaks again, and accounts for the five it stood in for.
        QVERIFY(c.shouldLog("k", "same", 60'000, &suppressed));
        QCOMPARE(suppressed, 5);

        // Count resets after being reported, so it is a per-window figure and never cumulative.
        QVERIFY(!c.shouldLog("k", "same", 61'000, &suppressed));
        QVERIFY(c.shouldLog("k", "same", 120'000, &suppressed));
        QCOMPARE(suppressed, 1);
    }

    // A changed message is the interesting event. Holding it for the rest of the window is the one
    // failure mode that would make this class harmful rather than merely noisy.
    void changedTextLogsImmediately()
    {
        LogCollapse c(60'000);
        int suppressed = -1;
        QVERIFY(c.shouldLog("k", "before", 0, &suppressed));
        QVERIFY(!c.shouldLog("k", "before", 1'000, &suppressed));
        QVERIFY(c.shouldLog("k", "after", 2'000, &suppressed));
        QCOMPARE(suppressed, 1);  // the one silent "before" is still accounted for
    }

    // Keys are independent: one chatty source must not silence another.
    void keysAreIndependent()
    {
        LogCollapse c(60'000);
        int suppressed = -1;
        QVERIFY(c.shouldLog("a", "text", 0, &suppressed));
        QVERIFY(c.shouldLog("b", "text", 0, &suppressed));
        QCOMPARE(suppressed, 0);
        QVERIFY(!c.shouldLog("a", "text", 1'000, &suppressed));
        QVERIFY(!c.shouldLog("b", "text", 1'000, &suppressed));
    }

    // The window boundary is inclusive, so a source sampling on exactly the window period (the
    // memory monitor at 60 s against a 60 s window would be the pathological case) is not held one
    // extra tick every time.
    void windowBoundaryIsInclusive()
    {
        LogCollapse c(10'000);
        int suppressed = -1;
        QVERIFY(c.shouldLog("k", "t", 0, &suppressed));
        QVERIFY(!c.shouldLog("k", "t", 9'999, &suppressed));
        QVERIFY(c.shouldLog("k", "t", 10'000, &suppressed));
    }

    // Suffix wording lives in one place so the callers cannot phrase it three ways.
    void suffixIsEmptyWhenNothingWasSuppressed()
    {
        QCOMPARE(LogCollapse::suffix(0, 60'000), QString());
        QCOMPARE(LogCollapse::suffix(-1, 60'000), QString());
        QCOMPARE(LogCollapse::suffix(3, 60'000),
                 QStringLiteral(" (+3 identical in the last 60 s)"));
    }
};

QTEST_MAIN(tst_LogCollapse)
#include "tst_logcollapse.moc"
