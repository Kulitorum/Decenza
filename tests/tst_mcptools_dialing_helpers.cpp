#include <QTest>
#include "mcp/mcptools_dialing_helpers.h"

using namespace McpDialingHelpers;

class TstMcpToolsDialingHelpers : public QObject
{
    Q_OBJECT

private slots:
    void emptyInput_returnsNoSessions();
    void singleShot_returnsOneSessionOfOne();
    void twoAdjacentShots_inSameSession();
    void twoFarApartShots_inSeparateSessions();
    void issueExample_threeShots_twoSessions();
    void exactlyAtThreshold_groupsTogether();
    void justOverThreshold_breaksSession();
    void thresholdIsConfigurable();
};

void TstMcpToolsDialingHelpers::emptyInput_returnsNoSessions()
{
    QVERIFY(groupSessions({}).isEmpty());
}

void TstMcpToolsDialingHelpers::singleShot_returnsOneSessionOfOne()
{
    const auto sessions = groupSessions({1000});
    QCOMPARE(sessions.size(), qsizetype(1));
    QCOMPARE(sessions[0].size(), qsizetype(1));
    QCOMPARE(sessions[0][0], qsizetype(0));
}

void TstMcpToolsDialingHelpers::twoAdjacentShots_inSameSession()
{
    // Two shots 7 minutes apart — well within the 60-min threshold.
    const auto sessions = groupSessions({2000, 2000 - 7 * 60});
    QCOMPARE(sessions.size(), qsizetype(1));
    QCOMPARE(sessions[0].size(), qsizetype(2));
    QCOMPARE(sessions[0][0], qsizetype(0));
    QCOMPARE(sessions[0][1], qsizetype(1));
}

void TstMcpToolsDialingHelpers::twoFarApartShots_inSeparateSessions()
{
    // Two shots 24 hours apart.
    const auto sessions = groupSessions({100000, 100000 - 24 * 3600});
    QCOMPARE(sessions.size(), qsizetype(2));
    QCOMPARE(sessions[0].size(), qsizetype(1));
    QCOMPARE(sessions[1].size(), qsizetype(1));
    QCOMPARE(sessions[0][0], qsizetype(0));
    QCOMPARE(sessions[1][0], qsizetype(1));
}

void TstMcpToolsDialingHelpers::issueExample_threeShots_twoSessions()
{
    // Mirrors the issue #1009 example: shots 884 (today 9:36), 883 (today 9:29),
    // 882 (yesterday 10:09). 884 and 883 are 7 min apart — same session.
    // 883 and 882 are ~23 hours apart — separate sessions.
    const qint64 t884 = 1000000;
    const qint64 t883 = t884 - 7 * 60;
    const qint64 t882 = t883 - 23 * 3600;
    const auto sessions = groupSessions({t884, t883, t882});

    QCOMPARE(sessions.size(), qsizetype(2));
    QCOMPARE(sessions[0].size(), qsizetype(2));
    QCOMPARE(sessions[1].size(), qsizetype(1));
    // First session has indices [0, 1] (884 + 883), second has [2] (882).
    QCOMPARE(sessions[0][0], qsizetype(0));
    QCOMPARE(sessions[0][1], qsizetype(1));
    QCOMPARE(sessions[1][0], qsizetype(2));
}

void TstMcpToolsDialingHelpers::exactlyAtThreshold_groupsTogether()
{
    // Exactly at the threshold (gap == threshold) → still same session.
    const auto sessions = groupSessions({3600, 0}, /*thresholdSec=*/3600);
    QCOMPARE(sessions.size(), qsizetype(1));
    QCOMPARE(sessions[0].size(), qsizetype(2));
}

void TstMcpToolsDialingHelpers::justOverThreshold_breaksSession()
{
    // 1 second over the threshold → distinct sessions.
    const auto sessions = groupSessions({3601, 0}, /*thresholdSec=*/3600);
    QCOMPARE(sessions.size(), qsizetype(2));
}

void TstMcpToolsDialingHelpers::thresholdIsConfigurable()
{
    // With a 30-min threshold, a 45-min gap should split.
    const auto sessions = groupSessions({3600, 3600 - 45 * 60}, /*thresholdSec=*/30 * 60);
    QCOMPARE(sessions.size(), qsizetype(2));
}

QTEST_APPLESS_MAIN(TstMcpToolsDialingHelpers)

#include "tst_mcptools_dialing_helpers.moc"
