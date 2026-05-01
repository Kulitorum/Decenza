// tst_shotrecord_cache — verifies the cachedAnalysis dedup added in change
// `cache-analyzeshot-on-shotrecord`. ShotHistoryStorage::loadShotRecordStatic
// stashes the AnalysisResult on the ShotRecord; convertShotRecord reads from
// there instead of running analyzeShot a second time. Direct-construction
// callers (without the cache) hit the fallback inline-analyzeShot path.
//
// The contract these tests pin down:
//   1. Given a ShotRecord with cachedAnalysis populated, convertShotRecord
//      MUST emit the cached `lines` verbatim (sentinel-based check).
//   2. Given a ShotRecord with no cachedAnalysis, convertShotRecord MUST
//      produce non-empty summaryLines + detectorResults via the fallback
//      analyzeShot call.
//   3. The cached path and the fallback path MUST produce byte-equal output
//      for the same input data.

#include <QtTest>

#include <QVariantMap>
#include <QVariantList>
#include <QVector>
#include <QPointF>
#include <QList>

#include "ai/shotanalysis.h"
#include "history/shothistory_types.h"
#include "history/shothistorystorage.h"

namespace {

QVector<QPointF> flatSeries(double t0, double t1, double value, double rate = 10.0)
{
    QVector<QPointF> pts;
    const double dt = 1.0 / rate;
    for (double t = t0; t <= t1 + 1e-9; t += dt) pts.append(QPointF(t, value));
    return pts;
}

QVector<QPointF> rampSeries(double t0, double t1, double v0, double v1, double rate = 10.0)
{
    QVector<QPointF> pts;
    const double dt = 1.0 / rate;
    const double span = t1 - t0;
    for (double t = t0; t <= t1 + 1e-9; t += dt) {
        const double alpha = span > 0 ? (t - t0) / span : 0.0;
        pts.append(QPointF(t, v0 + alpha * (v1 - v0)));
    }
    return pts;
}

HistoryPhaseMarker makeMarker(double time, const QString& label, int frameNumber, bool isFlowMode = false)
{
    HistoryPhaseMarker m;
    m.time = time;
    m.label = label;
    m.frameNumber = frameNumber;
    m.isFlowMode = isFlowMode;
    return m;
}

// Build a minimal ShotRecord shaped like a clean espresso shot. The exact
// numbers don't matter; analyzeShot just needs >= 10 pressure samples to not
// short-circuit out.
ShotRecord buildHealthyRecord()
{
    ShotRecord record;
    record.summary.id = 42;
    record.summary.uuid = QStringLiteral("test-uuid");
    record.summary.timestamp = 1700000000;
    record.summary.profileName = QStringLiteral("Test Profile");
    record.summary.duration = 30.0;
    record.summary.finalWeight = 36.0;
    record.summary.doseWeight = 18.0;
    record.summary.beverageType = QStringLiteral("espresso");
    record.targetWeight = 36.0;

    record.pressure = rampSeries(0.0, 8.0, 1.0, 9.0);
    record.pressure.append(flatSeries(8.1, 30.0, 9.0));
    record.flow = flatSeries(0.0, 30.0, 1.8);
    record.weight = rampSeries(0.0, 30.0, 0.0, 36.0);
    record.temperature = flatSeries(0.0, 30.0, 92.0);
    record.temperatureGoal = flatSeries(0.0, 30.0, 92.0);
    record.pressureGoal = record.pressure;
    record.flowGoal = flatSeries(0.0, 30.0, 1.8);
    record.conductanceDerivative = flatSeries(0.0, 30.0, 0.0);

    record.phases.append(makeMarker(0.0, QStringLiteral("Preinfusion"), 0, /*isFlowMode=*/true));
    record.phases.append(makeMarker(8.0, QStringLiteral("Pour"), 1, /*isFlowMode=*/false));

    return record;
}

} // namespace

class tst_ShotRecordCache : public QObject {
    Q_OBJECT

private slots:
    // Sentinel test: when cachedAnalysis is populated, convertShotRecord
    // MUST emit those exact lines without recomputing. The sentinel is a
    // string no real detector would produce — if recomputation fired, the
    // sentinel would be replaced by real analyzer output and the assertion
    // would fail.
    void cachedAnalysis_isReusedByConvert()
    {
        ShotRecord record = buildHealthyRecord();

        ShotAnalysis::AnalysisResult cached;
        QVariantMap sentinel;
        sentinel["text"] = QStringLiteral("__CACHE_SENTINEL__");
        sentinel["type"] = QStringLiteral("good");
        cached.lines.append(sentinel);
        cached.detectors.verdictCategory = QStringLiteral("clean");
        cached.detectors.pourTruncated = false;
        record.cachedAnalysis = cached;

        const QVariantMap result = ShotHistoryStorage::convertShotRecord(record).toVariantMap();
        const QVariantList lines = result.value("summaryLines").toList();

        QVERIFY2(!lines.isEmpty(), "summaryLines must be populated from the cache");
        QCOMPARE(lines.first().toMap().value("text").toString(),
                 QStringLiteral("__CACHE_SENTINEL__"));
        QCOMPARE(result.value("detectorResults").toMap().value("verdictCategory").toString(),
                 QStringLiteral("clean"));
    }

    // Fallback test: a ShotRecord constructed without cachedAnalysis (mimics
    // ShotHistoryExporter and other direct-construction paths) MUST still
    // produce a fully-populated summaryLines + detectorResults via the
    // inline analyzeShot fallback. Locks in that the dedup didn't break the
    // legacy direct-construction path.
    void noCachedAnalysis_fallsBackToInlineAnalyzeShot()
    {
        ShotRecord record = buildHealthyRecord();
        // cachedAnalysis intentionally left at default (std::nullopt).

        const QVariantMap result = ShotHistoryStorage::convertShotRecord(record).toVariantMap();
        const QVariantList lines = result.value("summaryLines").toList();
        const QVariantMap detectors = result.value("detectorResults").toMap();

        QVERIFY2(!lines.isEmpty(),
                 "fallback path must populate summaryLines via inline analyzeShot");
        QVERIFY2(detectors.contains(QStringLiteral("verdictCategory")),
                 "fallback path must populate detectorResults");
    }

    // Equivalence test: feed the same shot through both paths and assert
    // byte-equal output. Catches drift if the cached or fallback branches
    // are later modified to compute differently. Both records are built
    // from the same buildHealthyRecord() seed and analyzeShot is
    // deterministic, so we expect identical output — this test pins down
    // that no transformation happens between cache write and
    // convertShotRecord read on the cached path.
    void cachedAndFallback_paths_produceEquivalentOutput()
    {
        ShotRecord recordFallback = buildHealthyRecord();
        const QVariantMap fallbackResult = ShotHistoryStorage::convertShotRecord(recordFallback).toVariantMap();

        ShotRecord recordCached = buildHealthyRecord();
        // Run analyzeShot ourselves (matching what loadShotRecordStatic
        // would do) and stash the result in cachedAnalysis.
        recordCached.cachedAnalysis = ShotAnalysis::analyzeShot(
            recordCached.pressure, recordCached.flow, recordCached.weight,
            recordCached.temperature, recordCached.temperatureGoal,
            recordCached.conductanceDerivative,
            recordCached.phases, recordCached.summary.beverageType,
            recordCached.summary.duration,
            recordCached.pressureGoal, recordCached.flowGoal,
            /*analysisFlags=*/{}, /*firstFrameSec=*/-1.0,
            recordCached.targetWeight, recordCached.summary.finalWeight,
            /*expectedFrameCount=*/-1);
        const QVariantMap cachedResult = ShotHistoryStorage::convertShotRecord(recordCached).toVariantMap();

        // summaryLines must match line-for-line.
        const QVariantList fbLines = fallbackResult.value("summaryLines").toList();
        const QVariantList caLines = cachedResult.value("summaryLines").toList();
        QCOMPARE(caLines.size(), fbLines.size());
        for (qsizetype i = 0; i < fbLines.size(); ++i) {
            QCOMPARE(caLines[i].toMap().value("text").toString(),
                     fbLines[i].toMap().value("text").toString());
            QCOMPARE(caLines[i].toMap().value("type").toString(),
                     fbLines[i].toMap().value("type").toString());
        }

        // verdictCategory and pourTruncated must agree.
        const QVariantMap fbDet = fallbackResult.value("detectorResults").toMap();
        const QVariantMap caDet = cachedResult.value("detectorResults").toMap();
        QCOMPARE(caDet.value("verdictCategory").toString(),
                 fbDet.value("verdictCategory").toString());
        QCOMPARE(caDet.value("pourTruncated").toBool(),
                 fbDet.value("pourTruncated").toBool());
    }

    // Issue #964 Fix A: convertShotRecord exposes the shot's effective target
    // weight as `targetWeightG` (units-suffixed per CLAUDE.md MCP convention),
    // not the legacy `yieldOverride` name that originally meant a per-shot
    // user override.
    void convertShotRecord_emitsTargetWeightG()
    {
        ShotRecord record = buildHealthyRecord();
        record.targetWeight = 36.5;

        const QVariantMap result = ShotHistoryStorage::convertShotRecord(record).toVariantMap();

        QVERIFY2(result.contains("targetWeightG"),
                 "shot detail must expose targetWeightG");
        QCOMPARE(result.value("targetWeightG").toDouble(), 36.5);
    }

    // Issue #964 Fix B: `detectorResults.grind.gates` must surface the choked-
    // puck arm's inputs (pressurizedDurationSec, meanPressurizedFlowMlPerSec,
    // yieldRatio, flowSamples) plus the thresholds it compared against, so MCP
    // consumers can answer "why didn't this badge fire?" without reading C++.
    void detectorResults_grindGates_exposeArm2Inputs()
    {
        ShotRecord record = buildHealthyRecord();
        record.targetWeight = 36.0;

        const QVariantMap result = ShotHistoryStorage::convertShotRecord(record).toVariantMap();
        const QVariantMap detectors = result.value("detectorResults").toMap();
        const QVariantMap grind = detectors.value("grind").toMap();

        QVERIFY2(grind.contains("gates"),
                 "grind block must include gates substruct when Arm 2 ran");

        const QVariantMap gates = grind.value("gates").toMap();
        QVERIFY2(gates.contains("passed"), "gates must expose passed flag");
        QVERIFY2(gates.contains("flowSamples"), "gates must expose flowSamples");
        QVERIFY2(gates.contains("pressurizedDurationSec"),
                 "gates must expose pressurizedDurationSec");
        QVERIFY2(gates.contains("meanPressurizedFlowMlPerSec"),
                 "gates must expose meanPressurizedFlowMlPerSec");
        QVERIFY2(gates.contains("yieldRatio"), "gates must expose yieldRatio");
        QVERIFY2(gates.contains("minSamples"), "gates must expose minSamples threshold");
        QVERIFY2(gates.contains("minPressurizedSec"),
                 "gates must expose minPressurizedSec threshold");

        // Healthy shot: 36g/36g target → ratio 1.0
        QCOMPARE(gates.value("yieldRatio").toDouble(), 1.0);
        // Thresholds must match the constants used in the cascade.
        QCOMPARE(gates.value("minSamples").toInt(), 5);
        QCOMPARE(gates.value("minPressurizedSec").toDouble(),
                 ShotAnalysis::CHOKED_DURATION_MIN_SEC);
    }
};

QTEST_GUILESS_MAIN(tst_ShotRecordCache)

#include "tst_shotrecord_cache.moc"
