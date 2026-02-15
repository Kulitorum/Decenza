#include "shotsummarizer.h"
#include "../models/shotdatamodel.h"
#include "../profile/profile.h"
#include "../network/visualizeruploader.h"

#include <cmath>
#include <algorithm>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

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

void ShotSummarizer::detectChannelingInPhases(ShotSummary& summary, const QVector<QPointF>& flowData) const
{
    summary.channelingDetected = false;
    bool isFilter = summary.beverageType.toLower() == "filter" ||
                    summary.beverageType.toLower() == "pourover";
    if (!isFilter) {
        for (const auto& phase : summary.phases) {
            if (!phase.isFlowMode) continue;
            if (phase.duration < 3.0) continue;
            if (detectChanneling(flowData, phase.startTime, phase.endTime)) {
                summary.channelingDetected = true;
                break;
            }
        }
    }
}

void ShotSummarizer::calculateTemperatureStability(ShotSummary& summary,
    const QVector<QPointF>& tempData, const QVector<QPointF>& tempGoalData) const
{
    if (!tempGoalData.isEmpty()) {
        double deviationSum = 0;
        int count = 0;
        for (const auto& point : tempData) {
            double target = findValueAtTime(tempGoalData, point.x());
            if (target > 0) {
                deviationSum += std::abs(point.y() - target);
                count++;
            }
        }
        summary.temperatureUnstable = count > 0 && (deviationSum / count) > 2.0;
    } else {
        double tempStdDev = calculateStdDev(tempData, 0, summary.totalDuration);
        summary.temperatureUnstable = tempStdDev > 2.0;
    }
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
        if (profile->isRecipeMode()) {
            // Convert EditorType enum to string for the shared helper
            QString editorStr;
            switch (profile->recipeParams().editorType) {
            case EditorType::DFlow: editorStr = "dflow"; break;
            case EditorType::AFlow: editorStr = "aflow"; break;
            case EditorType::Pressure: editorStr = "pressure"; break;
            case EditorType::Flow: editorStr = "flow"; break;
            }
            summary.profileType = profileTypeDescription(editorStr);
        } else {
            summary.profileType = profile->mode() == Profile::Mode::FrameBased ? "Frame-based" : "Direct Control";
        }
    }

    // Get the data vectors
    const auto& pressureData = shotData->pressureData();
    const auto& flowData = shotData->flowData();
    const auto& tempData = shotData->temperatureData();
    const auto& weightFlowData = shotData->weightFlowRateData();  // Flow rate from scale (g/s)
    const auto& cumulativeWeightData = shotData->cumulativeWeightData();  // Cumulative weight (g)

    if (pressureData.isEmpty()) {
        return summary;
    }

    // Store raw curve data for detailed AI analysis
    summary.pressureCurve = pressureData;
    summary.flowCurve = flowData;
    summary.tempCurve = tempData;
    summary.weightCurve = weightFlowData;  // Flow rate useful for AI analysis (detecting channeling spikes)

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
    summary.grinderModel = metadata.grinderModel;
    summary.grinderSetting = metadata.grinderSetting;
    summary.drinkTds = metadata.drinkTds;
    summary.drinkEy = metadata.drinkEy;
    summary.enjoymentScore = metadata.espressoEnjoyment;
    summary.tastingNotes = metadata.espressoNotes;

    // Extraction indicators
    summary.timeToFirstDrip = findTimeToFirstDrip(flowData);
    // Channeling detection will be done after phase processing (see below)

    // Temperature stability check - compare actual vs TARGET (not just variance)
    const auto& tempGoalData = shotData->temperatureGoalData();
    calculateTemperatureStability(summary, tempData, tempGoalData);

    // Get phase markers from shot data
    QVariantList markers = shotData->phaseMarkersVariant();

    if (markers.isEmpty()) {
        // No markers - create a single "Extraction" phase
        PhaseSummary phase;
        phase.name = "Extraction";
        phase.startTime = 0;
        phase.endTime = summary.totalDuration;
        phase.duration = summary.totalDuration;

        phase.avgPressure = calculateAverage(pressureData, 0, summary.totalDuration);
        phase.maxPressure = calculateMax(pressureData, 0, summary.totalDuration);
        phase.minPressure = calculateMin(pressureData, 0, summary.totalDuration);
        phase.pressureAtStart = findValueAtTime(pressureData, 0);
        phase.pressureAtMiddle = findValueAtTime(pressureData, summary.totalDuration / 2);
        phase.pressureAtEnd = findValueAtTime(pressureData, summary.totalDuration);

        phase.avgFlow = calculateAverage(flowData, 0, summary.totalDuration);
        phase.maxFlow = calculateMax(flowData, 0, summary.totalDuration);
        phase.minFlow = calculateMin(flowData, 0, summary.totalDuration);
        phase.flowAtStart = findValueAtTime(flowData, 0);
        phase.flowAtMiddle = findValueAtTime(flowData, summary.totalDuration / 2);
        phase.flowAtEnd = findValueAtTime(flowData, summary.totalDuration);

        phase.avgTemperature = calculateAverage(tempData, 0, summary.totalDuration);
        phase.tempStability = calculateStdDev(tempData, 0, summary.totalDuration);

        double startWeight = findValueAtTime(cumulativeWeightData, 0);
        double endWeight = findValueAtTime(cumulativeWeightData, summary.totalDuration);
        phase.weightGained = endWeight - startWeight;

        summary.phases.append(phase);
    } else {
        // Process each phase from markers
        for (int i = 0; i < markers.size(); i++) {
            QVariantMap marker = markers[i].toMap();
            double startTime = marker["time"].toDouble();
            double endTime = (i + 1 < markers.size())
                ? markers[i + 1].toMap()["time"].toDouble()
                : summary.totalDuration;

            if (endTime <= startTime) continue;

            PhaseSummary phase;
            phase.name = marker["label"].toString();
            phase.startTime = startTime;
            phase.endTime = endTime;
            phase.duration = endTime - startTime;
            phase.isFlowMode = marker["isFlowMode"].toBool();

            // Pressure metrics
            phase.avgPressure = calculateAverage(pressureData, startTime, endTime);
            phase.maxPressure = calculateMax(pressureData, startTime, endTime);
            phase.minPressure = calculateMin(pressureData, startTime, endTime);
            phase.pressureAtStart = findValueAtTime(pressureData, startTime);
            phase.pressureAtMiddle = findValueAtTime(pressureData, (startTime + endTime) / 2);
            phase.pressureAtEnd = findValueAtTime(pressureData, endTime);

            // Flow metrics
            phase.avgFlow = calculateAverage(flowData, startTime, endTime);
            phase.maxFlow = calculateMax(flowData, startTime, endTime);
            phase.minFlow = calculateMin(flowData, startTime, endTime);
            phase.flowAtStart = findValueAtTime(flowData, startTime);
            phase.flowAtMiddle = findValueAtTime(flowData, (startTime + endTime) / 2);
            phase.flowAtEnd = findValueAtTime(flowData, endTime);

            // Temperature metrics
            phase.avgTemperature = calculateAverage(tempData, startTime, endTime);
            phase.tempStability = calculateStdDev(tempData, startTime, endTime);

            // Weight gained
            double startWeight = findValueAtTime(cumulativeWeightData, startTime);
            double endWeight = findValueAtTime(cumulativeWeightData, endTime);
            phase.weightGained = endWeight - startWeight;

            // Track preinfusion duration
            QString lowerName = phase.name.toLower();
            if (lowerName.contains("preinfus") || lowerName.contains("pre-infus") ||
                lowerName.contains("bloom") || lowerName.contains("soak")) {
                summary.preinfusionDuration += phase.duration;
            } else {
                summary.mainExtractionDuration += phase.duration;
            }

            summary.phases.append(phase);
        }
    }

    // Detect channeling only during FLOW-CONTROLLED phases where flow should be stable.
    detectChannelingInPhases(summary, flowData);

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

ShotSummary ShotSummarizer::summarizeFromHistory(const QVariantMap& shotData) const
{
    ShotSummary summary;

    // Profile info
    summary.profileTitle = shotData.value("profileName", "Unknown").toString();
    summary.beverageType = shotData.value("beverageType", "espresso").toString();
    summary.profileNotes = shotData.value("profileNotes").toString();

    // Extract profile type from stored profile JSON
    QString profileJson = shotData.value("profileJson").toString();
    if (!profileJson.isEmpty()) {
        QJsonDocument profileDoc = QJsonDocument::fromJson(profileJson.toUtf8());
        if (profileDoc.isObject()) {
            QJsonObject profileObj = profileDoc.object();
            bool isRecipeMode = profileObj["is_recipe_mode"].toBool(false);
            if (isRecipeMode && profileObj.contains("recipe")) {
                QString editorType = profileObj["recipe"].toObject()["editorType"].toString();
                summary.profileType = profileTypeDescription(editorType);
            } else {
                QString profileType = profileObj["profile_type"].toString();
                if (profileType == "settings_2a") summary.profileType = "Pressure profile";
                else if (profileType == "settings_2b") summary.profileType = "Flow profile";
            }
        }
        summary.profileRecipeDescription = Profile::describeFramesFromJson(profileJson);
    }

    // Overall metrics
    summary.doseWeight = shotData.value("doseWeight", 0.0).toDouble();
    summary.finalWeight = shotData.value("finalWeight", 0.0).toDouble();
    summary.totalDuration = shotData.value("duration", 0.0).toDouble();
    summary.ratio = summary.doseWeight > 0 ? summary.finalWeight / summary.doseWeight : 0;

    // DYE metadata
    summary.beanBrand = shotData.value("beanBrand").toString();
    summary.beanType = shotData.value("beanType").toString();
    summary.roastLevel = shotData.value("roastLevel").toString();
    summary.grinderModel = shotData.value("grinderModel").toString();
    summary.grinderSetting = shotData.value("grinderSetting").toString();
    summary.drinkTds = shotData.value("drinkTds", 0.0).toDouble();
    summary.drinkEy = shotData.value("drinkEy", 0.0).toDouble();
    summary.enjoymentScore = shotData.value("enjoyment", 0).toInt();
    summary.tastingNotes = shotData.value("espressoNotes").toString();

    // Convert curve data
    summary.pressureCurve = variantListToPoints(shotData.value("pressure").toList());
    summary.flowCurve = variantListToPoints(shotData.value("flow").toList());
    summary.tempCurve = variantListToPoints(shotData.value("temperature").toList());
    summary.weightCurve = variantListToPoints(shotData.value("weight").toList());
    summary.pressureGoalCurve = variantListToPoints(shotData.value("pressureGoal").toList());
    summary.flowGoalCurve = variantListToPoints(shotData.value("flowGoal").toList());
    summary.tempGoalCurve = variantListToPoints(shotData.value("temperatureGoal").toList());

    if (summary.pressureCurve.isEmpty()) return summary;

    // Temperature stability
    calculateTemperatureStability(summary, summary.tempCurve, summary.tempGoalCurve);

    // Phase markers
    QVariantList phases = shotData.value("phases").toList();
    if (!phases.isEmpty()) {
        for (int i = 0; i < phases.size(); i++) {
            QVariantMap marker = phases[i].toMap();
            double startTime = marker.value("time", 0.0).toDouble();
            double endTime = (i + 1 < phases.size())
                ? phases[i + 1].toMap().value("time", 0.0).toDouble()
                : summary.totalDuration;
            if (endTime <= startTime) continue;

            PhaseSummary phase;
            phase.name = marker.value("label", "Phase").toString();
            phase.startTime = startTime;
            phase.endTime = endTime;
            phase.duration = endTime - startTime;
            phase.isFlowMode = marker.value("isFlowMode", false).toBool();

            phase.pressureAtStart = findValueAtTime(summary.pressureCurve, startTime);
            phase.pressureAtMiddle = findValueAtTime(summary.pressureCurve, (startTime + endTime) / 2);
            phase.pressureAtEnd = findValueAtTime(summary.pressureCurve, endTime);
            phase.flowAtStart = findValueAtTime(summary.flowCurve, startTime);
            phase.flowAtMiddle = findValueAtTime(summary.flowCurve, (startTime + endTime) / 2);
            phase.flowAtEnd = findValueAtTime(summary.flowCurve, endTime);
            phase.avgTemperature = calculateAverage(summary.tempCurve, startTime, endTime);

            double startWeight = findValueAtTime(summary.weightCurve, startTime);
            double endWeight = findValueAtTime(summary.weightCurve, endTime);
            phase.weightGained = endWeight - startWeight;

            summary.phases.append(phase);
        }
    }

    if (summary.phases.isEmpty()) {
        PhaseSummary phase;
        phase.name = "Extraction";
        phase.startTime = 0;
        phase.endTime = summary.totalDuration;
        phase.duration = summary.totalDuration;

        phase.pressureAtStart = findValueAtTime(summary.pressureCurve, 0);
        phase.pressureAtMiddle = findValueAtTime(summary.pressureCurve, summary.totalDuration / 2);
        phase.pressureAtEnd = findValueAtTime(summary.pressureCurve, summary.totalDuration);
        phase.flowAtStart = findValueAtTime(summary.flowCurve, 0);
        phase.flowAtMiddle = findValueAtTime(summary.flowCurve, summary.totalDuration / 2);
        phase.flowAtEnd = findValueAtTime(summary.flowCurve, summary.totalDuration);

        summary.phases.append(phase);
    }

    // Channeling detection (skip for filter/pourover)
    detectChannelingInPhases(summary, summary.flowCurve);

    return summary;
}

QString ShotSummarizer::buildUserPrompt(const ShotSummary& summary) const
{
    QString prompt;
    QTextStream out(&prompt);

    // Shot summary
    out << "## Shot Summary\n\n";
    out << "- **Profile**: " << (summary.profileTitle.isEmpty() ? "Unknown" : summary.profileTitle);
    if (!summary.profileAuthor.isEmpty()) out << " (by " << summary.profileAuthor << ")";
    if (!summary.profileType.isEmpty()) out << " — " << summary.profileType;
    out << "\n";
    if (!summary.profileNotes.isEmpty()) {
        out << "- **Profile intent**: " << summary.profileNotes << "\n";
    }
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

    // Coffee info
    if (!summary.beanBrand.isEmpty() || !summary.beanType.isEmpty()) {
        out << "- **Coffee**: " << summary.beanBrand;
        if (!summary.beanBrand.isEmpty() && !summary.beanType.isEmpty()) out << " - ";
        out << summary.beanType;
        if (!summary.roastLevel.isEmpty()) out << " (" << summary.roastLevel << ")";
        if (!summary.roastDate.isEmpty()) out << ", roasted " << summary.roastDate;
        out << "\n";
    }
    if (!summary.grinderModel.isEmpty()) {
        out << "- **Grinder**: " << summary.grinderModel;
        if (!summary.grinderSetting.isEmpty()) out << " @ " << summary.grinderSetting;
        out << "\n";
    }
    if (summary.drinkTds > 0 || summary.drinkEy > 0) {
        out << "- **Extraction**: ";
        if (summary.drinkTds > 0) out << "TDS " << QString::number(summary.drinkTds, 'f', 2) << "%";
        if (summary.drinkTds > 0 && summary.drinkEy > 0) out << ", ";
        if (summary.drinkEy > 0) out << "EY " << QString::number(summary.drinkEy, 'f', 1) << "%";
        out << "\n";
    }
    out << "\n";

    // Profile recipe (frame sequence)
    if (!summary.profileRecipeDescription.isEmpty()) {
        out << summary.profileRecipeDescription << "\n";
    }

    // Phase breakdown: start, peak-deviation (most diagnostic), end
    out << "## Phase Data\n\n";
    out << "Each phase shows start, peak deviation from target (most diagnostic point), and end. Values: actual(target).\n\n";

    for (const auto& phase : summary.phases) {
        QString controlMode = phase.isFlowMode
            ? "FLOW-CONTROLLED"
            : "PRESSURE-CONTROLLED";

        out << "### " << phase.name << " (" << QString::number(phase.duration, 'f', 0) << "s) " << controlMode << "\n";

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
                        std::abs(peakDevTime - (phase.endTime - 0.1)) > 1.0;

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
            out << QString::number(pressure, 'f', 1);
            if (pTarget > 0.1) out << "(" << QString::number(pTarget, 'f', 0) << ")";
            out << "bar ";
            out << QString::number(flow, 'f', 1);
            if (fTarget > 0.1) out << "(" << QString::number(fTarget, 'f', 1) << ")";
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
    if (summary.enjoymentScore == 0 && summary.tastingNotes.isEmpty()) {
        out << "- No tasting feedback provided\n";
    }
    out << "\n";

    // Observations (anomaly flags)
    if (summary.channelingDetected || summary.temperatureUnstable) {
        out << "## Observations\n\n";
        if (summary.channelingDetected)
            out << "- **Flow instability**: Sudden flow spike during flow-controlled extraction phase — verify against profile intent before diagnosing channeling\n";
        if (summary.temperatureUnstable)
            out << "- **Temperature unstable**: Average deviation from target exceeds 2\u00B0C\n";
        out << "\n";
    }

    out << "Analyze the curve data and sensory feedback. Provide ONE specific, evidence-based recommendation.\n";

    return prompt;
}

QString ShotSummarizer::systemPrompt(const QString& beverageType)
{
    if (beverageType.toLower() == "filter" || beverageType.toLower() == "pourover") {
        return filterSystemPrompt();
    }
    return espressoSystemPrompt();
}

QString ShotSummarizer::espressoSystemPrompt()
{
    return QStringLiteral(R"(You are an espresso analyst helping dial in shots on a Decent DE1 profiling machine.

## Core Philosophy

**Taste is King.** Numbers are tools to understand taste, not goals in themselves. A shot that tastes great with "wrong" numbers is a great shot. A shot with "perfect" numbers that tastes bad needs fixing.

**Profile Intent is the Reference Frame.** Every profile was designed with specific goals. The profile's targets ARE the baseline, not generic espresso norms. A Blooming Espresso at 2 bar is not "low pressure" — it's doing exactly what it should. A turbo shot finishing in 15 seconds is not "too fast." Evaluate actual vs. intended, not actual vs. generic.

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
- High pressure (8-12 bar) with high flow target = good puck resistance, well-prepared puck
- The pressure "target" shown is actually a LIMITER (safety max), not a goal

**Pressure-controlled phases** (pressure target 6-11 bar, low/no flow target):
- The machine maintains target pressure
- Flow is the RESULT of puck resistance
- Low flow at target pressure = high resistance (fine grind)
- High flow at target pressure = low resistance (coarse grind)

**Key insight**: When actual pressure differs greatly from "target" during a flow-controlled phase, that's normal — check if FLOW matched its target instead. The machine achieved what it was trying to do.

**Declining pressure during flow phases is normal.** As the coffee puck erodes during extraction, resistance drops, so pressure naturally declines even at constant flow. This is especially pronounced in lever-style and D-Flow profiles that transition from pressure control to flow control (shown as "from PRESSURE X bar" in the recipe). A pressure curve that peaks early and gradually declines is the expected signature of these profiles — do NOT flag it as a problem.

**Flow variation during pressure-controlled phases is normal.** When the machine controls PRESSURE, flow is just a passive result of puck resistance. As the puck saturates, compresses, and erodes, flow will naturally spike and settle. This is NOT channeling — channeling can only be diagnosed during FLOW-CONTROLLED phases where the machine is actively targeting stable flow. High flow during a pressure ramp-up (e.g., Filling at 6 bar) is simply water pushing through a dry puck.

## Grinder & Burr Geometry

If the user shares their grinder model, consider burr geometry:
- **Flat burrs**: Produce bimodal particle distribution. More clarity in the cup but higher channeling risk. Flow deviations may indicate alignment issues.
- **Conical burrs**: Produce unimodal distribution. More forgiving puck prep, less channeling-prone, but less clarity. Flow tends to be more stable.
- **Grind setting**: A numeric grind setting is only meaningful relative to the specific grinder. Never compare settings across different grinder models.

If grinder info is not provided, do not assume a specific grinder type.

## How to Read the Data

You'll receive:
1. **Shot summary**: dose, yield, ratio, time, profile name
2. **Profile recipe**: frame-by-frame intent (control mode, setpoints, exit conditions)
3. **Phase breakdown**: each phase with start, peak-deviation, and end samples
4. **Extraction measurements**: TDS and EY if available (refractometer data)
5. **Tasting notes**: the user's flavor perception (most important!)

Phase data shows actual values with targets in parentheses. The "Peak delta" sample is the moment of maximum deviation from target for the controlled variable — this is where problems show up. If no peak-delta is shown, the phase tracked its target well.

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

### The Good Shot
- **Symptoms**: Balanced sweetness and acidity, pleasant body, clean finish
- **Diagnosis**: If it tastes good, it IS good — don't fix what isn't broken!

## Roast Considerations

- **Light roasts**: Need higher temp (93-96°C), longer ratios (1:2.5-3), more patience
- **Medium roasts**: Forgiving, standard parameters (92-94°C, 1:2-2.5)
- **Dark roasts**: Need lower temp (88-91°C), shorter ratios (1:1.5-2), easy to over-extract

## Forbidden Simplifications

Never give these generic responses without evidence from the data:
- **"Grind finer"** without supporting evidence (flow rate, shot time, or taste) — state what you observed and why it suggests a grind change
- **"9 bar is standard"** — the DE1 uses profiles with intentional pressure targets; 2-6 bar profiles exist by design and are not "low pressure"
- **"Aim for 25-30 seconds"** — shot time depends entirely on the profile's intent; turbo, blooming, and lever profiles all have different valid time ranges
- **"Use a 1:2 ratio"** — ratio depends on roast, profile, and preference; explain the reasoning, not the rule

## Response Guidelines

1. **Start with taste** — what did the user experience?
2. **Check profile intent** — did the shot achieve what the profile was designed to do?
3. **Identify ONE issue** — the most impactful thing to change
4. **Recommend ONE adjustment** — specific and actionable, with reasoning
5. **Explain what to look for** — how will we know if it worked?

If the shot tasted good (score 80+), acknowledge success! Suggest only minor refinements if any.

Keep responses concise and practical. The goal is a better-tasting next shot, not a perfect analysis.)");
}

QString ShotSummarizer::filterSystemPrompt()
{
    return QStringLiteral(R"(You are a filter coffee analyst helping optimise brews made on a Decent DE1 profiling machine.

## What is DE1 Filter Coffee?

The Decent DE1 espresso machine can brew filter-style coffee by pushing water through a coffee puck at low pressure and high flow. This produces a cup closer to pour-over or drip coffee than espresso — lower concentration, higher clarity, larger volume.

## Core Philosophy

**Taste is King.** Numbers are tools to understand taste, not goals in themselves.

**Profile Intent is the Reference Frame.** Each filter profile was designed with specific goals for flow rate, pressure, temperature, and grind size. The profile description (shown as "Profile intent" in the data) explains the author's design philosophy. **Always read and respect this.** If a profile says "grind as coarse as possible" or "use Turkish grind," that IS the intended operating point — do not recommend moving toward generic filter norms.

**Grind advice must match the profile's design.** Some profiles are designed for very coarse grinds (near French press), others for finer filter grinds. The profile intent tells you which. If the user's grind setting seems extreme but matches what the profile calls for, it's correct — diagnose taste issues through temperature, ratio, or technique instead.

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

## Grinder & Burr Geometry

If the user shares their grinder model, consider burr geometry:
- **Flat burrs**: Can produce exceptional clarity in filter. The bimodal distribution works well at filter concentration.
- **Conical burrs**: More body and texture, less clarity. Both are valid for filter.
- Filter grind is much coarser than espresso — grind settings are not comparable.
- **Grind setting numbers are only meaningful within the same grinder.** A setting of 50 on a Niche may be coarse or medium depending on recalibration. Never assume a number is "too high" or "too low" without understanding the grinder and what the profile expects.

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

## Forbidden Simplifications

Never give these generic responses without evidence from the data AND checking profile intent:
- **"Grind finer/coarser"** without checking what the profile description says about grind — if the profile calls for very coarse grind, don't recommend finer just because flow seems high or brew seems fast
- **"Your grind setting is too high/low"** — grind numbers are grinder-specific and profile-specific; a setting of 50 may be exactly right for a coarse-grind profile
- **"Typical filter grind is X"** — there is no universal filter grind; it depends entirely on the profile's design

When taste is flat/thin but the profile calls for coarse grind, explore temperature, water quality, ratio, dose, and bean freshness BEFORE suggesting grind changes.

## Response Guidelines

1. **Start with taste** — what did the user experience?
2. **Read the profile intent** — what grind, flow, and technique does the profile expect? State this so the user knows you understand their profile.
3. **Check profile intent** — did the brew achieve what the profile was designed to do?
4. **Identify ONE issue** — the most impactful thing to change
5. **Recommend ONE adjustment** — specific and actionable, with reasoning
6. **Explain what to look for** — how will we know if it worked?

If the brew tasted good (score 80+), acknowledge success! Suggest only minor refinements if any.

Keep responses concise and practical. The goal is a better-tasting next brew, not a perfect analysis.)");
}

double ShotSummarizer::findValueAtTime(const QVector<QPointF>& data, double time) const
{
    if (data.isEmpty()) return 0;

    // Find closest point
    for (int i = 0; i < data.size(); i++) {
        if (data[i].x() >= time) {
            if (i == 0) return data[i].y();
            // Linear interpolation
            double t = (time - data[i-1].x()) / (data[i].x() - data[i-1].x());
            return data[i-1].y() + t * (data[i].y() - data[i-1].y());
        }
    }
    return data.last().y();
}

double ShotSummarizer::calculateAverage(const QVector<QPointF>& data, double startTime, double endTime) const
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

double ShotSummarizer::calculateMax(const QVector<QPointF>& data, double startTime, double endTime) const
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

double ShotSummarizer::calculateMin(const QVector<QPointF>& data, double startTime, double endTime) const
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

double ShotSummarizer::calculateStdDev(const QVector<QPointF>& data, double startTime, double endTime) const
{
    if (data.isEmpty()) return 0;

    double avg = calculateAverage(data, startTime, endTime);
    double sumSquares = 0;
    int count = 0;

    for (const auto& point : data) {
        if (point.x() >= startTime && point.x() <= endTime) {
            double diff = point.y() - avg;
            sumSquares += diff * diff;
            count++;
        }
    }

    return count > 1 ? std::sqrt(sumSquares / (count - 1)) : 0;
}

double ShotSummarizer::findTimeToFirstDrip(const QVector<QPointF>& flowData) const
{
    const double threshold = 0.5;  // mL/s - when we consider "drip" has started
    for (const auto& point : flowData) {
        if (point.y() >= threshold) {
            return point.x();
        }
    }
    return 0;
}

bool ShotSummarizer::detectChanneling(const QVector<QPointF>& flowData, double startTime, double endTime) const
{
    if (flowData.size() < 10) return false;

    // Look for sudden flow spikes (>50% increase in ~1s) within a flow-controlled phase.
    // Only meaningful during flow-controlled phases where the machine targets stable flow.
    for (int i = 5; i < flowData.size() - 5; i++) {
        if (flowData[i].x() < startTime) continue;
        if (flowData[i].x() > endTime) break;

        double prevFlow = flowData[i - 5].y();
        double currFlow = flowData[i].y();

        if (prevFlow > 0.5 && currFlow > prevFlow * 1.5) {
            return true;
        }
    }
    return false;
}
