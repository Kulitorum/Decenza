#include "shotsummarizer.h"
#include "../models/shotdatamodel.h"
#include "../profile/profile.h"
#include "../network/visualizeruploader.h"

#include <cmath>
#include <algorithm>

ShotSummarizer::ShotSummarizer(QObject* parent)
    : QObject(parent)
{
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
        summary.profileType = profile->mode() == Profile::Mode::FrameBased ? "Frame-based" : "Direct Control";
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
    summary.enjoymentScore = metadata.espressoEnjoyment;
    summary.tastingNotes = metadata.espressoNotes;

    // Extraction indicators
    summary.timeToFirstDrip = findTimeToFirstDrip(flowData);
    // Channeling detection will be done after phase processing (see below)

    // Temperature stability check - compare actual vs TARGET (not just variance)
    // A declining temperature profile is intentional, not "unstable"
    const auto& tempGoalData = shotData->temperatureGoalData();
    if (!tempGoalData.isEmpty()) {
        // Calculate average deviation from target
        double deviationSum = 0;
        int count = 0;
        for (const auto& point : tempData) {
            double target = findValueAtTime(tempGoalData, point.x());
            if (target > 0) {
                deviationSum += std::abs(point.y() - target);
                count++;
            }
        }
        double avgDeviation = count > 0 ? deviationSum / count : 0;
        summary.temperatureUnstable = avgDeviation > 2.0;  // >2°C average deviation from target
    } else {
        // No target data - fall back to variance check
        double tempStdDev = calculateStdDev(tempData, 0, summary.totalDuration);
        summary.temperatureUnstable = tempStdDev > 2.0;
    }

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

    // Now detect channeling - find actual extraction phase start time
    // Look for phases named "Extraction", "Pour", "Hold" etc.
    double extractionStartTime = summary.timeToFirstDrip + 3.0;  // Default fallback
    for (const auto& phase : summary.phases) {
        QString lowerName = phase.name.toLower();
        // Skip preparatory phases - only check during actual extraction
        if (lowerName.contains("extract") || lowerName.contains("pour") ||
            lowerName.contains("hold") || lowerName == "main") {
            extractionStartTime = phase.startTime;
            break;
        }
    }
    summary.channelingDetected = detectChanneling(flowData, extractionStartTime);

    return summary;
}

QString ShotSummarizer::buildUserPrompt(const ShotSummary& summary) const
{
    QString prompt;
    QTextStream out(&prompt);

    // Shot summary
    out << "## Shot Summary\n\n";
    out << "- **Profile**: " << (summary.profileTitle.isEmpty() ? "Unknown" : summary.profileTitle) << "\n";
    out << "- **Dose**: " << QString::number(summary.doseWeight, 'f', 1) << "g → ";
    out << "**Yield**: " << QString::number(summary.finalWeight, 'f', 1) << "g ";
    out << "(ratio 1:" << QString::number(summary.ratio, 'f', 1) << ")\n";
    out << "- **Duration**: " << QString::number(summary.totalDuration, 'f', 0) << "s\n";

    // Coffee info
    if (!summary.beanBrand.isEmpty() || !summary.beanType.isEmpty()) {
        out << "- **Coffee**: " << summary.beanBrand;
        if (!summary.beanBrand.isEmpty() && !summary.beanType.isEmpty()) out << " - ";
        out << summary.beanType;
        if (!summary.roastLevel.isEmpty()) out << " (" << summary.roastLevel << ")";
        out << "\n";
    }
    if (!summary.grinderModel.isEmpty()) {
        out << "- **Grinder**: " << summary.grinderModel;
        if (!summary.grinderSetting.isEmpty()) out << " @ " << summary.grinderSetting;
        out << "\n";
    }
    out << "\n";

    // Phase breakdown with start/middle/end samples
    out << "## Phase Data\n\n";
    out << "Each phase shows samples at start, middle, and end. Values: actual (target).\n\n";

    for (const auto& phase : summary.phases) {
        // Use actual control mode from profile frame data
        QString controlMode = phase.isFlowMode
            ? "FLOW-CONTROLLED - pressure is just a result of resistance"
            : "PRESSURE-CONTROLLED - flow is just a result of resistance";

        out << "### " << phase.name << " (" << QString::number(phase.duration, 'f', 0) << "s) - " << controlMode << "\n";

        // Get samples at start, middle, end of phase
        double times[3] = { phase.startTime, (phase.startTime + phase.endTime) / 2, phase.endTime - 0.1 };
        const char* labels[3] = { "Start", "Middle", "End" };

        for (int i = 0; i < 3; i++) {
            double t = times[i];
            double pressure = findValueAtTime(summary.pressureCurve, t);
            double flow = findValueAtTime(summary.flowCurve, t);
            double temp = findValueAtTime(summary.tempCurve, t);
            double weight = findValueAtTime(summary.weightCurve, t);
            double pTarget = findValueAtTime(summary.pressureGoalCurve, t);
            double fTarget = findValueAtTime(summary.flowGoalCurve, t);
            double tTarget = findValueAtTime(summary.tempGoalCurve, t);

            out << "- **" << labels[i] << "** @" << QString::number(t, 'f', 0) << "s: ";
            out << QString::number(pressure, 'f', 1);
            if (pTarget > 0.1) out << "(" << QString::number(pTarget, 'f', 0) << ")";
            out << " bar, ";
            out << QString::number(flow, 'f', 1);
            if (fTarget > 0.1) out << "(" << QString::number(fTarget, 'f', 1) << ")";
            out << " ml/s, ";
            out << QString::number(temp, 'f', 0);
            if (tTarget > 0) out << "(" << QString::number(tTarget, 'f', 0) << ")";
            out << "°C, ";
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

    out << "Analyze the curve data and sensory feedback. Provide ONE specific, evidence-based recommendation.\n";

    return prompt;
}

QString ShotSummarizer::systemPrompt()
{
    return QStringLiteral(R"(You are an espresso analyst helping dial in shots on a Decent DE1 profiling machine.

## Core Philosophy

**Taste is King.** Numbers are tools to understand taste, not goals in themselves. A shot that tastes great with "wrong" numbers is a great shot. A shot with "perfect" numbers that tastes bad needs fixing.

## The DE1 Machine

The DE1 controls either PRESSURE or FLOW at any moment (never both - they're inversely related through puck resistance):
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

**Key insight**: When actual pressure differs greatly from "target" during a flow-controlled phase, that's normal - check if FLOW matched its target instead. The machine achieved what it was trying to do.

## How to Read the Data

You'll receive:
1. **Shot summary**: dose, yield, ratio, time, profile name
2. **Phase breakdown**: each phase with start/middle/end samples showing pressure, flow, temp, weight
3. **Tasting notes**: the user's flavor perception (most important!)

The phase data shows actual values with targets in parentheses. The target tells you what the profile intended - compare actual vs target to assess if the machine achieved what it was trying to do.

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
- **Fix**: Better distribution and tamping - NOT grind change

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
- **Diagnosis**: If it tastes good, it IS good - don't fix what isn't broken!

## Roast Considerations

- **Light roasts**: Need higher temp (93-96°C), longer ratios (1:2.5-3), more patience
- **Medium roasts**: Forgiving, standard parameters (92-94°C, 1:2-2.5)
- **Dark roasts**: Need lower temp (88-91°C), shorter ratios (1:1.5-2), easy to over-extract

## Response Guidelines

1. **Start with taste** - what did the user experience?
2. **Check if the shot achieved its targets** - did actual match intended?
3. **Identify ONE issue** - the most impactful thing to change
4. **Recommend ONE adjustment** - specific and actionable
5. **Explain what to look for** - how will we know if it worked?

If the shot tasted good (score 80+), acknowledge success! Suggest only minor refinements if any.

Keep responses concise and practical. The goal is a better-tasting next shot, not a perfect analysis.)");
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

bool ShotSummarizer::detectChanneling(const QVector<QPointF>& flowData, double afterTime) const
{
    if (flowData.size() < 10) return false;

    // Look for sudden flow spikes (>50% increase in 0.5s) AFTER preinfusion
    // During preinfusion, flow naturally ramps up as water saturates the puck - this is normal
    for (int i = 5; i < flowData.size() - 5; i++) {
        // Skip preinfusion phase - flow variations are expected there
        if (flowData[i].x() < afterTime) continue;

        double prevFlow = flowData[i - 5].y();
        double currFlow = flowData[i].y();

        // Only flag channeling if we see sudden spikes during main extraction
        if (prevFlow > 0.5 && currFlow > prevFlow * 1.5) {
            return true;
        }
    }
    return false;
}
