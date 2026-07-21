#include <QtTest>

#include "mcp/mcplogfilter.h"

using namespace McpLogFilter;

// Pure-function tests for the filter/tail helpers shared by debug_get_log and
// shots_get_debug_log (mcplogfilter.h), independent of the persisted log file,
// a shot database, or the MCP registry.
class tst_McpLogFilter : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void levelRank_ordersKnownLevels()
    {
        QVERIFY(levelRank("DEBUG") < levelRank("INFO"));
        QVERIFY(levelRank("INFO") < levelRank("WARN"));
        QVERIFY(levelRank("WARN") < levelRank("ERROR"));
        QVERIFY(levelRank("ERROR") < levelRank("FATAL"));
        QCOMPARE(levelRank("warn"), levelRank("WARN"));  // case-insensitive
        QCOMPARE(levelRank("nonsense"), -1);
    }

    void lineLevel_parsesWebDebugLoggerFormat()
    {
        QCOMPARE(lineLevel("[   0.123] DEBUG some message"), QStringLiteral("DEBUG"));
        QCOMPARE(lineLevel("[  12.000] ERROR  R2 error 0/2"), QStringLiteral("ERROR"));
        // Session markers / trim banners carry no level tag.
        QVERIFY(lineLevel("========== SESSION START: 2026-01-01T09:00:00 ==========").isEmpty());
    }

    void filterLines_substringIsCaseInsensitiveByDefault()
    {
        QStringList lines = {"[0.1] INFO  connecting to R2", "[0.2] INFO  scale ready", "[0.3] WARN  r2 error 0/2"};
        auto matches = filterLines(lines, 0, "R2", /*regex*/ false, QString());
        QCOMPARE(matches.size(), 2);
        QCOMPARE(matches[0].line, qsizetype(0));
        QCOMPARE(matches[1].line, qsizetype(2));
    }

    void filterLines_regexMode()
    {
        QStringList lines = {"[0.1] INFO  SAW trigger at 34g", "[0.2] INFO  nothing here", "[0.3] INFO  SAW trigger at 36g"};
        auto matches = filterLines(lines, 0, "SAW.*trigger", /*regex*/ true, QString());
        QCOMPARE(matches.size(), 2);
    }

    void filterLines_invalidRegexReportsError()
    {
        QString error;
        auto matches = filterLines({"anything"}, 0, "([", /*regex*/ true, QString(), &error);
        QVERIFY(matches.isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void filterLines_minLevelAlone()
    {
        QStringList lines = {
            "[0.1] DEBUG chatter",
            "[0.2] WARN  low water",
            "[0.3] ERROR BLE write failed",
        };
        auto matches = filterLines(lines, 0, QString(), false, "WARN");
        QCOMPARE(matches.size(), 2);
        QCOMPARE(matches[0].text, lines[1]);
        QCOMPARE(matches[1].text, lines[2]);
    }

    void filterLines_minLevelCombinedWithFilterIsAnd()
    {
        QStringList lines = {
            "[0.1] ERROR unrelated failure",
            "[0.2] DEBUG BLE chatter",
            "[0.3] ERROR BLE write failed",
        };
        auto matches = filterLines(lines, 0, "BLE", false, "ERROR");
        QCOMPARE(matches.size(), 1);
        QCOMPARE(matches[0].text, lines[2]);
    }

    void filterLines_emptyConstraintsPassEverything()
    {
        QStringList lines = {"a", "b", "c"};
        auto matches = filterLines(lines, 0, QString(), false, QString());
        QCOMPARE(matches.size(), 3);
    }

    void filterLines_absoluteLineNumbersHonorStartLine()
    {
        // Simulates a session starting at line 42 within the whole log.
        QStringList lines = {"[0.1] WARN a", "[0.2] INFO b", "[0.3] WARN c"};
        auto matches = filterLines(lines, 42, QString(), false, "WARN");
        QCOMPARE(matches.size(), 2);
        QCOMPARE(matches[0].line, qsizetype(42));
        QCOMPARE(matches[1].line, qsizetype(44));
    }

    void paginate_offsetAndLimit()
    {
        QList<LineMatch> matches;
        for (int i = 0; i < 10; ++i) matches.append({qsizetype(i), QString::number(i)});

        auto page = paginate(matches, /*offset*/ 3, /*limit*/ 4, /*tail*/ 0);
        QCOMPARE(page.size(), 4);
        QCOMPARE(page[0].line, qsizetype(3));
        QCOMPARE(page[3].line, qsizetype(6));
    }

    void paginate_offsetPastEndReturnsEmpty()
    {
        QList<LineMatch> matches = {{0, "a"}, {1, "b"}};
        QVERIFY(paginate(matches, /*offset*/ 5, /*limit*/ 10, /*tail*/ 0).isEmpty());
    }

    void paginate_tailReturnsLastN()
    {
        QList<LineMatch> matches;
        for (int i = 0; i < 10; ++i) matches.append({qsizetype(i), QString::number(i)});

        auto page = paginate(matches, /*offset*/ 0, /*limit*/ 500, /*tail*/ 3);
        QCOMPARE(page.size(), 3);
        QCOMPARE(page[0].line, qsizetype(7));
        QCOMPARE(page[2].line, qsizetype(9));
    }

    void paginate_tailOverridesOffset()
    {
        QList<LineMatch> matches;
        for (int i = 0; i < 10; ++i) matches.append({qsizetype(i), QString::number(i)});

        // Non-zero offset supplied alongside tail — tail wins per contract.
        auto page = paginate(matches, /*offset*/ 2, /*limit*/ 500, /*tail*/ 3);
        QCOMPARE(page.size(), 3);
        QCOMPARE(page[0].line, qsizetype(7));
    }

    void paginate_tailLargerThanSetReturnsWholeSet()
    {
        QList<LineMatch> matches = {{0, "a"}, {1, "b"}};
        auto page = paginate(matches, 0, 500, /*tail*/ 100);
        QCOMPARE(page.size(), 2);
    }

    void stripTimestampPrefix_removesLeadingBracketField()
    {
        QCOMPARE(stripTimestampPrefix("[   0.123] WARN  low water"), QStringLiteral("WARN  low water"));
        QCOMPARE(stripTimestampPrefix("[3819.468] WARN  x"), QStringLiteral("WARN  x"));
    }

    void stripTimestampPrefix_leavesUnprefixedLinesUnchanged()
    {
        // Shot debug log lines and session markers have no "[<elapsed>]" prefix.
        QCOMPARE(stripTimestampPrefix("BLE frame 1"), QStringLiteral("BLE frame 1"));
        QCOMPARE(stripTimestampPrefix("========== SESSION START: 2026-01-01T09:00:00 =========="),
                 QStringLiteral("========== SESSION START: 2026-01-01T09:00:00 =========="));
    }

    void dedupeConsecutive_collapsesARun()
    {
        QList<LineMatch> matches = {
            {10, "[ 101.178] WARN  _derived.text undefined at read"},
            {11, "[ 101.179] WARN  _derived.text undefined at read"},
            {12, "[ 101.180] WARN  _derived.text undefined at read"},
        };
        auto grouped = dedupeConsecutive(matches);
        QCOMPARE(grouped.size(), 1);
        QCOMPARE(grouped[0].line, qsizetype(10));
        QCOMPARE(grouped[0].count, qsizetype(3));
        QCOMPARE(grouped[0].lastLine, qsizetype(12));
        QCOMPARE(grouped[0].text, matches[0].text);
    }

    void dedupeConsecutive_nonConsecutiveRepeatsStaySeparate()
    {
        QList<LineMatch> matches = {
            {10, "[  1.000] WARN  retrying"},
            {11, "[  2.000] INFO  something unrelated"},
            {12, "[  3.000] WARN  retrying"},
        };
        auto grouped = dedupeConsecutive(matches);
        QCOMPARE(grouped.size(), 3);
        for (const auto& g : grouped)
            QCOMPARE(g.count, qsizetype(1));
    }

    void dedupeConsecutive_leavesGenuinelyDifferentLinesAlone()
    {
        // Different shot ids/sample counts — must NOT collapse even though the
        // message template is otherwise identical (see design.md Decision 6).
        QList<LineMatch> matches = {
            {1, "[  1.000] INFO  [Background] Shot-chart grab -> source shot 1120 samples 293"},
            {2, "[  2.000] INFO  [Background] Shot-chart grab -> source shot 1121 samples 292"},
        };
        auto grouped = dedupeConsecutive(matches);
        QCOMPARE(grouped.size(), 2);
    }

    void dedupeConsecutive_handlesLinesWithNoTimestampPrefix()
    {
        // Shot debug log style: no "[<elapsed>]" prefix at all.
        QList<LineMatch> matches = {
            {0, "BLE frame ack"},
            {1, "BLE frame ack"},
            {2, "BLE frame nack"},
        };
        auto grouped = dedupeConsecutive(matches);
        QCOMPARE(grouped.size(), 2);
        QCOMPARE(grouped[0].count, qsizetype(2));
        QCOMPARE(grouped[0].lastLine, qsizetype(1));
        QCOMPARE(grouped[1].count, qsizetype(1));
    }

    void dedupeConsecutive_emptyInputIsEmptyOutput()
    {
        QVERIFY(dedupeConsecutive({}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(tst_McpLogFilter)
#include "tst_mcplogfilter.moc"
