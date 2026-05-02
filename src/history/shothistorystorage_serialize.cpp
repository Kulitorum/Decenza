// ShotHistoryStorage::convertShotRecord — builds a typed ShotProjection from
// a fully-loaded ShotRecord. The body sits next to shothistorystorage.cpp
// rather than in shotprojection.cpp because it depends on the internal-detail
// helpers in shothistorystorage_internal.cpp (prepareAnalysisInputs, use12h).
// Co-locating the call sites means tests that pull in shotprojection.cpp
// don't transitively need internal.cpp — the dependency edge stays inside
// the history TU set that already pulls in internal.cpp anyway.

#include "shothistorystorage.h"
#include "shothistorystorage_internal.h"
#include "shotprojection.h"

#include "ai/shotanalysis.h"

#include <QDateTime>
#include <QJsonDocument>

namespace {

QVariantList pointsToVariant(const QVector<QPointF>& points)
{
    QVariantList list;
    list.reserve(points.size());
    for (const auto& pt : points) {
        QVariantMap p;
        p["x"] = pt.x();
        p["y"] = pt.y();
        list.append(p);
    }
    return list;
}

} // namespace

ShotProjection ShotHistoryStorage::convertShotRecord(const ShotRecord& record)
{
    using decenza::storage::detail::AnalysisInputs;
    using decenza::storage::detail::prepareAnalysisInputs;
    using decenza::storage::detail::use12h;

    ShotProjection p;
    if (record.summary.id == 0) return p;

    p.id = record.summary.id;
    p.uuid = record.summary.uuid;
    p.timestamp = record.summary.timestamp;
    auto isodt = QDateTime::fromSecsSinceEpoch(record.summary.timestamp);
    p.timestampIso = isodt.toOffsetFromUtc(isodt.offsetFromUtc()).toString(Qt::ISODate);
    p.profileName = record.summary.profileName;
    p.durationSec = record.summary.duration;
    p.finalWeightG = record.summary.finalWeight;
    p.doseWeightG = record.summary.doseWeight;
    p.beanBrand = record.summary.beanBrand;
    p.beanType = record.summary.beanType;
    p.enjoyment0to100 = record.summary.enjoyment;
    p.enjoymentSource = record.enjoymentSource.isEmpty()
        ? QStringLiteral("none") : record.enjoymentSource;
    p.hasVisualizerUpload = record.summary.hasVisualizerUpload;
    p.beverageType = record.summary.beverageType;
    p.roastDate = record.roastDate;
    p.roastLevel = record.roastLevel;
    p.grinderBrand = record.grinderBrand;
    p.grinderModel = record.grinderModel;
    p.grinderBurrs = record.grinderBurrs;
    p.grinderSetting = record.grinderSetting;
    p.drinkTdsPct = record.drinkTds;
    p.drinkEyPct = record.drinkEy;
    p.espressoNotes = record.espressoNotes;
    p.beanNotes = record.beanNotes;
    p.barista = record.barista;
    p.profileNotes = record.profileNotes;
    p.visualizerId = record.visualizerId;
    p.visualizerUrl = record.visualizerUrl;
    p.debugLog = record.debugLog;
    p.temperatureOverrideC = record.temperatureOverride;
    p.targetWeightG = record.targetWeight;
    p.profileJson = record.profileJson;
    p.profileKbId = record.profileKbId;

    p.pressure = pointsToVariant(record.pressure);
    p.flow = pointsToVariant(record.flow);
    p.temperature = pointsToVariant(record.temperature);
    p.temperatureMix = pointsToVariant(record.temperatureMix);
    p.resistance = pointsToVariant(record.resistance);
    p.conductance = pointsToVariant(record.conductance);
    p.darcyResistance = pointsToVariant(record.darcyResistance);
    p.conductanceDerivative = pointsToVariant(record.conductanceDerivative);
    p.waterDispensed = pointsToVariant(record.waterDispensed);
    p.pressureGoal = pointsToVariant(record.pressureGoal);
    p.flowGoal = pointsToVariant(record.flowGoal);
    p.temperatureGoal = pointsToVariant(record.temperatureGoal);
    p.weight = pointsToVariant(record.weight);
    p.weightFlowRate = pointsToVariant(record.weightFlowRate);

    p.channelingDetected = record.channelingDetected;
    p.temperatureUnstable = record.temperatureUnstable;
    p.grindIssueDetected = record.grindIssueDetected;
    p.skipFirstFrameDetected = record.skipFirstFrameDetected;
    p.pourTruncatedDetected = record.pourTruncatedDetected;

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
    // Slow path: ShotRecords without a cached analysis (test fixtures, direct
    // construction) own a fresh AnalysisResult in `analysisOwned`, and
    // `analysisPtr` points at it — keeping the address stable across the
    // dereference below.
    {
        ShotAnalysis::AnalysisResult analysisOwned;
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
        p.summaryLines = analysis.lines;

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
            if (d.grindGateYieldRatio > 0.0) {
                grind["yieldRatio"] = d.grindGateYieldRatio;
            }
        }
        if (!d.grindCoverage.isEmpty()) {
            grind["coverage"] = d.grindCoverage;
        }
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

        p.detectorResults = detectorResults;
    }

    if (!record.phaseSummariesJson.isEmpty()) {
        QJsonDocument phaseSummariesDoc = QJsonDocument::fromJson(record.phaseSummariesJson.toUtf8());
        p.phaseSummaries = phaseSummariesDoc.toVariant();
    }

    QVariantList phases;
    phases.reserve(record.phases.size());
    for (const auto& phase : record.phases) {
        QVariantMap phaseMap;
        phaseMap["time"] = phase.time;
        phaseMap["label"] = phase.label;
        phaseMap["frameNumber"] = phase.frameNumber;
        phaseMap["isFlowMode"] = phase.isFlowMode;
        phaseMap["transitionReason"] = phase.transitionReason;
        phases.append(phaseMap);
    }
    p.phases = phases;

    QDateTime dt = QDateTime::fromSecsSinceEpoch(record.summary.timestamp);
    p.dateTime = dt.toString(use12h() ? "yyyy-MM-dd h:mm:ss AP" : "yyyy-MM-dd HH:mm:ss");

    return p;
}
