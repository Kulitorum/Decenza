#include "weightprocessor.h"
#include "../ble/scales/scalelogging.h"  // the feed-liveness line is a [Scale] question
#include "sawlogging.h"
#include "sawprediction.h"
#include <QtMath>
#include <QDebug>
#include <chrono>

// Aliases, not copies — see sawlogging.h. This class runs on a worker thread and
// carries no logMessage signal, so the STDERR forms are the right ones here.
// The feed-liveness/stall/interval lines answer "did the readings arrive", which
// is a [Scale] question, not a [SAW] one — they were a sixth hand-rolled family
// ("[Weight-Worker]") that no registered marker matched.
#define SCALEFEED_LOG(msg)  SCALE_LOG_STDERR_TAGGED("ScaleFeed", msg)
#define SCALEFEED_WARN(msg) SCALE_WARN_STDERR_TAGGED("ScaleFeed", msg)

#define SAWW_LOG(msg)  SAW_LOG_STDERR("Worker", msg)
#define SAWW_INFO(msg) SAW_INFO_STDERR("Worker", msg)
#define SAWW_WARN(msg) SAW_WARN_STDERR("Worker", msg)

namespace {
qint64 monotonicMsNow()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
}

WeightProcessor::WeightProcessor(QObject* parent)
    : QObject(parent)
{
}

void WeightProcessor::processWeight(double weight)
{
    qint64 wallClock = m_wallClock();

    // Spike filter (issue #610): reject single-packet BLE corruption.
    // A Felicita scale was observed sending 1649g instead of ~10g, causing a
    // false SAW stop. Any reading that jumps more than 100g from the previous
    // sample is rejected. Auto-resets after 3 consecutive rejections to handle
    // legitimate shifts (cup removal, scale reconnect at different offset). The tare
    // used to rely on that hatch too; it is handled up front now — see below.
    //
    // Only active during extraction — between shots, cup placement/removal,
    // tare drift, and scale reconnect at different offset produce legitimate
    // 100g+ swings that the filter would reject, freezing the flow rate display
    // and polluting the debug log. The filter is reset by startExtraction()
    // before each shot and by resetForRetare() on mid-preheat retare.
    //
    // ONE step is exempt: the tare. startExtraction() clears the baseline, but the tare
    // is asynchronous AT THE SCALE — the samples arriving just after it still carry the
    // loaded portafilter's weight, and one of them becomes the new baseline. The drop to
    // zero that follows then reads as a >100 g spike, so the filter rejected the app's
    // own tare. Observed every shot on-device: "Spike rejected: weight= 0 last= 364.6"
    // twice, then the 3-rejection escape hatch accepting the baseline it should never
    // have questioned.
    //
    // The exemption is deliberately DIRECTIONAL: only a large step DOWN to near zero,
    // and only while awaiting a tare. Corruption of the #610 kind travels upward (1649 g
    // instead of 10 g), so nothing about that case needs the filter relaxed — and an
    // exemption for large steps in general would leave a window with no filter at all,
    // whose end depended on events that might never arrive (a scale that never reads
    // near zero, a shot where flow never starts). Every sample the SPIKE FILTER sees
    // other than the tare step is therefore filtered exactly as before.
    //
    // The m_awaitingTare window is not gone, though — it still gates the per-frame
    // weight exit at the bottom of this function, and on an untared cup that never
    // reads near zero in a shot where flow never starts, that gate stays shut for the
    // whole shot. That is the deliberate trade (see the comment there); it is a much
    // smaller surface than leaving the spike filter itself off.
    if (m_active && m_hasLastWeight && qAbs(weight - m_lastRawWeight) > 100.0) {
        if (m_awaitingTare && qAbs(weight) <= kTareLandedThresholdG
            && ++m_tareLandedSamples >= kTareLandedConfirmations) {
            // The tare landing, CONFIRMED. One packet is deliberately not enough.
            //
            // To be clear about the evidence: the observed "weight= 0 last= 364.6"
            // above was the REAL tare being wrongly rejected, not corruption — it is
            // not evidence that spurious zeros occur. The reason to require a second
            // sample is that single-packet corruption is the documented failure mode
            // this whole filter exists for (#610, a Felicita reporting 1649 g instead
            // of ~10 g), and Decenza supports sixteen scale types whose behaviour
            // around a tare we have no data on. Believing one spurious near-zero
            // would consume the exemption, leave the real tare step to be rejected
            // three times and escape-hatched, and open the per-frame weight exit on a
            // full pre-tare reading — firing exactly the skipFrame the gate below
            // exists to prevent. Same shape as the oscillation detector's settle
            // count, and it costs ~200 ms of preheat.
            m_awaitingTare = false;
            m_tareLandedSamples = 0;
            m_consecutiveRejections = 0;
        } else if (m_awaitingTare && qAbs(weight) <= kTareLandedThresholdG) {
            // Near zero but unconfirmed. Hold it: do NOT let it become the baseline,
            // or a spurious zero still displaces a real pre-tare reading.
            //
            // Logged because this DISCARDS a sample, and its sibling branch below logs
            // every sample it drops — a silent drop on a path that runs every shot is
            // the one you cannot diagnose from a field log. qDebug, not qWarning:
            // holding the first near-zero is the NORMAL case (the confirmation needs a
            // second one), so a warning would fire on every shot and fail the suite
            // under failOnWarning().
            //
            // Repeated lines here with no "tare confirmed" following them is the
            // signature of a scale alternating a loaded reading with a near-zero one
            // (e.g. 200, 0, 200, 0): each zero holds and is discarded without updating
            // m_lastRawWeight, each 200 is a 0 g step that resets the confirmation
            // count, so m_awaitingTare never clears for the whole preheat. Bounded —
            // markExtractionStart() arms unconditionally at flow start — but worth
            // seeing rather than guessing at.
            SAWW_LOG(QStringLiteral("Tare candidate held (unconfirmed): weight=%1 g last=%2 g "
                                    "confirmations=%3/%4")
                         .arg(weight, 0, 'f', 2).arg(m_lastRawWeight, 0, 'f', 2)
                         .arg(m_tareLandedSamples).arg(kTareLandedConfirmations));
            m_lastWallClockMs = wallClock;  // keep de-jitter timing accurate
            return;
        } else if (++m_consecutiveRejections < 3) {
            m_tareLandedSamples = 0;  // not near zero — any run of them is broken
            m_lastWallClockMs = wallClock;  // Keep de-jitter timing accurate
            // WARN. Measured, not assumed: zero occurrences across 22,265 log
            // lines and five sessions, so this is not a spam source, and a
            // rejected sample sitting next to a stop-at-weight decision is
            // exactly what a reader chasing a mis-stopped shot needs to see.
            // (An earlier pass demoted this to DEBUG on the asserted claim that
            // it "fires several times on a normal shot". It does not.)
            SAWW_WARN(QStringLiteral("Spike rejected: weight=%1 g last=%2 g")
                          .arg(weight, 0, 'f', 2).arg(m_lastRawWeight, 0, 'f', 2));
            return;
        } else {
            SAWW_WARN(QStringLiteral("Spike filter reset after %1 consecutive rejections "
                                     "— accepting new baseline: %2 g")
                          .arg(m_consecutiveRejections).arg(weight, 0, 'f', 2));
            m_consecutiveRejections = 0;
        }
    } else {
        // A near-zero reading with no big step (the common case — the scale was already
        // at zero) still satisfies the tare we were waiting for, and is confirmed the
        // same way. Samples here are accepted normally either way; only the flag waits.
        if (m_awaitingTare && qAbs(weight) <= kTareLandedThresholdG) {
            if (++m_tareLandedSamples >= kTareLandedConfirmations) {
                m_awaitingTare = false;
                m_tareLandedSamples = 0;
            }
        } else {
            m_tareLandedSamples = 0;
        }
        m_consecutiveRejections = 0;
    }
    // Captured before the overwrite below: pre-#1176, a sample equal to the
    // previous one was dropped by ScaleDevice's weightChanged dedup, so a
    // static-weight window (empty cup through DE1 EspressoPreheating) starved
    // this slot and tripped a false scale-feed stall. Such samples now arrive
    // via weightSampleReceived; see the diagnostic after the de-jitter block.
    const bool sampleValueUnchanged = m_hasLastWeight && weight == m_lastRawWeight;
    m_hasLastWeight = true;
    m_lastRawWeight = weight;

    // De-jitter: BLE events arrive on the main thread via QueuedConnection, and when
    // the main thread is busy (QML rendering), multiple events queue up and are
    // delivered in a burst. The worker thread processes them all within ~1ms, so
    // wall-clock timestamps cluster together. This makes LSLR see dt≈0 and return 0,
    // blinding SAW for the entire pour.
    //
    // Fix: detect batching (gap < 20ms) and assign synthetic timestamps spaced by
    // the calibrated scale interval. Non-batched events calibrate the interval via
    // exponential moving average, so this adapts to any scale rate (10Hz, 5Hz, 2Hz).
    constexpr qint64 kBatchThresholdMs = 20;    // Below this, events are batched
    constexpr qint64 kReconnectGapMs = 2000;    // Above this, ignore as reconnect
    constexpr double kEmaAlpha = 0.3;           // Smoothing factor for interval estimate

    qint64 sinceLast = m_lastWallClockMs > 0 ? (wallClock - m_lastWallClockMs) : -1;
    m_lastWallClockMs = wallClock;

    // #1176 liveness diagnostic. An unchanged-value sample during a shot's
    // static window (classically an empty cup through EspressoPreheating)
    // would, pre-fix, have been swallowed by ScaleDevice's weightChanged
    // dedup — the feed looked dead and a false scale-feed stall fired. It now
    // reaches us via weightSampleReceived. Log it (throttled ~2s, shot context
    // only) so the fix is provable from a field debug log without needing the
    // recorded weight curve. Event-based throttle (no timer): gated on sample
    // arrival + the injected wall clock.
    if (sampleValueUnchanged && (m_active || m_preheatActive) && m_tareComplete
        && (m_lastConstantSampleLogMs == 0
            || wallClock - m_lastConstantSampleLogMs >= 2000)) {
        m_lastConstantSampleLogMs = wallClock;
        // [Scale], not [SAW], even though this file is otherwise SAW's worker:
        // the line answers "did the weight readings keep arriving", which is a
        // scale question. Filing it under SAW would leave a reader chasing a
        // missing feed inside the stop logic. It was a fifth hand-rolled
        // marker-shaped prefix ("[ScaleFeed]") that no registered marker matched.
        //
        // DEBUG, not INFO: it exists to prove a NON-bug (a static reading is a
        // live feed, not a stalled one). 59 of them in a 48 h capture, on a 2 s
        // dedupe window, none of which a user needs.
        SCALE_LOG_STDERR_TAGGED("ScaleFeed",
            QStringLiteral("alive: constant weight %1 g still streaming via "
                           "weightSampleReceived (pre-#1176 this static window read "
                           "as a stalled feed)").arg(weight, 0, 'f', 1));
    }

    // Scale-feed liveness: a genuine (non-spike) sample arrived, so the feed
    // is alive again — clear the stall flag so a later stall in this shot can
    // be re-detected. Recovery edge (observe-mode change): if a stall had been
    // signalled this cycle, emit scaleFeedResumed once on the 1→0 edge with
    // the silent gap (now − the last good sample before the silence, captured
    // in m_feedStallStartMs when the stall was detected). Pure sample edge, no
    // timer. enforce-mode behavior is unaffected — this is observation only;
    // the transport decides whether to log it.
    if (m_scaleFeedStale) {
        qint64 gapMs = 0;
        if (m_feedStallStartMs > 0) {
            gapMs = wallClock - m_feedStallStartMs;
        } else {
            // Unreachable on every current path (checkScaleFeedStall always
            // sets m_feedStallStartMs alongside m_scaleFeedStale). Log loudly
            // rather than silently emit gapMs=0 — a fake "recovered after
            // 0.0 s" would be plausible-looking but wrong observe evidence.
            SCALEFEED_WARN(QStringLiteral("scaleFeedResumed with no recorded stall-start — "
                                          "emitting gap 0 (investigate: a stall was flagged "
                                          "without m_feedStallStartMs being set)"));
        }
        emit scaleFeedResumed(gapMs);
    }
    // Recovery cancels any pending/active confirmation: a feed that came back
    // never confirms, so enforce never latches on a self-recovering blip. A
    // later independent stall re-arms suspected→confirmed cleanly. Unconditional
    // + idempotent (clears the no-stall path too).
    resetStallTracking();

    qint64 sampleTs;
    if (sinceLast < 0 || sinceLast > kBatchThresholdMs) {
        // First call or non-batched: use wall-clock as ground truth.
        // A previous batch may have pushed synthetic timestamps ahead of wall-clock.
        // Instead of clamping by a full interval (which causes runaway drift — each
        // clamp pushes synthetic further ahead, so subsequent events also get clamped),
        // use wall-clock directly and only enforce minimal monotonicity (+1ms).
        // This lets synthetic reconverge with wall-clock within a few samples.
        sampleTs = wallClock;
        if (m_lastSampleTs > 0 && sampleTs < m_lastSampleTs) {
            // Minimal advance: just +1ms to maintain monotonicity. This means synthetic
            // barely advances while wall-clock catches up, closing the gap naturally.
            sampleTs = m_lastSampleTs + 1;
        }
        if (sinceLast > kBatchThresholdMs && sinceLast < kReconnectGapMs) {
            // EMA calibration (ignores gaps > 2s as reconnects)
            if (m_estimatedIntervalMs == 0)
                m_estimatedIntervalMs = static_cast<int>(sinceLast);
            else
                m_estimatedIntervalMs = static_cast<int>((1.0 - kEmaAlpha) * m_estimatedIntervalMs + kEmaAlpha * sinceLast);
        }
    } else if (m_estimatedIntervalMs > 0) {
        // Batched (< 20ms since last) and calibrated: spread using estimated interval.
        // Cap synthetic so it can't drift more than 2 intervals ahead of wall-clock.
        // Without this cap, a burst of N batched events pushes synthetic N*interval ms
        // ahead, and subsequent non-batched events take many cycles to reconverge.
        sampleTs = m_lastSampleTs + m_estimatedIntervalMs;
        qint64 maxAhead = wallClock + m_estimatedIntervalMs * 2;
        if (sampleTs > maxAhead) {
            sampleTs = maxAhead;
        }
        // Enforce monotonicity after capping — cap could push below m_lastSampleTs.
        // Use estimated interval (not +1ms) to avoid near-zero dt in LSLR.
        if (sampleTs < m_lastSampleTs) {
            sampleTs = m_lastSampleTs + m_estimatedIntervalMs;
        }
    } else {
        // Batched but uncalibrated: use wall-clock (LSLR may see dt≈0 until calibrated)
        sampleTs = wallClock;
        if (m_active && !m_uncalibratedBatchWarned) {
            SAWW_WARN(QStringLiteral("De-jitter: batched event before calibration — LSLR may "
                                     "return 0 until a non-batched gap is observed"));
            m_uncalibratedBatchWarned = true;
        }
    }
    m_lastSampleTs = sampleTs;

    // Record sample for LSLR (1-second rolling window)
    m_weightSamples.append({sampleTs, weight});
    while (!m_weightSamples.isEmpty() && (sampleTs - m_weightSamples.first().timestamp) > 1000) {
        m_weightSamples.removeFirst();
    }

    // Compute flow rates (always, even outside extraction — for QML display and settling)
    double flowRate = computeLSLR(1000);

    // Adaptive short window: ensure at least 3 samples are covered regardless of
    // the scale's reporting rate. At 5Hz (Decent Scale) the span of 2 intervals is
    // 400ms; the 500ms floor dominates and the window stays at 500ms. At 2Hz (Bookoo)
    // the span of 2 intervals is ~1000ms, giving 1000ms+50ms, capped at 1000ms to
    // match the rolling buffer — covers 2–3 samples vs. only 1–2 at 500ms.
    // Without this, sparse scales flicker flowRateShort near 0 and SAW is gated out.
    int shortWindowMs = 500;
    if (m_weightSamples.size() >= 3) {
        qint64 spanOf3 = m_weightSamples.last().timestamp
                         - m_weightSamples[m_weightSamples.size() - 3].timestamp;
        shortWindowMs = qBound(500, static_cast<int>(spanOf3) + 50, 1000);
    }
    double flowRateShort = computeLSLR(shortWindowMs);

    emit flowRatesReady(weight, flowRate, flowRateShort);

    // SAW and per-frame checks only during active extraction
    if (!m_active) return;

    // Bookoo (and any scale) tare oscillation guard: large negative swing during
    // extraction means the scale is mid-reset. Block SAW and enter recovery mode —
    // matching de1app's on_tare_seen pattern: clear the LSLR and wait for the scale
    // to return to ~0g before re-arming, so oscillation samples never corrupt SAW.
    // Uses raw weight — a single -6g reading is a clear tare-reset signal.
    if (m_tareComplete && weight < -5.0) {
        m_tareComplete = false;
        m_oscillationDetected = true;
        m_settleCount = 0;
        m_weightSamples.clear();  // Discard oscillation samples from LSLR

        m_hasLastWeight = false;  // Accept first reading at any weight after recovery
        SAWW_WARN(QStringLiteral("Scale oscillation detected (weight=%1 g) — stop-at-weight "
                                 "blocked, awaiting settle").arg(weight, 0, 'f', 2));
    }

    // Mid-shot oscillation recovery: once scale returns to ~0g stably, re-arm SAW
    // with a clean LSLR baseline. Requires 3 consecutive near-zero readings (~0.4s
    // at 5Hz, ~1.0s at 2Hz) to avoid re-arming while the scale is still mid-oscillation.
    // This mirrors de1app's _tare_awaiting_zero / on_tare_seen mechanism.
    if (!m_tareComplete && m_oscillationDetected) {
        if (qAbs(weight) < 2.0) {
            if (++m_settleCount >= 3) {
                m_tareComplete = true;
                m_oscillationDetected = false;
                m_settleCount = 0;
                m_weightSamples.clear();  // Fresh LSLR baseline from post-settle readings
                m_hasLastWeight = false;  // Accept first reading at any weight after recovery
                SAWW_LOG(QStringLiteral("Scale settled after oscillation, stop-at-weight re-armed"));
            }
        } else {
            m_settleCount = 0;  // Reset counter if weight leaves the near-zero band
        }
    }

    if (!m_tareComplete) {
        // Throttle warning to every 5s to avoid log spam at 5Hz
        if (wallClock - m_lastTareWarnMs >= 5000) {
            SAWW_LOG(QStringLiteral("Active but tare not complete — skipping stop-at-weight, "
                                    "weight=%1 g").arg(weight, 0, 'f', 2));
            m_lastTareWarnMs = wallClock;
        }
        return;
    }

    // Sanity check: unreasonable weight early in extraction (likely untared cup)
    if (m_extractionStartTime > 0) {
        double extractionTime = (wallClock - m_extractionStartTime) / 1000.0;
        if (extractionTime < 3.0 && weight > 50.0) {
            SAWW_WARN(QStringLiteral("Sanity check: weight %1 g at %2 s into extraction — "
                                     "skipping stop-at-weight (likely untared cup)")
                          .arg(weight, 0, 'f', 2).arg(extractionTime, 0, 'f', 1));
            if (!m_untaredCupSignalled) {
                m_untaredCupSignalled = true;
                emit untaredCupDetected();
            }
            return;
        }
    }

    // Suppress SAW during preinfusion frames (matches de1app: SAW only after
    // current frame >= number_of_preinfuse_frames)
    bool pastPreinfusion = (m_currentFrame >= m_preinfuseFrameCount);

    // Stop-at-weight check (requires valid flow rate for drip prediction)
    if (pastPreinfusion && !m_stopTriggered && m_targetWeight > 0 && flowRateShort < 0.5) {
        // Throttle this log to every 5s, include LSLR diagnostic info
        if (wallClock - m_lastLowFlowLogMs >= 5000) {
            // Compute dt for the short window to diagnose why LSLR returns 0
            double shortDt = 0;
            if (m_weightSamples.size() >= 2) {
                qint64 cutoff = m_weightSamples.last().timestamp - shortWindowMs;
                qsizetype si = m_weightSamples.size() - 1;
                while (si > 0 && m_weightSamples[si - 1].timestamp >= cutoff) --si;
                shortDt = (m_weightSamples.last().timestamp - m_weightSamples[si].timestamp) / 1000.0;
            }
            SAWW_LOG(QStringLiteral("Flow too low for stop-at-weight check: flowShort=%1 g/s "
                                    "weight=%2 g target=%3 g samples=%4 shortWindow=%5 ms "
                                    "shortDt=%6 s gate=%7 s")
                         .arg(flowRateShort, 0, 'f', 2).arg(weight, 0, 'f', 2)
                         .arg(m_targetWeight, 0, 'f', 2).arg(m_weightSamples.size())
                         .arg(shortWindowMs).arg(shortDt, 0, 'f', 3)
                         .arg(shortWindowMs * 0.65 / 1000.0, 0, 'f', 3));
            m_lastLowFlowLogMs = wallClock;
        }
    }
    // Ignore SAW for the first 5 seconds of extraction (matches de1app).
    // Prevents false triggers from tare settling, puck swelling, scale noise.
    double extractionElapsed = (m_extractionStartTime > 0) ? (wallClock - m_extractionStartTime) / 1000.0 : 0.0;
    if (pastPreinfusion && !m_stopTriggered && m_targetWeight > 0 && flowRateShort >= 0.5
        && extractionElapsed > 5.0) {
        // Log once when flow becomes valid — confirms de-jitter is working
        if (!m_flowBecameValidLogged && m_extractionStartTime > 0) {
            m_flowBecameValidLogged = true;
            double extractionTime = (wallClock - m_extractionStartTime) / 1000.0;
            // INFO: this marks the moment stop-at-weight can act at all. When it
            // arrives LATE the target has already passed and the shot overshoots,
            // which is exactly the failure a reader is trying to explain — so it
            // belongs in the narrative, not below it. Once per shot.
            SAWW_INFO(QStringLiteral("Flow became valid: flowShort=%1 g/s flowLong=%2 g/s "
                                     "weight=%3 g at %4 s")
                          .arg(flowRateShort, 0, 'f', 2).arg(flowRate, 0, 'f', 2)
                          .arg(weight, 0, 'f', 2).arg(extractionTime, 0, 'f', 1));
        }
        double cappedFlow = qMin(flowRateShort, 12.0);
        double expectedDrip = getExpectedDrip(cappedFlow);
        double stopThreshold = m_targetWeight - expectedDrip;

        if (weight >= stopThreshold) {
            m_stopTriggered = true;
            qint64 triggerMs = monotonicMsNow();
            // INFO: the stop decision itself. One line per shot, and it is the
            // whole answer to "why did my shot stop where it did" — every input
            // to the decision is on it. This subsystem previously had ZERO INFO
            // lines, so a user-level read showed stop-at-weight only when it
            // complained and never when it worked.
            SAWW_INFO(QStringLiteral("Stop triggered: weight=%1 g threshold=%2 g "
                                     "flow=%3 g/s (short) expectedDrip=%4 g target=%5 g")
                          .arg(weight, 0, 'f', 2).arg(stopThreshold, 0, 'f', 2)
                          .arg(flowRateShort, 0, 'f', 2).arg(expectedDrip, 0, 'f', 2)
                          .arg(m_targetWeight, 0, 'f', 2));
            emit sawTriggered(weight, flowRateShort, m_targetWeight);
            emit stopNow(triggerMs);
        }
    }

    // Per-frame weight exit check.
    //
    // Gated on the tare having landed. This check compares an ABSOLUTE weight against
    // a threshold, and while m_awaitingTare holds, absolute weight is meaningless — the
    // zero point is mid-move. A pre-tare reading (a loaded portafilter, ~360 g) clears
    // any plausible exitWeight outright, so acting on it would skip a frame on the
    // strength of a number we have already decided not to trust. That was reachable
    // before this gate for the first post-startExtraction sample, which bypasses the
    // spike filter to establish the baseline; the tare hold-off would have widened it
    // to every pre-tare sample. Unlike the SAW stop below, this branch has no
    // extractionElapsed > 5 s guard of its own, so it needs its own.
    if (!m_awaitingTare && m_currentFrame >= 0 && m_currentFrame < m_frameExitWeights.size()) {
        double exitWeight = m_frameExitWeights[m_currentFrame];
        if (exitWeight > 0 && weight >= exitWeight && !m_frameWeightSkipSent.contains(m_currentFrame)) {
            // On a mixed frame (weight exit + firmware exit), the firmware can
            // advance the frame on its own while our blind SkipToNext is in
            // flight, double-advancing the profile. Consult the arbiter: defer
            // the tablet skip when the firmware is near and trending toward its
            // own exit, otherwise fire normally. Weight-only frames (no firmware
            // exit) skip the arbiter and fire as before.
            const FrameExitCondition fwExit =
                (m_currentFrame < m_frameExitConditions.size())
                    ? m_frameExitConditions[m_currentFrame]
                    : FrameExitCondition{};
            if (fwExit.kind != FrameExitCondition::Kind::None) {
                if (m_stepExitArbiter.evaluate(m_currentFrame, fwExit,
                                               m_currentPressure, m_currentFlow)
                    == StepExitArbiter::Verdict::Defer) {
                    // Do not send and do not mark sent — re-evaluate next sample.
                    // If the firmware advances meanwhile, the next sample sees a
                    // new m_currentFrame and never skips the frame it already left.
                    return;
                }
            }
            SAWW_LOG(QStringLiteral("Frame-weight exit: weight %1 g >= %2 g on frame %3")
                         .arg(weight, 0, 'f', 2).arg(exitWeight, 0, 'f', 2).arg(m_currentFrame));
            m_frameWeightSkipSent.insert(m_currentFrame);
            emit skipFrame(m_currentFrame);
        }
    }
}

void WeightProcessor::configure(double targetWeight, int preinfuseFrameCount,
                                QVector<double> frameExitWeights,
                                QVector<FrameExitCondition> frameExitConditions,
                                QVector<double> learningDrips, QVector<double> learningFlows,
                                bool sawConverged, double sensorLagSeconds)
{
    m_targetWeight = targetWeight;
    m_preinfuseFrameCount = preinfuseFrameCount;
    m_frameExitWeights = frameExitWeights;
    m_frameExitConditions = frameExitConditions;
    m_learningDrips = learningDrips;
    m_learningFlows = learningFlows;
    m_sawConverged = sawConverged;
    m_sensorLagSeconds = sensorLagSeconds;
}

void WeightProcessor::setTargetWeight(double weight)
{
    if (m_targetWeight == weight) return;
    qInfo().noquote() << "WeightProcessor: targetWeight" << m_targetWeight << "->" << weight
                      << "(active=" << m_active << ")";
    m_targetWeight = weight;
}

void WeightProcessor::setCurrentFrame(int frameNumber, double pressure, double flow)
{
    m_currentPressure = pressure;
    m_currentFlow = flow;
    if (frameNumber != m_currentFrame) {
        // Firmware advanced: drop arbiter deferral state for frames it has
        // passed, so a deferred weight skip is never sent for a stale frame.
        m_stepExitArbiter.onFrameAdvanced(frameNumber);
    }
    m_currentFrame = frameNumber;
    // setCurrentFrame() is driven by the DE1 shot-sample stream (~5Hz), which
    // keeps ticking even when the scale is silent — the cadence that detects an
    // ongoing stall during a shot / preheat (processWeight only runs when a
    // sample arrives, so it cannot detect a fully-silent feed).
    checkScaleFeedStall(frameNumber);
}

void WeightProcessor::resetStallTracking()
{
    // The three fields move together — never clear a subset (an illegal
    // "confirmed but not stale" gates the enforce backoff incorrectly).
    m_scaleFeedStale = false;
    m_scaleStallConfirmed = false;
    m_feedStallStartMs = 0;
}

void WeightProcessor::checkScaleFeedStall(int frameNumber)
{
    // Scale-agnostic in-shot liveness backstop (BLE connection-priority).
    // "Weight is expected to stream" when an espresso cycle is in progress —
    // active extraction OR the pre-shot EspressoPreheating phase — and tare is
    // complete (tare proves the scale should be reporting). Requires at least
    // one processed sample since the last extraction reset (m_lastWallClockMs
    // > 0; reset only in startExtraction()/resetForRetare(), NOT per-gate) so
    // we never fire before the scale naturally starts. A legitimately idle
    // scale with no espresso cycle never trips (gate false).
    const bool shotContext = (m_active || m_preheatActive) && m_tareComplete;
    if (!shotContext) return;
    if (m_lastWallClockMs <= 0) return;

    const qint64 gapMs = m_wallClock() - m_lastWallClockMs;  // silence so far

    if (!m_scaleFeedStale) {
        // Not yet stalled. SUSPECTED edge at kScaleStaleMs — unchanged signal
        // (observe logging + diagnostics rely on it). A suspected stall does
        // NOT by itself latch a backoff: enforce acts only on CONFIRMED.
        // gapMs (from m_lastWallClockMs) is correct here: m_feedStallStartMs
        // is not yet set, and m_lastWallClockMs IS the last good sample.
        if (gapMs <= kScaleStaleMs) return;
        m_scaleFeedStale = true;
        m_scaleStallConfirmed = false;
        // Freeze the silent-gap origin at the last good sample's wall-clock.
        // BOTH the resume gap (processWeight) AND the CONFIRM threshold below
        // measure from this frozen value — NOT from m_lastWallClockMs, which
        // a rejected-spike packet advances (spike path: m_lastWallClockMs =
        // wallClock; return; — issue #610). Anchoring confirm here makes it
        // spike-immune: a genuinely dead feed that emits periodic corrupt
        // spikes (the #1176/#610 overlap) still confirms instead of having
        // its confirm clock reset to ~0 by every spike.
        m_feedStallStartMs = m_lastWallClockMs;
        SCALEFEED_WARN(QStringLiteral("Scale feed stalled > %1 ms while weight expected "
                                      "(frame %2 active=%3 preheat=%4) — SUSPECTED "
                                      "(not yet confirmed)")
                           .arg(kScaleStaleMs).arg(frameNumber)
                           .arg(m_active ? QStringLiteral("true") : QStringLiteral("false"))
                           .arg(m_preheatActive ? QStringLiteral("true")
                                                : QStringLiteral("false")));
        emit scaleFeedStalled(gapMs);
        return;
    }

    // Still stalled (no genuine sample has arrived — a sample would have
    // cleared m_scaleFeedStale in processWeight() and emitted scaleFeedResumed,
    // which is what CANCELS confirmation: a self-recovering blip never reaches
    // here a second time). CONFIRM once the silence persists past the larger
    // threshold. Measured from the frozen m_feedStallStartMs (spike-immune),
    // NOT m_lastWallClockMs. Event-based on the DE1 tick — no timer.
    const qint64 confirmGapMs = m_wallClock() - m_feedStallStartMs;
    if (!m_scaleStallConfirmed && confirmGapMs >= kScaleStallConfirmMs) {
        m_scaleStallConfirmed = true;
        SCALEFEED_WARN(QStringLiteral("Scale feed stall CONFIRMED — still dead %1 ms (> %2 ms) "
                                      "with no recovery (frame %3 active=%4 preheat=%5)")
                           .arg(confirmGapMs).arg(kScaleStallConfirmMs).arg(frameNumber)
                           .arg(m_active ? QStringLiteral("true") : QStringLiteral("false"))
                           .arg(m_preheatActive ? QStringLiteral("true")
                                                : QStringLiteral("false")));
        emit scaleFeedStallConfirmed(confirmGapMs);
    }
}

void WeightProcessor::setShotCycleActive(bool active)
{
    if (m_preheatActive == active) return;
    m_preheatActive = active;
    // Leaving the preheat window (idle/sleep/extraction handoff): clear the
    // stale flag so a later cycle can re-detect. m_active extraction is
    // unaffected (it owns its own reset in startExtraction()).
    if (!active && !m_active) resetStallTracking();
}

void WeightProcessor::setTareComplete(bool complete)
{
    m_tareComplete = complete;
    if (complete) {
        // Confirm tare clears any pending oscillation recovery — ensures SAW is
        // re-armed if called mid-shot (e.g. physical scale reconnects after a BLE drop).
        m_oscillationDetected = false;
        m_settleCount = 0;
        m_hasLastWeight = false;  // Tare shifts weight baseline — accept first post-tare reading
    }
}

void WeightProcessor::startExtraction()
{
    m_active = true;
    m_stopTriggered = false;
    m_extractionStartTime = 0;  // Set later by markExtractionStart() when flow actually begins
    m_frameWeightSkipSent.clear();
    m_stepExitArbiter.reset();
    m_weightSamples.clear();

    m_lastRawWeight = 0;
    m_hasLastWeight = false;
    m_consecutiveRejections = 0;
    m_awaitingTare = true;  // Cleared when the scale is seen to reach zero
    m_tareLandedSamples = 0;
    m_currentFrame = -1;
    m_tareComplete = false;
    m_oscillationDetected = false;
    m_settleCount = 0;
    m_lastTareWarnMs = 0;
    m_lastLowFlowLogMs = 0;
    m_lastConstantSampleLogMs = 0;
    m_flowBecameValidLogged = false;
    m_untaredCupSignalled = false;
    m_lastWallClockMs = 0;
    m_lastSampleTs = 0;
    m_uncalibratedBatchWarned = false;
    resetStallTracking();
    // Keep m_estimatedIntervalMs — it calibrates across shots for the same scale
}

void WeightProcessor::markExtractionStart()
{
    if (!m_active || m_extractionStartTime != 0) return;
    m_extractionStartTime = m_wallClock();
    // Flow has begun, so whatever the scale reads now is its post-tare zero even if
    // it never passed through the near-zero window (an untared cup left on the
    // platter, say). Arm the spike filter unconditionally here.
    //
    // Deliberately NOT warned about. An earlier revision logged when m_awaitingTare
    // was still set here, on the theory that reaching flow without an observed tare is
    // abnormal. There is no evidence for that theory and good reason to doubt it: the
    // near-zero window is 5 g, and across the sixteen supported scale types any scale
    // that settles a little above it after a tare would warn on every single shot. The
    // condition is also already surfaced properly — untaredCupDetected() carries it to
    // the UI — and by the next line the app has recovered, so per-frame exits work from
    // flow start regardless. A log line here would be noise on a handled path.
    //
    // The SAW stop of #610 cannot fire before this point regardless — it requires
    // extractionElapsed > 5 s, and extractionElapsed is 0 while m_extractionStartTime
    // is 0 — so the hold-off costs nothing there. It is the per-frame weight exit that
    // needed protecting in the meantime, and that has its own !m_awaitingTare gate.
    m_awaitingTare = false;
}

void WeightProcessor::endShotCycle()
{
    // Disarm on espresso-cycle EXIT, whether or not flow ever happened.
    // stopExtraction() cannot cover this: it hangs off shotEnded, which is
    // gated on flow having STARTED, while startExtraction() arms at cycle
    // start (during preheat). So a cycle aborted before flow — stop tapped,
    // machine aborts, BLE drops — left SAW armed against the dead shot's
    // target, re-checking every weight sample until the next shot re-armed
    // it. Same signal asymmetry that leaked the yield latch, same fix.
    // On a normal shot stopExtraction has already run and this is a no-op;
    // deliberately NOT stopExtraction itself, whose scale-rate diagnostic
    // would then log twice per shot (and which hot water/flush still reach
    // via shotEnded without ever arming).
    m_active = false;
}

void WeightProcessor::stopExtraction()
{
    m_active = false;

    // Log measured scale reporting rate and de-jitter state — captured in shot debug log.
    // Helps diagnose SAW issues on slow-reporting scales (Bookoo ~2Hz, etc.).
    if (m_weightSamples.size() >= 3) {
        qint64 span = m_weightSamples.last().timestamp - m_weightSamples.first().timestamp;
        if (span > 0) {
            double avgIntervalMs = span / static_cast<double>(m_weightSamples.size() - 1);
            SCALEFEED_LOG(QStringLiteral("Scale interval: avg %1 ms (%2 Hz) over %3 samples "
                                         "(last 1s) | de-jitter calibrated: %4 ms")
                              .arg(static_cast<int>(avgIntervalMs))
                              .arg(1000.0 / avgIntervalMs, 0, 'f', 1)
                              .arg(m_weightSamples.size()).arg(m_estimatedIntervalMs));
        }
    }

    // Don't clear weight samples — logging block above reads them for rate diagnostics,
    // and settling still needs them for LSLR-based flow rate computation.
}

void WeightProcessor::resetForRetare()
{
    m_weightSamples.clear();

    m_lastRawWeight = 0;
    m_hasLastWeight = false;
    m_consecutiveRejections = 0;
    m_awaitingTare = true;  // Retare moves the zero point again — same reasoning
    m_tareLandedSamples = 0;
    m_extractionStartTime = 0;  // Will be set when extraction actually starts
    m_stopTriggered = false;
    m_frameWeightSkipSent.clear();
    m_stepExitArbiter.reset();
    m_lastWallClockMs = 0;
    m_lastSampleTs = 0;
    resetStallTracking();
    m_uncalibratedBatchWarned = false;  // Timestamps cleared — de-jitter needs recalibration
    m_lastTareWarnMs = 0;
    m_lastLowFlowLogMs = 0;
    m_lastConstantSampleLogMs = 0;
    m_flowBecameValidLogged = false;
    SAWW_LOG(QStringLiteral("Reset for auto-retare"));
}

double WeightProcessor::computeLSLR(int windowMs) const
{
    if (m_weightSamples.size() < 2) return 0.0;

    qint64 now = m_weightSamples.last().timestamp;
    qint64 cutoff = now - windowMs;

    // Find start of window
    qsizetype startIdx = m_weightSamples.size() - 1;
    while (startIdx > 0 && m_weightSamples[startIdx - 1].timestamp >= cutoff) {
        --startIdx;
    }

    qsizetype n = m_weightSamples.size() - startIdx;
    if (n < 2) return 0.0;

    double dt = (m_weightSamples.last().timestamp - m_weightSamples[startIdx].timestamp) / 1000.0;
    if (dt < (windowMs * 0.65 / 1000.0)) return 0.0;  // Wait until window is ~65% full

    // Least-squares linear regression: fits w = slope*t + intercept
    // slope = flow rate in g/s. Uses all samples in the window, averaging
    // out noise from scale quantization and BLE timing jitter.
    qint64 t0 = m_weightSamples[startIdx].timestamp;
    double sumT = 0, sumW = 0, sumTW = 0, sumTT = 0;
    for (qsizetype i = startIdx; i < m_weightSamples.size(); ++i) {
        double t = (m_weightSamples[i].timestamp - t0) / 1000.0;
        sumT += t;
        sumW += m_weightSamples[i].weight;
        sumTW += t * m_weightSamples[i].weight;
        sumTT += t * t;
    }
    double denom = n * sumTT - sumT * sumT;
    double slope = (denom > 1e-12) ? (n * sumTW - sumT * sumW) / denom : 0.0;

    return qMax(0.0, slope);
}

double WeightProcessor::getExpectedDrip(double currentFlowRate) const
{
    // Uses snapshot of SAW learning data taken at configure() time.
    // Math is shared with Settings::getExpectedDrip[For] via SawPrediction::
    // weightedDripPrediction so σ and the kernel stay in lockstep.
    if (m_learningDrips.isEmpty()) {
        // No learning data — use scale-specific sensor lag as first-shot default.
        // Matches de1app: flow × (sensor_lag + 0.1s DE1 machine lag), capped at 8g.
        return qMin(currentFlowRate * (m_sensorLagSeconds + 0.1), 8.0);
    }

    const qsizetype maxEntries = m_sawConverged ? 12 : 8;
    const double recencyMax = 10.0;
    const double recencyMin = m_sawConverged ? 3.0 : 1.0;

    const qsizetype count = qMin(m_learningDrips.size(), maxEntries);
    QVector<double> drips = m_learningDrips.mid(0, count);
    QVector<double> flows = m_learningFlows.mid(0, count);

    const double prediction = SawPrediction::weightedDripPrediction(
        drips, flows, currentFlowRate, recencyMax, recencyMin);

    if (qIsNaN(prediction)) {
        // Total weight under the kMinTotalWeight floor → every entry's flow is
        // far from currentFlowRate. Fall back to the sensor-lag default.
        return qMin(currentFlowRate * (m_sensorLagSeconds + 0.1), 8.0);
    }
    return prediction;
}
