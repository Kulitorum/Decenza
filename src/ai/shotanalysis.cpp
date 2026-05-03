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

        // For pressure-mode phases, exclude low-pressure preinfusion soaks (e.g.
        // Londinium / Adaptive 3-bar soak). Puck-wetting dC/dt fluctuations at
        // soak pressure produce the same elevated signal as channeling, but are
        // not diagnostic — the puck is still absorbing water, not being extracted.
        // Flow-mode phases bypass this gate (their goalSeries is flow, not pressure).
        if (!isFlowMode && goalNow < WINDOW_MIN_EXTRACTION_BAR) {
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

        // Exclude samples where pressure is RISING fast, regardless of phase
        // mode. Two intentional ramp dynamics produce the same conductance-
        // drop signature that the dC/dt detector would otherwise read as
        // sustained channeling:
        //
        //   * Flow-mode lever rise: flow goal is steady (e.g. 7.5 ml/s
        //     preinfusion) while the puck builds pressure under a
        //     pressure-ceiling exit condition. Cremina, Damian LRv3,
        //     80's Espresso preinfusion all hit this.
        //
        //   * Pressure-mode rise-and-hold: pressure goal is locked at the
        //     target (e.g. 7.8 bar) but actual pressure is still ramping
        //     toward it. The convergence check (|actual - goal| / goal ≤
        //     0.15) admits samples while actual is within 15 % of goal,
        //     even when actual is still rising fast — and during that
        //     final leg, dC/dt clamps at the negative floor as conductance
        //     drops with pressure. Shot 889 (a clean 35.6 g extraction on
        //     80's Espresso) tripped this and falsely fired channeling.
        //
        // Direction matters: real puck failures collapse flow under
        // approximately stable pressure, so pressure stays in-window. Bloom
        // transitions and pressure-mode → flow-mode handoffs see pressure
        // FALL rapidly — those are legitimate channeling signals (or
        // expected transients) and must not be masked, so we only fence on
        // rises here. The dC/dt detector counts both signs of conductance
        // change as channeling (flow-surge gushers and post-channel flow
        // collapse), and neither failure mode coincides with pressure
        // climbing past the WINDOW_STATIONARY_REL threshold under a
        // converged-and-held goal — so excluding rising-pressure samples
        // doesn't mask either signature in pressure mode.
        {
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

bool ShotAnalysis::reachedExtractionPhase(const QList<HistoryPhaseMarker>& phases,
                                           double shotDuration)
{
    if (shotDuration <= 0.0) return false;
    for (const auto& pm : phases) {
        if (pm.frameNumber < 1) continue;
        // Frame ≥ 1 marker found. The shot must have continued past it for
        // long enough to actually run extraction. Aborted shots sometimes
        // record the 0→1 transition in firmware in the final ms before the
        // shot ends (frame marker present, but no samples land inside it),
        // so a presence-only check would let those slip through.
        if (shotDuration - pm.time >= TEMP_MIN_EXTRACTION_SEC)
            return true;
    }
    return false;
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

    // Yield-overshoot ("gusher") arm. Fires independently of any pressure/flow
    // window because a gusher by definition can't sustain pressure long enough
    // to gate the pressure-mode choke arms. Catches shots like 18 g → 40 g
    // target, 56 g actual at grind 11 — the puck offered too little resistance,
    // water blew through, the shot finished much heavier than intended. Same
    // precondition as the choked-puck yield arm (target > 0 && final > 0); both
    // arms are mutually exclusive on yield ratio (one < 0.85, the other > 1.20).
    if (targetWeightG > 0.0 && finalWeightG > 0.0
        && (finalWeightG / targetWeightG) > YIELD_OVERSHOOT_RATIO_MIN) {
        result.hasData = true;
        result.yieldOvershoot = true;
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

    // Record the gate inputs unconditionally so consumers can answer
    // "why didn't this badge fire?" even on gate-fail paths. `gatePassed`
    // tracks the flow-arm gate (the original 15s ≥ 4 bar gate) — that's
    // the specific gate shot 745 was silenced by before #966 split the arms.
    // The yield arm has a looser gate (flowSamples ≥ 5), diagnosable from
    // the raw `flowSamples` field.
    result.gateRan = true;
    result.gateFlowSamples = flowSamples;
    result.gatePressurizedDurationSec = pressurizedDuration;
    result.gateMeanPressurizedFlowMlPerSec = flowSamples > 0 ? flowSum / flowSamples : 0.0;
    result.gateYieldRatio = (targetWeightG > 0.0 && finalWeightG > 0.0)
        ? finalWeightG / targetWeightG : 0.0;
    result.gatePassed = flowSamples >= 5 && pressurizedDuration >= CHOKED_DURATION_MIN_SEC;

    // Two arms with split gates:
    //  - Flow arm needs sustained pressurized flow (≥ 15s ≥ 4 bar) to
    //    compute a meaningful mean-flow average.
    //  - Yield arm only needs ≥ 5 pressurized samples (puck saw
    //    meaningful pressure briefly); its diagnosis is yield-based
    //    and does not read mean pressurized flow.
    // The shared gate previously hid shot 745-class misses (Adaptive v2,
    // 35s pour, 64% yield, ~8.8 s pressurized) — silenced by the 15s gate
    // even though the yield arm could have spoken. See openspec change
    // tighten-grind-yield-shortfall-arm and #963.
    if (flowSamples >= 5) {
        const bool flowArmGatesPassed = result.gatePassed;

        bool flowChoked = false;
        if (flowArmGatesPassed) {
            flowChoked = result.gateMeanPressurizedFlowMlPerSec < CHOKED_FLOW_MAX_MLPS;
        }

        // Yield-ratio arm: same diagnosis (grind too fine), milder severity.
        // Catches shots like 745 (Adaptive v2, 64% of target with brief
        // pressurized window). finalWeightG works on either real or virtual
        // scale (FlowScale integrates flow with dose-aware puck-absorption
        // compensation), so this fires headless too.
        const bool yieldShortfall = targetWeightG > 0.0
            && finalWeightG > 0.0
            && (finalWeightG / targetWeightG) < CHOKED_YIELD_RATIO_MAX;

        // hasData when any arm could speak: the flow arm's gates passed
        // (regardless of whether choke fired) OR the yield arm fired.
        // Verified-clean still requires the strong flow-arm gates.
        if (flowArmGatesPassed || yieldShortfall) {
            result.hasData = true;
        }

        if (flowChoked || yieldShortfall) {
            result.chokedPuck = true;
            result.sampleCount = flowSamples;
            // Leave delta carrying its flow-vs-goal meaning; consumers
            // short-circuit on chokedPuck before reading delta.
        } else if (flowArmGatesPassed
                   && !result.yieldOvershoot
                   && std::abs(result.delta) <= FLOW_DEVIATION_THRESHOLD) {
            // Flow-arm gates passed, no choke fired, no overshoot, and
            // Arm 1 (if it ran) found delta within tolerance. The puck
            // behaved through a sustained pressurized pour — strong verify.
            result.verifiedClean = true;
            // Leave sampleCount as Arm 1 set it. Arm 1 assigns sampleCount
            // before its own count >= 5 gate, so the value here is whatever
            // Arm 1 saw — could be the qualifying-samples count (≥ 5) when
            // Arm 1 produced data, a partial count (1-4) when Arm 1 ran
            // but didn't pass its gate, or 0 when Arm 1's block was
            // bypassed entirely (empty flowModeRanges or no flowGoal).
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
    return r.chokedPuck || r.yieldOvershoot || std::abs(r.delta) > FLOW_DEVIATION_THRESHOLD;
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

ShotAnalysis::AnalysisResult ShotAnalysis::analyzeShot(
    const QVector<QPointF>& pressure,
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
    double finalWeightG,
    int expectedFrameCount)
{
    AnalysisResult result;
    QVariantList& lines = result.lines;
    DetectorResults& d = result.detectors;

    if (pressure.size() < 10) {
        QVariantMap line;
        line["text"] = QStringLiteral("Not enough data to analyze.");
        line["type"] = QStringLiteral("observation");
        line["kind"] = QStringLiteral("insufficient_data");
        lines.append(line);
        // Leave detectors at defaults — every "checked" flag stays false,
        // signalling "no analysis was possible" to MCP consumers. Set
        // verdictCategory explicitly so consumers performing strict enum
        // matches don't see an empty string for this edge case.
        d.verdictCategory = QStringLiteral("insufficientData");
        return result;
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
    d.pourStartSec = pourStart;
    d.pourEndSec = pourEnd;

    // --- Pour-truncated detection (runs first; dominates the cascade) ---
    // When peak pressure stayed below PRESSURE_FLOOR_BAR the puck never built,
    // so channeling / flow-trend / temp-stability / grind blocks are all
    // reading off curves the failed puck didn't produce. Skip those blocks
    // entirely when this fires, and emit a single "Puck failed" warning +
    // verdict that names the meta-action ("don't tune off this shot"). Peak
    // pressure is computed locally for the warning text — detectPourTruncated
    // doesn't return it.
    const bool pourTruncated = detectPourTruncated(pressure, pourStart, pourEnd, beverageType);
    d.pourTruncated = pourTruncated;
    double peakPressureBar = 0.0;
    if (pourTruncated) {
        for (const auto& pt : pressure) {
            if (pt.x() < pourStart) continue;
            if (pt.x() > pourEnd) break;
            if (pt.y() > peakPressureBar) peakPressureBar = pt.y();
        }
        d.peakPressureBar = peakPressureBar;
    }

    // --- dC/dt analysis (channeling) ---
    bool skipChanneling = pourTruncated
        || shouldSkipChannelingCheck(beverageType, flow, pourStart, pourEnd)
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

        d.channelingChecked = true;
        d.channelingSpikeTimeSec = spikeTime;
        QVariantMap line;
        if (severity == ChannelingSeverity::Sustained) {
            d.channelingSeverity = QStringLiteral("sustained");
            line["text"] = QStringLiteral("Sustained channeling detected in dC/dt \u2014 puck prep issue");
            line["type"] = QStringLiteral("warning");
            line["kind"] = QStringLiteral("channeling_sustained");
        } else if (severity == ChannelingSeverity::Transient) {
            d.channelingSeverity = QStringLiteral("transient");
            line["text"] = QStringLiteral("Transient channel at %1s (self-healed)").arg(spikeTime, 0, 'f', 0);
            line["type"] = QStringLiteral("caution");
            line["kind"] = QStringLiteral("channeling_transient");
        } else {
            d.channelingSeverity = QStringLiteral("none");
            line["text"] = QStringLiteral("Puck stable \u2014 no channeling spikes in dC/dt");
            line["type"] = QStringLiteral("good");
            line["kind"] = QStringLiteral("channeling_none");
        }
        lines.append(line);
    }

    // --- Flow trend during extraction ---
    // Skipped for profiles where declining/rising flow is intentional (e.g. Cremina lever).
    // Suppressed when pourTruncated fires — rising/falling flow is meaningless on a puck
    // that never built pressure.
    const bool flowTrendOk = analysisFlags.contains(QStringLiteral("flow_trend_ok"));
    if (!pourTruncated && !flowTrendOk && pourStart > 0 && pourEnd > pourStart && flow.size() > 10) {
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
            const double delta = (flowEndSum / flowEndCount) - (flowStartSum / flowStartCount);
            d.flowTrendChecked = true;
            d.flowTrendDeltaMlPerSec = delta;
            if (delta > 0.5) {
                d.flowTrend = QStringLiteral("rising");
                QVariantMap line;
                line["text"] = QStringLiteral("Flow rose %1 mL/s during extraction (puck erosion)").arg(delta, 0, 'f', 1);
                line["type"] = QStringLiteral("caution");
                line["kind"] = QStringLiteral("flow_rising");
                lines.append(line);
            } else if (delta < -0.5) {
                d.flowTrend = QStringLiteral("falling");
                QVariantMap line;
                line["text"] = QStringLiteral("Flow dropped %1 mL/s (fines migration or clogging)").arg(std::abs(delta), 0, 'f', 1);
                line["type"] = QStringLiteral("caution");
                line["kind"] = QStringLiteral("flow_falling");
                lines.append(line);
            } else {
                d.flowTrend = QStringLiteral("stable");
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
        const double firstPhaseTime = phases.isEmpty() ? 0 : phases.first().time;
        const double preinfDuration = preinfEnd - firstPhaseTime;
        if (preinfWeight > 0.5 && preinfDuration > 1.0) {
            d.preinfusionObserved = true;
            d.preinfusionDripWeightG = preinfWeight;
            d.preinfusionDripDurationSec = preinfDuration;
            QVariantMap line;
            line["text"] = QStringLiteral("Preinfusion: %1g in %2s")
                .arg(preinfWeight, 0, 'f', 1).arg(preinfDuration, 0, 'f', 1);
            line["type"] = QStringLiteral("observation");
            line["kind"] = QStringLiteral("preinfusion_drip");
            lines.append(line);
        }
    }

    // --- Temperature stability ---
    // Gated on reachedExtractionPhase so aborted shots that died during
    // preinfusion-start (frame 0 only) don't fire on the preheat ramp.
    // Suppressed when pourTruncated fires — temp drift on a puck that never
    // built is a downstream symptom, not a useful diagnosis.
    if (!pourTruncated
        && temperature.size() > 10 && temperatureGoal.size() > 10 && pourStart > 0
        && reachedExtractionPhase(phases, duration)) {
        d.tempStabilityChecked = true;
        if (hasIntentionalTempStepping(temperatureGoal)) {
            d.tempIntentionalStepping = true;
        } else {
            const double avgDev = avgTempDeviation(temperature, temperatureGoal, pourStart, pourEnd);
            d.tempAvgDeviationC = avgDev;
            if (avgDev > TEMP_UNSTABLE_THRESHOLD) {
                d.tempUnstable = true;
                QVariantMap line;
                line["text"] = QStringLiteral("Temperature drifted %1\u00B0C from goal on average").arg(avgDev, 0, 'f', 1);
                line["type"] = QStringLiteral("caution");
                line["kind"] = QStringLiteral("temperature_drift");
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
    //
    // Suppressed when pourTruncated fires: with peak < 2.5 bar the flow-mode
    // phases tracked the preinfusion goal perfectly (puck wasn't holding
    // water back) so delta sits at ~0, and the pressure-mode arms can never
    // satisfy their 4 bar / 15 s gate. The detector reads "no signal," which
    // would FP a "Clean shot" verdict on a puck failure.
    const GrindCheck grind = pourTruncated
        ? GrindCheck{}
        : analyzeFlowVsGoal(flow, flowGoal, phases,
                            pourStart, pourEnd,
                            beverageType, analysisFlags,
                            pressure,
                            targetWeightG, finalWeightG);
    // Mirror channelingChecked / flowTrendChecked: `checked` reflects whether
    // the detector ran, not whether it was reachable. pourTruncated cascade
    // and the beverage / grind_check_skip skip path both leave grindChecked
    // false so MCP consumers don't misread `checked == true` + `hasData ==
    // false` as "ran but found nothing" when the detector was actually
    // suppressed by config.
    d.grindChecked = !pourTruncated && !grind.skipped;
    d.grindHasData = grind.hasData;
    d.grindChokedPuck = grind.chokedPuck;
    d.grindYieldOvershoot = grind.yieldOvershoot;
    d.grindVerifiedClean = grind.verifiedClean;
    d.grindFlowDeltaMlPerSec = grind.delta;
    d.grindSampleCount = grind.sampleCount;
    d.grindGateRan = grind.gateRan;
    d.grindGatePassed = grind.gatePassed;
    d.grindGateFlowSamples = grind.gateFlowSamples;
    d.grindGatePressurizedDurationSec = grind.gatePressurizedDurationSec;
    d.grindGateMeanPressurizedFlowMlPerSec = grind.gateMeanPressurizedFlowMlPerSec;
    d.grindGateYieldRatio = grind.gateYieldRatio;
    // Coverage signal: distinguishes "verified clean" from "no usable data
    // on this profile shape" so the dialog can stop claiming "Puck held
    // well." on shots where the detector simply couldn't see anything.
    // Suppressed when pourTruncated fires (the cascade dominator already
    // explains why the grind block was skipped). Also empty when the pour
    // window is degenerate — pourEnd <= pourStart means the marker stream
    // was malformed (legacy time-base bug); nothing useful to say.
    if (!pourTruncated && pourEnd > pourStart) {
        if (grind.skipped) {
            d.grindCoverage = QStringLiteral("skipped");
        } else if (grind.verifiedClean) {
            d.grindCoverage = QStringLiteral("verified");
        } else if (!grind.hasData) {
            d.grindCoverage = QStringLiteral("notAnalyzable");
        } else {
            // hasData=true but not verifiedClean → an actual issue fired
            // (chokedPuck, yieldOvershoot, or large delta). Coverage is
            // still "verified" because the detector ran and produced
            // data — coverage signals data availability, not health
            // outcome. The specific diagnosis is in grindDirection and
            // the verdict.
            d.grindCoverage = QStringLiteral("verified");
        }
    }
    if (grind.hasData) {
        if (grind.yieldOvershoot) {
            // Gusher: yield blew past target by > 20%. The puck offered too
            // little resistance and the shot finished much heavier than
            // intended. Phase-agnostic \u2014 the arm runs purely off yield
            // ratio. Pre-empts both chokedPuck (the two arms can only
            // co-fire from chokedPuck's flow sub-arm, which a gusher cannot
            // satisfy in practice \u2014 and even then, gusher is the more
            // applicable diagnosis) and the directional flow-vs-goal
            // caution. Order matches the verdict cascade below.
            d.grindDirection = QStringLiteral("yieldOvershoot");
            const double overG = finalWeightG - targetWeightG;
            QVariantMap line;
            line["text"] = QStringLiteral("Yield ran %1 g over target \u2014 "
                "puck offered too little resistance, grind way too coarse")
                .arg(overG, 0, 'f', 1);
            line["type"] = QStringLiteral("warning");
            line["kind"] = QStringLiteral("grind_yield_overshoot");
            lines.append(line);
        } else if (grind.chokedPuck) {
            d.grindDirection = QStringLiteral("chokedPuck");
            QVariantMap line;
            line["text"] = QStringLiteral("Pour produced near-zero flow while pressure held \u2014 "
                "puck choked, grind way too fine");
            line["type"] = QStringLiteral("warning");
            line["kind"] = QStringLiteral("grind_choked_puck");
            lines.append(line);
        } else if (grind.delta < -FLOW_DEVIATION_THRESHOLD) {
            d.grindDirection = QStringLiteral("tooFine");
            QVariantMap line;
            line["text"] = QStringLiteral("Flow averaged %1 ml/s below target \u2014 grind may be too fine")
                .arg(std::abs(grind.delta), 0, 'f', 1);
            line["type"] = QStringLiteral("caution");
            line["kind"] = QStringLiteral("grind_too_fine");
            lines.append(line);
        } else if (grind.delta > FLOW_DEVIATION_THRESHOLD) {
            d.grindDirection = QStringLiteral("tooCoarse");
            QVariantMap line;
            line["text"] = QStringLiteral("Flow averaged %1 ml/s above target \u2014 grind may be too coarse")
                .arg(grind.delta, 0, 'f', 1);
            line["type"] = QStringLiteral("caution");
            line["kind"] = QStringLiteral("grind_too_coarse");
            lines.append(line);
        } else {
            d.grindDirection = QStringLiteral("onTarget");
            if (grind.verifiedClean) {
                // Positive "we saw a healthy pressurized pour and the puck
                // tracked goal" signal. Today's verdict on lever / two-frame
                // profiles infers "Clean" from no-data; this line confirms
                // it from data. Skipped when the detector was silent
                // (hasData=false), which is handled by the notAnalyzable
                // branch below.
                QVariantMap line;
                line["text"] = QStringLiteral("Grind tracked goal during pour");
                line["type"] = QStringLiteral("good");
                line["kind"] = QStringLiteral("grind_clean");
                lines.append(line);
            }
        }
    } else if (d.grindCoverage == QStringLiteral("notAnalyzable")) {
        // Espresso shot, non-degenerate window, but neither arm produced
        // data. Common on simple two-frame profiles (Preinfusion + Pour:
        // A-Flow, La Pavoni, Malabar, Italian Style) where Arm 1's
        // flow-mode windows lie entirely before pourStart and Arm 2's
        // pressurized-duration gate isn't met. Stop pretending the puck
        // held well — say so out loud.
        QVariantMap line;
        line["text"] = QStringLiteral("Could not analyze grind on this profile shape — "
            "check flow trend, channeling, and taste instead");
        line["type"] = QStringLiteral("observation");
        line["kind"] = QStringLiteral("grind_not_analyzable");
        lines.append(line);
    }

    // --- Pour truncated (puck failure) ---
    // Detection ran at the top of the function; this block emits the
    // user-facing warning line. The cause list spans the full population:
    // grind way too coarse, distribution failure (massive channel), no/loose
    // puck or missing basket, severe underdose, or a profile that doesn't
    // enforce a pressure cap (high flow goal with no ceiling). The user
    // can't discriminate among those from the curve — verdict below tells
    // them to fix prep and pull again rather than tune off this shot.
    if (pourTruncated) {
        QVariantMap line;
        line["text"] = QStringLiteral("Pour never pressurized (peak %1 bar) \u2014 "
            "puck offered no resistance. Likely causes: grind way too coarse, "
            "distribution failure, no/loose puck, severe underdose, or profile "
            "without a pressure cap.")
            .arg(peakPressureBar, 0, 'f', 1);
        line["type"] = QStringLiteral("warning");
        line["kind"] = QStringLiteral("pour_truncated");
        lines.append(line);
    }

    // --- Skip-first-frame ---
    // Re-derive from phase markers (already available) so analyzeShot() does not
    // need a separate skipFirstFrameDetected parameter. Catches FW bug (machine
    // never executed frame 0) or profile first step running far shorter than
    // configured. firstFrameConfiguredSeconds (when known) avoids false-positives
    // on profiles with frame[0].seconds == 2.
    const bool skipFirstFrame = detectSkipFirstFrame(
        phases, expectedFrameCount, firstFrameConfiguredSeconds);
    d.skipFirstFrame = skipFirstFrame;
    if (skipFirstFrame) {
        QVariantMap line;
        line["text"] = QStringLiteral("First profile step skipped \u2014 "
            "likely a DE1 firmware bug (power-cycle machine to fix) "
            "or first step too short (check profile settings)");
        line["type"] = QStringLiteral("warning");
        line["kind"] = QStringLiteral("skip_first_frame");
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
        // Dominates over every other signal. Lead with the meta-action
        // ("don't tune off this shot") because peak pressure never built —
        // the channeling / grind / temp detectors are reading off curves the
        // failed puck didn't produce, so their silence (or any drift they
        // happen to flag) is not a tuning signal. Naming the unreliable
        // detectors explicitly is important: a user who sees no Channeling
        // chip might otherwise assume "OK at least no channeling," which is
        // wrong on a puck failure.
        d.verdictCategory = QStringLiteral("puckTruncated");
        verdict["text"] = QStringLiteral("Verdict: Don't tune off this shot \u2014 "
            "peak pressure never built, so the other quality signals "
            "(channeling, grind direction, temp) are unreliable. Check prep "
            "(dose, distribution, basket, grind) and pull another.");
    } else if (skipFirstFrame) {
        // Skip-first-frame is a machine/profile issue, not puck integrity — give specific advice
        // so the user isn't told to adjust grind when the real fix is a power-cycle.
        d.verdictCategory = QStringLiteral("skipFirstFrame");
        verdict["text"] = QStringLiteral("Verdict: First profile step was skipped \u2014 "
            "power-cycle the machine to fix a firmware bug, "
            "or review the profile's first step settings.");
    } else if (grind.yieldOvershoot) {
        // The mirror of the choked-puck verdict: yield blew past target by
        // > 20%, the puck offered too little resistance. Mutually exclusive
        // with chokedPuck only on the yield-ratio sub-arm (one < 0.85, the
        // other > 1.20); chokedPuck's flow sub-arm could in principle co-
        // fire, but a gusher cannot satisfy the 15 s \u00d7 4 bar gate in
        // practice. yieldOvershoot is the more applicable diagnosis when
        // both flag, so it sits above chokedPuck here. Both deserve their
        // own emphatic verdict rather than collapsing into the generic
        // "Puck integrity issue" line below.
        d.verdictCategory = QStringLiteral("yieldOvershoot");
        verdict["text"] = QStringLiteral("Verdict: Pour gushed past target \u2014 grind way too coarse. Grind much finer.");
    } else if (grind.chokedPuck) {
        // Pressure built but the puck refused to extract \u2014 the diagnosis is
        // unambiguously "grind way too fine," not a distribution problem.
        // Pre-empts the generic "Puck integrity issue" verdict that the
        // hasWarning branch below would otherwise emit. Sits below
        // skipFirstFrame because a frame-skip bug can synthesise extraction
        // dynamics that look like a choke (frame 1 takes over a profile
        // step that holds high pressure with low flow); fixing the machine
        // is the prerequisite, after which the user can re-evaluate grind.
        d.verdictCategory = QStringLiteral("chokedPuck");
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
            d.verdictCategory = QStringLiteral("puckIntegrityGrindFine");
            verdict["text"] = QStringLiteral("Verdict: Puck integrity issue \u2014 improve distribution. Grind is running fine \u2014 try coarser.");
        } else if (grindCoarse) {
            d.verdictCategory = QStringLiteral("puckIntegrityGrindCoarse");
            verdict["text"] = QStringLiteral("Verdict: Puck integrity issue \u2014 improve distribution. Grind is running coarse \u2014 try finer.");
        } else {
            d.verdictCategory = QStringLiteral("puckIntegrity");
            verdict["text"] = QStringLiteral("Verdict: Puck integrity issue \u2014 improve distribution.");
        }
    } else if (hasCaution) {
        // No puck-integrity warning: if the only caution is a grind direction,
        // name the direction explicitly rather than giving a generic "minor
        // issues" verdict.
        const bool grindFine = grind.hasData && grind.delta < -FLOW_DEVIATION_THRESHOLD;
        const bool grindCoarse = grind.hasData && grind.delta > FLOW_DEVIATION_THRESHOLD;
        if (grindFine) {
            d.verdictCategory = QStringLiteral("minorIssuesGrindFine");
            verdict["text"] = QStringLiteral("Verdict: Grind appears too fine \u2014 try coarser.");
        } else if (grindCoarse) {
            d.verdictCategory = QStringLiteral("minorIssuesGrindCoarse");
            verdict["text"] = QStringLiteral("Verdict: Grind appears too coarse \u2014 try finer.");
        } else {
            d.verdictCategory = QStringLiteral("minorIssues");
            verdict["text"] = QStringLiteral("Verdict: Decent shot with minor issues to watch.");
        }
    } else if (d.grindCoverage == QStringLiteral("notAnalyzable")) {
        // No warnings, no cautions, but the grind detector silently bailed
        // on this profile shape — the cascade has been claiming "Puck held
        // well." on these shots regardless of actual grind. Be honest.
        d.verdictCategory = QStringLiteral("cleanGrindNotAnalyzable");
        verdict["text"] = QStringLiteral("Verdict: Clean shot, but grind could not be evaluated for this profile shape.");
    } else {
        d.verdictCategory = QStringLiteral("clean");
        verdict["text"] = QStringLiteral("Verdict: Clean shot. Puck held well.");
    }
    verdict["type"] = QStringLiteral("verdict");
    lines.append(verdict);

    return result;
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
                                             double finalWeightG,
                                             int expectedFrameCount)
{
    return analyzeShot(pressure, flow, weight, temperature, temperatureGoal,
                       conductanceDerivative, phases, beverageType, duration,
                       pressureGoal, flowGoal, analysisFlags,
                       firstFrameConfiguredSeconds, targetWeightG, finalWeightG,
                       expectedFrameCount)
        .lines;
}
