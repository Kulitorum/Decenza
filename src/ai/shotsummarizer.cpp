#include "shotsummarizer.h"
#include "shotanalysis.h"
#include "../history/shothistory_types.h"  // HistoryPhaseMarker — passed to ShotAnalysis::analyzeShot
#include "../models/shotdatamodel.h"
#include "../profile/profile.h"
#include "../network/visualizeruploader.h"  // ShotMetadata struct (lives in this header for historical reasons)
#include "../core/grinderaliases.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QDate>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

// Static members for profile knowledge cache
QMap<QString, ShotSummarizer::ProfileKnowledge> ShotSummarizer::s_profileKnowledge;
bool ShotSummarizer::s_knowledgeLoaded = false;

// Static cache for profile catalog (compact one-liner per KB profile)
QString ShotSummarizer::s_profileCatalog;

// Static cache for dial-in reference tables
QString ShotSummarizer::s_dialInReference;
bool ShotSummarizer::s_dialInReferenceLoaded = false;

// Normalize a profile key: lowercase, strip diacritics, normalize punctuation
static QString normalizeProfileKey(const QString& key)
{
    QString normalized = key.toLower().trimmed();
    // Normalize common diacritics (é→e, è→e, ê→e, ë→e, etc.)
    normalized.replace(QChar(0x00E9), 'e');  // é
    normalized.replace(QChar(0x00E8), 'e');  // è
    normalized.replace(QChar(0x00EA), 'e');  // ê
    normalized.replace(QChar(0x00EB), 'e');  // ë
    // Normalize & ↔ and
    normalized.replace(QStringLiteral(" & "), QStringLiteral(" and "));
    return normalized;
}

ShotSummarizer::ShotSummarizer(QObject* parent)
    : QObject(parent)
{
}

QString ShotSummarizer::profileTypeDescription(const QString& editorType)
{
    if (editorType == "dflow") return "D-Flow (lever-style: pressure peaks then declines during flow extraction)";
    if (editorType == "aflow") return "A-Flow (pressure ramp into flow extraction)";
    if (editorType == "pressure") return "Pressure profile (pressure-controlled extraction)";
    if (editorType == "flow") return "Flow profile (flow-controlled extraction)";
    return QString();
}

PhaseSummary ShotSummarizer::makeWholeShotPhase(const QVector<QPointF>& pressure,
                                                const QVector<QPointF>& flow,
                                                const QVector<QPointF>& temperature,
                                                const QVector<QPointF>& weight,
                                                double totalDuration)
{
    PhaseSummary phase;
    phase.name = QStringLiteral("Extraction");
    phase.startTime = 0;
    phase.endTime = totalDuration;
    phase.duration = totalDuration;

    phase.avgPressure = calculateAverage(pressure, 0, totalDuration);
    phase.maxPressure = calculateMax(pressure, 0, totalDuration);
    phase.minPressure = calculateMin(pressure, 0, totalDuration);
    phase.pressureAtStart = findValueAtTime(pressure, 0);
    phase.pressureAtMiddle = findValueAtTime(pressure, totalDuration / 2);
    phase.pressureAtEnd = findValueAtTime(pressure, totalDuration);

    phase.avgFlow = calculateAverage(flow, 0, totalDuration);
    phase.maxFlow = calculateMax(flow, 0, totalDuration);
    phase.minFlow = calculateMin(flow, 0, totalDuration);
    phase.flowAtStart = findValueAtTime(flow, 0);
    phase.flowAtMiddle = findValueAtTime(flow, totalDuration / 2);
    phase.flowAtEnd = findValueAtTime(flow, totalDuration);

    phase.avgTemperature = calculateAverage(temperature, 0, totalDuration);

    if (!weight.isEmpty()) {
        const double startWeight = findValueAtTime(weight, 0);
        const double endWeight = findValueAtTime(weight, totalDuration);
        phase.weightGained = endWeight - startWeight;
    }
    return phase;
}

QList<PhaseSummary> ShotSummarizer::buildPhaseSummariesForRange(
    const QVector<QPointF>& pressure,
    const QVector<QPointF>& flow,
    const QVector<QPointF>& temperature,
    const QVector<QPointF>& weight,
    const QList<HistoryPhaseMarker>& markers,
    double totalDuration)
{
    QList<PhaseSummary> phases;
    phases.reserve(markers.size());
    for (qsizetype i = 0; i < markers.size(); i++) {
        const HistoryPhaseMarker& marker = markers[i];
        const double startTime = marker.time;
        const double endTime = (i + 1 < markers.size())
            ? markers[i + 1].time
            : totalDuration;
        // Degenerate phases (endTime <= startTime) skip the per-phase metric
        // computation but the caller's parallel marker list still appended
        // the corresponding HistoryPhaseMarker — frame transitions matter to
        // skip-first-frame detection even when their span is degenerate.
        if (endTime <= startTime) continue;

        PhaseSummary phase;
        phase.name = marker.label;
        phase.startTime = startTime;
        phase.endTime = endTime;
        phase.duration = endTime - startTime;
        phase.isFlowMode = marker.isFlowMode;

        phase.avgPressure = calculateAverage(pressure, startTime, endTime);
        phase.maxPressure = calculateMax(pressure, startTime, endTime);
        phase.minPressure = calculateMin(pressure, startTime, endTime);
        phase.pressureAtStart = findValueAtTime(pressure, startTime);
        phase.pressureAtMiddle = findValueAtTime(pressure, (startTime + endTime) / 2);
        phase.pressureAtEnd = findValueAtTime(pressure, endTime);

        phase.avgFlow = calculateAverage(flow, startTime, endTime);
        phase.maxFlow = calculateMax(flow, startTime, endTime);
        phase.minFlow = calculateMin(flow, startTime, endTime);
        phase.flowAtStart = findValueAtTime(flow, startTime);
        phase.flowAtMiddle = findValueAtTime(flow, (startTime + endTime) / 2);
        phase.flowAtEnd = findValueAtTime(flow, endTime);

        phase.avgTemperature = calculateAverage(temperature, startTime, endTime);

        if (!weight.isEmpty()) {
            const double startWeight = findValueAtTime(weight, startTime);
            const double endWeight = findValueAtTime(weight, endTime);
            phase.weightGained = endWeight - startWeight;
        }

        phases.append(phase);
    }
    return phases;
}


void ShotSummarizer::markPerPhaseTempInstability(ShotSummary& summary,
    const QVector<QPointF>& tempData, const QVector<QPointF>& tempGoalData) const
{
    if (tempGoalData.isEmpty()) return;

    for (auto& phase : summary.phases) {
        if (ShotAnalysis::hasIntentionalTempStepping(tempGoalData, phase.startTime, phase.endTime)) {
            phase.temperatureUnstable = false;
            continue;
        }
        double avgDev = ShotAnalysis::avgTempDeviation(tempData, tempGoalData, phase.startTime, phase.endTime);
        phase.temperatureUnstable = (avgDev > ShotAnalysis::TEMP_UNSTABLE_THRESHOLD);
    }
}

void ShotSummarizer::runShotAnalysisAndPopulate(ShotSummary& summary,
    const QVector<QPointF>& pressure,
    const QVector<QPointF>& flow,
    const QVector<QPointF>& weight,
    const QVector<QPointF>& temperature,
    const QVector<QPointF>& temperatureGoal,
    const QVector<QPointF>& conductanceDerivative,
    const QList<HistoryPhaseMarker>& markers,
    const QVector<QPointF>& pressureGoal,
    const QVector<QPointF>& flowGoal,
    const QStringList& analysisFlags,
    double firstFrameSeconds,
    double targetWeightG,
    int frameCount) const
{
    const ShotAnalysis::AnalysisResult analysis = ShotAnalysis::analyzeShot(
        pressure, flow, weight, temperature, temperatureGoal,
        conductanceDerivative, markers,
        summary.beverageType, summary.totalDuration,
        pressureGoal, flowGoal, analysisFlags,
        firstFrameSeconds, targetWeightG, summary.finalWeight,
        frameCount);
    summary.summaryLines = analysis.lines;
    summary.pourTruncatedDetected = analysis.detectors.pourTruncated;
    if (!summary.pourTruncatedDetected
        && ShotAnalysis::reachedExtractionPhase(markers, summary.totalDuration))
        markPerPhaseTempInstability(summary, temperature, temperatureGoal);
}

ShotSummary ShotSummarizer::summarize(const ShotDataModel* shotData,
                                       const Profile* profile,
                                       const ShotMetadata& metadata,
                                       double doseWeight,
                                       double finalWeight) const
{
    ShotSummary summary;

    if (!shotData) {
        return summary;
    }

    // Profile info
    if (profile) {
        summary.profileTitle = profile->title();
        summary.profileNotes = profile->profileNotes();
        summary.profileAuthor = profile->author();
        summary.beverageType = profile->beverageType();
        summary.profileRecipeDescription = profile->describeFrames();
        summary.targetWeight = profile->targetWeight();

        // Profile style from editor type — tells the AI what kind of extraction curve to expect
        QString editorStr = profile->editorType();
        if (editorStr != QLatin1String("advanced")) {
            summary.profileType = profileTypeDescription(editorStr);
        } else {
            summary.profileType = profile->mode() == Profile::Mode::FrameBased ? "Frame-based" : "Direct Control";
        }

        summary.profileKbId = computeProfileKbId(profile->title(), editorStr);
    }

    // Get the data vectors
    const auto& pressureData = shotData->pressureData();
    const auto& flowData = shotData->flowData();
    const auto& tempData = shotData->temperatureData();
    const auto& cumulativeWeightData = shotData->cumulativeWeightData();  // Cumulative weight (g)

    if (pressureData.isEmpty()) {
        return summary;
    }

    // Store raw curve data for detailed AI analysis
    summary.pressureCurve = pressureData;
    summary.flowCurve = flowData;
    summary.tempCurve = tempData;
    summary.weightCurve = cumulativeWeightData;  // Cumulative weight (g) — matches history path

    // Store target/goal curves (what the profile intended)
    summary.pressureGoalCurve = shotData->pressureGoalData();
    summary.flowGoalCurve = shotData->flowGoalData();
    summary.tempGoalCurve = shotData->temperatureGoalData();

    // Overall metrics
    summary.totalDuration = pressureData.last().x();
    summary.doseWeight = doseWeight;
    summary.finalWeight = finalWeight;
    summary.ratio = doseWeight > 0 ? finalWeight / doseWeight : 0;

    // DYE metadata
    summary.beanBrand = metadata.beanBrand;
    summary.beanType = metadata.beanType;
    summary.roastDate = metadata.roastDate;
    summary.roastLevel = metadata.roastLevel;
    summary.grinderBrand = metadata.grinderBrand;
    summary.grinderModel = metadata.grinderModel;
    summary.grinderBurrs = metadata.grinderBurrs;
    summary.grinderSetting = metadata.grinderSetting;
    summary.drinkTds = metadata.drinkTds;
    summary.drinkEy = metadata.drinkEy;
    summary.enjoymentScore = metadata.espressoEnjoyment;
    summary.tastingNotes = metadata.espressoNotes;

    // Phase processing — walk the typed marker list once to build the
    // HistoryPhaseMarker stream `analyzeShot` consumes, then hand that stream
    // to buildPhaseSummariesForRange to compute the per-phase metrics for
    // the AI prompt. Detector orchestration runs after both passes complete.
    QList<HistoryPhaseMarker> historyMarkers;
    const auto& markers = shotData->phaseMarkersList();
    historyMarkers.reserve(markers.size());

    if (markers.isEmpty()) {
        summary.phases.append(makeWholeShotPhase(pressureData, flowData,
                                                 tempData, cumulativeWeightData,
                                                 summary.totalDuration));
    } else {
        // Build the typed marker list once; it feeds both the per-phase
        // metric helper and ShotAnalysis::analyzeShot. The marker list can
        // differ in length from the resulting PhaseSummary list — degenerate
        // phases (endTime <= startTime) contribute a marker (frame
        // transitions matter to skip-first-frame detection) but no
        // PhaseSummary entry. They are consumed by different code paths
        // and never joined by index.
        for (qsizetype i = 0; i < markers.size(); i++) {
            const PhaseMarker& marker = markers[i];
            HistoryPhaseMarker h;
            h.time = marker.time;
            h.label = marker.label;
            h.frameNumber = marker.frameNumber;
            h.isFlowMode = marker.isFlowMode;
            h.transitionReason = marker.transitionReason;
            historyMarkers.append(h);
        }
        summary.phases = buildPhaseSummariesForRange(
            pressureData, flowData, tempData, cumulativeWeightData,
            historyMarkers, summary.totalDuration);
    }

    // Detector orchestration delegated to runShotAnalysisAndPopulate, the
    // shared helper that wraps analyzeShot and stamps both summaryLines and
    // pourTruncatedDetected onto summary. The suppression cascade (pour
    // truncated → channeling/temp/grind forced false) lives in exactly one
    // place — see SHOT_REVIEW.md §3.
    const auto& tempGoalData = shotData->temperatureGoalData();
    const QStringList analysisFlags = getAnalysisFlags(summary.profileKbId);
    const double firstFrameSeconds = (profile && !profile->steps().isEmpty())
        ? profile->steps().first().seconds : -1.0;
    // Pass the profile's real frame count so detectSkipFirstFrame correctly
    // suppresses 1-frame profiles (no second frame to skip to). Without this,
    // analyzeShot would default to expectedFrameCount = -1 and emit a false-
    // positive "First profile step skipped" line on every 1-frame shot — a
    // divergence from the save/load/MCP paths which already pass frameCount.
    const int frameCount = (profile && !profile->steps().isEmpty())
        ? static_cast<int>(profile->steps().size()) : -1;

    runShotAnalysisAndPopulate(summary,
        pressureData, flowData, cumulativeWeightData, tempData, tempGoalData,
        shotData->conductanceDerivativeData(), historyMarkers,
        summary.pressureGoalCurve, summary.flowGoalCurve, analysisFlags,
        firstFrameSeconds, summary.targetWeight, frameCount);

    return summary;
}

// Helper to convert QVariantList of {x, y} maps to QVector<QPointF>
static QVector<QPointF> variantListToPoints(const QVariantList& list)
{
    QVector<QPointF> points;
    points.reserve(list.size());
    for (const QVariant& v : list) {
        QVariantMap p = v.toMap();
        points.append(QPointF(p.value("x", 0.0).toDouble(), p.value("y", 0.0).toDouble()));
    }
    return points;
}

ShotSummary ShotSummarizer::summarizeFromHistory(const ShotProjection& shotData) const
{
    ShotSummary summary;

    // Profile info
    summary.profileTitle = shotData.profileName.isEmpty() ? QStringLiteral("Unknown") : shotData.profileName;
    summary.beverageType = shotData.beverageType.isEmpty() ? QStringLiteral("espresso") : shotData.beverageType;
    summary.profileNotes = shotData.profileNotes;
    summary.profileKbId = shotData.profileKbId;

    // Parse stored profile JSON once and use it for: (1) editorType-derived
    // profile-style description, (2) frame description, (3) firstFrameSeconds
    // for skip-first-frame detection. Was three separate parses; now one.
    const QString profileJson = shotData.profileJson;
    QJsonDocument profileDoc;
    if (!profileJson.isEmpty()) {
        profileDoc = QJsonDocument::fromJson(profileJson.toUtf8());
        if (profileDoc.isObject()) {
            const QJsonObject profileObj = profileDoc.object();
            // Derive editorType from title + profileType (matching Profile::editorType()).
            // Legacy shots may also have is_recipe_mode + recipe.editorType as a fallback.
            QString editorType;
            const QString title = profileObj["title"].toString();
            const QString t = title.startsWith(QLatin1Char('*')) ? title.mid(1) : title;
            if (t.startsWith(QStringLiteral("D-Flow"), Qt::CaseInsensitive))
                editorType = QStringLiteral("dflow");
            else if (t.startsWith(QStringLiteral("A-Flow"), Qt::CaseInsensitive))
                editorType = QStringLiteral("aflow");
            if (editorType.isEmpty()) {
                // Legacy fallback: is_recipe_mode + recipe.editorType (pre-PR#579 shots)
                if (profileObj["is_recipe_mode"].toBool(false) && profileObj.contains("recipe"))
                    editorType = profileObj["recipe"].toObject()["editorType"].toString();
            }
            if (editorType.isEmpty()) {
                QString profileType = profileObj["legacy_profile_type"].toString();
                if (profileType.isEmpty()) profileType = profileObj["profile_type"].toString();
                if (profileType == QLatin1String("settings_2a")) editorType = QStringLiteral("pressure");
                else if (profileType == QLatin1String("settings_2b")) editorType = QStringLiteral("flow");
            }
            if (!editorType.isEmpty() && editorType != QLatin1String("advanced"))
                summary.profileType = profileTypeDescription(editorType);
        }
        summary.profileRecipeDescription = Profile::describeFramesFromJson(profileJson);
    }

    // Overall metrics
    summary.doseWeight = shotData.doseWeightG;
    summary.finalWeight = shotData.finalWeightG;
    summary.totalDuration = shotData.durationSec;
    summary.ratio = summary.doseWeight > 0 ? summary.finalWeight / summary.doseWeight : 0;

    // DYE metadata
    summary.beanBrand = shotData.beanBrand;
    summary.beanType = shotData.beanType;
    summary.roastDate = shotData.roastDate;
    summary.roastLevel = shotData.roastLevel;
    summary.grinderBrand = shotData.grinderBrand;
    summary.grinderModel = shotData.grinderModel;
    summary.grinderBurrs = shotData.grinderBurrs;
    summary.grinderSetting = shotData.grinderSetting;
    summary.drinkTds = shotData.drinkTdsPct;
    summary.drinkEy = shotData.drinkEyPct;
    summary.enjoymentScore = shotData.enjoyment0to100;
    summary.tastingNotes = shotData.espressoNotes;

    // Convert curve data
    summary.pressureCurve = variantListToPoints(shotData.pressure);
    summary.flowCurve = variantListToPoints(shotData.flow);
    summary.tempCurve = variantListToPoints(shotData.temperature);
    summary.weightCurve = variantListToPoints(shotData.weight);
    summary.pressureGoalCurve = variantListToPoints(shotData.pressureGoal);
    summary.flowGoalCurve = variantListToPoints(shotData.flowGoal);
    summary.tempGoalCurve = variantListToPoints(shotData.temperatureGoal);

    if (summary.pressureCurve.isEmpty()) return summary;

    // Phase processing — build PhaseSummary (per-phase metrics for the prompt)
    // and HistoryPhaseMarker (typed input for ShotAnalysis::analyzeShot)
    // in a single pass over the stored phase list. Skipped-phase rows still
    // contribute their HistoryPhaseMarker (frame transitions matter to
    // skip-first-frame detection even when their span is degenerate).
    QList<HistoryPhaseMarker> historyMarkers;
    const QVariantList phases = shotData.phases;
    historyMarkers.reserve(phases.size());

    if (!phases.isEmpty()) {
        // Build the typed marker list once; it feeds both the per-phase
        // metric helper and ShotAnalysis::analyzeShot. Skipped-phase rows
        // (degenerate spans handled by buildPhaseSummariesForRange) still
        // contribute their HistoryPhaseMarker because frame transitions
        // matter to skip-first-frame detection.
        for (const QVariant& v : phases) {
            const QVariantMap marker = v.toMap();
            HistoryPhaseMarker h;
            h.time = marker.value("time", 0.0).toDouble();
            // Match the pre-helper inline loop's fallback: legacy/malformed
            // shotData rows with a missing "label" key surface as "Phase"
            // rather than empty string in the per-phase prompt block.
            h.label = marker.value("label", "Phase").toString();
            h.frameNumber = marker.value("frameNumber", 0).toInt();
            h.isFlowMode = marker.value("isFlowMode", false).toBool();
            h.transitionReason = marker.value("transitionReason").toString();
            historyMarkers.append(h);
        }
        summary.phases = buildPhaseSummariesForRange(
            summary.pressureCurve, summary.flowCurve,
            summary.tempCurve, summary.weightCurve,
            historyMarkers, summary.totalDuration);
    }

    if (summary.phases.isEmpty()) {
        summary.phases.append(makeWholeShotPhase(summary.pressureCurve, summary.flowCurve,
                                                 summary.tempCurve, summary.weightCurve,
                                                 summary.totalDuration));
    }

    // Fast path: when shotData came out of ShotHistoryStorage::convertShotRecord
    // it already carries `summaryLines` (from convertShotRecord's analyzeShot
    // pass) and `detectorResults.pourTruncated`. Reuse those directly instead
    // of running analyzeShot a second time on the same data — both the fast
    // path's pre-computed lines and the slow path's recomputation invoke the
    // same analyzeShot body on equivalent inputs, so the two paths produce
    // matching observation lines. Per-phase temperature markers are still
    // gated on the same conditions as the slow path; only the detector
    // orchestration block is bypassed.
    //
    // Precondition: callers populating `summaryLines` MUST also populate
    // `detectorResults.pourTruncated` (convertShotRecord does both, atomically).
    // If detectorResults is absent, .toBool() defaults to false, so a callsite
    // that stuffs only summaryLines could mis-suppress per-phase temp markers
    // on a truncated-pour shot. Today no such callsite exists.
    if (!shotData.summaryLines.isEmpty()) {
        summary.summaryLines = shotData.summaryLines;
        summary.pourTruncatedDetected = shotData.detectorResults.value("pourTruncated").toBool();
        if (!summary.pourTruncatedDetected
            && ShotAnalysis::reachedExtractionPhase(historyMarkers, summary.totalDuration))
            markPerPhaseTempInstability(summary, summary.tempCurve, summary.tempGoalCurve);
        return summary;
    }

    // Slow path: shotData not produced by convertShotRecord (imported shots,
    // direct test callers) has empty summaryLines and needs the full analysis
    // pipeline. Delegate detector orchestration to runShotAnalysisAndPopulate,
    // the same helper summarize() uses on the live path — see summarize() for
    // rationale. historyMarkers was already populated alongside the
    // PhaseSummary list above (single pass).
    const QStringList analysisFlags = getAnalysisFlags(summary.profileKbId);

    // First-frame seconds reuses the profileDoc parsed at the top of this
    // function — Profile::fromJson normalizes both modern and legacy shapes,
    // so skip-first-frame detection stays accurate on legacy shots whose
    // first frame was configured > 2 s.
    double firstFrameSeconds = -1.0;
    int frameCount = -1;
    if (profileDoc.isObject()) {
        const Profile p = Profile::fromJson(profileDoc);
        if (!p.steps().isEmpty())
            firstFrameSeconds = p.steps().first().seconds;
        frameCount = static_cast<int>(p.steps().size());
    }

    const QVector<QPointF> derivCurve = variantListToPoints(shotData.conductanceDerivative);

    // Per-shot targetWeight drives both arms of the grind-vs-yield check
    // (the choked-puck yield arm and the gusher arm added in PR #910) —
    // matches the input convertShotRecord passes to analyzeShot.
    const double targetWeightG = shotData.targetWeightG;

    runShotAnalysisAndPopulate(summary,
        summary.pressureCurve, summary.flowCurve, summary.weightCurve,
        summary.tempCurve, summary.tempGoalCurve, derivCurve, historyMarkers,
        summary.pressureGoalCurve, summary.flowGoalCurve, analysisFlags,
        firstFrameSeconds, targetWeightG, frameCount);

    return summary;
}

QString ShotSummarizer::buildUserPrompt(const ShotSummary& summary, RenderMode mode) const
{
    QString prompt;
    QTextStream out(&prompt);
    const bool isHistoryBlock = (mode == RenderMode::HistoryBlock);

    // Shot summary — shot-VARIABLE fields only. Per openspec
    // optimize-dialing-context-payload (tasks 8 + 9): profile identity
    // (title / intent / recipe) lives in `result.profile`; bean identity
    // lives in `currentBean`; grinder brand/model/burrs lives in
    // `currentBean.grinder*` and `dialInSessions[].context`. The prose
    // body carries only what changes per-shot (dose, yield, ratio,
    // duration, grinder setting, extraction, peaks, phase data, detector
    // observations). Removing these constants saves ~5,400 chars across a
    // 4-shot history block (the Northbound 80's Espresso baseline).
    //
    // `HistoryBlock` mode skips this top-level header line — the caller
    // (`AIManager::requestRecentShotContext`) wraps each block in its
    // own `### Shot (date)` header, so the per-shot `## Shot Summary`
    // header would be redundant under that wrapper.
    if (!isHistoryBlock)
        out << "## Shot Summary\n\n";
    out << "- **Dose**: " << QString::number(summary.doseWeight, 'f', 1) << "g → ";
    out << "**Yield**: " << QString::number(summary.finalWeight, 'f', 1) << "g";
    if (summary.targetWeight > 0) {
        out << " (target " << QString::number(summary.targetWeight, 'f', 0) << "g, ";
        double diff = summary.finalWeight - summary.targetWeight;
        if (std::abs(diff) >= 0.5)
            out << (diff > 0 ? "+" : "") << QString::number(diff, 'f', 1) << "g";
        else
            out << "on target";
        out << ")";
    }
    out << " ratio 1:" << QString::number(summary.ratio, 'f', 1) << "\n";
    out << "- **Duration**: " << QString::number(summary.totalDuration, 'f', 0) << "s\n";
    if (!summary.grinderSetting.isEmpty()) {
        out << "- **Grind setting**: " << summary.grinderSetting << "\n";
    }
    if (summary.drinkTds > 0 || summary.drinkEy > 0) {
        out << "- **Extraction**: ";
        if (summary.drinkTds > 0) out << "TDS " << QString::number(summary.drinkTds, 'f', 2) << "%";
        if (summary.drinkTds > 0 && summary.drinkEy > 0) out << ", ";
        if (summary.drinkEy > 0) out << "EY " << QString::number(summary.drinkEy, 'f', 1) << "%";
        out << "\n";
    }

    // Overall shot peaks across ALL phases — so the AI can compare against profile peak-pressure
    // targets (e.g. D-Flow "grind for 6–9 bar peak") without conflating per-phase peaks.
    {
        double peakPressureVal = 0, peakPressureTime = 0;
        for (const auto& pt : summary.pressureCurve) {
            if (pt.y() > peakPressureVal) { peakPressureVal = pt.y(); peakPressureTime = pt.x(); }
        }
        double peakFlowVal = 0, peakFlowTime = 0;
        for (const auto& pt : summary.flowCurve) {
            if (pt.y() > peakFlowVal) { peakFlowVal = pt.y(); peakFlowTime = pt.x(); }
        }
        if (peakPressureVal > 0.1 || peakFlowVal > 0.1) {
            out << "- **Overall shot peaks**: ";
            out << "pressure " << QString::number(peakPressureVal, 'f', 2) << " bar @" << QString::number(peakPressureTime, 'f', 0) << "s, ";
            out << "flow " << QString::number(peakFlowVal, 'f', 2) << " ml/s @" << QString::number(peakFlowTime, 'f', 0) << "s\n";
        }
    }
    out << "\n";

    // Profile recipe lives in `result.profile.recipe` (single canonical
    // surface) — see openspec optimize-dialing-context-payload, task 8. The
    // pre-change emission of `summary.profileRecipeDescription` here
    // duplicated that JSON field and added ~210 chars per per-shot block.

    // Phase breakdown: start, peak-deviation (most diagnostic), end
    out << "## Phase Data\n\n";
    out << "Each phase shows peak values with timing, then start, peak deviation from target, and end. Values: actual(target).\n\n";

    for (const auto& phase : summary.phases) {
        QString controlMode = phase.isFlowMode
            ? "FLOW-CONTROLLED"
            : "PRESSURE-CONTROLLED";

        out << "### " << phase.name << " (" << QString::number(phase.duration, 'f', 0) << "s) " << controlMode << "\n";

        // Show phase peak values with timing so the AI knows actual extremes and curve shape
        if (phase.maxPressure > 0.1 || phase.maxFlow > 0.1) {
            // Find time of peak pressure within this phase
            double peakPressureTime = phase.startTime;
            double peakPressureVal = 0;
            for (const auto& pt : summary.pressureCurve) {
                if (pt.x() < phase.startTime || pt.x() > phase.endTime) continue;
                if (pt.y() > peakPressureVal) { peakPressureVal = pt.y(); peakPressureTime = pt.x(); }
            }
            // Find time of peak flow within this phase
            double peakFlowTime = phase.startTime;
            double peakFlowVal = 0;
            for (const auto& pt : summary.flowCurve) {
                if (pt.x() < phase.startTime || pt.x() > phase.endTime) continue;
                if (pt.y() > peakFlowVal) { peakFlowVal = pt.y(); peakFlowTime = pt.x(); }
            }
            out << "- Peak within this phase only: ";
            out << "pressure " << QString::number(peakPressureVal, 'f', 2) << " bar @" << QString::number(peakPressureTime, 'f', 0) << "s, ";
            out << "flow " << QString::number(peakFlowVal, 'f', 2) << " ml/s @" << QString::number(peakFlowTime, 'f', 0) << "s\n";
        }

        // Find time of max deviation from target for the controlled variable
        double peakDevTime = (phase.startTime + phase.endTime) / 2;  // fallback to middle
        double maxDev = 0;
        const auto& actualCurve = phase.isFlowMode ? summary.flowCurve : summary.pressureCurve;
        const auto& goalCurve = phase.isFlowMode ? summary.flowGoalCurve : summary.pressureGoalCurve;

        for (const auto& pt : actualCurve) {
            if (pt.x() < phase.startTime || pt.x() > phase.endTime) continue;
            double target = findValueAtTime(goalCurve, pt.x());
            double dev = std::abs(pt.y() - target);
            if (dev > maxDev) {
                maxDev = dev;
                peakDevTime = pt.x();
            }
        }

        // Sample at start, peak-deviation, end
        double times[3] = { phase.startTime, peakDevTime, phase.endTime - 0.1 };
        const char* labels[3] = { "Start", "Peak\u0394", "End" };

        // Skip peak-deviation if it's too close to start or end (within 1s)
        bool showPeak = std::abs(peakDevTime - phase.startTime) > 1.0 &&
                        std::abs(peakDevTime - phase.endTime) > 1.0;

        for (int i = 0; i < 3; i++) {
            if (i == 1 && !showPeak) continue;

            double t = times[i];
            double pressure = findValueAtTime(summary.pressureCurve, t);
            double flow = findValueAtTime(summary.flowCurve, t);
            double temp = findValueAtTime(summary.tempCurve, t);
            double weight = findValueAtTime(summary.weightCurve, t);
            double pTarget = findValueAtTime(summary.pressureGoalCurve, t);
            double fTarget = findValueAtTime(summary.flowGoalCurve, t);
            double tTarget = findValueAtTime(summary.tempGoalCurve, t);

            out << "- " << labels[i] << " @" << QString::number(t, 'f', 0) << "s: ";
            out << QString::number(pressure, 'f', 2);
            if (pTarget > 0.1) out << "(" << QString::number(pTarget, 'f', 2) << ")";
            out << "bar ";
            out << QString::number(flow, 'f', 2);
            if (fTarget > 0.1) out << "(" << QString::number(fTarget, 'f', 2) << ")";
            out << "ml/s ";
            out << QString::number(temp, 'f', 0);
            if (tTarget > 0) out << "(" << QString::number(tTarget, 'f', 0) << ")";
            out << "\u00B0C ";
            out << QString::number(weight, 'f', 1) << "g\n";
        }
        // Suppress per-phase temp instability when the puck never built \u2014
        // temp drift on a failed pour is a downstream symptom, not signal.
        if (phase.temperatureUnstable && !summary.pourTruncatedDetected)
            out << "- **Temperature instability**: Average temperature deviated from target by >2\u00B0C during this phase\n";
        out << "\n";
    }

    // Tasting feedback - put this prominently as it's most important
    out << "## Tasting Feedback\n\n";
    if (summary.enjoymentScore > 0) {
        out << "- **Score**: " << summary.enjoymentScore << "/100";
        if (summary.enjoymentScore >= 80) out << " - Good shot!";
        else if (summary.enjoymentScore >= 60) out << " - Decent, room for improvement";
        else if (summary.enjoymentScore >= 40) out << " - Needs work";
        else out << " - Problematic";
        out << "\n";
    }
    if (!summary.tastingNotes.isEmpty()) {
        out << "- **Notes**: \"" << summary.tastingNotes << "\"\n";
    }
    if (summary.enjoymentScore == 0 && summary.tastingNotes.isEmpty()) {
        out << "- No tasting feedback provided\n";
    }
    out << "\n";

    // Detector observations — the same line list ShotAnalysis::analyzeShot
    // produces for the in-app Shot Summary dialog, minus the verdict line.
    //
    // Why omit the verdict: the verdict is a deterministic, prescriptive
    // conclusion ("Puck choked — grind way too fine. Coarsen significantly.")
    // computed from the same observations the AI is already seeing. Including
    // it would anchor the LLM on a pre-cooked answer and collapse the
    // advisor's job to "say it again with bean context." Letting the AI reason
    // independently from the deterministic *signals* (which it can't reliably
    // compute from raw curves on its own — see the channeling and choked-puck
    // arms) preserves the value-add over the badge UI. The user still sees
    // the verdict in the dialog; the AI synthesizes its own.
    //
    // The preamble frames severity tags as detector confidence, not the
    // advisor's final assessment, to discourage parroting [warning] lines as
    // imperatives.
    QVariantList nonVerdictLines;
    for (const QVariant& v : summary.summaryLines) {
        if (v.toMap().value(QStringLiteral("type")).toString() != QLatin1String("verdict"))
            nonVerdictLines.append(v);
    }

    if (!nonVerdictLines.isEmpty()) {
        // Per openspec optimize-dialing-context-payload (task 3): the
        // legend explaining `[warning] / [caution] / [good] / [observation]`
        // tags lives in the system prompt, not in every per-call prose
        // body. Per-line tags survive here; the AI reads the legend once
        // per conversation from `shotAnalysisSystemPrompt`. Saves
        // ~430 chars per per-shot block on the in-app history path that
        // calls buildUserPrompt N times.
        //
        // Per task 10: `HistoryBlock` mode skips this top-level header
        // line so it doesn't render redundantly under each `### Shot
        // (date)` wrapper. The per-line tagged observations themselves
        // still emit (they are shot-variable detector signals).
        if (!isHistoryBlock)
            out << "## Detector Observations\n\n";
        for (const QVariant& v : nonVerdictLines) {
            const QVariantMap line = v.toMap();
            out << "- [" << line.value(QStringLiteral("type")).toString() << "] "
                << line.value(QStringLiteral("text")).toString() << "\n";
        }
        out << "\n";
    }

    return prompt;
}

QString ShotSummarizer::buildHistoryContext(const QVariantList& recentShots)
{
    if (recentShots.isEmpty()) return QString();

    QString result;
    QTextStream out(&result);

    out << "## Recent Shot History (same profile family, newest first)\n\n";
    out << "Use this to identify dial-in trends — what changed between shots and how it affected the result.\n\n";

    // Convert each map entry to ShotProjection so the rest of this block reads
    // fields with compile-time-checked names. The input is a QVariantList from
    // ShotHistoryStorage::loadRecentShotsByKbIdStatic — that producer emits a
    // map with the same keys as ShotProjection's Q_PROPERTYs, so the round-trip
    // through fromVariantMap() is lossless for the fields read here.

    // Per openspec optimize-dialing-context-payload (task 10.4): the
    // input list is already filtered by profile_kb_id (loadRecentShotsByKbIdStatic),
    // so every shot shares the same profile name and recipe. Emit them
    // once at the top instead of N× per shot. The first shot with a
    // populated profileJson seeds the recipe (all shots on the same KB
    // family render to the same frame description).
    QString profileName, profileRecipe;
    for (const QVariant& v : recentShots) {
        const ShotProjection s = ShotProjection::fromVariantMap(v.toMap());
        if (profileName.isEmpty() && !s.profileName.isEmpty())
            profileName = s.profileName;
        if (profileRecipe.isEmpty() && !s.profileJson.isEmpty())
            profileRecipe = Profile::describeFramesFromJson(s.profileJson);
        if (!profileName.isEmpty() && !profileRecipe.isEmpty()) break;
    }
    if (!profileName.isEmpty()) {
        out << "### Profile: " << profileName << "\n";
        if (!profileRecipe.isEmpty())
            out << profileRecipe << "\n";
        else
            out << "\n";
    }

    for (qsizetype i = 0; i < recentShots.size(); ++i) {
        const ShotProjection shot = ShotProjection::fromVariantMap(recentShots[i].toMap());

        // Skip entries with no meaningful data (corrupt or incomplete records)
        if (shot.doseWeightG <= 0 && shot.finalWeightG <= 0 && shot.durationSec <= 0) continue;

        const double ratio = shot.doseWeightG > 0 ? shot.finalWeightG / shot.doseWeightG : 0;

        out << "### Shot " << (i + 1) << " (" << shot.timestampIso << ")\n";
        // `Profile:` and `Recipe:` are hoisted to the single header above
        // (task 10.4) — per-shot repetition was redundant, the input is
        // already KB-filtered.
        out << "- Dose: " << QString::number(shot.doseWeightG, 'f', 1) << "g → Yield: "
            << QString::number(shot.finalWeightG, 'f', 1) << "g (1:" << QString::number(ratio, 'f', 1) << ")\n";
        out << "- Duration: " << QString::number(shot.durationSec, 'f', 0) << "s\n";

        // Grinder info
        if (!shot.grinderBrand.isEmpty() || !shot.grinderModel.isEmpty() || !shot.grinderSetting.isEmpty()) {
            out << "- Grinder: ";
            if (!shot.grinderBrand.isEmpty()) out << shot.grinderBrand;
            if (!shot.grinderModel.isEmpty()) {
                if (!shot.grinderBrand.isEmpty()) out << " ";
                out << shot.grinderModel;
            }
            if (!shot.grinderBurrs.isEmpty()) out << " with " << shot.grinderBurrs;
            if (!shot.grinderSetting.isEmpty()) out << " @ " << shot.grinderSetting;
            out << "\n";
        }

        // Temperature override
        if (shot.temperatureOverrideC > 0) {
            out << "- Temperature override: " << QString::number(shot.temperatureOverrideC, 'f', 1) << "°C\n";
        }

        // Bean info
        if (!shot.beanBrand.isEmpty() || !shot.beanType.isEmpty()) {
            out << "- Beans: " << shot.beanBrand;
            if (!shot.beanBrand.isEmpty() && !shot.beanType.isEmpty()) out << " - ";
            out << shot.beanType;
            if (!shot.roastLevel.isEmpty()) out << " (" << shot.roastLevel << ")";
            out << "\n";
        }

        // Extraction measurements
        if (shot.drinkTdsPct > 0 || shot.drinkEyPct > 0) {
            out << "- Extraction: ";
            if (shot.drinkTdsPct > 0) out << "TDS " << QString::number(shot.drinkTdsPct, 'f', 2) << "%";
            if (shot.drinkTdsPct > 0 && shot.drinkEyPct > 0) out << ", ";
            if (shot.drinkEyPct > 0) out << "EY " << QString::number(shot.drinkEyPct, 'f', 1) << "%";
            out << "\n";
        }

        // Score and tasting notes
        if (shot.enjoyment0to100 > 0) {
            out << "- Score: " << shot.enjoyment0to100 << "/100\n";
        }
        if (!shot.espressoNotes.isEmpty()) {
            out << "- Notes: \"" << shot.espressoNotes << "\"\n";
        }

        out << "\n";
    }

    return result;
}

QString ShotSummarizer::systemPrompt(const QString& beverageType)
{
    if (beverageType.toLower() == "filter" || beverageType.toLower() == "pourover") {
        return filterSystemPrompt();
    }
    return espressoSystemPrompt();
}

QString ShotSummarizer::shotAnalysisSystemPrompt(const QString& beverageType, const QString& profileTitle,
                                                   const QString& profileType, const QString& profileKbId)
{
    QString base = systemPrompt(beverageType);

    // Per openspec optimize-dialing-context-payload (task 4): structural
    // gating fields live in the JSON payload; their per-call framing
    // strings (which were skimmed past by the AI) move here, taught once
    // per conversation.
    base += QStringLiteral("\n\n## How to Read Structured Fields\n\n"
        "The `dialing_get_context` JSON payload carries structural fields whose\n"
        "semantics are consistent across calls. Treat them as gates on your advice:\n\n"
        "**`result.profile`**: the single canonical source for profile metadata —\n"
        "`filename`, `title`, `intent`, `recipe`, `targetWeightG`,\n"
        "`targetTemperatureC`, and `recommendedDoseG` (when set). Read profile\n"
        "intent and frame recipe from here. The `shotAnalysis` prose body\n"
        "carries shot-VARIABLE data only (dose, yield, duration, grind setting,\n"
        "extraction, peaks, phase data, detector observations) — it never\n"
        "carries `Profile:`, `Profile intent:`, or `## Profile Recipe`.\n\n"
        "**`currentBean`** + **`dialInSessions[].context`**: shot-INVARIANT\n"
        "identity. `currentBean.brand` / `.type` / `.roastLevel` carry bean\n"
        "identity; `currentBean.grinderBrand` / `.grinderModel` / `.grinderBurrs`\n"
        "carry grinder identity for the live setup. The `shotAnalysis` prose\n"
        "carries neither — it never carries a `Coffee:` / `Beans:` line nor a\n"
        "`Grinder:` line with brand/model/burrs. Only the per-shot variable\n"
        "`Grind setting:` appears in prose.\n\n"
        "**`tastingFeedback`**: carries booleans `hasEnjoymentScore`, `hasNotes`,\n"
        "`hasRefractometer`. When ALL three are false, ASK the user how the shot\n"
        "tasted (score 1–100, 1–2 lines of flavor notes, TDS reading if available)\n"
        "before suggesting changes. Curve-only analysis without taste feedback\n"
        "misses the variable that matters most.\n\n"
        "**`currentBean.beanFreshness`**: when present, carries `roastDate`,\n"
        "`freshnessKnown` (currently always `false` until storage tracking is\n"
        "added), and an `instruction`. NEVER quote calendar age until\n"
        "`freshnessKnown` is `true`. Many users freeze beans and thaw weekly —\n"
        "calendar days from `roastDate` are not freshness without storage context.\n"
        "ASK the user about storage before applying any bean-aging guidance from\n"
        "the dial-in reference tables.\n\n"
        "**`currentBean.inferredFields`**: when present, lists field names that\n"
        "were inferred from the most recent shot (because the user's DYE settings\n"
        "were blank), not entered by the user. Confirm with the user before\n"
        "recommending a change to any inferred field.\n\n"
        "**`dialInSessions[].context`**: hoists shot-identity fields shared across\n"
        "an iteration session (`grinderBrand`, `grinderModel`, `grinderBurrs`,\n"
        "`beanBrand`, `beanType`). When a per-shot entry under `shots[]` omits\n"
        "one of these fields, that shot uses the session's `context` value.\n"
        "When a per-shot entry carries the field directly, it overrides the\n"
        "context for that shot only. CAVEAT: a hoisted context value reflects\n"
        "the first non-empty value across the session — for legacy shots whose\n"
        "field was never recorded, the AI sees the modern value as if it\n"
        "applied. When advising on a specific older shot's grinder/bean, treat\n"
        "the session context as a best-effort inference, not a guaranteed\n"
        "match for that shot's actual recorded data.\n");

    // Detector-observations legend. Per openspec optimize-dialing-context-payload
    // (task 3), this lives in the system prompt (taught once per
    // conversation) instead of the per-call prose body. Per-shot blocks
    // still emit `[warning] / [caution] / [good] / [observation]` tags
    // on individual detector lines; this legend tells the AI how to
    // weight them.
    base += QStringLiteral("\n\n## Reading Detector Observations\n\n"
        "Per-shot blocks may include a `## Detector Observations` section listing\n"
        "lines tagged with severity. The lines come from the same deterministic\n"
        "detectors that drive the in-app Shot Summary badges the user sees. Treat\n"
        "them as diagnostic signals (evidence), not your conclusions. Severity\n"
        "tags reflect detector confidence, not your final assessment:\n\n"
        "- [warning] high-confidence failure mode (sustained channeling, choked puck, yield overshoot/gusher, pour truncated, frame skip)\n"
        "- [caution] directional hint (grind drift, flow trend, temp drift)\n"
        "- [good] positive signal (puck stable)\n"
        "- [observation] context (preinfusion drip mass)\n\n"
        "Cross-check against the raw curves and the user's tasting feedback, and\n"
        "reason independently — you have richer context (bean, prior shots, tasting\n"
        "notes) than the deterministic detectors do.\n");

    // Append dial-in reference tables for espresso (cacheable, shared with MCP)
    if (beverageType.toLower() != "filter" && beverageType.toLower() != "pourover") {
        loadDialInReference();
        if (!s_dialInReference.isEmpty()) {
            base += QStringLiteral("\n\n## Espresso Dial-In Reference Tables\n\n"
                "Structured relationships between espresso variables and their effects on taste. "
                "Use these tables to make specific, multi-variable recommendations.\n\n")
                + s_dialInReference;
        }
    }

    // Include profile catalog for cross-profile awareness (espresso only — catalog
    // and "When to Suggest a Different Profile" guidance are espresso-centric)
    if (beverageType.toLower() != "filter" && beverageType.toLower() != "pourover") {
        loadProfileKnowledge();
        if (!s_profileCatalog.isEmpty()) {
            base += QStringLiteral("\n\n## Available Profiles with Curated Knowledge\n\n"
                "These profiles have detailed knowledge entries. When the user's roast, beans, "
                "or goals suggest a better match, you can recommend switching to one of these. "
                "The current shot's profile has a detailed section below — the others are available "
                "for comparison and recommendations.\n\n")
                + s_profileCatalog;
        }
    }

    // Look up profile-specific knowledge by KB ID (computed from title/alias matching),
    // falling back to fuzzy title/editorType matching for shots without a stored KB ID
    QString profileSection;
    if (!profileKbId.isEmpty()) {
        loadProfileKnowledge();
        if (s_profileKnowledge.contains(profileKbId)) {
            profileSection = s_profileKnowledge.value(profileKbId).content;
        }
    }
    if (profileSection.isEmpty()) {
        profileSection = findProfileSection(profileTitle, profileType);
    }
    if (!profileSection.isEmpty()) {
        base += QStringLiteral("\n\n## Current Profile Knowledge\n\n"
            "The following is curated knowledge about the specific profile used in this shot. "
            "Use this to understand what is INTENTIONAL behavior vs. what indicates a problem.\n\n")
            + profileSection;
    }

    return base;
}

void ShotSummarizer::loadProfileKnowledge()
{
    if (s_knowledgeLoaded) return;
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    if (s_knowledgeLoaded) return;  // re-check after acquiring lock

    QFile file(QStringLiteral(":/ai/profile_knowledge.md"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "ShotSummarizer: Failed to load profile knowledge resource";
        return;
    }

    QString content = QTextStream(&file).readAll();
    file.close();

    // Parse markdown sections: each "## Title" starts a new profile
    // Build a map from lowercase key → ProfileKnowledge
    const QStringList lines = content.split('\n');
    QString currentTitle;
    QString currentContent;

    auto commitSection = [&]() {
        if (currentTitle.isEmpty() || currentContent.isEmpty()) return;

        ProfileKnowledge pk;
        pk.name = currentTitle;
        pk.content = currentContent.trimmed();

        // Extract the main name and any aliases from "Also matches:" line
        QStringList keys;

        // Primary title may have " / " separators (e.g. "D-Flow / Damian's D-Flow / D-Flow/Q")
        const QStringList titleParts = currentTitle.split(QStringLiteral(" / "));
        for (const QString& part : titleParts) {
            keys << part.trimmed().toLower();
        }

        // Check for "Also matches:" and "AnalysisFlags:" lines in content
        for (const QString& line : currentContent.split('\n')) {
            if (line.startsWith(QStringLiteral("Also matches:"))) {
                QString aliases = line.mid(14).trimmed();
                // Remove surrounding quotes and split by comma
                const QStringList aliasParts = aliases.split(',');
                for (const QString& alias : aliasParts) {
                    QString clean = alias.trimmed();
                    clean.remove('"');
                    if (!clean.isEmpty()) {
                        keys << clean.toLower();
                    }
                }
            } else if (line.startsWith(QStringLiteral("AnalysisFlags:"))) {
                const QString flagStr = line.mid(14).trimmed();
                for (const QString& f : flagStr.split(',')) {
                    const QString flag = f.trimmed();
                    if (!flag.isEmpty()) pk.analysisFlags << flag;
                }
            }
        }

        // Register all keys (normalized for accent/punctuation matching)
        for (const QString& key : keys) {
            s_profileKnowledge.insert(normalizeProfileKey(key), pk);
        }
    };

    for (const QString& line : lines) {
        if (line.startsWith(QStringLiteral("## ")) && !line.startsWith(QStringLiteral("### "))) {
            // Commit previous section
            commitSection();
            currentTitle = line.mid(3).trimmed();
            currentContent.clear();
        } else if (!currentTitle.isEmpty()) {
            currentContent += line + '\n';
        }
    }
    // Commit last section
    commitSection();

    qDebug() << "ShotSummarizer: Loaded" << s_profileKnowledge.size()
             << "profile knowledge entries";

    buildProfileCatalog();

    // Mark loaded only after parse + catalog build complete. The
    // double-checked-locking pattern at the top of this function depends
    // on the flag indicating "data ready to read", not "we got far enough
    // to open the file". Setting it earlier let a second thread that won
    // the outer race read an empty s_profileKnowledge.
    s_knowledgeLoaded = true;
}

void ShotSummarizer::buildProfileCatalog()
{
    // Build compact one-liner per unique KB profile for cross-profile awareness.
    // Extracts Category and Roast lines from each profile's content.
    QSet<QString> seen;
    QStringList lines;

    for (auto it = s_profileKnowledge.constBegin(); it != s_profileKnowledge.constEnd(); ++it) {
        const ProfileKnowledge& pk = it.value();
        if (seen.contains(pk.name)) continue;
        seen.insert(pk.name);

        QString category;
        QString roast;
        for (const QString& line : pk.content.split('\n')) {
            if (line.startsWith(QStringLiteral("Category:")) && category.isEmpty()) {
                category = line.mid(9).trimmed();
            } else if (line.startsWith(QStringLiteral("Roast:")) && roast.isEmpty()) {
                roast = line.mid(6).trimmed();
            }
        }

        QString entry = pk.name + QStringLiteral(" — ") + category;
        if (!roast.isEmpty()) {
            entry += QStringLiteral(". ") + roast;
        }
        lines << entry;
    }

    // Sort alphabetically for consistent ordering
    lines.sort(Qt::CaseInsensitive);
    s_profileCatalog = lines.join('\n');

    qDebug() << "ShotSummarizer: Built profile catalog with" << lines.size() << "entries";
}

void ShotSummarizer::loadDialInReference()
{
    if (s_dialInReferenceLoaded) return;

    QFile file(QStringLiteral(":/ai/espresso_dial_in_reference.md"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "ShotSummarizer: Failed to load dial-in reference resource";
        s_dialInReferenceLoaded = true;
        return;
    }
    s_dialInReferenceLoaded = true;

    QString content = QTextStream(&file).readAll();
    file.close();

    // Strip the preamble (title, source attribution, description) — already introduced
    // by the section header in shotAnalysisSystemPrompt(). Seek to the first HR separator.
    qsizetype pos = content.indexOf(QStringLiteral("\n---\n"));
    if (pos > 0)
        content = content.mid(pos + 5).trimmed();  // skip past "\n---\n"

    s_dialInReference = content;
    qDebug() << "ShotSummarizer: Loaded dial-in reference tables ("
             << s_dialInReference.size() << "chars)";
}

// Shared matching logic: returns the matched key in s_profileKnowledge, or empty string.
// profileTitle: the profile's display name (e.g. "D-Flow / my recipe")
// editorTypeHint: either the raw editorType string ("dflow", "aflow") or the
//   profileType description string ("D-Flow (lever-style...)") — both are handled.
QString ShotSummarizer::matchProfileKey(const QMap<QString, ShotSummarizer::ProfileKnowledge>& knowledge,
                                        const QString& profileTitle, const QString& editorTypeHint)
{
    if (knowledge.isEmpty()) return QString();

    // Try title-based matching first
    if (!profileTitle.isEmpty()) {
        QString key = normalizeProfileKey(profileTitle);

        // Direct match
        if (knowledge.contains(key)) {
            return key;
        }

        // Try without version suffixes (e.g., "Adaptive v2.1" → "adaptive v2")
        // Try progressively shorter prefixes
        for (const auto& knownKey : knowledge.keys()) {
            if (key.startsWith(knownKey) || knownKey.startsWith(key)) {
                return knownKey;
            }
        }

        // Fuzzy: check if any known key is contained within the profile title
        for (const auto& knownKey : knowledge.keys()) {
            if (knownKey.length() >= 4 && key.contains(knownKey)) {
                return knownKey;
            }
        }
    }

    // Fallback: match by editor type
    // This handles user-created profiles from the D-Flow/A-Flow editors
    // that may have completely custom titles
    if (!editorTypeHint.isEmpty()) {
        // Map raw editorType strings and description prefixes to knowledge base keys
        static const QMap<QString, QString> editorTypeToKey = {
            { QStringLiteral("dflow"),  QStringLiteral("d-flow / default") },
            { QStringLiteral("aflow"),  QStringLiteral("a-flow") },
            { QStringLiteral("D-Flow"), QStringLiteral("d-flow / default") },
            { QStringLiteral("A-Flow"), QStringLiteral("a-flow") },
        };

        // Try exact match first (raw editorType like "dflow")
        if (editorTypeToKey.contains(editorTypeHint)) {
            const QString& mapped = editorTypeToKey.value(editorTypeHint);
            if (knowledge.contains(mapped)) return mapped;
        }

        // Try prefix match (description string like "D-Flow (lever-style...)")
        for (auto it = editorTypeToKey.constBegin(); it != editorTypeToKey.constEnd(); ++it) {
            if (editorTypeHint.startsWith(it.key())) {
                if (knowledge.contains(it.value())) {
                    return it.value();
                }
            }
        }
    }

    return QString();
}

QString ShotSummarizer::findProfileSection(const QString& profileTitle, const QString& profileType)
{
    if (profileTitle.isEmpty() && profileType.isEmpty()) return QString();

    loadProfileKnowledge();

    QString key = matchProfileKey(s_profileKnowledge, profileTitle, profileType);
    if (!key.isEmpty()) {
        return s_profileKnowledge.value(key).content;
    }
    return QString();
}

QString ShotSummarizer::profileKnowledgeForKbId(const QString& profileKbId)
{
    if (profileKbId.isEmpty()) return QString();
    loadProfileKnowledge();
    if (s_profileKnowledge.contains(profileKbId))
        return s_profileKnowledge.value(profileKbId).content;
    return QString();
}

QStringList ShotSummarizer::getAnalysisFlags(const QString& kbId)
{
    if (kbId.isEmpty()) return {};
    loadProfileKnowledge();
    return s_profileKnowledge.value(kbId).analysisFlags;
}

QString ShotSummarizer::computeProfileKbId(const QString& profileTitle, const QString& editorType)
{
    if (profileTitle.isEmpty() && editorType.isEmpty()) return QString();

    loadProfileKnowledge();

    return matchProfileKey(s_profileKnowledge, profileTitle, editorType);
}

QString ShotSummarizer::espressoSystemPrompt()
{
    return QStringLiteral(R"(You are an espresso analyst helping dial in shots on a Decent DE1 profiling machine.

)") + sharedCorePhilosophy() + QStringLiteral(R"(
A Blooming Espresso at 2 bar is not "low pressure" — it's doing exactly what it should. A turbo shot finishing in 15 seconds is not "too fast."

## The DE1 Machine

The DE1 controls either PRESSURE or FLOW at any moment (never both — they're inversely related through puck resistance):
- When controlling FLOW: pressure is the result of puck resistance
- When controlling PRESSURE: flow is the result of puck resistance

Profiles have named phases (Prefill, Preinfusion, Extraction, etc.) that execute sequentially. Each phase has its own targets and behavior.

## Reading Targets vs Limiters

The data shows actual values with targets in parentheses. Here's how to interpret them:

**Flow-controlled phases** (flow target 4-8+ ml/s):
- The machine pushes water at the target flow rate
- Pressure builds as a RESULT of puck resistance
- Pressure typically reaches 6-10 bar depending on grind and puck prep
- The pressure "target" shown is actually a LIMITER (safety max), not a goal

**Pressure-controlled phases** (pressure target 6-11 bar, low/no flow target):
- The machine maintains target pressure
- Flow is the RESULT of puck resistance
- Low flow at target pressure = high resistance (fine grind)
- High flow at target pressure = low resistance (coarse grind)

**Key insight**: When actual pressure differs greatly from "target" during a flow-controlled phase, that's normal — check if FLOW matched its target instead. The machine achieved what it was trying to do.

**Declining pressure during flow phases is normal.** As the coffee puck erodes during extraction, resistance drops, so pressure naturally declines even at constant flow. This is especially pronounced in lever-style and D-Flow profiles that transition from pressure control to flow control (shown as "from PRESSURE X bar" in the recipe). A pressure curve that peaks early and gradually declines is the expected signature of these profiles — do NOT flag it as a problem.

**Flow variation during pressure-controlled phases is normal.** When the machine controls PRESSURE, flow is just a passive result of puck resistance. As the puck saturates, compresses, and erodes, flow will naturally spike and settle. A flow spike on its own is NOT channeling — channeling is diagnosed from the conductance derivative (dC/dt), which measures how the flow↔pressure relationship changes. High flow during a pressure ramp-up (e.g., Filling at 6 bar) is simply water pushing through a dry puck and is expected.

## Reading the Recipe for Expected Behavior

The profile recipe is included with each shot. Use it to set expectations BEFORE looking at actual data:

**Temperature stepping**: If frames use different temperatures (e.g., 84°C fill → 94°C pour), actual temperature will ALWAYS lag behind the target. The heater pumps hot water that mixes with cooler water above the puck — a 5-8°C gap between target and actual during transitions is normal physics. Only flag temperature issues if actual temp deviates from target during a STABLE phase (same temperature across consecutive frames).

**Flow-controlled pour with pressure limiter**: When a pour frame controls FLOW (e.g., 1.8 ml/s) with a high pressure limiter (e.g., 10 bar), pressure will peak based on puck resistance and decline as the puck erodes. The limiter is a safety ceiling, not a goal. Pressure anywhere from 4 bar to the limiter is normal. The peak depends on grind — do not assume a specific peak unless the profile notes state one.

**Pressure → Flow transition**: When a profile switches from pressure-controlled fill/infuse to flow-controlled pour, pressure becomes passive after the switch. A declining pressure curve is the expected signature of this pattern, not a problem. This is the lever/flow hybrid pattern used by D-Flow, Londinium, and similar profiles.

**Exit conditions**: Frames with exit conditions (e.g., "exit:p>3.0") advance when the condition is met. Short phase durations (1-2s) after exit conditions are normal — the machine transitions quickly.

## How to Read the Data

You'll receive:
1. **Shot summary**: dose, yield, ratio, time, profile name
2. **Profile recipe**: frame-by-frame intent (control mode, setpoints, exit conditions)
3. **Phase breakdown**: each phase with start, peak-deviation, and end samples
4. **Extraction measurements**: TDS and EY if available (refractometer data)
5. **Tasting notes**: the user's flavor perception (most important!)

Phase data shows actual values with targets in parentheses. The "PeakΔ" sample is the moment of maximum deviation from target for the controlled variable — this is where problems show up. If no PeakΔ is shown, the phase tracked its target well.

If no tasting feedback is provided, analyze curves and extraction metrics, but note that taste feedback would improve the analysis. Do not guess what the user tasted.

)") + sharedGrinderGuidance() + QStringLiteral(R"(
- **Flat burrs**: Higher channeling risk in espresso. Flow deviations may indicate alignment issues.
- **Conical burrs**: More forgiving puck prep, flow tends to be more stable.

## Common Espresso Patterns

### The Gusher
- **Symptoms**: Very fast shot (<20s), flow way above target, thin/watery taste
- **Cause**: Grind too coarse or severe channeling
- **Fix**: Grind finer (if consistent) or improve puck prep (if erratic)

### The Choker
- **Symptoms**: Very slow shot (>45s), flow way below target, bitter/astringent taste
- **Cause**: Grind too fine
- **Fix**: Grind coarser

### The Channeler
- **Symptoms**: Erratic flow during extraction, uneven taste, sour and bitter notes together
- **Cause**: Water finding paths of least resistance through puck
- **Fix**: Better distribution and tamping — NOT grind change

### The Sour Shot
- **Symptoms**: Bright acidity, thin body, tea-like, possibly underextracted
- **Possible causes**: Temperature too low, ratio too short, shot too fast
- **Fix**: Increase temp 2°C, or pull longer, or grind finer (one at a time!)

### The Bitter Shot
- **Symptoms**: Harsh, astringent, dry finish, overextracted
- **Possible causes**: Temperature too high, ratio too long, shot too slow
- **Fix**: Decrease temp 2°C, or cut shot earlier, or grind coarser

### The Hollow Shot
- **Symptoms**: Lacks body, feels empty in the middle, thin mouthfeel
- **Cause**: Often channeling or underextraction
- **Fix**: Improve puck prep or increase extraction (finer/hotter/longer)

## Roast Considerations

- **Light roasts**: Need higher temp (93-96°C), longer ratios (1:2.5-3), more patience
- **Medium roasts**: Forgiving, standard parameters (92-94°C, 1:2-2.5)
- **Dark roasts**: Need lower temp (88-91°C), shorter ratios (1:1.5-2), easy to over-extract

)") + sharedBeanKnowledge() + QStringLiteral(R"(
- **Roaster style**: If you recognize the roaster (e.g., known for light Nordic-style roasts vs. traditional Italian), factor that into your temperature and ratio suggestions.

)") + sharedForbiddenSimplifications() + QStringLiteral(R"(
- **"9 bar is standard"** — the DE1 uses profiles with intentional pressure targets; 2-6 bar profiles exist by design and are not "low pressure"
- **"Aim for 25-30 seconds"** — shot time depends entirely on the profile's intent; turbo, blooming, and lever profiles all have different valid time ranges
- **"Use a 1:2 ratio"** — ratio depends on roast, profile, and preference; explain the reasoning, not the rule

## When to Suggest a Different Profile

If the "Available Profiles with Curated Knowledge" section is present in this prompt, you may recommend switching profiles when:
- The user's roast level clearly mismatches the current profile's design (e.g., ultra-light beans on a dark-optimized lever profile)
- Multiple shots show the same persistent issue that a different profile addresses by design (e.g., always channeling at 9 bar → suggest a 6 bar profile like Gentle & Sweet)
- The user explicitly asks about other profiles or different brewing styles

Do NOT suggest a profile change after a single shot unless the mismatch is severe. Give the current profile 2-3 shots to dial in first. When recommending, explain WHY the alternative suits their beans/goals better.

)") + sharedResponseGuidelines() + QStringLiteral(R"(
Keep responses concise and practical. The goal is a better-tasting next shot, not a perfect analysis.)");
}

QString ShotSummarizer::filterSystemPrompt()
{
    return QStringLiteral(R"(You are a filter coffee analyst helping optimise brews made on a Decent DE1 profiling machine.

## What is DE1 Filter Coffee?

The Decent DE1 espresso machine can brew filter-style coffee by pushing water through a coffee puck at low pressure and high flow. This produces a cup closer to pour-over or drip coffee than espresso — lower concentration, higher clarity, larger volume.

)") + sharedCorePhilosophy() + QStringLiteral(R"(
Each filter profile was designed with specific goals for flow rate, pressure, temperature, and grind size. **Grind advice must match the profile's design.** Some profiles are designed for very coarse grinds (near French press), others for finer filter grinds. The profile intent tells you which. If the user's grind setting seems extreme but matches what the profile calls for, it's correct — diagnose taste issues through temperature, ratio, or technique instead.

## How DE1 Filter Differs from Traditional Filter

- **Pressure**: Typically 1-3 bar (vs near-zero in pour-over). This is intentional, not a problem.
- **Brew time**: Typically 2-6 minutes depending on dose and profile.
- **Ratios**: Typically 1:10 to 1:17 (similar to traditional filter).
- **Temperature**: Typically 90-100°C, often higher than espresso.
- **Grind size**: Varies widely by profile — from slightly finer than pour-over to as coarse as French press. **Read the profile description to know what grind the profile expects.**
- **Dose**: Often 15-25g, similar to pour-over.

## Reading Targets vs Limiters

The data shows actual values with targets in parentheses. Filter profiles are almost entirely flow-controlled:

**Flow-controlled phases** (most filter phases):
- The machine pushes water at the target flow rate (often 4-8+ ml/s)
- Pressure builds as a RESULT of puck resistance — it is NOT a target
- The pressure value in parentheses is a LIMITER (safety cap), not a goal
- Seeing pressure at 1.2 bar with a "target" of 3 bar is perfectly normal — the limiter was never reached
- **Do not diagnose pressure as "low" or "off-target" during flow-controlled phases**

**Pressure-controlled phases** (rare in filter, sometimes used for bloom):
- The machine maintains target pressure (usually very low, 0.5-2 bar)
- Flow is the RESULT of puck resistance

**Key insight**: When actual pressure differs greatly from the shown "target" during a flow-controlled phase, that's expected behavior. The machine achieved what it was trying to do (the flow target). The pressure value shown is just a safety ceiling.

## Bloom and Soak Phases

Many filter profiles include an initial bloom or soak phase:
- **Purpose**: Wet the coffee bed evenly and allow CO2 to escape (degassing), improving even extraction
- **What it looks like**: Low or zero flow for 30-60+ seconds at the start of the brew
- **This is intentional** — do not flag low flow or long pauses during bloom as problems
- After bloom, the main pour phase begins with higher flow
- Some profiles pulse water during bloom (on-off-on) — this is by design

If a profile has a phase named "Bloom", "Soak", "Wet", or "Saturate", treat it as a preparation phase, not extraction.

## Reading the Data

The data shows the same format as espresso shots — phase breakdown with pressure, flow, temperature, and weight at start/middle/end. Key differences in interpretation:

- **Low pressure (0-3 bar) is normal** — do not suggest increasing pressure
- **High flow (3-8+ ml/s) is normal** — this is how filter profiles work
- **Long brew times are normal** — a 4-minute brew is not a "choker"
- **High ratios are normal** — 1:15 is standard, not excessive
- **Flow variation at high flow rates is normal** — at 6+ ml/s, turbulence causes natural fluctuation that is NOT channeling

)") + sharedGrinderGuidance() + QStringLiteral(R"(
- **Flat burrs**: Can produce exceptional clarity in filter. The bimodal distribution works well at filter concentration.
- **Conical burrs**: More body and texture, less clarity. Both are valid for filter.
- Filter grind is much coarser than espresso — grind settings are not comparable.

## Common Filter Issues

### Astringent / Dry Finish
- **Cause**: Over-extraction, often from too fine a grind or too high a temperature
- **Fix**: Grind coarser or reduce temperature 2-3°C

### Thin / Watery / Hollow
- **Cause**: Under-extraction from too coarse a grind, too low temperature, or insufficient contact time
- **Fix**: Grind finer or increase temperature 2-3°C

### Bitter / Harsh
- **Cause**: Over-extraction or water too hot
- **Fix**: Reduce temperature, grind slightly coarser, or reduce brew time

### Sour / Sharp Acidity
- **Cause**: Under-extraction
- **Fix**: Increase temperature, grind finer, or extend brew time

### Muddy / Lacking Clarity
- **Cause**: Too many fines (grinder-dependent) or channeling through the puck
- **Fix**: Grind coarser, improve puck prep, or check grinder alignment

### Sweet and Balanced
- **Diagnosis**: If it tastes good, it IS good — don't fix what isn't broken!

## Roast Considerations

- **Light roasts**: Higher temperature (95-100°C), benefit from longer contact time
- **Medium roasts**: Versatile, standard parameters (92-96°C)
- **Dark roasts**: Lower temperature (88-93°C), shorter brew time, easy to over-extract

)") + sharedBeanKnowledge() + QStringLiteral(R"(
- **Roaster style**: If you recognize the roaster, factor their typical roast philosophy into your suggestions.

)") + sharedForbiddenSimplifications() + QStringLiteral(R"(
- **"Your grind setting is too high/low"** — grind numbers are grinder-specific and profile-specific; a setting of 50 may be exactly right for a coarse-grind profile
- **"Typical filter grind is X"** — there is no universal filter grind; it depends entirely on the profile's design

When taste is flat/thin but the profile calls for coarse grind, explore temperature, water quality, ratio, dose, and bean freshness BEFORE suggesting grind changes.

)") + sharedResponseGuidelines() + QStringLiteral(R"(
Keep responses concise and practical. The goal is a better-tasting next brew, not a perfect analysis.)");
}

double ShotSummarizer::findValueAtTime(const QVector<QPointF>& data, double time)
{
    if (data.isEmpty()) return 0;

    // Use binary search for O(log N) lookup in time-sorted data
    auto it = std::lower_bound(data.begin(), data.end(), time, [](const QPointF& p, double t) {
        return p.x() < t;
    });

    if (it == data.end()) return data.last().y();
    if (it == data.begin()) return it->y();

    // Linear interpolation between *prev and *it
    const auto& p1 = *(it - 1);
    const auto& p2 = *it;
    double dx = p2.x() - p1.x();
    if (std::abs(dx) < 1e-6) return p2.y(); // Guard against division by zero

    double t = (time - p1.x()) / dx;
    return p1.y() + t * (p2.y() - p1.y());
}

double ShotSummarizer::calculateAverage(const QVector<QPointF>& data, double startTime, double endTime)
{
    if (data.isEmpty()) return 0;

    double sum = 0;
    int count = 0;
    for (const auto& point : data) {
        if (point.x() >= startTime && point.x() <= endTime) {
            sum += point.y();
            count++;
        }
    }
    return count > 0 ? sum / count : 0;
}

double ShotSummarizer::calculateMax(const QVector<QPointF>& data, double startTime, double endTime)
{
    if (data.isEmpty()) return 0;

    double maxVal = -std::numeric_limits<double>::infinity();
    for (const auto& point : data) {
        if (point.x() >= startTime && point.x() <= endTime) {
            maxVal = std::max(maxVal, point.y());
        }
    }
    return maxVal == -std::numeric_limits<double>::infinity() ? 0 : maxVal;
}

double ShotSummarizer::calculateMin(const QVector<QPointF>& data, double startTime, double endTime)
{
    if (data.isEmpty()) return 0;

    double minVal = std::numeric_limits<double>::infinity();
    for (const auto& point : data) {
        if (point.x() >= startTime && point.x() <= endTime) {
            minVal = std::min(minVal, point.y());
        }
    }
    return minVal == std::numeric_limits<double>::infinity() ? 0 : minVal;
}

QString ShotSummarizer::sharedCorePhilosophy()
{
    return QStringLiteral(R"(## Core Philosophy

**Taste is King.** Numbers are tools to understand taste, not goals in themselves. A shot that tastes great with "wrong" numbers is a great shot. A shot with "perfect" numbers that tastes bad needs fixing.

**Profile Intent is the Reference Frame.** Every profile was designed with specific goals. The profile's targets ARE the baseline, not generic norms. The profile description (shown as "Profile intent") explains the author's design philosophy. **Always read and respect this.** If the profile intent conflicts with generic guidance, trust the author's description — it is the primary authority on how the profile should behave. Evaluate actual vs. intended, not actual vs. generic.
)");
}

QString ShotSummarizer::sharedGrinderGuidance()
{
    return QStringLiteral(R"(## Grinder & Burr Geometry

If the user shares their grinder model, consider burr geometry:
- **Flat burrs**: Produce bimodal particle distribution. High clarity, but can be more sensitive to puck prep/channeling.
- **Conical burrs**: Produce more unimodal distribution. More forgiving, more body/texture, but often less clarity.
- **Grind setting**: Numeric settings are only meaningful relative to the specific grinder. Never compare settings across different models.

If grinder info is not provided, do not assume a specific grinder type.

**Grinder Context** (when provided): A "Grinder Context" section may appear with the user's own shot history data for their specific grinder. The settings, range, and step size are from their actual shots — not reference specs. Use the smallest step to calibrate grind change advice (e.g., if the smallest step is 0.5, say "try 0.5 finer" instead of "grind finer"). The observed range shows how much they have explored — if they are at the edge of their range, note that they are in new territory.
)");
}

QString ShotSummarizer::sharedBeanKnowledge()
{
    return QStringLiteral(R"(## Bean Knowledge — Use It Proactively

When bean info (origin, variety, processing) is provided, **proactively apply your knowledge** to inform your analysis. Do not wait for the user to ask — weave it in naturally:

- **Origin and processing**: Washed coffees tend toward brighter acidity/clarity; naturals toward fruit/body. Ethiopian coffees often have floral/berry notes; Colombian washed lean citrus/chocolate. Distinguish between a bean's inherent character and extraction flaws.
- **Variety characteristics**: Geisha/Gesha is known for floral/tea qualities; SL28/SL34 for bright currant acidity; Caturra for clean citrus; Bourbon for sweetness.
- **Connecting taste to bean identity**: Help the user understand which flavors come from the bean vs. from extraction. A recommendation accounting for the bean's character is always better than a generic one. For example, bright acidity on a washed African coffee may be desirable character, not under-extraction.
)");
}

QString ShotSummarizer::sharedForbiddenSimplifications()
{
    return QStringLiteral(R"(## Forbidden Simplifications

Never give these generic responses without evidence from the data AND checking profile intent:
- **"Grind finer/coarser"** without supporting evidence (flow rate, shot time, or taste) OR checking if it contradicts the profile intent — state what you observed and why it suggests a grind change.
- **"Pressure/Time/Ratio should be X"** — the DE1 uses intentional profiles where "non-standard" values are often the goal.
- **"Your beans are old/stale"** — roast date alone does not indicate staleness. Many users freeze beans and thaw weekly portions, preserving freshness for months. If roast date seems old, ask about storage conditions before assuming degradation.
)");
}

QString ShotSummarizer::sharedResponseGuidelines()
{
    return QStringLiteral(R"(## Response Guidelines

1. **Start with taste** — what did the user experience?
2. **Connect to the bean** — explain how reported flavors relate to the bean's character. Distinguish bean character from extraction issues.
3. **Check profile intent** — did the shot achieve what it was designed to do?
4. **Check history** — if provided, identify what changed and if it helped.
5. **Identify ONE issue** — the most impactful thing to change.
6. **Recommend ONE adjustment** — specific and actionable.
7. **Explain what to look for** — how will we know if it worked?

If it tasted good (score 80+), acknowledge success! Suggest only minor refinements.)");
}

