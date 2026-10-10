#include "shotsummarizer.h"
#include "shotanalysis.h"
#include "../history/shothistory_types.h"  // HistoryPhaseMarker — passed to ShotAnalysis::analyzeShot
#include "../profile/profile.h"
#include "profileshapeindex.h"   // resolveProfileKb — title steps, then shape
#include "dialing_blocks.h"   // shared buildCurrentBeanBlock — single source of truth for currentBean

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


void ShotSummarizer::runShotAnalysisAndPopulate(ShotSummary& summary,
    const QVector<QPointF>& pressure,
    const QVector<QPointF>& flow,
    const QVector<QPointF>& weight,
    const QVector<QPointF>& conductanceDerivative,
    const QList<HistoryPhaseMarker>& markers,
    const QVector<QPointF>& pressureGoal,
    const QVector<QPointF>& flowGoal,
    const QStringList& analysisFlags,
    const QStringList& kbIds,
    double firstFrameSeconds,
    double targetWeightG,
    int frameCount) const
{
    // profileKbResolved gates grind Arm 1: "do we have any profile context at
    // all". Read off the caller's resolved set, NOT off summary.profileKbId —
    // that column is a title resolution only, so a shape-resolved shot carries
    // an empty id while having full context, and deriving the bit here would
    // skip Arm 1 on exactly the shots the shape work exists to serve. See
    // openspec changes skip-grind-arm1-when-kb-unresolved and
    // resolve-profile-kb-by-shape.
    const bool profileKbResolved = !kbIds.isEmpty();
    const ShotAnalysis::AnalysisResult analysis = ShotAnalysis::analyzeShot(
        pressure, flow, weight,
        conductanceDerivative, markers,
        summary.beverageType, summary.totalDuration,
        pressureGoal, flowGoal, analysisFlags,
        firstFrameSeconds, targetWeightG, summary.finalWeight,
        frameCount, expertBandForKbIds(kbIds),
        profileKbResolved);
    summary.summaryLines = analysis.lines;
    summary.pourTruncatedDetected = analysis.detectors.pourTruncated;
}

ShotSummary ShotSummarizer::summarizeFromHistory(const ShotProjection& shotData) const
{
    ShotSummary summary;

    // Profile info
    summary.profileTitle = shotData.profileName.isEmpty() ? QStringLiteral("Unknown") : shotData.profileName;
    summary.beverageType = shotData.beverageType.isEmpty() ? QStringLiteral("espresso") : shotData.beverageType;
    summary.profileNotes = shotData.profileNotes;
    summary.profileKbId = shotData.profileKbId;
    summary.targetWeight = shotData.targetWeightG;
    if (!shotData.profileJson.isEmpty())
        summary.profileSteps = Profile::describeFramesFromJson(shotData.profileJson);

    // Parse stored profile JSON once and use it for: (1) editorType-derived
    // profile-style description, (2) frame description, (3) firstFrameSeconds
    // for skip-first-frame detection. Was three separate parses; now one.
    const QString profileJson = shotData.profileJson;
    QJsonDocument profileDoc;
    if (!profileJson.isEmpty()) {
        profileDoc = QJsonDocument::fromJson(profileJson.toUtf8());
        if (profileDoc.isObject()) {
            const QJsonObject profileObj = profileDoc.object();
            // Profile's brewing temperature target — overridden by the
            // shot's per-pull temperatureOverrideC if non-zero, else
            // sourced from the profile's espresso_temperature field.
            if (shotData.temperatureOverrideC > 0)
                summary.targetTemperatureC = shotData.temperatureOverrideC;
            else if (profileObj.contains("espresso_temperature"))
                // profileJsonToDouble, not toDouble: the stored profile snapshot
                // string-encodes numbers (canonical DE1 v2 format), and a raw
                // toDouble() on a String returns 0 silently.
                summary.targetTemperatureC = profileJsonToDouble(profileObj["espresso_temperature"], 0.0);
            if (profileObj["has_recommended_dose"].toBool(false))
                summary.recommendedDoseG = profileJsonToDouble(profileObj["recommended_dose"], 0.0);
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
    }

    // Overall metrics
    summary.doseWeight = shotData.doseWeightG;
    summary.finalWeight = shotData.finalWeightG;
    summary.totalDuration = shotData.durationSec;
    summary.ratio = summary.doseWeight > 0 ? summary.finalWeight / summary.doseWeight : 0;

    // DYE metadata
    summary.beanBrand = shotData.beanBrand;
    summary.beanType = shotData.beanType;
    summary.beanBaseJson = shotData.beanBaseJson;
    summary.roastDate = shotData.roastDate;
    summary.roastLevel = shotData.roastLevel;
    summary.grinderBrand = shotData.grinderBrand;
    summary.grinderModel = shotData.grinderModel;
    summary.grinderBurrs = shotData.grinderBurrs;
    summary.grinderSetting = shotData.grinderSetting;
    summary.rpm = static_cast<int>(shotData.rpm);
    summary.drinkTds = shotData.drinkTdsPct;
    summary.drinkEy = shotData.drinkEyPct;
    summary.enjoymentScore = shotData.enjoyment0to100;
    summary.tastingNotes = shotData.espressoNotes;
    summary.tasteBalance = shotData.tasteBalance;
    summary.tasteBody = shotData.tasteBody;

    // Canonical currentBean inputs — single shared mapping. Carries the
    // puck-prep, basket, and freeze/thaw fields the old per-surface hand-roll
    // dropped, so the advisor sees the same currentBean as dialing_get_context.
    summary.beanInputs = DialingBlocks::beanInputsFromProjection(shotData);
    // ShotProjection.stoppedBy was introduced by #1161 (see
    // shotprojection.h:127); #1280 added the forwarding into buildShotBlock
    // so the standalone shot prompt carries the stop-reason anchor too.
    summary.stoppedBy = shotData.stoppedBy;

    // Convert curve data
    summary.pressureCurve = curveToPoints(shotData.pressure);
    summary.flowCurve = curveToPoints(shotData.flow);
    summary.tempCurve = curveToPoints(shotData.temperature);
    summary.weightCurve = curveToPoints(shotData.weight);
    summary.pressureGoalCurve = curveToPoints(shotData.pressureGoal);
    summary.flowGoalCurve = curveToPoints(shotData.flowGoal);
    summary.tempGoalCurve = curveToPoints(shotData.temperatureGoal);

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
    // matching observation lines.
    //
    // Precondition: callers populating `summaryLines` MUST also populate
    // `detectorResults.pourTruncated` (convertShotRecord does both, atomically),
    // since downstream consumers read the flag separately from the prose.
    if (!shotData.summaryLines.isEmpty()) {
        summary.summaryLines = shotData.summaryLines;
        summary.pourTruncatedDetected = shotData.detectorResults.value("pourTruncated").toBool();
        return summary;
    }

    // Slow path: shotData not produced by convertShotRecord (imported shots,
    // direct test callers) has empty summaryLines and needs the full analysis
    // pipeline. Delegate detector orchestration to runShotAnalysisAndPopulate,
    // which is this helper's only caller. historyMarkers was already populated
    // alongside the PhaseSummary list above (single pass).
    //
    // One Profile parse serves three consumers: the KB resolution, the
    // first-frame duration and the frame count. Profile::fromJson normalizes
    // both modern and legacy shapes, so skip-first-frame detection stays
    // accurate on legacy shots whose first frame was configured > 2 s.
    double firstFrameSeconds = -1.0;
    int frameCount = -1;
    QStringList kbIds;
    if (profileDoc.isObject()) {
        const Profile p = Profile::fromJson(profileDoc);
        frameCount = static_cast<int>(p.steps().size());
        // Both of these need frames, and for the same reason the guard is
        // load-bearing rather than defensive: a default-constructed Profile is
        // titled "Default", which is a REAL shipped profile carrying
        // flow_trend_ok, so resolving one would hand an unparseable shot
        // another profile's suppression flags. The resolution itself is the
        // one the storage layer runs (prepareAnalysisInputs): title steps
        // first, then SHAPE.
        if (!p.steps().isEmpty()) {
            firstFrameSeconds = p.steps().first().seconds;
            kbIds = resolveProfileKb(p).ids;
        }
    }
    // Fall back to the persisted id when the shot has no usable profile JSON,
    // which is every row saved before profile_json was stored.
    if (kbIds.isEmpty() && !summary.profileKbId.isEmpty())
        kbIds << summary.profileKbId;

    const QStringList analysisFlags = getAnalysisFlags(kbIds);

    const QVector<QPointF> derivCurve = curveToPoints(shotData.conductanceDerivative);

    // Per-shot targetWeight drives both arms of the grind-vs-yield check
    // (the choked-puck yield arm and the gusher arm added in PR Kulitorum/Decenza#910) —
    // matches the input convertShotRecord passes to analyzeShot.
    const double targetWeightG = shotData.targetWeightG;

    runShotAnalysisAndPopulate(summary,
        summary.pressureCurve, summary.flowCurve, summary.weightCurve,
        derivCurve, historyMarkers,
        summary.pressureGoalCurve, summary.flowGoalCurve, analysisFlags,
        kbIds, firstFrameSeconds, targetWeightG, frameCount);

    return summary;
}

static QJsonObject buildCurrentBeanBlock(const ShotSummary& summary)
{
    // Renders straight from the beanInputs that summarizeFromHistory()
    // assembled via the shared beanInputsFromProjection() mapper — the same
    // mapper dialing_get_context uses — so the two surfaces emit byte-
    // equivalent currentBean JSON for the same shot. The field mapping lives
    // in the mapper, never duplicated here.
    return DialingBlocks::buildCurrentBeanBlock(summary.beanInputs);
}

static QJsonObject buildCurrentProfileBlock(const ShotSummary& summary)
{
    QJsonObject profile;
    profile["title"] = summary.profileTitle;
    if (!summary.profileNotes.isEmpty()) profile["intent"] = summary.profileNotes;
    // Issue #1158: append the stop-at-weight clarification via the
    // shared helper so this (advisor) path and dialing_get_context's
    // MCP profile block render the steps identically.
    if (!summary.profileSteps.isEmpty())
        profile["steps"] = DialingBlocks::withStopAtWeightNote(
            summary.profileSteps, summary.targetWeight);
    if (summary.targetWeight > 0) profile["targetWeightG"] = summary.targetWeight;
    if (summary.targetTemperatureC > 0) profile["targetTemperatureC"] = summary.targetTemperatureC;
    if (summary.recommendedDoseG > 0) profile["recommendedDoseG"] = summary.recommendedDoseG;
    return profile;
}

static QJsonObject buildTastingFeedbackBlock(const ShotSummary& summary)
{
    // Only structural booleans — the per-call recommendation framing is
    // taught once in the system prompt's "How to read structured fields"
    // section, not repeated per call. Mirrors dialing_get_context's
    // tastingFeedback shape so a single system prompt reads correctly off
    // either surface.
    QJsonObject tf;
    tf["hasEnjoymentScore"] = summary.enjoymentScore > 0;
    tf["hasNotes"] = !summary.tastingNotes.isEmpty();
    tf["hasRefractometer"] = summary.drinkTds > 0 || summary.drinkEy > 0;
    // Structured taste taps (add-ai-taste-intake) count as tasting feedback:
    // when either is set the user HAS told us how it tasted, so the advisor
    // must not open by asking "how did it taste?". Also carry the values so the
    // model reasons on them directly.
    const bool hasTasteAxis = !summary.tasteBalance.isEmpty() || !summary.tasteBody.isEmpty();
    tf["hasTasteAxis"] = hasTasteAxis;
    if (!summary.tasteBalance.isEmpty()) tf["tasteBalance"] = summary.tasteBalance;
    if (!summary.tasteBody.isEmpty()) tf["tasteBody"] = summary.tasteBody;
    return tf;
}

// Helper: peak {value, atSec} over the given curve.
static QJsonObject peakWithTime(const QVector<QPointF>& curve)
{
    double peakVal = 0;
    double peakTime = 0;
    for (const auto& pt : curve) {
        if (pt.y() > peakVal) { peakVal = pt.y(); peakTime = pt.x(); }
    }
    QJsonObject obj;
    obj["value"] = QString::number(peakVal, 'f', 2).toDouble();
    obj["atSec"] = QString::number(peakTime, 'f', 0).toInt();
    return obj;
}

// Helper: peak {value, atSec} for a curve restricted to a [start, end]
// time window. Used for per-phase peaks within the structured block.
static QJsonObject peakWithTimeInWindow(const QVector<QPointF>& curve,
                                         double startTime, double endTime)
{
    double peakVal = 0;
    double peakTime = startTime;
    for (const auto& pt : curve) {
        if (pt.x() < startTime || pt.x() > endTime) continue;
        if (pt.y() > peakVal) { peakVal = pt.y(); peakTime = pt.x(); }
    }
    QJsonObject obj;
    obj["value"] = QString::number(peakVal, 'f', 2).toDouble();
    obj["atSec"] = QString::number(peakTime, 'f', 0).toInt();
    return obj;
}

static QJsonObject buildOverallPeaksBlock(const ShotSummary& summary)
{
    QJsonObject peaks;
    const QJsonObject pressurePeak = peakWithTime(summary.pressureCurve);
    const QJsonObject flowPeak = peakWithTime(summary.flowCurve);
    if (pressurePeak.value(QStringLiteral("value")).toDouble() > 0.1)
        peaks["pressureBar"] = pressurePeak;
    if (flowPeak.value(QStringLiteral("value")).toDouble() > 0.1)
        peaks["flowMlPerSec"] = flowPeak;
    return peaks;
}

// Build a structured phases[] array. Each phase carries name, duration,
// control mode, and peak pressure / flow within the phase. Phase samples
// (start / peakDeviation / end) stay in the prose body for now — the
// deterministic detector lines that summarize them already live in
// `detectorObservations`. Issue #1037: structural fields the AI can
// iterate over without pattern-matching prose.
//
// Threading: pure read of `summary.phases` and the curve members. Safe
// to call on any thread that owns `summary`. Same threading contract
// as `buildUserPromptObject` overall.
static QJsonArray buildPhasesBlock(const ShotSummary& summary)
{
    QJsonArray phases;
    for (const auto& phase : summary.phases) {
        QJsonObject p;
        p["name"] = phase.name;
        p["durationSec"] = QString::number(phase.duration, 'f', 0).toInt();
        // Human-readable enum (CLAUDE.md MCP convention).
        p["controlMode"] = phase.isFlowMode
            ? QStringLiteral("flow")
            : QStringLiteral("pressure");
        QJsonObject phasePeaks;
        const QJsonObject pp = peakWithTimeInWindow(
            summary.pressureCurve, phase.startTime, phase.endTime);
        const QJsonObject fp = peakWithTimeInWindow(
            summary.flowCurve, phase.startTime, phase.endTime);
        if (pp.value(QStringLiteral("value")).toDouble() > 0.1)
            phasePeaks["pressureBar"] = pp;
        if (fp.value(QStringLiteral("value")).toDouble() > 0.1)
            phasePeaks["flowMlPerSec"] = fp;
        if (!phasePeaks.isEmpty()) p["peaks"] = phasePeaks;
        phases.append(p);
    }
    return phases;
}

// Build a structured detectorObservations[] array. Each entry is
// `{type, kind, text}`:
//   - `type` ∈ {warning, caution, good, observation} — severity tag.
//   - `kind` is a stable enum identifier ("channeling_sustained",
//     "grind_too_fine", etc.) populated by the
//     deterministic detector pipeline. Consumers SHOULD read by `kind`
//     instead of substring-matching `text`, which is freeform prose
//     intended for the LLM and may be reworded across releases.
//   - `text` is the human-readable line shown in the in-app dialog.
//
// The verdict line (`type=verdict`) is omitted — it's a deterministic
// prescriptive conclusion ("Puck choked — grind way too fine.
// Coarsen significantly.") that would anchor the LLM on a pre-cooked
// answer. The non-verdict lines still ship — they are pre-interpreted,
// severity-tagged observation strings, not raw curve data — but
// withholding the verdict preserves the LLM's value-add of synthesizing
// across signals (bean / prior shots / tasting feedback) instead of
// parroting the verdict line. See the long rationale in
// renderShotAnalysisProse.
//
// `kind` is omitted only for legacy lines that predate #1037 — every
// production line emitted by ShotAnalysis::analyzeShot today carries
// one. See `src/ai/shotanalysis.cpp` for the canonical kind list.
static QJsonArray buildDetectorObservationsBlock(const ShotSummary& summary)
{
    QJsonArray observations;
    for (const QVariant& v : summary.summaryLines) {
        const QVariantMap m = v.toMap();
        const QString type = m.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("verdict")) continue;
        QJsonObject obs;
        obs["type"] = type;
        obs["text"] = m.value(QStringLiteral("text")).toString();
        const QString kind = m.value(QStringLiteral("kind")).toString();
        if (!kind.isEmpty()) obs["kind"] = kind;
        observations.append(obs);
    }
    return observations;
}

static QJsonObject buildShotBlock(const ShotSummary& summary)
{
    // Shot-VARIABLE fields the AIConversation change-detection layer
    // diffs between adjacent shots in a multi-shot session. Issue #1039:
    // before this block existed, AIConversation parsed dose / yield /
    // duration / score / notes via brittle regex against the prose
    // body. Now they live as structured fields the consumer can read
    // directly. Identity fields (bean / grinder / profile) stay in
    // `currentBean` / `profile` — this block only carries what the
    // user iterates on.
    //
    // Issue #1037 layered structured `phases[]`, `detectorObservations[]`,
    // and `overallPeaks` on top so the AI can iterate over phase data
    // and detector signals programmatically instead of pattern-matching
    // prose.
    //
    // Empty / zero / false fields are omitted so the regex consumer's
    // legacy "field absent on either side ⇒ skip the diff" semantics
    // carry over to the structured path without special-casing.
    QJsonObject shot;
    if (summary.doseWeight > 0) shot["doseG"] = summary.doseWeight;
    if (summary.finalWeight > 0) shot["yieldG"] = summary.finalWeight;
    if (summary.totalDuration > 0) shot["durationSec"] = summary.totalDuration;
    if (summary.ratio > 0) shot["ratio"] = summary.ratio;
    if (!summary.grinderSetting.isEmpty()) shot["grinderSetting"] = summary.grinderSetting;
    if (summary.rpm > 0) shot["rpm"] = summary.rpm;  // RPM half of the dial-in (sparse)
    if (summary.drinkTds > 0) shot["extractionTdsPct"] = summary.drinkTds;
    if (summary.drinkEy > 0) shot["extractionEyPct"] = summary.drinkEy;
    // CLAUDE.md MCP convention: scale lives in the field name for
    // bounded values. Mirrors `dialing_get_context.bestRecentShot.enjoyment0to100`.
    if (summary.enjoymentScore > 0) shot["enjoyment0to100"] = summary.enjoymentScore;
    if (!summary.tastingNotes.isEmpty()) shot["notes"] = summary.tastingNotes;
    // Structured taste taps (add-ai-taste-intake): the two dial-in axes the
    // curve can't reveal, so the model reasons on them directly.
    if (!summary.tasteBalance.isEmpty()) shot["tasteBalance"] = summary.tasteBalance;
    if (!summary.tasteBody.isEmpty()) shot["tasteBody"] = summary.tasteBody;
    // #1280: stop-reason anchor. Allowlist matches dialing_blocks.cpp so the
    // standalone shot block carries the same field set as bestRecentShot /
    // dialInSessions[].history. "profileEnd" and empty are intentionally
    // omitted — the system prompt's "stoppedBy → is the yield a real outcome
    // or a user choice?" rubric documents how the model should treat an
    // absent field (profile-end vs DE1 hardware button).
    if (summary.stoppedBy == QStringLiteral("manual")
        || summary.stoppedBy == QStringLiteral("weight")
        || summary.stoppedBy == QStringLiteral("volume"))
        shot["stoppedBy"] = summary.stoppedBy;
    const QJsonObject overallPeaks = buildOverallPeaksBlock(summary);
    if (!overallPeaks.isEmpty()) shot["overallPeaks"] = overallPeaks;
    const QJsonArray phases = buildPhasesBlock(summary);
    if (!phases.isEmpty()) shot["phases"] = phases;
    const QJsonArray detectorObservations = buildDetectorObservationsBlock(summary);
    if (!detectorObservations.isEmpty())
        shot["detectorObservations"] = detectorObservations;
    return shot;
}

QJsonObject ShotSummarizer::buildUserPromptObject(const ShotSummary& summary, RenderMode mode) const
{
    // HistoryBlock mode has no JSON envelope — its callers concatenate
    // prose-per-shot under `### Shot (date)` wrappers and never see this
    // object. The assert catches misuse in dev; the early-return preserves
    // safe (if useless) behavior in release.
    Q_ASSERT_X(mode != RenderMode::HistoryBlock,
               "ShotSummarizer::buildUserPromptObject",
               "HistoryBlock mode has no JSON envelope; use buildUserPrompt() instead");
    if (mode == RenderMode::HistoryBlock) {
        return QJsonObject();
    }

    // Standalone mode: JSON envelope so the system prompt's references to
    // `currentBean.*`, `profile.*`, `tastingFeedback.*`, etc., land on
    // actual fields. The existing prose body lives verbatim under
    // `shotAnalysis` — preserves the deterministic detector lines,
    // phase data, etc. in the form the model already understands. (It had a
    // second audience once, the regex consumers in AIConversation; those are
    // deleted, so the format now answers to the model alone.)
    // Key names mirror dialing_get_context's response shape so a single
    // system prompt reads correctly off either surface.
    QJsonObject payload;
    payload["currentBean"] = buildCurrentBeanBlock(summary);
    payload["profile"] = buildCurrentProfileBlock(summary);
    payload["tastingFeedback"] = buildTastingFeedbackBlock(summary);
    // Shot-VARIABLE structured fields (issue #1039). The downstream
    // change-detection layer in `AIConversation` reads these directly
    // instead of regex-extracting them out of the prose body. Empty
    // when the shot has no quantitative data populated yet.
    const QJsonObject shot = buildShotBlock(summary);
    if (!shot.isEmpty()) payload["shot"] = shot;
    payload["shotAnalysis"] = renderShotAnalysisProse(summary, mode);
    return payload;
}

QString ShotSummarizer::buildShotAnalysisProse(const ShotSummary& summary) const
{
    return renderShotAnalysisProse(summary, RenderMode::Standalone);
}

QString ShotSummarizer::buildUserPrompt(const ShotSummary& summary, RenderMode mode) const
{
    // HistoryBlock mode: per-shot prose embedded under a `### Shot (date)`
    // header by the caller. Stays prose so the multi-shot history block
    // reads naturally; JSON-per-shot would be unreadable when concatenated.
    if (mode == RenderMode::HistoryBlock) {
        return renderShotAnalysisProse(summary, mode);
    }

    return serializePayload(buildUserPromptObject(summary, mode));
}

QString ShotSummarizer::serializePayload(const QJsonObject& payload)
{
    return QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

QString ShotSummarizer::renderShotAnalysisProse(const ShotSummary& summary, RenderMode mode) const
{
    QString prompt;
    QTextStream out(&prompt);
    const bool isHistoryBlock = (mode == RenderMode::HistoryBlock);

    // Shot summary — shot-VARIABLE fields only. Per openspec
    // optimize-dialing-context-payload (tasks 8 + 9): profile identity
    // (title / intent / steps) lives in `result.profile`; bean identity
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
        out << "- **Grind setting**: " << summary.grinderSetting;
        if (summary.rpm > 0)  // pair the RPM half for variable-RPM grinders
            out << " @ " << summary.rpm << " RPM";
        out << "\n";
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
    // Structured taste taps (add-ai-taste-intake) — the user's coarse
    // single-tap impression of the two dial-in axes the curve can't reveal.
    if (!summary.tasteBalance.isEmpty()) {
        out << "- **Taste (extraction balance)**: " << summary.tasteBalance << "\n";
    }
    if (!summary.tasteBody.isEmpty()) {
        out << "- **Body**: " << summary.tasteBody << "\n";
    }
    if (summary.enjoymentScore == 0 && summary.tastingNotes.isEmpty()
        && summary.tasteBalance.isEmpty() && summary.tasteBody.isEmpty()) {
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
    // so every shot shares the same profile name and steps. Emit them
    // once at the top instead of N× per shot. The first shot with a
    // populated profileJson seeds the steps (all shots on the same KB
    // family render to the same frame description).
    QString profileName, profileSteps;
    for (const QVariant& v : recentShots) {
        const ShotProjection s = ShotProjection::fromVariantMap(v.toMap());
        if (profileName.isEmpty() && !s.profileName.isEmpty())
            profileName = s.profileName;
        if (profileSteps.isEmpty() && !s.profileJson.isEmpty())
            profileSteps = Profile::describeFramesFromJson(s.profileJson);
        if (!profileName.isEmpty() && !profileSteps.isEmpty()) break;
    }
    if (!profileName.isEmpty()) {
        out << "### Profile: " << profileName << "\n";
        if (!profileSteps.isEmpty())
            out << profileSteps << "\n";
        else
            out << "\n";
    }

    for (qsizetype i = 0; i < recentShots.size(); ++i) {
        const ShotProjection shot = ShotProjection::fromVariantMap(recentShots[i].toMap());

        // Skip entries with no meaningful data (corrupt or incomplete records)
        if (shot.doseWeightG <= 0 && shot.finalWeightG <= 0 && shot.durationSec <= 0) continue;

        const double ratio = shot.doseWeightG > 0 ? shot.finalWeightG / shot.doseWeightG : 0;

        out << "### Shot " << (i + 1) << " (" << shot.timestampIso << ")\n";
        // `Profile:` and the steps are hoisted to the single header above
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
            if (shot.rpm > 0) out << " " << shot.rpm << " RPM";  // pair RPM half
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
    base += QStringLiteral(R"(

## How to Read Structured Fields

Treat these JSON payload fields as gates on your advice:

**`result.profile`**: the canonical source for profile metadata — `filename`, `title`, `intent`, `steps`, `targetWeightG`, `targetTemperatureC`, and `recommendedDoseG` (when set). Read profile intent and frame steps here. The `shotAnalysis` prose carries shot-VARIABLE data only (dose, yield, duration, grind setting, extraction, peaks, phase data, detector observations); profile, bean and grinder identity come only from the structured blocks.

**`currentBean`** + **`dialInSessions[].context`**: shot-INVARIANT identity for the resolved shot. `currentBean.brand` / `.type` / `.roastLevel` are bean identity; `currentBean.grinderBrand` / `.grinderModel` / `.grinderBurrs` are grinder identity. Every `currentBean` field describes THE SETUP THAT PRODUCED THE RESOLVED SHOT, not what is loaded on the machine now. An empty string means the shot did NOT record that field (common on legacy shots), not that the user has no grinder / bean / etc. Ask before recommending a change to any blank field.

**`tastingFeedback`**: booleans `hasEnjoymentScore`, `hasNotes`, `hasRefractometer`, `hasTasteAxis`, plus `tasteBalance` (sour/balanced/bitter) and `tasteBody` (thin/medium/heavy) when the user tapped them. `tasteBalance` is the extraction axis — sour ⇒ likely under-extracted (grind finer / hotter / longer), bitter ⇒ likely over-extracted (grind coarser / cooler / shorter); `tasteBody` is the concentration/mouthfeel axis. When ALL four booleans are false, ASK the user how the shot tasted (score 1–100, 1–2 lines of flavor notes, TDS reading if available) before suggesting changes. If `hasTasteAxis` is true, the user HAS told you how it tasted — do NOT open by asking; reason from the tapped axes.

**Repeated untasted shots (stricter than the rule above)**: when tasting feedback (score, notes, or a taste tap) has been absent for the LAST 2 OR MORE shots in this conversation, ask for a taste score before using success/quality language ("successful", "optimal", "excellent", "dialed in") about those shots from pressure/flow curves alone. You may still describe the curves, framed as preliminary pending taste feedback. This applies even if an earlier shot in the conversation had a score.

**`currentBean.beanFreshness`**: optional `roastDate`, storage dates, a `referenceDate` (the day the shot was pulled), a `freshnessKnown` flag, and an `instruction`. Always follow `instruction`.
- `freshnessKnown` `false`: no storage dates are recorded, but `roastDate` is the UPPER BOUND on staleness (freezing pauses staling, airtight/vacuum slows it). A RECENT roast is fresh regardless of storage — do NOT ask about storage. Only an OLD roast is ambiguous (frozen-and-fresh vs left-out-and-stale); only then ask. If a `storageHint` is present, never re-ask storage type; at most ask (old roast only) for the aging-start date.
- `freshnessKnown` `true`: storage IS known — do NOT ask. When `restAgeDays` is present it is the beans' age, computed for you with frozen time removed (only freezing pauses aging: roast→freeze plus thaw→`referenceDate`; a frozen bag with no `defrostDate` is being ground straight from the freezer) — quote it rather than recomputing. When it is absent (no usable roast date), state no age; ask for the roast date if it matters. `openedDate` (when this portion was first used) does NOT reset age; it is how long the beans have been exposed to air. An `openedDate` alone does not make storage known: every bag gets one from its first shot. Low age (about a week or less) means under-rested and gassy — such beans choke the puck, run long, over-extract, and usually want a COARSER grind that settles back over the next few days — including a portion frozen soon after roast and just thawed.

**`dialInSessions[].context`**: hoists shot-identity fields shared across a session — the equipment package (`grinderBrand`, `grinderModel`, `grinderBurrs`, `basketBrand`, `basketModel`, `puckPrep`), plus `beanBrand`, `beanType` and the storage dates (`frozenDate`/`defrostDate`/`storageHint`/`openedDate`). It also hoists `roastDate`, so two roasts of one coffee read as different beans. A `shots[]` entry that omits one of these uses the `context` value; one that carries it overrides the context for that shot only, and a storage date of `null` means that shot recorded none (a frozen shot with no thaw, or a shot from before these dates were kept). A shot's `restAgeDays`, and `bestRecentShot.restAgeDays`, are that shot's bean age as above; compare them before attributing a change to grind or dose. `bestRecentShot.sameBagAsCurrent` says whether the anchor came from the bag in use now. CAVEAT: for the other fields a hoisted value is the first non-empty value across the session, so a legacy shot that never recorded the field appears to have the modern value. For an older shot's grinder/bean, treat context as a best-effort inference, not its recorded data.

**`noDialInHistory`** (when present): appears INSTEAD of `dialInSessions` and means the history query ran and matched no prior shot on this equipment package; it carries the matched `equipment` and `matchedShotCount: 0`. It is a fact, not a gap to fill: there is no earlier shot to cite, so judge the current shot on its own data. If it and `dialInSessions` are both absent, no history was established (not requested, or the lookup did not complete) — not evidence of an empty history.

**`recentAdvice`** (when present): up to 3 of YOUR prior recommendations on this profile, each with `turnsAgo`, your prior `structuredNext` (prediction), and `userResponse` (the user's actual next shot). It is your track record: self-correct mid-session rather than restarting analysis. Choose the next move from `userResponse.adherence`:
- `"followed"` AND outcome got worse (low `outcomeRating0to100`, OR most of `outcomeInPredictedRange.*` is `false`) ⇒ REVISE direction; do not repeat a failed experiment.
- `"followed"` AND outcome was good ⇒ commit harder; the direction is right.
- `"ignored"` ⇒ the user did NOT run your experiment. STAY THE COURSE before pivoting; you have no data point yet.
- `"partial"` ⇒ some but not all parameters moved. Note what's missing and ASK before revising.
- `"unclear"` ⇒ your prior recommendation named no checkable setting (e.g. "a touch coarser" instead of a dial value), so adherence is UNKNOWN. Treat it like `"ignored"`: STAY THE COURSE, do not assume it ran, and this time give a concrete value in `grinderSetting`.

When `outcomeRating0to100` is OMITTED, the follow-up was unrated: assume neither good nor bad, use `outcomeInPredictedRange` for a curve-shape signal, and ask about taste.

**Referring to shots when you reply to the user**: cite shots by their LOCAL DATE AND TIME (the handle the user sees in Shot History), NEVER by the numeric `id` (an internal key the user cannot find in the app). Every shot in `dialInSessions`, `bestRecentShot`, and `shots_list` carries a local ISO `timestamp`; render it as a person reads a clock ("your May 10, 9:04 AM shot"), not raw ISO. Use `id` only as an opaque argument to other tools.

**Only cite a shot that is in this context.** Never state a score, taste note, grind setting or date for a shot absent from `dialInSessions`, `bestRecentShot`, `recentAdvice`, the current shot, or a tool result you fetched; a remembered or inferred shot is not the user's. If the history you need is absent, say what is missing and judge the shot on its own data.

**Grind settings compare only WITHIN one equipment package.** A grind number is a position on one grinder's dial (9.5 on a Niche Zero and on an EG-1 are unrelated), and the same number through a different basket is a different flow. The history here is already filtered to the current package (grinder, basket and puck prep), so other gear is absent by design. Do not reason across packages.
)");

    // Conversational metadata corrections (capability shot-metadata-capture).
    // When the user volunteers a bean-field correction mid-conversation
    // ("actually it's really dark", "the bean is from Sey", "roasted
    // 2026-04-15"), the app parses the correction and writes it back to
    // the anchored shot's metadata before the next request lands. This
    // teaches the model to (a) acknowledge the write so the user knows it
    // stuck, and (b) trust the next-turn `currentBean.*` over what the
    // user typed last turn.
    base += QStringLiteral(R"(

## Conversational metadata corrections

If the user clarifies bean info in their reply (roast level, brand, roast date), the app silently writes the correction back to the shot's metadata. BRIEFLY acknowledge it in your next reply with one line (e.g., "Got it — I've updated the shot's roast to Dark"), then advise using the corrected value. On later turns, trust `currentBean.*`, not the user's last-turn phrasing.

Bean-identity fields (roastLevel, beanBrand, roastDate) are the only fields captured this way. Per-shot physical recordings (dose, yield, grind setting, duration, curves) are NOT editable from conversation — if those look wrong, ask the user to pull a new shot.
)");

    // Structured nextShot output. The shot-analysis system prompt teaches
    // the model to emit a fenced ```json block at the very end of any
    // response that makes a concrete parameter recommendation (grind,
    // RPM, dose, yield target, profile). The app parses that block out of the
    // response, persists it alongside the assistant turn in
    // `AIConversation`, and surfaces it on the `ai_advisor_invoke` MCP
    // envelope so downstream consumers don't have to re-parse prose. The
    // block MUST be omitted entirely when the response is a clarifying
    // question or has no parameter recommendation — there is no null-state
    // placeholder.
    base += QStringLiteral(R"RF(

## Response Format

When your response recommends a concrete change to grinder setting, RPM, dose, yield target, or profile, append the `nextShot` block — a fenced code block tagged `json` — at the very end of your message — after the prose and any closing thoughts, with NOTHING following the closing fence except whitespace. If your response is a clarifying question (e.g., asking how the shot tasted) or otherwise makes no parameter recommendation (including "re-pull at the same settings"), OMIT the block entirely — no placeholder.

Schema:

- `grinderSetting` (string) — REQUIRED iff you recommend moving grind. Omit when grind is unchanged.
- `rpm` (integer) — REQUIRED iff you recommend moving motor RPM. ONLY for variable-RPM grinders (the shot's `rpm` / `grinderContext.rpmsObserved` are present); OMIT for fixed-RPM grinders and when RPM is unchanged. RPM and grind are independent axes; include only the one you are changing.
- `doseG` (number) — REQUIRED iff you recommend moving dose. Omit when dose is unchanged.
- `targetWeightG` (number) — REQUIRED iff you recommend a different stop-at-weight yield (e.g. a shorter ratio). Omit when unchanged.
- `profileTitle` (string) — REQUIRED iff you recommend switching profile. The title (`result.profile.title`), not the filename. Omit otherwise.
- `expectedDurationSec` ([low, high]) — REQUIRED. Predicted duration window if your recommendation is followed.
- `expectedFlowMlPerSec` ([low, high]) — REQUIRED.
- `expectedPeakPressureBar` ([low, high]) — OPTIONAL. Only when your advice specifically targets pressure dynamics.
- `successCondition` (string) — REQUIRED. A short predicate the user can read (e.g., `"score >= 70 OR (durationSec in [32,38] AND flowMlPerSec in [1.0,1.5])"`).
- `reasoning` (string) — REQUIRED. One sentence explaining WHY.

Example (grind change):

```json
{
  "grinderSetting": "4.75",
  "expectedDurationSec": [32, 38],
  "expectedFlowMlPerSec": [1.0, 1.5],
  "successCondition": "durationSec in [32,38] AND flowMlPerSec in [1.0,1.5]",
  "reasoning": "Slow flow toward profile target without going past the choke point"
}
```

The opening fence is exactly ```json (never ```nextShot). Use only ASCII double-quotes for keys and strings — no smart quotes.
)RF");

    // Detector-observations legend. Per openspec optimize-dialing-context-payload
    // (task 3), this lives in the system prompt (taught once per
    // conversation) instead of the per-call prose body. Per-shot blocks
    // still emit `[warning] / [caution] / [good] / [observation]` tags
    // on individual detector lines; this legend tells the AI how to
    // weight them.
    base += QStringLiteral(R"(

## Reading Detector Observations

Per-shot blocks may include a `## Detector Observations` section of severity-tagged lines from the same deterministic detectors behind the in-app Shot Summary badges. Treat them as evidence; tags reflect detector confidence, not your final assessment:

- [warning] high-confidence failure mode (sustained channeling, choked puck, yield overshoot/gusher, pour truncated, frame skip)
- [caution] directional hint (grind drift, flow trend)
- [good] positive signal (puck stable)
- [observation] context (preinfusion drip mass)

Cross-check against the raw curves and the user's tasting feedback and reason independently — you have richer context (bean, prior shots, tasting notes) than the detectors.
)");

    // Append dial-in reference tables for espresso (cacheable, shared with MCP)
    if (beverageType.toLower() != "filter" && beverageType.toLower() != "pourover") {
        if (const QString& dialIn = dialInReference(); !dialIn.isEmpty()) {
            base += QStringLiteral("\n\n## Espresso Dial-In Reference Tables\n\n"
                "How espresso variables affect taste. Use them to choose which variable to change "
                "and in which direction.\n\n")
                + dialIn;
        }
    }

    // Include profile catalog for cross-profile awareness (espresso only — catalog
    // and "When to Suggest a Different Profile" guidance are espresso-centric)
    if (beverageType.toLower() != "filter" && beverageType.toLower() != "pourover") {
        loadProfileKnowledge();
        if (!s_profileCatalog.isEmpty()) {
            base += QStringLiteral("\n\n## Available Profiles with Curated Knowledge\n\n"
                "When the user's roast, beans, or goals suggest a better match, you can recommend "
                "switching to one of these. The current shot's profile has a detailed section "
                "below.\n\n")
                + s_profileCatalog;

            // Profile families — every catalog entry above carries a
            // [family: <name>] tag. Profiles in the same family share the
            // same underlying mechanic; switching within a family is
            // usually a parameter tweak in disguise (e.g., D-Flow → Londinium:
            // both lever-decline). This block is added unconditionally
            // after the catalog so the rule sits where the data is.
            base += QStringLiteral(R"(

## Profile families

Each profile carries a `[family: <name>]` tag; profiles in the same family implement the same extraction mechanic. A within-family switch (e.g., D-Flow → Londinium — both `lever-decline`) is USUALLY a parameter tweak in disguise: adjusting temperature, dose, or grind on the current profile achieves the same outcome. It is only meaningful when the alternative encodes a constraint the user CANNOT replicate by tweaking the current profile (e.g., `80's Espresso` is `lever-decline` like D-Flow, but bakes in a low-temperature regime — 82°C declining to 72°C — that's hard to replicate by editing D-Flow's frame temps).

When you recommend a profile switch, name the family of the current and proposed profile and what the family change buys the user. If both are the same family, EITHER explain the specific constraint the alternative bakes in, OR drop the recommendation and suggest a parameter tweak on the current profile.

## Other-profile parameter discipline

You have full step data (frame setpoints, temperatures, pressures, durations) ONLY for the current shot's profile, in `result.profile.steps`. For every other catalog profile you have ONLY the one-line description (category, family, roast suitability). DO NOT quote numeric setpoints of non-current profiles (e.g., "Londinium runs 89-90°C", "E61 peaks at 9 bar") — they are not in your context, and inventing them is hallucination.

Describe a different profile qualitatively — "lower temperature regime", "higher peak pressure", "shorter total duration", "flow-controlled instead of pressure-controlled" — and let the user pull a reference shot on it to see its actual numbers. If the user explicitly asks for a non-current profile's setpoints, say you don't have its steps and offer qualitative tradeoffs.
)");
        }

        // Cross-cutting reference sections (Skip-Catalog: true) — currently
        // contains "Cross-Profile Grind Ordering". Injected within the espresso
        // path (filter and pour-over excluded) so the model can reason about
        // cross-profile and cross-roast grind direction.
        const QString crossProfile = crossProfileReferenceContent();
        if (!crossProfile.isEmpty()) {
            base += QStringLiteral("\n\n") + crossProfile;
        }
    }

    // Look up profile-specific knowledge by KB ID (computed from title/alias matching),
    // falling back to fuzzy title/editorType matching for shots without a stored KB ID
    QString resolvedKbId = profileKbId;
    loadProfileKnowledge();
    if (resolvedKbId.isEmpty() || !s_profileKnowledge.contains(resolvedKbId)) {
        resolvedKbId = matchProfileKey(s_profileKnowledge, profileTitle, profileType);
    }
    if (!resolvedKbId.isEmpty() && s_profileKnowledge.contains(resolvedKbId)) {
        const auto pk = s_profileKnowledge.value(resolvedKbId);
        // Name the KB entry explicitly (issue #1459): without a label here,
        // this prose was indistinguishable from any other catalog entry's
        // description, and the model picked a plausible-sounding catalog
        // name by matching the DESCRIBED characteristics (e.g. attributing
        // Allongé's "constant ~4.5 ml/s flow, low pressure" to TurboTurbo,
        // which shares that description) instead of the shot's actual
        // profile. `pk.name` is the matched KB entry's canonical display
        // name, NOT the shot's own title, and NOT its `family` tag (the
        // `[family: ...]` cluster used elsewhere for switching guidance) —
        // a custom or renamed profile can resolve to this KB entry while
        // having a different title in `result.profile.title`.
        base += QStringLiteral("\n\n## Current Profile Knowledge: ") + pk.name + QStringLiteral("\n\n"
            "Curated knowledge for the profile matched to this shot. The heading is the matched KB "
            "entry's canonical name; the shot's actual title is `result.profile.title` and may "
            "differ if the user renamed or customized the profile. Always refer to the shot by its "
            "ACTUAL title, never by this KB entry's name. Use this knowledge to tell INTENTIONAL "
            "behavior from a problem.\n\n")
            + pk.content;
    }

    return base;
}


QString ShotSummarizer::espressoSystemPrompt()
{
    return QStringLiteral(R"(You are an espresso analyst helping dial in shots on a Decent DE1 profiling machine.

)") + sharedCorePhilosophy() + QStringLiteral(R"(
## The DE1 Machine

The DE1 controls either PRESSURE or FLOW at any moment, never both (they're inversely related through puck resistance); whichever is not controlled is the result of puck resistance. Profiles run named phases (Prefill, Preinfusion, Extraction, etc.) sequentially, each with its own targets and behavior.

## Reading Targets vs Limiters

Phase data shows actual values with targets in parentheses.

**Flow-controlled phases** (e.g. a 4-8 ml/s fill or a 1.5-2.5 ml/s pour): the machine pushes water at the target flow; pressure builds as a RESULT of puck resistance — in a pour, typically 6-10 bar depending on grind and puck prep. The pressure "target" shown is a LIMITER (safety max), not a goal — when actual pressure differs greatly from it, that's normal; check whether FLOW matched its target instead.

**Pressure-controlled phases** (a pressure target with low/no flow target — 6-11 bar in a pour, far lower in soak and bloom phases): the machine holds target pressure; flow is the RESULT. Low flow at target pressure = high resistance (fine grind); high flow = low resistance (coarse grind).

**Declining pressure during flow phases is normal.** As the puck erodes, resistance drops, so pressure declines even at constant flow. A pressure curve that peaks early and gradually declines is the expected signature — especially in lever-style and D-Flow/Londinium-type profiles that switch from pressure-controlled fill/infuse to flow-controlled pour (shown as "from PRESSURE X bar" in the steps), after which pressure is passive. Do NOT flag it as a problem.

**Flow variation during pressure-controlled phases is normal.** Flow is passive, so it spikes and settles as the puck saturates, compresses, and erodes. A flow spike on its own is NOT channeling — channeling is diagnosed from the conductance derivative (dC/dt), which measures how the flow↔pressure relationship changes. High flow during a pressure ramp-up (e.g., Filling at 6 bar) is water pushing through a dry puck — expected.

## Reading the Profile Steps for Expected Behavior

Use the profile steps to set expectations BEFORE reading the actual data:

**Temperature stepping**: when frames use different temperatures (e.g., 84°C fill → 94°C pour), actual temperature ALWAYS lags the target; a 5-8°C gap during transitions is normal. Only flag temperature when actual deviates from target during a STABLE phase (same temperature across consecutive frames).

**Flow-controlled pour with pressure limiter** (e.g., 1.8 ml/s with a 10 bar limiter): pressure peaks according to puck resistance; anything from 4 bar to the limiter is normal. The peak depends on grind — do not assume a specific peak unless the profile notes state one.

**Stop-at-weight + flow-controlled pour → yield and duration are mechanical, not dial-in feedback**: when the profile's pour frame is FLOW-controlled and a `targetWeightG` is set (in dial-in history, flow control is the explicit `pourControl: "flow"` on the session `context`, on the individual shot when a session mixes variants, and on `bestRecentShot`), the scale cutoff pins the final yield and total time ≈ stopWeight ÷ flowTarget — both set by the shot's settings, not the grind. Do NOT credit a grind change for "yield landed on target", and do NOT treat a shorter or longer duration as a dial-in or quality signal for these shots. Grind only moves yield/time here in the extremes: a puck so fine it chokes and never reaches the flow target, or so coarse it gushes with almost no resistance. Judge these shots by the pressure the puck developed at the target flow, taste, TDS/EY, and channeling.

**`stoppedBy` → is the yield a real outcome or a user choice?**: dial-in shots, `bestRecentShot`, and `shots_list` rows may carry `stoppedBy`: `"weight"` / `"volume"` (stop-at-weight / stop-at-volume cutoff), or `"manual"` (the user tapped Stop).
- `"manual"`: final yield, ratio, and total duration are WHATEVER the user stopped at, NOT extraction outcomes. Do NOT diagnose grind, "inconsistent yield", under/over-extraction, or ratio from them; judge only pressure/flow behavior up to the stop, taste, TDS/EY, and channeling, or ask the user to pull one to completion.
- ABSENT: the shot ran to completion OR was stopped by the DE1's physical button (the machine does not report which). If `yieldG` is well short of `targetWeightG` (roughly <90%), treat it exactly like `"manual"`; at/near `targetWeightG`, treat it as a normal completed shot.
- `"weight"`/`"volume"`: the cutoff pinned yield to the target, so yield on target is mechanical (do not credit a grind change for it), but pressure-at-flow, duration, taste, and channeling remain valid signals.

**Exit conditions**: frames with exit conditions (e.g., "exit:p>3.0") advance when the condition is met; short phases (1-2s) after them are normal.

## How to Read the Data

Each phase has start / peak-deviation / end samples. The "PeakΔ" sample is the controlled variable's maximum deviation from target — where problems show up; no PeakΔ means the phase tracked its target well. Without tasting feedback you may describe the curves and extraction metrics, but follow the `tastingFeedback` rule (ask before suggesting changes) and do not guess what the user tasted.

)") + sharedGrinderGuidance() + QStringLiteral(R"(
In espresso, flat burrs carry higher channeling risk (flow deviations may indicate alignment issues); conical burrs make puck prep more forgiving and flow more stable.

## Grinder Adjustment Procedure

Before recommending a grinder change with any magnitude (clicks, microns, "to setting X", "half a step"):

1. **Check available shot history.** Always start with `dialInSessions` — recent dial-in shots on the current equipment package. **If you have shot-history tools** (MCP clients have `shots_list`, filterable by `profileName` and `beanBrand`), call them for broader history. The in-app advisor has no tools — only what is in the prompt.
2. **If a reference shot exists**: anchor to it — cite its setting and identify the shot by local date and time, per "Referring to shots" ("you pulled this profile at grinder setting 7 on your May 10, 9:04 AM shot — start there").
3. **Switching profiles**: use a `grinderCalibration` number only as its rules allow (Cross-Profile Grind Ordering).
4. **Otherwise**: move one `grinderContext.stepSize` from the current setting in the needed direction and give that dial value (setting 9, step 0.25, coarser → "9.25"); in prose call it a first step, not a calibrated amount. If no `stepSize` is known, stay directional and omit the `nextShot` block: a direction is not a concrete change.

"Common Espresso Patterns" below gives the **direction** of a grinder change; the **magnitude** comes only from this procedure — never a guess, never UGS arithmetic. UGS distances (Cross-Profile Grind Ordering) are relative comparisons between profiles: never convert one into grinder steps, microns, or letter-coded positions yourself — that needs a per-user calibration, and UGS values are not on the grinder dial. When in doubt, take the single step.

## Common Espresso Patterns

**Lever ordering.** Grind and ratio are the primary espresso levers — settle those first. Temperature is a smaller, later adjustment; don't reach for it to fix sourness or bitterness until grind and ratio are dialed. (Exception: when the profile's description calls out temperature as central to its design, respect the author's intent — see the "Profile Intent is the Reference Frame" note.)

### The Gusher
Symptoms: very fast for the profile (e.g. <20s on a standard one), flow way above target, thin/watery. Cause: grind too coarse or severe channeling. Fix: grind finer (if consistent) or improve puck prep (if erratic).

### The Choker
Symptoms: very slow for the profile (e.g. >45s on a standard one), flow way below target, bitter/astringent. Cause: grind too fine. Fix: grind coarser.

### The Channeler
Symptoms: erratic flow during extraction, uneven taste, sour and bitter notes together. Cause: water finding paths of least resistance through the puck. Fix: better distribution and tamping — NOT a grind change.

### The Sour Shot
Symptoms: bright acidity, thin body, tea-like, possibly underextracted. Possible causes: ratio too short, shot too fast, grind too coarse (temperature too low is secondary). Fix, one change at a time: grind finer, or pull longer (lengthen the ratio) — settle those first; raise temp 2°C only after. Caveat: if channeling appears *after* going finer, re-check puck prep first (see The Channeler); if it persists, step back to the previous grind setting — past that point, finer extracts *less*.

### The Bitter Shot
Symptoms: harsh, astringent, dry finish, overextracted. Possible causes: ratio too long, shot too slow, grind too fine (temperature too high is secondary). Fix, one change at a time: grind coarser, or cut the shot earlier — settle those first; drop temp 2°C only after.

### The Hollow Shot
Symptoms: lacks body, empty in the middle, thin mouthfeel. Cause: often channeling or underextraction. Fix: improve puck prep or increase extraction (finer/hotter/longer).

## Roast Considerations

Light roasts want longer ratios (1:2.5-3) and patience; medium roasts are forgiving (1:2-2.5); dark roasts want shorter ratios (1:1.5-2) and over-extract easily. Starting temperatures by roast are in the Dial-In Reference Tables.

)") + sharedBeanKnowledge() + QStringLiteral(R"(
- **Roaster style**: if you recognize the roaster (e.g., light Nordic-style vs. traditional Italian), factor that into temperature and ratio suggestions.

)") + sharedForbiddenSimplifications() + QStringLiteral(R"(
- **"9 bar is standard"** — DE1 profiles have intentional pressure targets; 2-6 bar profiles exist by design and are not "low pressure" (a Blooming Espresso at 2 bar is doing exactly what it should)
- **"Aim for 25-30 seconds"** — shot time depends on the profile's intent; turbo, blooming, and lever profiles have different valid time ranges (a turbo shot finishing in 15 seconds is not "too fast")
- **"Use a 1:2 ratio"** — ratio depends on roast, profile, and preference; explain the reasoning, not the rule

## When to Suggest a Different Profile

If "Available Profiles with Curated Knowledge" is present in this prompt, you may recommend switching profiles when:
- the roast clearly mismatches the current profile's design (e.g., ultra-light beans on a dark-optimized lever profile)
- multiple shots show the same persistent issue that another profile addresses by design (e.g., always channeling at 9 bar → a 6 bar profile like Gentle & Sweet)
- the user explicitly asks about other profiles or brewing styles

Do NOT suggest a profile change after a single shot unless the mismatch is severe; give the current profile 2-3 shots first. Explain WHY the alternative suits their beans/goals better.

)") + sharedResponseGuidelines() + QStringLiteral(R"(
Keep responses concise and practical: the goal is a better next shot, not a perfect analysis.)");
}

QString ShotSummarizer::filterSystemPrompt()
{
    return QStringLiteral(R"(You are a filter coffee analyst helping optimise brews made on a Decent DE1 profiling machine.

## What is DE1 Filter Coffee?

The DE1 brews filter coffee by pushing water through the puck at low pressure and high flow: lower concentration, higher clarity and larger volume than espresso, like pour-over.

)") + sharedCorePhilosophy() + QStringLiteral(R"(
**Grind advice must match the profile's design.** Some profiles expect very coarse grinds (near French press), others finer filter grinds; the profile intent tells you which. If the user's grind seems extreme but matches what the profile calls for, it's correct — diagnose taste issues through temperature, ratio, or technique instead.

## How DE1 Filter Differs from Traditional Filter

- **Pressure**: typically 1-3 bar (vs near-zero in pour-over); 0-3 bar is normal and intentional — do not suggest increasing pressure.
- **Flow**: 3-8+ ml/s is normal — this is how filter profiles work. At 6+ ml/s, turbulence causes natural fluctuation that is NOT channeling.
- **Brew time**: typically 2-6 minutes depending on dose and profile; a 4-minute brew is not a "choker".
- **Ratios**: typically 1:10 to 1:17 (similar to traditional filter); 1:15 is standard, not excessive.
- **Temperature**: typically 90-100°C, often higher than espresso.
- **Grind size**: from slightly finer than pour-over to as coarse as French press — **read the profile description to know what grind it expects.**
- **Dose**: often 15-25g, similar to pour-over.

## Reading Targets vs Limiters

Phase data (pressure, flow, temperature, weight at start/middle/end) shows actual values with targets in parentheses; judge it against the filter norms above, not espresso norms. Filter profiles are almost entirely flow-controlled:

**Flow-controlled phases** (most filter phases): the machine pushes water at the target flow (often 4-8+ ml/s); pressure is a RESULT of puck resistance, NOT a target. The pressure value in parentheses is a LIMITER (safety cap), not a goal — 1.2 bar against a "target" of 3 bar is perfectly normal (the limiter was never reached). **Do not diagnose pressure as "low" or "off-target" during flow-controlled phases.**

**Pressure-controlled phases** (rare in filter, sometimes used for bloom): the machine holds target pressure (usually very low, 0.5-2 bar); flow is the RESULT of puck resistance.

## Bloom and Soak Phases

Many filter profiles open with a bloom or soak phase that wets the bed evenly and lets CO2 escape (degassing). It shows as low or zero flow for 30-60+ seconds at the start — **intentional**; do not flag low flow or long pauses during bloom. The main pour follows at higher flow. Some profiles pulse water during bloom (on-off-on) by design. Treat a phase named "Bloom", "Soak", "Wet", or "Saturate" as preparation, not extraction.

)") + sharedGrinderGuidance() + QStringLiteral(R"(
In filter, flat burrs can produce exceptional clarity (the bimodal distribution works well at filter concentration); conical burrs give more body and texture, less clarity. Both are valid. Filter grind is much coarser than espresso — the settings are not comparable.

## Common Filter Issues

**Lever ordering.** Grind and brew time are the primary levers here too — settle those first; temperature 2-3°C adjustments come after. (Exceptions: when the profile's design pins the grind — see the grind-advice rule above — or its description calls out temperature as central, follow the profile's intent per the "Profile Intent is the Reference Frame" note.)

### Astringent / Dry Finish
Cause: over-extraction, often from too fine a grind or too high a temperature. Fix: grind coarser, then cooler.

### Thin / Watery / Hollow
Cause: under-extraction from too coarse a grind, too low a temperature, or insufficient contact time. Fix: grind finer or extend contact time, then hotter.

### Bitter / Harsh
Cause: over-extraction or water too hot. Fix: grind slightly coarser or shorten brew time, then cooler.

### Sour / Sharp Acidity
Cause: under-extraction. Fix: grind finer or extend brew time, then hotter.

### Muddy / Lacking Clarity
Cause: too many fines (grinder-dependent) or channeling through the puck. Fix: grind coarser, improve puck prep, or check grinder alignment.

### Sweet and Balanced
If it tastes good, it IS good — don't fix what isn't broken!

## Roast Considerations

- **Light roasts**: higher temperature (95-100°C), benefit from longer contact time
- **Medium roasts**: versatile, standard parameters (92-96°C)
- **Dark roasts**: lower temperature (88-93°C), shorter brew time, easy to over-extract

)") + sharedBeanKnowledge() + QStringLiteral(R"(
- **Roaster style**: if you recognize the roaster, factor their typical roast philosophy into your suggestions.

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
    // The bolded title "Profile Intent is the Reference Frame" is referenced verbatim
    // by the "Lever ordering" notes in espressoSystemPrompt() and filterSystemPrompt().
    return QStringLiteral(R"(## Core Philosophy

**Taste is King.** Numbers are tools to understand taste, not goals. A shot that tastes great with "wrong" numbers is a great shot; one with "perfect" numbers that tastes bad needs fixing.

**Profile Intent is the Reference Frame.** Every profile was designed with specific goals; its targets ARE the baseline, not generic norms. The profile description (`result.profile.intent`) is the author's design philosophy — **always read and respect it.** When it conflicts with generic guidance, trust the author's description: it is the primary authority on how the profile should behave. Evaluate actual vs. intended, not actual vs. generic.
)");
}

QString ShotSummarizer::sharedGrinderGuidance()
{
    return QStringLiteral(R"(## Grinder & Burr Geometry

If the user shares their grinder model, consider burr geometry:
- **Flat burrs**: bimodal particle distribution. High clarity, but more sensitive to puck prep/channeling.
- **Conical burrs**: more unimodal distribution. More forgiving, more body/texture, often less clarity.

If grinder info is not provided, do not assume a grinder type.

**`grinderContext`** (when present): the user's own settings on this grinder — `settingsObserved`, `observedMinSetting`/`observedMaxSetting` and `stepSize` — from their actual shots, not reference specs. `stepSize` is the size of one grind move: say "try 0.25 finer", not "grind finer". At the edge of the observed range, note that they are in new territory.
)");
}

QString ShotSummarizer::sharedBeanKnowledge()
{
    return QStringLiteral(R"(## Bean Knowledge — Use It Proactively

When bean info (origin, variety, processing) is provided, **proactively apply your knowledge** — don't wait to be asked:

- **Origin and processing**: washed coffees lean brighter acidity/clarity, naturals fruit/body. Ethiopian coffees often have floral/berry notes; Colombian washed lean citrus/chocolate.
- **Variety characteristics**: Geisha/Gesha floral/tea; SL28/SL34 bright currant acidity; Caturra clean citrus; Bourbon sweetness.
- **Bean vs. extraction**: distinguish a bean's inherent character from extraction flaws and account for it in recommendations — e.g., bright acidity on a washed African coffee may be desirable character, not under-extraction.
)");
}

QString ShotSummarizer::sharedForbiddenSimplifications()
{
    return QStringLiteral(R"(## Forbidden Simplifications

Never give these generic responses without evidence from the data AND checking profile intent:
- **"Grind finer/coarser"** without supporting evidence (flow rate, shot time, or taste) and a check that it doesn't contradict the profile intent — state what you observed and why it suggests a grind change.
- **"Pressure/Time/Ratio should be X"** — DE1 profiles are intentional; "non-standard" values are often the goal.
- **"Your beans are old/stale"** — roast date alone does not indicate staleness: many users freeze beans and thaw weekly portions, or keep them airtight/vacuum-sealed, for months. Apply the `currentBean.beanFreshness` rules (How to Read Structured Fields), including that low age (a recent roast, or a portion frozen soon after roast and just thawed) can mean UNDER-rested, gassy beans that want a coarser grind, not "fresher" ones.
)");
}

QString ShotSummarizer::sharedResponseGuidelines()
{
    return QStringLiteral(R"(## Response Guidelines

1. **Start with taste** — what did the user experience?
2. **Connect to the bean** — relate reported flavors to the bean's character vs. extraction issues.
3. **Check profile intent** — did the shot achieve what it was designed to do?
4. **Check history** — if provided, what changed and did it help?
5. **Identify ONE issue** — the most impactful thing to change.
6. **Recommend ONE adjustment** — specific and actionable.
7. **Explain what to look for** — how will we know it worked?

If it tasted good (score 80+), acknowledge success and suggest only minor refinements.)");
}

