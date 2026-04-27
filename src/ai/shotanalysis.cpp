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

    // The detector counts BOTH signs of dC/dt as channeling. The textbook
    // channel signature is positive (flow surges, pressure dips,
    // conductance jumps up), but the post-channel collapse — flow drops
    // toward zero while pressure stays roughly stable — produces sustained
    // negative dC/dt of equal diagnostic value. Distinguishing that real
    // signal from the lever pressure-rise pattern (also negative dC/dt, but
    // caused by intentional pressure ramping rather than puck failure) is
    // the window builder's job: see buildChannelingWindows, which masks out
    // samples where pressure is rapidly climbing.
    for (const auto& pt : conductanceDerivative) {
        if (pt.x() < analysisStart) continue;
        if (pt.x() > analysisEnd) break;
        if (!inAnyWindow(pt.x())) continue;
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

        // Flow-mode phases: exclude samples where pressure is RISING fast.
        // The flow goal can be steady (e.g. 7.5 ml/s preinfusion) while the
        // puck builds pressure under a pressure-ceiling exit condition. The
        // resulting rapid pressure rise drives conductance sharply down —
        // sustained negative dC/dt that's the lever-rise dynamic, not
        // channeling.
        //
        // Direction matters: real puck failures collapse flow under
        // approximately stable pressure, so pressure stays in-window.
        // Bloom transitions and pressure-mode → flow-mode handoffs see
        // pressure FALL rapidly — those are legitimate channeling signals
        // (or expected transients) and must not be masked, so we only
        // fence on rises here.
        if (isFlowMode) {
            const double pressureNow = lookupOrNaN(pressure, t);
            const double pressureFut = lookupOrNaN(pressure, t + WINDOW_HALF_SEC);
            if (!std::isnan(pressureNow) && !std::isnan(pressureFut)
                && pressureNow > 0.5
                && pressureFut > pressureNow * (1.0 + WINDOW_STATIONARY_REL)) {
                flushCurrent();
                continue;
            }
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
    const QStringList& analysisFlags,
    const QVector<QPointF>& pressure,
    double targetWeightG,
    double finalWeightG)
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

    if (pourStart >= pourEnd || flow.isEmpty())
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
        // Pump-ramp trim applies to the first flow-mode phase that
        // coincides with pourStart — that's the one where the pump goes
        // from idle to commanded flow. A flow-mode phase that starts
        // before pourStart (e.g. a fill phase) has its samples filtered
        // out by the pour-window gate below anyway; consuming the
        // "first seen" flag on it would silently skip the trim where it
        // actually belongs. The 0.1 s margin absorbs BLE timestamp jitter.
        //
        // Both trims are gated on the resulting range staying at least
        // kMinPostTrimRangeSec long. On extreme puck-failure shots —
        // where the firmware bails out within a couple of seconds because
        // the puck offers no resistance — the first flow-mode phase can
        // be a fraction of a second. Trimming 0.5 s off the front of that
        // phase would leave nothing to analyze and the detector would
        // silently report no-data instead of catching the obvious gusher.
        constexpr double kMinPostTrimRangeSec = 1.0;
        bool firstFlowModeAtPourStartSeen = false;
        for (qsizetype i = 0; i < phases.size(); ++i) {
            if (!phases[i].isFlowMode) continue;
            double start = phases[i].time;
            double end = (i + 1 < phases.size()) ? phases[i + 1].time : pourEnd;
            if (!firstFlowModeAtPourStartSeen
                && phases[i].time + 0.1 >= pourStart) {
                if ((end - start) - GRIND_PUMP_RAMP_SKIP_SEC >= kMinPostTrimRangeSec) {
                    start += GRIND_PUMP_RAMP_SKIP_SEC;
                }
                firstFlowModeAtPourStartSeen = true;
            }
            if (i + 1 < phases.size()
                && phases[i + 1].transitionReason.compare(
                       QStringLiteral("pressure"), Qt::CaseInsensitive) == 0) {
                if ((end - start) - GRIND_LIMITER_TAIL_SKIP_SEC >= kMinPostTrimRangeSec) {
                    end -= GRIND_LIMITER_TAIL_SKIP_SEC;
                }
            }
            if (end > start) flowModeRanges.append({start, end});
        }
    }
    // Flow-vs-goal averaging path. Skipped when no flow-mode windows
    // qualify or when the profile carries no flow goal.
    if (!flowModeRanges.isEmpty() && !flowGoal.isEmpty()) {
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
        if (count >= 5) {
            result.hasData = true;
            result.delta = (actualSum / count) - (goalSum / count);
        }
    }

    // Choked-puck check, restricted to pressure-mode portions of the pour.
    // Runs in addition to the flow-vs-goal path (not as a fallback) because
    // shots like 80's Espresso have a healthy flow-mode preinfusion that
    // makes delta ≈ 0 — the choke happens entirely in the pressure-mode
    // tail and is invisible to the flow-vs-goal averaging. When phases
    // carry no pressure-mode markers (lever profiles labeled isFlowMode=true
    // throughout), treat the whole pour window as a candidate; the per-
    // sample CHOKED_PRESSURE_MIN_BAR gate still restricts to actually-
    // pressurized portions.
    if (pressure.isEmpty())
        return result;

    QVector<Range> pressureModeRanges;
    for (qsizetype i = 0; i < phases.size(); ++i) {
        if (phases[i].isFlowMode) continue;
        const double start = phases[i].time;
        const double end = (i + 1 < phases.size()) ? phases[i + 1].time : pourEnd;
        if (end > start) pressureModeRanges.append({start, end});
    }
    auto inPressureMode = [&pressureModeRanges](double t) {
        if (pressureModeRanges.isEmpty()) return true;
        for (const auto& r : pressureModeRanges) {
            if (t >= r.start && t <= r.end) return true;
        }
        return false;
    };

    double pressurizedDuration = 0.0;
    double flowSum = 0.0;
    qsizetype flowSamples = 0;
    double prevX = 0.0;
    bool prevValid = false;
    for (const auto& fp : flow) {
        if (fp.x() < pourStart || fp.x() > pourEnd
            || !inPressureMode(fp.x())) {
            prevValid = false;
            continue;
        }
        const double press = findValueAtTime(pressure, fp.x());
        if (press < CHOKED_PRESSURE_MIN_BAR) {
            prevValid = false;
            continue;
        }
        if (prevValid) {
            const double dt = fp.x() - prevX;
            if (dt > 0 && dt < 1.0)  // cap dt to ignore samples after a gap
                pressurizedDuration += dt;
        }
        flowSum += fp.y();
        ++flowSamples;
        prevX = fp.x();
        prevValid = true;
    }

    if (flowSamples >= 5 && pressurizedDuration >= CHOKED_DURATION_MIN_SEC) {
        const double meanFlow = flowSum / flowSamples;
        const bool flowChoked = meanFlow < CHOKED_FLOW_MAX_MLPS;
        // Yield-ratio arm: same diagnosis (grind too fine), milder severity.
        // Catches shots like 883 — 70 % of target with mean pressurized flow
        // ~0.6 ml/s, just over the flow threshold but the puck still failed
        // to deliver. finalWeightG works on either real or virtual scale
        // (FlowScale integrates flow with dose-aware puck-absorption
        // compensation), so this fires headless too.
        const bool yieldShortfall = targetWeightG > 0.0
            && finalWeightG > 0.0
            && (finalWeightG / targetWeightG) < CHOKED_YIELD_RATIO_MAX;
        if (flowChoked || yieldShortfall) {
            result.hasData = true;
            result.chokedPuck = true;
            result.sampleCount = flowSamples;
            // Leave delta carrying its flow-vs-goal meaning; consumers
            // short-circuit on chokedPuck before reading delta.
        }
    }

    return result;
}

bool ShotAnalysis::detectGrindIssue(const QVector<QPointF>& flow,
                                     const QVector<QPointF>& flowGoal,
                                     const QList<HistoryPhaseMarker>& phases,
                                     double pourStart, double pourEnd,
                                     const QString& beverageType,
                                     const QStringList& analysisFlags,
                                     const QVector<QPointF>& pressure,
                                     double targetWeightG,
                                     double finalWeightG)
{
    const GrindCheck r = analyzeFlowVsGoal(flow, flowGoal, phases, pourStart, pourEnd,
                                            beverageType, analysisFlags, pressure,
                                            targetWeightG, finalWeightG);
    if (r.skipped || !r.hasData) return false;
    return r.chokedPuck || std::abs(r.delta) > FLOW_DEVIATION_THRESHOLD;
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
                                        int expectedFrameCount,
                                        double firstFrameConfiguredSeconds)
{
    if (phases.isEmpty())
        return false;

    if (expectedFrameCount >= 0 && expectedFrameCount < 2)
        return false;

    // Decenza inserts a synthetic "Start" marker at extraction start before any
    // real frame-change markers. Ignore it so we can track "did we ever see
    // frame 0 before a non-zero frame?" against saved history.
    qsizetype firstRealMarker = 0;
    if (phases.first().label == QStringLiteral("Start") && phases.first().frameNumber == 0)
        firstRealMarker = 1;

    bool sawFrameZero = false;
    for (qsizetype i = firstRealMarker; i < phases.size(); ++i) {
        const HistoryPhaseMarker& phase = phases[i];
        if (phase.frameNumber < 0)
            continue;
        if (expectedFrameCount >= 0 && phase.frameNumber >= expectedFrameCount)
            continue;

        if (phase.frameNumber == 0) {
            sawFrameZero = true;
            continue;
        }

        // FW-bug branch: frame 0 was never observed before a non-zero frame.
        // Mirror the de1app Tcl plugin's hard 2 s window — that plugin polls
        // during extraction and can only catch it before t=2 s, so for parity
        // we only flag here within that same window.
        if (!sawFrameZero)
            return phase.time < 2.0;

        // Short-first-step branch: frame 0 was observed; judge whether it
        // ran long enough. The de1app Tcl plugin uses a hard 2 s wall, which
        // false-positives on profiles configured with frame[0].seconds == 2
        // because BLE notification jitter routinely lands the frame-1 marker
        // a few hundred ms early. When the configured first-frame duration
        // is known, compare against half of configured (capped at 2 s) so a
        // 1.87 s actual on a 2 s configured frame — 94 % of plan — does not
        // flag.
        double cutoff = 2.0;
        if (firstFrameConfiguredSeconds > 0.0)
            cutoff = std::min(2.0, 0.5 * firstFrameConfiguredSeconds);

        return phase.time < cutoff;
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
                                             const QStringList& analysisFlags,
                                             double firstFrameConfiguredSeconds,
                                             double targetWeightG,
                                             double finalWeightG)
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
    // a pressure-controlled pour). The choked-puck check inside
    // analyzeFlowVsGoal() also runs additively on pressure-mode portions of
    // the pour, so a shot with a healthy flow-mode preinfusion AND a choked
    // pressure-mode tail can have both delta near zero and chokedPuck true.
    const GrindCheck grind = analyzeFlowVsGoal(flow, flowGoal, phases,
                                                pourStart, pourEnd,
                                                beverageType, analysisFlags,
                                                pressure,
                                                targetWeightG, finalWeightG);
    if (grind.hasData) {
        if (grind.chokedPuck) {
            QVariantMap line;
            line["text"] = QStringLiteral("Pour produced near-zero flow while pressure held \u2014 "
                "puck choked, grind way too fine");
            line["type"] = QStringLiteral("warning");
            lines.append(line);
        } else if (grind.delta < -FLOW_DEVIATION_THRESHOLD) {
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
    // need a separate skipFirstFrameDetected parameter. Catches FW bug (machine
    // never executed frame 0) or profile first step running far shorter than
    // configured. firstFrameConfiguredSeconds (when known) avoids false-positives
    // on profiles with frame[0].seconds == 2.
    const bool skipFirstFrame = detectSkipFirstFrame(
        phases, /*expectedFrameCount=*/-1, firstFrameConfiguredSeconds);
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
    } else if (grind.chokedPuck) {
        // Pressure built but the puck refused to extract \u2014 the diagnosis is
        // unambiguously "grind way too fine," not a distribution problem.
        // Pre-empts the generic "Puck integrity issue" verdict that the
        // hasWarning branch below would otherwise emit. Sits below
        // skipFirstFrame because a frame-skip bug can synthesise extraction
        // dynamics that look like a choke (frame 1 takes over a profile
        // step that holds high pressure with low flow); fixing the machine
        // is the prerequisite, after which the user can re-evaluate grind.
        verdict["text"] = QStringLiteral("Verdict: Puck choked \u2014 grind way too fine. Coarsen significantly.");
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
