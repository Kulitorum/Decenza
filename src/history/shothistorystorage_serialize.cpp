// ShotHistoryStorage::convertShotRecord — serialization of a fully-loaded
// ShotRecord into the QVariantMap shape consumed by QML, MCP, and the
// Visualizer/Decenza upload flows. Split out of the main TU because it
// owns ~200 lines of mechanical key-value mapping plus the
// analyzeShot-fast-path-vs-fallback branch — a separate concern from the
// DB lifecycle / save / load code that lives in shothistorystorage.cpp.
//
// convertShotRecord is a static method (declared `static` in
// shothistorystorage.h), so all state is reached through the `record`
// argument with no `this` pointer involved — that's what makes the
// file split mechanical. Behavior is identical to the pre-split
// implementation.

#include "shothistorystorage.h"
#include "shothistorystorage_internal.h"

#include "ai/shotanalysis.h"

#include <QDateTime>
#include <QJsonDocument>

QVariantMap ShotHistoryStorage::convertShotRecord(const ShotRecord& record)
{
    using decenza::storage::detail::AnalysisInputs;
    using decenza::storage::detail::prepareAnalysisInputs;
    using decenza::storage::detail::use12h;

    QVariantMap result;
    if (record.summary.id == 0) return result;

    result["id"] = record.summary.id;
    result["uuid"] = record.summary.uuid;
    result["timestamp"] = record.summary.timestamp;
    // ISO 8601 with timezone for MCP consumers (CLAUDE.md: "Never return Unix timestamps")
    auto isodt = QDateTime::fromSecsSinceEpoch(record.summary.timestamp);
    result["timestampIso"] = isodt.toOffsetFromUtc(isodt.offsetFromUtc()).toString(Qt::ISODate);
    result["profileName"] = record.summary.profileName;
    result["duration"] = record.summary.duration;
    result["finalWeight"] = record.summary.finalWeight;
    result["doseWeight"] = record.summary.doseWeight;
    result["beanBrand"] = record.summary.beanBrand;
    result["beanType"] = record.summary.beanType;
    result["enjoyment"] = record.summary.enjoyment;
    result["hasVisualizerUpload"] = record.summary.hasVisualizerUpload;
    result["beverageType"] = record.summary.beverageType;
    result["roastDate"] = record.roastDate;
    result["roastLevel"] = record.roastLevel;
    result["grinderBrand"] = record.grinderBrand;
    result["grinderModel"] = record.grinderModel;
    result["grinderBurrs"] = record.grinderBurrs;
    result["grinderSetting"] = record.grinderSetting;
    result["drinkTds"] = record.drinkTds;
    result["drinkEy"] = record.drinkEy;
    result["espressoNotes"] = record.espressoNotes;
    result["beanNotes"] = record.beanNotes;
    result["barista"] = record.barista;
    result["profileNotes"] = record.profileNotes;
    result["visualizerId"] = record.visualizerId;
    result["visualizerUrl"] = record.visualizerUrl;
    result["debugLog"] = record.debugLog;
    result["temperatureOverride"] = record.temperatureOverride;
    result["targetWeightG"] = record.targetWeight;
    result["profileJson"] = record.profileJson;
    result["profileKbId"] = record.profileKbId;

    auto pointsToVariant = [](const QVector<QPointF>& points) {
        QVariantList list;
        for (const auto& pt : points) {
            QVariantMap p;
            p["x"] = pt.x();
            p["y"] = pt.y();
            list.append(p);
        }
        return list;
    };

    result["pressure"] = pointsToVariant(record.pressure);
    result["flow"] = pointsToVariant(record.flow);
    result["temperature"] = pointsToVariant(record.temperature);
    result["temperatureMix"] = pointsToVariant(record.temperatureMix);
    result["resistance"] = pointsToVariant(record.resistance);
    result["conductance"] = pointsToVariant(record.conductance);
    result["darcyResistance"] = pointsToVariant(record.darcyResistance);
    result["conductanceDerivative"] = pointsToVariant(record.conductanceDerivative);
    result["waterDispensed"] = pointsToVariant(record.waterDispensed);
    result["pressureGoal"] = pointsToVariant(record.pressureGoal);
    result["flowGoal"] = pointsToVariant(record.flowGoal);
    result["temperatureGoal"] = pointsToVariant(record.temperatureGoal);
    result["weight"] = pointsToVariant(record.weight);
    result["weightFlowRate"] = pointsToVariant(record.weightFlowRate);

    result["channelingDetected"] = record.channelingDetected;
    result["temperatureUnstable"] = record.temperatureUnstable;
    result["grindIssueDetected"] = record.grindIssueDetected;
    result["skipFirstFrameDetected"] = record.skipFirstFrameDetected;
    result["pourTruncatedDetected"] = record.pourTruncatedDetected;

    // Run the full shot-summary detector pipeline once and expose both the
    // prose lines (rendered by the in-app dialog) and the structured detector
    // outputs (consumed by external MCP agents). Sharing one analyzeShot()
    // call guarantees the prose and the structured fields describe the same
    // evaluation — no chance for them to drift across consumers.
    //
    // Fast path: when the ShotRecord came out of loadShotRecordStatic, the
    // AnalysisResult is already cached on `record.cachedAnalysis` (populated
    // alongside the badge projection). Read from there to avoid running
    // analyzeShot a second time on identical inputs.
    //
    // Slow path: direct-construction callers (tests, any future path that
    // bypasses loadShotRecordStatic) hand us a ShotRecord without
    // `cachedAnalysis`. prepareAnalysisInputs bundles analysisFlags +
    // firstFrameSeconds + frameCount so this call site stays in lock-step
    // with saveShot and loadShotRecordStatic — same lookups, same args
    // passed to analyzeShot.
    {
        ShotAnalysis::AnalysisResult analysisOwned;  // storage if we need to compute
        const ShotAnalysis::AnalysisResult* analysisPtr = nullptr;
        if (record.cachedAnalysis.has_value()) {
            analysisPtr = &record.cachedAnalysis.value();
        } else {
            const AnalysisInputs inputs = prepareAnalysisInputs(record.profileKbId, record.profileJson);
            analysisOwned = ShotAnalysis::analyzeShot(
                record.pressure, record.flow, record.weight,
                record.temperature, record.temperatureGoal, record.conductanceDerivative,
                record.phases, record.summary.beverageType, record.summary.duration,
                record.pressureGoal, record.flowGoal,
                inputs.analysisFlags, inputs.firstFrameSeconds,
                record.targetWeight, record.summary.finalWeight,
                inputs.frameCount);
            analysisPtr = &analysisOwned;
        }
        const ShotAnalysis::AnalysisResult& analysis = *analysisPtr;
        result["summaryLines"] = analysis.lines;

        const auto& d = analysis.detectors;
        QVariantMap detectorResults;

        QVariantMap channeling;
        channeling["checked"] = d.channelingChecked;
        if (d.channelingChecked) {
            channeling["severity"] = d.channelingSeverity;
            channeling["spikeTimeSec"] = d.channelingSpikeTimeSec;
        }
        detectorResults["channeling"] = channeling;

        QVariantMap flowTrend;
        flowTrend["checked"] = d.flowTrendChecked;
        if (d.flowTrendChecked) {
            flowTrend["direction"] = d.flowTrend;
            flowTrend["deltaMlPerSec"] = d.flowTrendDeltaMlPerSec;
        }
        detectorResults["flowTrend"] = flowTrend;

        QVariantMap preinfusion;
        preinfusion["observed"] = d.preinfusionObserved;
        if (d.preinfusionObserved) {
            preinfusion["dripWeightG"] = d.preinfusionDripWeightG;
            preinfusion["durationSec"] = d.preinfusionDripDurationSec;
        }
        detectorResults["preinfusion"] = preinfusion;

        QVariantMap tempStability;
        tempStability["checked"] = d.tempStabilityChecked;
        if (d.tempStabilityChecked) {
            tempStability["intentionalStepping"] = d.tempIntentionalStepping;
            tempStability["avgDeviationC"] = d.tempAvgDeviationC;
            tempStability["unstable"] = d.tempUnstable;
        }
        detectorResults["tempStability"] = tempStability;

        QVariantMap grind;
        grind["checked"] = d.grindChecked;
        grind["hasData"] = d.grindHasData;
        if (d.grindHasData) {
            grind["direction"] = d.grindDirection;
            grind["deltaMlPerSec"] = d.grindFlowDeltaMlPerSec;
            grind["sampleCount"] = static_cast<qlonglong>(d.grindSampleCount);
            grind["chokedPuck"] = d.grindChokedPuck;
            grind["yieldOvershoot"] = d.grindYieldOvershoot;
            grind["verifiedClean"] = d.grindVerifiedClean;
            // yieldRatio is populated whenever Arm 2's gate ran with a known
            // target+final pair. Lifted to the top of the grind object (in
            // addition to gates.yieldRatio) so consumers can read the value
            // both checks compare against without descending into gates.
            if (d.grindGateYieldRatio > 0.0) {
                grind["yieldRatio"] = d.grindGateYieldRatio;
            }
        }
        if (!d.grindCoverage.isEmpty()) {
            grind["coverage"] = d.grindCoverage;
        }
        // Gate diagnostics from the choked-puck arm. Always emitted when the
        // arm ran (`gateRan == true`) — even on gate-fail paths — so MCP
        // consumers can answer "why didn't this badge fire?" without parsing
        // the cascade source. Thresholds are emitted alongside the inputs
        // so consumers don't need to re-derive them from CHOKED_* constants.
        if (d.grindGateRan) {
            QVariantMap gates;
            gates["passed"] = d.grindGatePassed;
            gates["flowSamples"] = static_cast<qlonglong>(d.grindGateFlowSamples);
            gates["pressurizedDurationSec"] = d.grindGatePressurizedDurationSec;
            gates["meanPressurizedFlowMlPerSec"] = d.grindGateMeanPressurizedFlowMlPerSec;
            gates["yieldRatio"] = d.grindGateYieldRatio;
            gates["minSamples"] = 5;
            gates["minPressurizedSec"] = ShotAnalysis::CHOKED_DURATION_MIN_SEC;
            gates["minPressureBar"] = ShotAnalysis::CHOKED_PRESSURE_MIN_BAR;
            gates["chokedFlowMaxMlPerSec"] = ShotAnalysis::CHOKED_FLOW_MAX_MLPS;
            gates["chokedYieldRatioMax"] = ShotAnalysis::CHOKED_YIELD_RATIO_MAX;
            gates["yieldOvershootRatioMin"] = ShotAnalysis::YIELD_OVERSHOOT_RATIO_MIN;
            grind["gates"] = gates;
        }
        detectorResults["grind"] = grind;

        detectorResults["pourTruncated"] = d.pourTruncated;
        if (d.pourTruncated) detectorResults["peakPressureBar"] = d.peakPressureBar;
        detectorResults["pourStartSec"] = d.pourStartSec;
        detectorResults["pourEndSec"] = d.pourEndSec;
        detectorResults["skipFirstFrame"] = d.skipFirstFrame;
        detectorResults["verdictCategory"] = d.verdictCategory;

        result["detectorResults"] = detectorResults;
    }

    // Phase summaries for UI (computed at save time or on-the-fly for legacy shots)
    if (!record.phaseSummariesJson.isEmpty()) {
        QJsonDocument phaseSummariesDoc = QJsonDocument::fromJson(record.phaseSummariesJson.toUtf8());
        result["phaseSummaries"] = phaseSummariesDoc.toVariant();
    }

    QVariantList phases;
    for (const auto& phase : record.phases) {
        QVariantMap p;
        p["time"] = phase.time;
        p["label"] = phase.label;
        p["frameNumber"] = phase.frameNumber;
        p["isFlowMode"] = phase.isFlowMode;
        p["transitionReason"] = phase.transitionReason;
        phases.append(p);
    }
    result["phases"] = phases;

    QDateTime dt = QDateTime::fromSecsSinceEpoch(record.summary.timestamp);
    result["dateTime"] = dt.toString(use12h() ? "yyyy-MM-dd h:mm:ss AP" : "yyyy-MM-dd HH:mm:ss");

    return result;
}
