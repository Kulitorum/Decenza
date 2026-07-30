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
        LogCollapse::Collapsed c2;
        QVERIFY(c.shouldLog("k", "text", 0, &c2));
        QCOMPARE(c2.suppressed, 0);
    }

    // The whole point: identical repeats inside the window stay silent, and the count of what was
    // swallowed rides along on the next line that does print.
    void identicalRepeatsCollapseAndReportCount()
    {
        LogCollapse c(60'000);
        LogCollapse::Collapsed c2;
        QVERIFY(c.shouldLog("k", "same", 0, &c2));

        for (int i = 1; i <= 5; ++i)
            QVERIFY(!c.shouldLog("k", "same", i * 1'000, &c2));

        // Window elapsed — speaks again, and accounts for the five it stood in for.
        QVERIFY(c.shouldLog("k", "same", 60'000, &c2));
        QCOMPARE(c2.suppressed, 5);

        // Count resets after being reported, so it is a per-window figure and never cumulative.
        QVERIFY(!c.shouldLog("k", "same", 61'000, &c2));
        QVERIFY(c.shouldLog("k", "same", 120'000, &c2));
        QCOMPARE(c2.suppressed, 1);
    }

    // A changed message is the interesting event. Holding it for the rest of the window is the one
    // failure mode that would make this class harmful rather than merely noisy.
    void changedTextLogsImmediately()
    {
        LogCollapse c(60'000);
        LogCollapse::Collapsed c2;
        QVERIFY(c.shouldLog("k", "before", 0, &c2));
        QVERIFY(!c.shouldLog("k", "before", 1'000, &c2));
        QVERIFY(c.shouldLog("k", "after", 2'000, &c2));
        QCOMPARE(c2.suppressed, 1);  // the one silent "before" is still accounted for
    }

    // Keys are independent: one chatty source must not silence another.
    void keysAreIndependent()
    {
        LogCollapse c(60'000);
        LogCollapse::Collapsed c2;
        QVERIFY(c.shouldLog("a", "text", 0, &c2));
        QVERIFY(c.shouldLog("b", "text", 0, &c2));
        QCOMPARE(c2.suppressed, 0);
        QVERIFY(!c.shouldLog("a", "text", 1'000, &c2));
        QVERIFY(!c.shouldLog("b", "text", 1'000, &c2));
    }

    // The window boundary is inclusive, so a source sampling on exactly the window period (the
    // memory monitor at 60 s against a 60 s window would be the pathological case) is not held one
    // extra tick every time.
    void windowBoundaryIsInclusive()
    {
        LogCollapse c(10'000);
        LogCollapse::Collapsed c2;
        QVERIFY(c.shouldLog("k", "t", 0, &c2));
        QVERIFY(!c.shouldLog("k", "t", 9'999, &c2));
        QVERIFY(c.shouldLog("k", "t", 10'000, &c2));
    }

    // Suffix wording lives in one place so the callers cannot phrase it five ways.
    void suffixIsEmptyWhenNothingWasSuppressed()
    {
        QCOMPARE(LogCollapse::suffix({0, 60'000}), QString());
        QCOMPARE(LogCollapse::suffix({-1, 60'000}), QString());
        QCOMPARE(LogCollapse::suffix({3, 60'000}),
                 QStringLiteral(" (+3 identical in the preceding 60 s)"));
    }

    // The span the suffix reports is MEASURED, not the window.
    //
    // The window is only a minimum, so for a bursty source the two diverge without limit: five
    // repeats over 5 s, then silence, then one more line an hour later. Reporting the window would
    // date that hour-old burst to "the last 60 s" and put a reader on the wrong side of the
    // interesting gap. Pinned because every periodic caller produces logs where the two numbers
    // agree — the wrong version looks correct in all five real call sites.
    void suffixReportsMeasuredSpanNotWindow()
    {
        LogCollapse c(60'000);
        LogCollapse::Collapsed c2;
        QVERIFY(c.shouldLog("k", "same", 0, &c2));
        for (int i = 1; i <= 5; ++i)
            QVERIFY(!c.shouldLog("k", "same", i * 1'000, &c2));

        // Source goes quiet, then speaks once an hour later.
        QVERIFY(c.shouldLog("k", "same", 3'600'000, &c2));
        QCOMPARE(c2.suppressed, 5);
        QCOMPARE(c2.spanMs, 3'600'000);
        QCOMPARE(LogCollapse::suffix(c2),
                 QStringLiteral(" (+5 identical in the preceding 3600 s)"));
    }

    // A first emit stands in for nothing, so it must not claim a span either — the entry's
    // lastEmitMs is still 0 there, and reporting nowMs would make the very first line of a session
    // announce a span equal to the whole uptime.
    void firstEmitReportsNoSpan()
    {
        LogCollapse c(60'000);
        LogCollapse::Collapsed c2;
        QVERIFY(c.shouldLog("k", "text", 500'000, &c2));
        QCOMPARE(c2.suppressed, 0);
        QCOMPARE(c2.spanMs, 0);
    }
};

QTEST_MAIN(tst_LogCollapse)
#include "tst_logcollapse.moc"
