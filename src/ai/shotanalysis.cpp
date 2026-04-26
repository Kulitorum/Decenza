#include "shotanalysis.h"
#include "history/shothistorystorage.h"  // HistoryPhaseMarker

#include <QVariantMap>
#include <algorithm>
#include <cmath>

namespace {

// Locate the HistoryPhaseMarker span that contains time `t`. Returns the
// phase's isFlowMode flag via out param. Falls back to the last phase for
// times past all markers. Returns false when phases is empty (no mode known).
bool phaseAtTime(const QList<HistoryPhaseMarker>& phases, double t, bool* outIsFlowMode)
{
    if (phases.isEmpty()) return false;
    const HistoryPhaseMarker* active = &phases.first();
    for (const auto& phase : phases) {
        if (phase.time <= t) active = &phase;
        else break;
    }
    if (outIsFlowMode) *outIsFlowMode = active->isFlowMode;
    return true;
}

// Interpolate value from a (time, value) series at time t. Returns std::nan
// when the series is empty or t falls outside [first.x, last.x] — callers
// treat that as "no goal data for this moment."
double lookupOrNaN(const QVector<QPointF>& data, double t)
{
    if (data.isEmpty()) return std::nan("");
    if (t < data.first().x() || t > data.last().x()) return std::nan("");
    // Linear interpolation between the nearest bracketing samples.
    for (qsizetype i = 1; i < data.size(); ++i) {
        if (data[i].x() >= t) {
            const double x0 = data[i - 1].x();
            const double x1 = data[i].x();
            const double y0 = data[i - 1].y();
            const double y1 = data[i].y();
            if (x1 <= x0) return y1;
            const double alpha = (t - x0) / (x1 - x0);
            return y0 + alpha * (y1 - y0);
        }
    }
    return data.last().y();
}

} // namespace

ShotAnalysis::ChannelingSeverity ShotAnalysis::detectChannelingFromDerivative(
    const QVector<QPointF>& conductanceDerivative,
    double pourStart, double pourEnd,
    const QVector<DetectionWindow>& windows,
    double* outMaxSpikeTime)
{
    if (outMaxSpikeTime) *outMaxSpikeTime = 0.0;
    if (conductanceDerivative.isEmpty()) return ChannelingSeverity::None;

    // Empty windows mean either (a) buildChannelingWindows had phase data
    // but found no stationary+converged time range — the shot was all-ramp,
    // no reliable analysis possible — or (b) the caller explicitly wants
    // silence. Either way, return None rather than silently falling back
    // to an unrestricted detector that would re-flag every lever/ramp shot.
    // Callers that want unrestricted detection should pass a single
    // whole-pour window explicitly (see buildChannelingWindows, which emits
    // that when phase data is absent).
    if (windows.isEmpty()) return ChannelingSeverity::None;

    const double analysisStart = pourStart + CHANNELING_DC_POUR_SKIP_SEC;
    // Trim the final CHANNELING_DC_POUR_SKIP_END_SEC of pour. Flat-pressure
    // profiles (E61, Classic Italian) naturally see dC/dt climb at the end
    // because flow keeps rising from puck erosion — that's a dialing
    // observation, not a puck-integrity failure. Mirror the existing
    // start-skip so both transition fringes are excluded.
    const double analysisEnd = pourEnd - CHANNELING_DC_POUR_SKIP_END_SEC;
    double maxSpike = 0.0;
    double maxSpikeTime = 0.0;
    int sustainedCount = 0;

    auto inAnyWindow = [&windows](double t) {
        for (const auto& w : windows) {
            if (t >= w.start && t <= w.end) return true;
        }
        return false;
    };

    // Channeling is a positive-dC/dt signature: a channel forms, flow surges
    // and pressure dips, conductance (flow/pressure) jumps up. Negative dC/dt
    // is the opposite — flow falling relative to pressure — which is the
    // normal dynamic on lever pressure-rise frames (pressure climbs faster
    // than flow follows) and on pressure-decline frames as the puck thins.
    // Treating |dC/dt| as channeling false-flags every lever pour, so we
    // count only signed positive excursions here.
    for (const auto& pt : conductanceDerivative) {
        if (pt.x() < analysisStart) continue;
        if (pt.x() > analysisEnd) break;
        if (!inAnyWindow(pt.x())) continue;
        const double v = pt.y();
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

QVector<ShotAnalysis::DetectionWindow> ShotAnalysis::buildChannelingWindows(
    const QVector<QPointF>& pressure,
    const QVector<QPointF>& flow,
    const QVector<QPointF>& pressureGoal,
    const QVector<QPointF>& flowGoal,
    const QList<HistoryPhaseMarker>& phases,
    double pourStart, double pourEnd)
{
    QVector<DetectionWindow> windows;
    if (pourEnd <= pourStart) return windows;

    // No phase data: return a single whole-pour window so
    // detectChannelingFromDerivative runs unrestricted. This preserves
    // detector coverage on legacy shots that predate phase-marker storage.
    // An empty return is reserved for "phases exist but no stationary
    // window qualifies" — the detector treats that as a silent deliberate
    // pass instead of falling back to unrestricted false positives.
    if (phases.isEmpty()) {
        windows.append({pourStart, pourEnd});
        return windows;
    }

    // Walk pressure samples as the "time grid" for evaluation. Pressure is
    // always populated and runs the full shot. Flow samples may be missing
    // during early fill on some scales; pressure gives a stable time axis.
    const QVector<QPointF>& grid = !pressure.isEmpty() ? pressure : flow;
    if (grid.isEmpty()) return windows;

    DetectionWindow current{-1.0, -1.0};
    auto flushCurrent = [&]() {
        if (current.start >= 0 && current.end > current.start) {
            // Merge into previous window if the gap is small (≤ WINDOW_GAP_MERGE_SEC).
            if (!windows.isEmpty()
                && current.start - windows.last().end <= WINDOW_GAP_MERGE_SEC) {
                windows.last().end = current.end;
            } else {
                windows.append(current);
            }
        }
        current = {-1.0, -1.0};
    };

    // Trim both ends of the pour so the window bounds match what
    // detectChannelingFromDerivative will actually analyze — otherwise
    // windows silently overstate their coverage by up to
    // CHANNELING_DC_POUR_SKIP_END_SEC at the tail.
    const double analysisStart = pourStart + CHANNELING_DC_POUR_SKIP_SEC;
    const double analysisEnd = pourEnd - CHANNELING_DC_POUR_SKIP_END_SEC;

    for (const auto& pt : grid) {
        const double t = pt.x();
        if (t < analysisStart) continue;
        if (t > analysisEnd) break;

        bool isFlowMode = false;
        if (!phaseAtTime(phases, t, &isFlowMode)) {
            flushCurrent();
            continue;
        }

        const QVector<QPointF>& goalSeries = isFlowMode ? flowGoal : pressureGoal;
        const QVector<QPointF>& actualSeries = isFlowMode ? flow : pressure;

        const double goalNow = lookupOrNaN(goalSeries, t);
        const double goalPast = lookupOrNaN(goalSeries, t - WINDOW_HALF_SEC);
        const double goalFut = lookupOrNaN(goalSeries, t + WINDOW_HALF_SEC);
        const double actual = lookupOrNaN(actualSeries, t);

        // No goal data at this moment (outside series bounds or sentinel) → skip.
        if (std::isnan(goalNow) || std::isnan(goalPast) || std::isnan(goalFut)
            || std::isnan(actual)) {
            flushCurrent();
            continue;
        }
        if (goalNow < WINDOW_MIN_GOAL) {
            // Goal is zero/sentinel here — no active control → skip.
            flushCurrent();
            continue;
        }

        // Stationarity: both past and future goal values within WINDOW_STATIONARY_REL of goalNow.
        const double relPast = std::abs(goalPast - goalNow) / goalNow;
        const double relFut = std::abs(goalFut - goalNow) / goalNow;
        if (relPast > WINDOW_STATIONARY_REL || relFut > WINDOW_STATIONARY_REL) {
            flushCurrent();
            continue;
        }

        // Convergence: actual within WINDOW_CONVERGED_REL of goalNow.
        const double convergenceErr = std::abs(actual - goalNow) / goalNow;
        if (convergenceErr > WINDOW_CONVERGED_REL) {
            flushCurrent();
            continue;
        }

        // Sample qualifies. Extend or start the current window.
        if (current.start < 0) {
            current.start = t;
        }
        current.end = t;
    }
    flushCurrent();

    return windows;
}

bool ShotAnalysis::shouldSkipChannelingCheck(const QString& beverageType,
                                               const QVector<QPointF>& flowData,
                                               double pourStart, double pourEnd)
{
    QString bev = beverageType.toLower();
    // Non-espresso modes: filter/pourover brew through a paper filter, tea
    // steeps, steam is a manual health check, cleaning/calibration flow hot
    // water through an empty portafilter. None of these have a puck whose
    // integrity dC/dt can meaningfully score.
    if (bev == QStringLiteral("filter")
        || bev == QStringLiteral("pourover")
        || bev == QStringLiteral("tea")
        || bev == QStringLiteral("steam")
        || bev == QStringLiteral("cleaning"))
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

ShotAnalysis::GrindCheck ShotAnalysis::analyzeFlowVsGoal(
    const QVector<QPointF>& flow,
    const QVector<QPointF>& flowGoal,
    const QList<HistoryPhaseMarker>& phases,
    double pourStart, double pourEnd,
    const QString& beverageType,
    const QStringList& analysisFlags)
{
    GrindCheck result;

    const QString bev = beverageType.toLower();
    const bool bevSkip = (bev == QStringLiteral("filter")
                          || bev == QStringLiteral("pourover")
                          || bev == QStringLiteral("tea")
                          || bev == QStringLiteral("steam")
                          || bev == QStringLiteral("cleaning"));
    if (analysisFlags.contains(QStringLiteral("grind_check_skip")) || bevSkip) {
        result.skipped = true;
        return result;
    }

    if (pourStart >= pourEnd || flow.isEmpty() || flowGoal.isEmpty())
        return result;

    // Build inclusive flow-mode time ranges from phase markers. A sample at
    // time t qualifies only when it falls inside a flow-mode phase; this
    // gates out pressure-controlled phases whose "flow goal" is really a
    // safety limiter (80's Espresso rise+decline, Cremina lever, Londinium
    // pour, etc.) — comparing actual flow against that ceiling is the
    // canonical false-positive source.
    //
    // Two boundary trims (see GRIND_*_SKIP_SEC in shotanalysis.h) apply
    // within each flow-mode range so we don't average pump-ramp lag or the
    // post-limiter-engaged tail. Without them the lever preinfusion shape
    // (pump-ramp at start + pressure ceiling activates at end) reads as a
    // sustained "too fine" delta even on clean shots.
    struct Range { double start; double end; };
    QVector<Range> flowModeRanges;
    if (!phases.isEmpty()) {
        bool firstFlowModeSeen = false;
        for (qsizetype i = 0; i < phases.size(); ++i) {
            if (!phases[i].isFlowMode) continue;
            double start = phases[i].time;
            double end = (i + 1 < phases.size()) ? phases[i + 1].time : pourEnd;
            if (!firstFlowModeSeen) {
                start += GRIND_PUMP_RAMP_SKIP_SEC;
                firstFlowModeSeen = true;
            }
            if (i + 1 < phases.size()
                && phases[i + 1].transitionReason.compare(
                       QStringLiteral("pressure"), Qt::CaseInsensitive) == 0) {
                end -= GRIND_LIMITER_TAIL_SKIP_SEC;
            }
            if (end > start) flowModeRanges.append({start, end});
        }
    }
    if (flowModeRanges.isEmpty()) {
        // No flow-mode phase present in the pour — check cannot run.
        return result;
    }

    auto inFlowMode = [&flowModeRanges](double t) {
        for (const auto& r : flowModeRanges) {
            if (t >= r.start && t <= r.end) return true;
        }
        return false;
    };

    double actualSum = 0, goalSum = 0;
    qsizetype count = 0;
    for (const auto& fp : flow) {
        if (fp.x() < pourStart || fp.x() > pourEnd) continue;
        if (!inFlowMode(fp.x())) continue;
        double goal = findValueAtTime(flowGoal, fp.x());
        if (goal < FLOW_GOAL_MIN_AVG) continue;  // preinfusion sentinel / unset goal
        actualSum += fp.y();
        goalSum += goal;
        ++count;
    }

    result.sampleCount = count;
    if (count < 5) return result;

    result.hasData = true;
    result.delta = (actualSum / count) - (goalSum / count);
    return result;
}

bool ShotAnalysis::detectGrindIssue(const QVector<QPointF>& flow,
                                     const QVector<QPointF>& flowGoal,
                                     const QList<HistoryPhaseMarker>& phases,
                                     double pourStart, double pourEnd,
                                     const QString& beverageType,
                                     const QStringList& analysisFlags)
{
    const GrindCheck r = analyzeFlowVsGoal(flow, flowGoal, phases, pourStart, pourEnd,
                                            beverageType, analysisFlags);
    if (r.skipped || !r.hasData) return false;
    return std::abs(r.delta) > FLOW_DEVIATION_THRESHOLD;
}

bool ShotAnalysis::detectPourTruncated(const QVector<QPointF>& pressure,
                                        double pourStart, double pourEnd,
                                        const QString& beverageType)
{
    // Non-espresso modes legitimately run below PRESSURE_FLOOR_BAR (tea
    // steeps cold, pourover runs at a few bar max, cleaning just flushes).
    // Skip entirely — same rule the channeling/grind detectors use.
    const QString bev = beverageType.toLower();
    if (bev == QStringLiteral("filter")
        || bev == QStringLiteral("pourover")
        || bev == QStringLiteral("tea")
        || bev == QStringLiteral("steam")
        || bev == QStringLiteral("cleaning"))
        return false;

    if (pressure.size() < 10 || pourEnd <= pourStart) return false;

    // Scan the pour window for peak pressure. We look only inside the pour
    // (not the entire sample range) because some profiles briefly spike
    // during fill before the puck is engaged — that's not diagnostic of
    // whether extraction actually built pressure.
    double peakBar = 0.0;
    for (const auto& pt : pressure) {
        if (pt.x() < pourStart) continue;
        if (pt.x() > pourEnd) break;
        if (pt.y() > peakBar) peakBar = pt.y();
    }
    return peakBar < PRESSURE_FLOOR_BAR;
}

bool ShotAnalysis::detectSkipFirstFrame(const QList<HistoryPhaseMarker>& phases,
                                        int expectedFrameCount)
{
    if (phases.isEmpty())
        return false;

    if (expectedFrameCount >= 0 && expectedFrameCount < 2)
        return false;

    // Decenza inserts a synthetic "Start" marker at extraction start before any
    // real frame-change markers. Ignore it so we can mirror the de1app plugin's
    // "did we ever see frame 0 before a non-zero frame in the first 2 seconds?"
    // behavior against saved history.
    qsizetype firstRealMarker = 0;
    if (phases.first().label == QStringLiteral("Start") && phases.first().frameNumber == 0)
        firstRealMarker = 1;

    for (qsizetype i = firstRealMarker; i < phases.size(); ++i) {
        const HistoryPhaseMarker& phase = phases[i];
        if (phase.frameNumber < 0)
            continue;
        if (expectedFrameCount >= 0 && phase.frameNumber >= expectedFrameCount)
            continue;

        if (phase.frameNumber == 0)
            continue;

        // Match the Tcl plugin's detection window: only classify transitions that
        // happen in the first 2 seconds of extraction.
        if (phase.time >= 2.0)
            return false;

        // Non-zero frame inside the 2-second window — flag the shot. Both the
        // firmware bug (frame 0 never seen) and the short-first-step case
        // (frame 0 briefly seen, then jumped away) share this badge intentionally.
        return true;
    }

    return false;
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
        // Build mode-aware inclusion windows. Pressure-mode phases with a
        // ramping pressureGoal (lever decline, post-preinfusion pour ramps)
        // are excluded; flow-mode phases with a stationary flowGoal qualify.
        // buildChannelingWindows emits a whole-pour fallback when phases is
        // empty so legacy shots still get analyzed; when phases are present
        // but nothing qualifies (all-ramp shot) it returns empty and the
        // detector stays silent rather than re-flagging on noise.
        const QVector<DetectionWindow> windows = buildChannelingWindows(
            pressure, flow, pressureGoal, flowGoal, phases, pourStart, pourEnd);

        double spikeTime = 0.0;
        ChannelingSeverity severity = detectChannelingFromDerivative(
            conductanceDerivative, pourStart, pourEnd, windows, &spikeTime);

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
    // Phase-mode aware: averages only across flow-controlled phases where
    // flow goal is an actual target (not a safety limiter riding on top of
    // a pressure-controlled pour). See analyzeFlowVsGoal() for details.
    const GrindCheck grind = analyzeFlowVsGoal(flow, flowGoal, phases,
                                                pourStart, pourEnd,
                                                beverageType, analysisFlags);
    if (grind.hasData) {
        if (grind.delta < -FLOW_DEVIATION_THRESHOLD) {
            QVariantMap line;
            line["text"] = QStringLiteral("Flow averaged %1 ml/s below target \u2014 grind may be too fine")
                .arg(std::abs(grind.delta), 0, 'f', 1);
            line["type"] = QStringLiteral("caution");
            lines.append(line);
        } else if (grind.delta > FLOW_DEVIATION_THRESHOLD) {
            QVariantMap line;
            line["text"] = QStringLiteral("Flow averaged %1 ml/s above target \u2014 grind may be too coarse")
                .arg(grind.delta, 0, 'f', 1);
            line["type"] = QStringLiteral("caution");
            lines.append(line);
        }
    }

    // --- Pour truncated (puck failure) ---
    // Catches shots that the channeling + grind detectors miss: pressure
    // never builds, conductance saturates, flow tracks preinfusion goal, so
    // every normal detector stays silent. Looking straight at peak pressure
    // exposes the failure. Emits its own warning line (not a verdict — the
    // existing verdict machinery covers that below).
    const bool pourTruncated = detectPourTruncated(pressure, pourStart, pourEnd, beverageType);
    if (pourTruncated) {
        QVariantMap line;
        line["text"] = QStringLiteral("Pour never pressurized \u2014 puck failed "
            "(massive channel, missing puck, or grind radically too coarse)");
        line["type"] = QStringLiteral("warning");
        lines.append(line);
    }

    // --- Skip-first-frame ---
    // Re-derive from phase markers (already available) so generateSummary() does not
    // need a separate skipFirstFrameDetected parameter. Mirrors the Tcl plugin: FW bug
    // (machine never executed frame 0) or profile first step too short (< 2 s).
    const bool skipFirstFrame = detectSkipFirstFrame(phases);
    if (skipFirstFrame) {
        QVariantMap line;
        line["text"] = QStringLiteral("First profile step skipped \u2014 "
            "likely a DE1 firmware bug (power-cycle machine to fix) "
            "or first step too short (check profile settings)");
        line["type"] = QStringLiteral("warning");
        lines.append(line);
    }

    // --- Verdict ---
    bool hasWarning = false, hasCaution = false;
    for (const auto& lineVar : lines) {
        QString type = lineVar.toMap()["type"].toString();
        if (type == "warning") hasWarning = true;
        if (type == "caution") hasCaution = true;
    }

    QVariantMap verdict;
    if (pourTruncated) {
        // Dominates over every other signal — if the puck failed to build
        // pressure, channeling / grind direction / temperature advice are
        // all irrelevant. User needs to know the puck failed.
        verdict["text"] = QStringLiteral("Verdict: Puck failed \u2014 pour never "
            "pressurized. Check dose, distribution, and whether grind is drastically too coarse.");
    } else if (skipFirstFrame) {
        // Skip-first-frame is a machine/profile issue, not puck integrity — give specific advice
        // so the user isn't told to adjust grind when the real fix is a power-cycle.
        verdict["text"] = QStringLiteral("Verdict: First profile step was skipped \u2014 "
            "power-cycle the machine to fix a firmware bug, "
            "or review the profile's first step settings.");
    } else if (hasWarning) {
        // Channeling is a puck-prep finding. The flow-vs-goal grind direction
        // signal is independent — it only indicates direction when it fired
        // on its own. Do not collapse the two into "Puck collapsed — grind
        // too fine": sustained dC/dt elevation doesn't tell us which
        // direction grind is off, and a real collapse would show as a
        // gusher, not the slow shot that the old heuristic often matched.
        const bool grindFine = grind.hasData && grind.delta < -FLOW_DEVIATION_THRESHOLD;
        const bool grindCoarse = grind.hasData && grind.delta > FLOW_DEVIATION_THRESHOLD;
        if (grindFine) {
            verdict["text"] = QStringLiteral("Verdict: Puck integrity issue \u2014 improve distribution. Grind is running fine \u2014 try coarser.");
        } else if (grindCoarse) {
            verdict["text"] = QStringLiteral("Verdict: Puck integrity issue \u2014 improve distribution. Grind is running coarse \u2014 try finer.");
        } else {
            verdict["text"] = QStringLiteral("Verdict: Puck integrity issue \u2014 improve distribution.");
        }
    } else if (hasCaution) {
        // No puck-integrity warning: if the only caution is a grind direction,
        // name the direction explicitly rather than giving a generic "minor
        // issues" verdict.
        const bool grindFine = grind.hasData && grind.delta < -FLOW_DEVIATION_THRESHOLD;
        const bool grindCoarse = grind.hasData && grind.delta > FLOW_DEVIATION_THRESHOLD;
        if (grindFine) {
            verdict["text"] = QStringLiteral("Verdict: Grind appears too fine \u2014 try coarser.");
        } else if (grindCoarse) {
            verdict["text"] = QStringLiteral("Verdict: Grind appears too coarse \u2014 try finer.");
        } else {
            verdict["text"] = QStringLiteral("Verdict: Decent shot with minor issues to watch.");
        }
    } else {
        verdict["text"] = QStringLiteral("Verdict: Clean shot. Puck held well.");
    }
    verdict["type"] = QStringLiteral("verdict");
    lines.append(verdict);

    return lines;
}
