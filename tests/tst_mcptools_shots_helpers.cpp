#include <QTest>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include "mcp/mcptools_shots_helpers.h"

using McpShotsHelpers::reshapeDetectorEnvelopes;
using McpShotsHelpers::stripTimeSeriesFields;

class TstMcpToolsShotsHelpers : public QObject
{
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }
    void noDetectorResults_isNoOp();
    void cleanShot_pourEnvelope_peakPressureBarIsNull();
    void truncatedShot_pourEnvelope_peakPressureBarPopulated();
    void skipFirstFrame_isWrappedAsObject();
    void verdictCategory_isWrappedAsObject();
    void oldKeysAreRemoved();
    void wrappedDetectorsAreUntouched();
    void strip_dropsTimeSeries();
    void strip_dropsAnUnknownSeries();
    void strip_keepsSummaryArraysAndScalars();
    void strip_dropsHeavyStrings();
};

// Minimal helper to build the inline-scalar shape that the serializer
// produces in src/history/shothistorystorage_serialize.cpp:204-209.
static QJsonObject makeRawDetectorResults(bool truncated = false,
                                          bool skip = false,
                                          const QString& verdict = QStringLiteral("clean"))
{
    QJsonObject d;
    d["pourTruncated"] = truncated;
    d["pourStartSec"] = 1.871;
    d["pourEndSec"] = 26.281;
    if (truncated) d["peakPressureBar"] = 11.4;
    d["skipFirstFrame"] = skip;
    d["verdictCategory"] = verdict;
    return d;
}

void TstMcpToolsShotsHelpers::noDetectorResults_isNoOp()
{
    QJsonObject obj{{"id", 42}};
    reshapeDetectorEnvelopes(obj);
    QCOMPARE(obj.value("id").toInt(), 42);
    QVERIFY(!obj.contains("detectorResults"));
}

void TstMcpToolsShotsHelpers::cleanShot_pourEnvelope_peakPressureBarIsNull()
{
    QJsonObject obj{{"detectorResults", makeRawDetectorResults(/*truncated=*/false)}};
    reshapeDetectorEnvelopes(obj);

    const QJsonObject pour = obj.value("detectorResults").toObject().value("pour").toObject();
    QCOMPARE(pour.value("truncated").toBool(), false);
    QCOMPARE(pour.value("startSec").toDouble(), 1.871);
    QCOMPARE(pour.value("endSec").toDouble(), 26.281);
    // Uniform shape: peakPressureBar is always present, null when not truncated.
    QVERIFY(pour.contains("peakPressureBar"));
    QVERIFY(pour.value("peakPressureBar").isNull());
}

void TstMcpToolsShotsHelpers::truncatedShot_pourEnvelope_peakPressureBarPopulated()
{
    QJsonObject obj{{"detectorResults", makeRawDetectorResults(/*truncated=*/true)}};
    reshapeDetectorEnvelopes(obj);

    const QJsonObject pour = obj.value("detectorResults").toObject().value("pour").toObject();
    QCOMPARE(pour.value("truncated").toBool(), true);
    QVERIFY(!pour.value("peakPressureBar").isNull());
    QCOMPARE(pour.value("peakPressureBar").toDouble(), 11.4);
}

void TstMcpToolsShotsHelpers::skipFirstFrame_isWrappedAsObject()
{
    QJsonObject obj{{"detectorResults", makeRawDetectorResults(false, /*skip=*/true)}};
    reshapeDetectorEnvelopes(obj);

    const QJsonValue skipVal = obj.value("detectorResults").toObject().value("skipFirstFrame");
    QVERIFY(skipVal.isObject());
    QCOMPARE(skipVal.toObject().value("detected").toBool(), true);
}

void TstMcpToolsShotsHelpers::verdictCategory_isWrappedAsObject()
{
    QJsonObject obj{{"detectorResults",
                     makeRawDetectorResults(true, false, QStringLiteral("puckTruncated"))}};
    reshapeDetectorEnvelopes(obj);

    const QJsonObject verdict = obj.value("detectorResults").toObject().value("verdict").toObject();
    QCOMPARE(verdict.value("category").toString(), QStringLiteral("puckTruncated"));
}

void TstMcpToolsShotsHelpers::oldKeysAreRemoved()
{
    QJsonObject obj{{"detectorResults", makeRawDetectorResults(true, false)}};
    reshapeDetectorEnvelopes(obj);

    const QJsonObject d = obj.value("detectorResults").toObject();
    // None of the old top-level scalar keys should remain.
    QVERIFY(!d.contains("pourTruncated"));
    QVERIFY(!d.contains("pourStartSec"));
    QVERIFY(!d.contains("pourEndSec"));
    QVERIFY(!d.contains("peakPressureBar"));
    QVERIFY(!d.contains("verdictCategory"));
    // skipFirstFrame is repurposed as an object envelope, so it stays as a
    // key but must now be an object, not a bool.
    QVERIFY(d.contains("skipFirstFrame"));
    QVERIFY(d.value("skipFirstFrame").isObject());
}

void TstMcpToolsShotsHelpers::wrappedDetectorsAreUntouched()
{
    QJsonObject grind;
    grind["checked"] = true;
    grind["direction"] = "onTarget";
    QJsonObject channeling;
    channeling["checked"] = false;

    QJsonObject d = makeRawDetectorResults();
    d["grind"] = grind;
    d["channeling"] = channeling;

    QJsonObject obj{{"detectorResults", d}};
    reshapeDetectorEnvelopes(obj);

    const QJsonObject reshaped = obj.value("detectorResults").toObject();
    QCOMPARE(reshaped.value("grind").toObject().value("direction").toString(),
             QStringLiteral("onTarget"));
    QCOMPARE(reshaped.value("channeling").toObject().value("checked").toBool(), false);
}

// A time series as ShotProjection emits it: a list of {x,y} point objects.
static QJsonArray pointSeries()
{
    return QJsonArray{
        QJsonObject{{"x", 0.0}, {"y", 9.0}},
        QJsonObject{{"x", 0.2}, {"y", 8.8}},
    };
}

void TstMcpToolsShotsHelpers::strip_dropsTimeSeries()
{
    QJsonObject obj{
        {"pressure", pointSeries()},
        {"temperatureGoal", pointSeries()},
        {"temperatureMixGoal", pointSeries()},
    };
    stripTimeSeriesFields(obj);

    QVERIFY(!obj.contains("pressure"));
    QVERIFY(!obj.contains("temperatureGoal"));
    QVERIFY(!obj.contains("temperatureMixGoal"));
}

// The reason this is an allowlist. A series added to ShotProjection tomorrow —
// one this function has never heard of — must not reach an LLM just because
// nobody remembered to name it here. That is how temperatureMixGoal leaked.
void TstMcpToolsShotsHelpers::strip_dropsAnUnknownSeries()
{
    QJsonObject obj{{"someFutureSeries", pointSeries()}};
    stripTimeSeriesFields(obj);

    QVERIFY(!obj.contains("someFutureSeries"));
}

void TstMcpToolsShotsHelpers::strip_keepsSummaryArraysAndScalars()
{
    QJsonObject obj{
        {"summaryLines", QJsonArray{QStringLiteral("Clean extraction")}},
        {"phases", QJsonArray{QJsonObject{{"time", 0.0}, {"label", QStringLiteral("preinfusion")}}}},
        {"phaseSummaries", QJsonArray{QJsonObject{{"name", QStringLiteral("pour")}}}},
        {"detectorResults", QJsonObject{{"verdictCategory", QStringLiteral("clean")}}},
        {"id", 42},
        {"profileName", QStringLiteral("D-Flow/Q")},
        {"enjoyment0to100", 80},
    };
    stripTimeSeriesFields(obj);

    // The summary's own arrays survive — they are the payload, not the ballast.
    QCOMPARE(obj.value("summaryLines").toArray().size(), 1);
    QCOMPARE(obj.value("phases").toArray().size(), 1);
    QCOMPARE(obj.value("phaseSummaries").toArray().size(), 1);
    // detectorResults is an object, so the array sweep must not touch it.
    QVERIFY(obj.contains("detectorResults"));
    // Scalars are untouched: adding one must not require an edit here.
    QCOMPARE(obj.value("id").toInt(), 42);
    QCOMPARE(obj.value("profileName").toString(), QStringLiteral("D-Flow/Q"));
    QCOMPARE(obj.value("enjoyment0to100").toInt(), 80);
}

void TstMcpToolsShotsHelpers::strip_dropsHeavyStrings()
{
    QJsonObject obj{
        {"debugLog", QStringLiteral("...50K of log...")},
        {"profileJson", QStringLiteral("{...}")},
        {"espressoNotes", QStringLiteral("tasted good")},
    };
    stripTimeSeriesFields(obj);

    QVERIFY(!obj.contains("debugLog"));
    QVERIFY(!obj.contains("profileJson"));
    // Not heavy — a note is exactly what the advisor wants.
    QCOMPARE(obj.value("espressoNotes").toString(), QStringLiteral("tasted good"));
}

QTEST_APPLESS_MAIN(TstMcpToolsShotsHelpers)

#include "tst_mcptools_shots_helpers.moc"
