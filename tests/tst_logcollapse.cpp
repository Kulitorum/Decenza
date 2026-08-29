#include <QtTest>

#include <algorithm>

#include "core/logcollapse.h"

// LogCollapse decides whether a repeating log line is worth printing. Every periodic source in the
// tree shares it (the MMR keepalive, the memory sampler, the battery poll, the ShotServer request
// log, MQTT's retry ladder), which is exactly why the rules are pinned here: a change that makes it
// slightly more eager to suppress would go unnoticed in the app and quietly cost the next log
// reader the line they needed.
//
// Two modes, and both are pinned. kChangesOnly is what those five use — an unchanged line never
// repeats at any spacing — while a finite window remains for the one caller whose repeats are
// themselves evidence (BleGattQueue's dispatch line, gated on a foreign-wait threshold).
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

    // kChangesOnly: no elapsed time is ever enough to re-print an unchanged line. The failure this
    // guards is a regression to window semantics via some "sensible maximum" clamp — which would
    // look harmless, pass every other case here, and put a keepalive back in the log every N hours.
    void changesOnlyNeverRepeatsAnUnchangedLine()
    {
        LogCollapse c(LogCollapse::kChangesOnly);
        LogCollapse::Collapsed c2;
        QVERIFY(c.shouldLog("k", "same", 0, &c2));

        // Ten hours, a day, and a week later: still nothing.
        QVERIFY(!c.shouldLog("k", "same", 10LL * 60 * 60 * 1000, &c2));
        QVERIFY(!c.shouldLog("k", "same", 24LL * 60 * 60 * 1000, &c2));
        QVERIFY(!c.shouldLog("k", "same", 7LL * 24 * 60 * 60 * 1000, &c2));
    }

    // ...and the tally is not lost by that silence. This is what makes kChangesOnly safe for the
    // two callers that never flush (the memory sampler, the battery poll): the count and the span
    // ride out on the next line whose text differs, which is the line a reader wanted anyway.
    void changesOnlyStillReportsTheTallyOnTheNextChange()
    {
        LogCollapse c(LogCollapse::kChangesOnly);
        LogCollapse::Collapsed c2;
        QVERIFY(c.shouldLog("k", "before", 0, &c2));

        constexpr qint64 kMinute = 60'000;
        for (int i = 1; i <= 180; ++i)
            QVERIFY(!c.shouldLog("k", "before", i * kMinute, &c2));

        QVERIFY(c.shouldLog("k", "after", 181 * kMinute, &c2));
        QCOMPARE(c2.suppressed, 180);
        QCOMPARE(c2.spanMs, 181 * kMinute);
    }

    // The finite window still behaves exactly as it did. BleGattQueue depends on it, and the
    // sentinel is a comparison against m_windowMs — get that branch wrong and every windowed caller
    // silently becomes changes-only, which no case above would catch.
    void finiteWindowIsUnaffectedByTheSentinel()
    {
        LogCollapse c(60'000);
        LogCollapse::Collapsed c2;
        QVERIFY(c.shouldLog("k", "same", 0, &c2));
        QVERIFY(!c.shouldLog("k", "same", 59'999, &c2));
        QVERIFY(c.shouldLog("k", "same", 60'000, &c2));
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

    // flush() and flushAll() had no coverage at all until an episodic caller
    // needed them. Both are the mechanism that keeps one run's tally off the
    // next run's first line, so "it compiles" was the only thing asserting them.

    void flushAllReturnsEveryPendingTallyAndForgetsTheKeys()
    {
        LogCollapse c(60'000);
        LogCollapse::Collapsed ignored;

        // Two keys, each emitted once then repeated inside the window.
        QVERIFY(c.shouldLog("a", "same", 0, &ignored));
        QVERIFY(!c.shouldLog("a", "same", 1'000, &ignored));
        QVERIFY(!c.shouldLog("a", "same", 2'000, &ignored));
        QVERIFY(c.shouldLog("b", "other", 0, &ignored));
        QVERIFY(!c.shouldLog("b", "other", 1'000, &ignored));

        auto pending = c.flushAll(3'000);
        std::sort(pending.begin(), pending.end(),
                  [](const auto& l, const auto& r) { return l.first < r.first; });
        QCOMPARE(pending.size(), 2);
        QCOMPARE(pending[0].first, QStringLiteral("a"));
        QCOMPARE(pending[0].second.suppressed, 2);
        QCOMPARE(pending[0].second.spanMs, 3'000);
        QCOMPARE(pending[1].second.suppressed, 1);

        // Forgotten, so the next run starts as a first sighting rather than
        // carrying this one's count -- the whole point of flushing.
        QVERIFY(c.flushAll(4'000).isEmpty());
        LogCollapse::Collapsed after;
        QVERIFY(c.shouldLog("a", "same", 5'000, &after));
        QCOMPARE(after.suppressed, 0);
        QCOMPARE(after.spanMs, 0);
    }

    // A key that was emitted and never repeated has nothing to say, and saying
    // it would put "(+0 identical)" noise on every run end.
    void flushAllSkipsKeysWithNothingSuppressed()
    {
        LogCollapse c(60'000);
        LogCollapse::Collapsed ignored;
        QVERIFY(c.shouldLog("a", "once", 0, &ignored));
        QVERIFY(c.flushAll(1'000).isEmpty());
    }

    // Single-key flush() had no test at all while flushAll() had two, and the
    // callers that need THIS one are the episodic ones that can name their keys:
    // the BLE scan lifecycle flushing three keys in stopScan(), the Decent
    // battery poll flushing at disconnect, the constant-weight liveness line
    // flushing at shot start.
    //
    // The property under test is the one that makes those callers correct —
    // flush() must FORGET the key, not merely report it. A flush that reported
    // and kept would leave the run's count in the table to be printed again on
    // the next run's first line, which is the "last week's repeat count on
    // today's first line" failure logcollapse.h records four of six callers
    // having shipped. It is invisible in any single-run test, which is exactly
    // why it kept happening.
    void flushForgetsTheKeySoTheNextRunStartsClean()
    {
        LogCollapse c(LogCollapse::kChangesOnly);
        LogCollapse::Collapsed out;

        QVERIFY(c.shouldLog("k", "same", 0, &out));
        QVERIFY(!c.shouldLog("k", "same", 1'000, &out));
        QVERIFY(!c.shouldLog("k", "same", 2'000, &out));

        const auto flushed = c.flush("k", 3'000);
        QCOMPARE(flushed.suppressed, 2);
        QCOMPARE(flushed.spanMs, 3'000);

        // Flushing again reports nothing: the tally was consumed, not copied.
        QCOMPARE(c.flush("k", 4'000).suppressed, 0);

        // And the next run is a first sighting, carrying none of the previous
        // run's count or span.
        QVERIFY(c.shouldLog("k", "same", 5'000, &out));
        QCOMPARE(out.suppressed, 0);
        QCOMPARE(out.spanMs, 0);
    }

    // flush() on a key that never emitted returns an empty tally rather than
    // fabricating one.
    //
    // Relied on directly: BLEManager::stopScan() flushes all three scan-cycle
    // keys unconditionally, and most calls happen when no burst ran, so an
    // unknown key is the COMMON case there rather than an edge. If it returned
    // a default-constructed-but-nonzero tally, or inserted the key on lookup,
    // every ordinary scan stop would print "(+0 identical...)" noise — or worse,
    // a span measured from the epoch.
    void flushOnAnUnknownKeyReportsNothing()
    {
        LogCollapse c(LogCollapse::kChangesOnly);
        const auto flushed = c.flush("never-seen", 9'000);
        QCOMPARE(flushed.suppressed, 0);
        QCOMPARE(flushed.spanMs, 0);

        // The lookup must not have created the key either: a first real sighting
        // still reports itself as one.
        LogCollapse::Collapsed out;
        QVERIFY(c.shouldLog("never-seen", "text", 10'000, &out));
        QCOMPARE(out.suppressed, 0);
    }
};

QTEST_MAIN(tst_LogCollapse)
#include "tst_logcollapse.moc"
