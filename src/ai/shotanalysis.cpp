#include "shotanalysis.h"
#include "history/shothistorystorage.h"  // HistoryPhaseMarker

#include <QVariantMap>
#include <cmath>

ShotAnalysis::ChannelingSeverity ShotAnalysis::detectChannelingFromDerivative(
    const QVector<QPointF>& conductanceDerivative,
    double pourStart, double pourEnd,
    double* outMaxSpikeTime)
{
    if (outMaxSpikeTime) *outMaxSpikeTime = 0.0;
    if (conductanceDerivative.isEmpty()) return ChannelingSeverity::None;

    const double analysisStart = pourStart + CHANNELING_DC_POUR_SKIP_SEC;
    double maxSpike = 0.0;
    double maxSpikeTime = 0.0;
    int sustainedCount = 0;

    for (const auto& pt : conductanceDerivative) {
        if (pt.x() < analysisStart) continue;
        if (pt.x() > pourEnd) break;
        const double v = std::abs(pt.y());
        if (v > maxSpike) {
            maxSpike = v;
            maxSpikeTime = pt.x();
        }
        if (v > CHANNELING_DC_ELEVATED) ++sustainedCount;
    }

    if (outMaxSpikeTime) *outMaxSpikeTime = maxSpikeTime;

    if (sustainedCount > CHANNELING_DC_SUSTAINED_COUNT)
        return ChannelingSeverity::Sustained;
    if (maxSpike > CHANNELING_DC_TRANSIENT_PEAK)
        return ChannelingSeverity::Transient;
    return ChannelingSeverity::None;
}

bool ShotAnalysis::shouldSkipChannelingCheck(const QString& beverageType,
                                               const QVector<QPointF>& flowData,
                                               double pourStart, double pourEnd)
{
    QString bev = beverageType.toLower();
    if (bev == QStringLiteral("filter") || bev == QStringLiteral("pourover"))
        return true;

    // Check for turbo: avg flow during extraction > threshold
    if (pourStart < pourEnd && !flowData.isEmpty()) {
        double sum = 0;
        int count = 0;
        for (const auto& fp : flowData) {
            if (fp.x() < pourStart) continue;
            if (fp.x() > pourEnd) break;
            if (fp.y() > 0.05) { sum += fp.y(); ++count; }
        }
        if (count > 0 && (sum / count) > CHANNELING_MAX_AVG_FLOW)
            return true;
    }

    return false;
}

bool ShotAnalysis::hasIntentionalTempStepping(const QVector<QPointF>& tempGoalData)
{
    double goalMin = 999, goalMax = 0;
    for (const auto& gp : tempGoalData) {
        if (gp.y() > 0) {
            goalMin = qMin(goalMin, gp.y());
            goalMax = qMax(goalMax, gp.y());
        }
    }
    return (goalMax - goalMin) > TEMP_STEPPING_RANGE;
}

bool ShotAnalysis::hasIntentionalTempStepping(const QVector<QPointF>& tempGoalData,
                                                double startTime, double endTime)
{
    double goalMin = 999, goalMax = 0;
    bool hasGoal = false;
    for (const auto& gp : tempGoalData) {
        if (gp.x() < startTime) continue;
        if (gp.x() > endTime) break;
        if (gp.y() > 0) {
            goalMin = qMin(goalMin, gp.y());
            goalMax = qMax(goalMax, gp.y());
            hasGoal = true;
        }
    }
    return hasGoal && (goalMax - goalMin) > TEMP_STEPPING_RANGE;
}

double ShotAnalysis::avgTempDeviation(const QVector<QPointF>& tempData,
                                        const QVector<QPointF>& tempGoalData,
                                        double startTime, double endTime)
{
    double devSum = 0;
    int count = 0;
    for (const auto& pt : tempData) {
        if (pt.x() < startTime) continue;
        if (pt.x() > endTime) break;
        double goal = findValueAtTime(tempGoalData, pt.x());
        if (goal > 0) {
            devSum += std::abs(pt.y() - goal);
            ++count;
        }
    }
    return count > 0 ? devSum / count : 0.0;
}

double ShotAnalysis::findValueAtTime(const QVector<QPointF>& data, double time)
{
    if (data.isEmpty()) return 0.0;
    for (const auto& pt : data) {
        if (pt.x() >= time) return pt.y();
    }
    return data.last().y();
}

QVariantList ShotAnalysis::generateSummary(const QVector<QPointF>& pressure,
                                             const QVector<QPointF>& flow,
                                             const QVector<QPointF>& weight,
                                             const QVector<QPointF>& temperature,
                                             const QVector<QPointF>& temperatureGoal,
                                             const QVector<QPointF>& conductanceDerivative,
                                             const QList<HistoryPhaseMarker>& phases,
                                             const QString& beverageType,
                                             double duration,
                                             const QVector<QPointF>& pressureGoal,
                                             const QVector<QPointF>& flowGoal,
                                             const QStringList& analysisFlags)
{
    QVariantList lines;

    if (pressure.size() < 10) {
        QVariantMap line;
        line["text"] = QStringLiteral("Not enough data to analyze.");
        line["type"] = QStringLiteral("observation");
        lines.append(line);
        return lines;
    }

    // --- Find phase boundaries ---
    double preinfEnd = 0, pourStart = 0, pourEnd = duration;
    for (const auto& phase : phases) {
        QString label = phase.label.toLower();
        if (label.contains("infus") || label == "start") preinfEnd = phase.time;
        if (label.contains("pour")) pourStart = phase.time;
        if (label == "end") pourEnd = phase.time;
    }
    if (pourStart == 0 && preinfEnd > 0) pourStart = preinfEnd;

    // --- dC/dt analysis (channeling) ---
    bool skipChanneling = shouldSkipChannelingCheck(beverageType, flow, pourStart, pourEnd)
        || analysisFlags.contains(QStringLiteral("channeling_expected"));

    if (!skipChanneling && !conductanceDerivative.isEmpty()) {
        double spikeTime = 0.0;
        ChannelingSeverity severity = detectChannelingFromDerivative(
            conductanceDerivative, pourStart, pourEnd, &spikeTime);

        QVariantMap line;
        if (severity == ChannelingSeverity::Sustained) {
            line["text"] = QStringLiteral("Sustained channeling detected in dC/dt \u2014 puck prep issue");
            line["type"] = QStringLiteral("warning");
        } else if (severity == ChannelingSeverity::Transient) {
            line["text"] = QStringLiteral("Transient channel at %1s (self-healed)").arg(spikeTime, 0, 'f', 0);
            line["type"] = QStringLiteral("caution");
        } else {
            line["text"] = QStringLiteral("Puck stable \u2014 no channeling spikes in dC/dt");
            line["type"] = QStringLiteral("good");
        }
        lines.append(line);
    }

    // --- Flow trend during extraction ---
    // Skipped for profiles where declining/rising flow is intentional (e.g. Cremina lever).
    const bool flowTrendOk = analysisFlags.contains(QStringLiteral("flow_trend_ok"));
    if (!flowTrendOk && pourStart > 0 && pourEnd > pourStart && flow.size() > 10) {
        double flowStartSum = 0, flowEndSum = 0;
        int flowStartCount = 0, flowEndCount = 0;
        const double pourSpan = pourEnd - pourStart;
        for (const auto& fp : flow) {
            if (fp.x() < pourStart || fp.x() > pourEnd) continue;
            double progress = (fp.x() - pourStart) / pourSpan;
            if (progress < 0.3) { flowStartSum += fp.y(); ++flowStartCount; }
            if (progress > 0.7) { flowEndSum += fp.y(); ++flowEndCount; }
        }
        if (flowStartCount > 0 && flowEndCount > 0) {
            double delta = (flowEndSum / flowEndCount) - (flowStartSum / flowStartCount);
            QVariantMap line;
            if (delta > 0.5) {
                line["text"] = QStringLiteral("Flow rose %1 mL/s during extraction (puck erosion)").arg(delta, 0, 'f', 1);
                line["type"] = QStringLiteral("caution");
                lines.append(line);
            } else if (delta < -0.5) {
                line["text"] = QStringLiteral("Flow dropped %1 mL/s (fines migration or clogging)").arg(std::abs(delta), 0, 'f', 1);
                line["type"] = QStringLiteral("caution");
                lines.append(line);
            }
        }
    }

    // --- Preinfusion drip ---
    if (preinfEnd > 0 && !weight.isEmpty()) {
        double preinfWeight = 0;
        for (const auto& wp : weight) {
            if (wp.x() <= preinfEnd) preinfWeight = wp.y();
            else break;
        }
        double firstPhaseTime = phases.isEmpty() ? 0 : phases.first().time;
        double preinfDuration = preinfEnd - firstPhaseTime;
        if (preinfWeight > 0.5 && preinfDuration > 1.0) {
            QVariantMap line;
            line["text"] = QStringLiteral("Preinfusion: %1g in %2s")
                .arg(preinfWeight, 0, 'f', 1).arg(preinfDuration, 0, 'f', 1);
            line["type"] = QStringLiteral("observation");
            lines.append(line);
        }
    }

    // --- Temperature stability ---
    if (temperature.size() > 10 && temperatureGoal.size() > 10 && pourStart > 0) {
        if (!hasIntentionalTempStepping(temperatureGoal)) {
            double avgDev = avgTempDeviation(temperature, temperatureGoal, pourStart, pourEnd);
            if (avgDev > TEMP_UNSTABLE_THRESHOLD) {
                QVariantMap line;
                line["text"] = QStringLiteral("Temperature drifted %1\u00B0C from goal on average").arg(avgDev, 0, 'f', 1);
                line["type"] = QStringLiteral("caution");
                lines.append(line);
            }
        }
    }

    // --- Flow vs goal (grind direction) ---
    // Only analyze during the pour phase where flow goal is meaningful (> FLOW_GOAL_MIN_AVG).
    // A consistent gap between actual and goal flow indicates grind is off.
    double flowGrindDelta = 0.0;  // positive = flow above goal (coarse), negative = below (fine)
    bool hasFlowGoalData = false;
    if (pourStart < pourEnd && !flow.isEmpty() && !flowGoal.isEmpty()) {
        double actualSum = 0, goalSum = 0;
        int count = 0;
        for (const auto& fp : flow) {
            if (fp.x() < pourStart || fp.x() > pourEnd) continue;
            double goal = findValueAtTime(flowGoal, fp.x());
            if (goal < FLOW_GOAL_MIN_AVG) continue;  // skip preinfusion/low-target periods
            actualSum += fp.y();
            goalSum += goal;
            ++count;
        }
        if (count >= 5) {
            hasFlowGoalData = true;
            flowGrindDelta = (actualSum / count) - (goalSum / count);
            if (flowGrindDelta < -FLOW_DEVIATION_THRESHOLD) {
                QVariantMap line;
                line["text"] = QStringLiteral("Flow averaged %1 ml/s below target \u2014 grind may be too fine")
                    .arg(std::abs(flowGrindDelta), 0, 'f', 1);
                line["type"] = QStringLiteral("caution");
                lines.append(line);
            } else if (flowGrindDelta > FLOW_DEVIATION_THRESHOLD) {
                QVariantMap line;
                line["text"] = QStringLiteral("Flow averaged %1 ml/s above target \u2014 grind may be too coarse")
                    .arg(flowGrindDelta, 0, 'f', 1);
                line["type"] = QStringLiteral("caution");
                lines.append(line);
            }
        }
    }

    // --- Verdict ---
    bool hasWarning = false, hasCaution = false;
    for (const auto& lineVar : lines) {
        QString type = lineVar.toMap()["type"].toString();
        if (type == "warning") hasWarning = true;
        if (type == "caution") hasCaution = true;
    }

    QVariantMap verdict;
    if (hasWarning) {
        // Channeling detected — direction depends on whether flow was running slow or fast.
        // Too-fine grind can cause puck collapse and channeling just as much as too-coarse.
        if (hasFlowGoalData && flowGrindDelta < -FLOW_DEVIATION_THRESHOLD) {
            verdict["text"] = QStringLiteral("Verdict: Puck collapsed \u2014 grind too fine. Try coarser and improve distribution.");
        } else if (hasFlowGoalData && flowGrindDelta > FLOW_DEVIATION_THRESHOLD) {
            verdict["text"] = QStringLiteral("Verdict: Puck integrity issues \u2014 improve distribution. Grind appears coarse \u2014 try finer.");
        } else {
            verdict["text"] = QStringLiteral("Verdict: Puck integrity issues \u2014 improve distribution, consider grinding finer.");
        }
    } else if (hasCaution) {
        verdict["text"] = QStringLiteral("Verdict: Decent shot with minor issues to watch.");
    } else {
        verdict["text"] = QStringLiteral("Verdict: Clean shot. Puck held well.");
    }
    verdict["type"] = QStringLiteral("verdict");
    lines.append(verdict);

    return lines;
}
