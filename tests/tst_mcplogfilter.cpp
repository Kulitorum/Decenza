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
};

QTEST_GUILESS_MAIN(tst_McpLogFilter)
#include "tst_mcplogfilter.moc"
