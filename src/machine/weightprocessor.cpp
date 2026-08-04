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
    // the calibrated scale interval, so this adapts to any scale rate (10Hz, 5Hz, 2Hz).
    constexpr qint64 kBatchThresholdMs = 20;    // Below this, events are batched
    constexpr qint64 kReconnectGapMs = 2000;    // Above this, ignore as reconnect
    constexpr qint64 kRateWindowMs = 1000;      // Arrival-rate calibration window

    qint64 sinceLast = m_lastWallClockMs > 0 ? (wallClock - m_lastWallClockMs) : -1;
    m_lastWallClockMs = wallClock;

    // Calibrate the interval from the ARRIVAL RATE, counting every arrival.
    //
    // This used to EMA `sinceLast` over non-batched gaps only, and on a bursty
    // transport those gaps are the INTER-BURST gaps — nothing like the scale's
    // cadence. A 10 Hz feed delivering 5-frame bursts every 500 ms calibrated to
    // ~496 ms instead of 100 ms, so the synthetic clock advanced ~5x faster than
    // real time, ran past `maxAhead` below, and pinned frame after frame to the
    // same clamped value: one burst stamped [0, 496, 992, 992, 992]. Three samples
    // sharing a timestamp contribute no span, computeLSLR's fill gate refused to
    // fit, flowRateShort read exactly 0.00, and SAW was blind mid-pour — measured
    // at ~175 ms and 0.36 g past its own threshold on a Half Decent WiFi scale.
    //
    // Counting arrivals over a wall-clock window is immune to how they are bunched:
    // 10 arrivals in 1000 ms is 100 ms/sample whether they came evenly or all at
    // once. The old EMA is kept below as the bootstrap for the first window, since
    // this one cannot report until kRateWindowMs of wall time has passed.
    if (m_rateWindowStartMs == 0 || sinceLast > kReconnectGapMs) {
        // Measurements from before a reconnect describe a different stream.
        // Through the helper, never field-by-field: m_rateRecentCount is a FILL
        // count that the minimum loop below uses as an index bound, which is only
        // valid while writes restart at zero. Clearing the count here and leaving
        // m_rateRecentNext where it was is a two-line edit that reads correct and
        // is not — the next window writes at the stale index, the loop reads
        // [0, count) and so reads the pre-reconnect value it was just told to
        // discard while never reading the fresh one. Two windows, ~2 s, of the
        // wrong cadence, arriving exactly when a feed has just come back.
        resetRateCalibration(wallClock);
    }
    ++m_rateWindowCount;
    const qint64 rateSpanMs = wallClock - m_rateWindowStartMs;
    if (rateSpanMs >= kRateWindowMs && m_rateWindowCount >= 2) {
        // count-1 intervals span the window. qMax guards a pathological burst that
        // puts every arrival on one wall-clock millisecond: a 0 interval would make
        // every synthetic timestamp identical, the exact failure being fixed here.
        const int measured =
            qMax(1, static_cast<int>(rateSpanMs / (m_rateWindowCount - 1)));
        // MINIMUM of the last few windows, not an average and not the latest.
        //
        // Contamination here is one-directional, which is what makes the minimum
        // the right statistic rather than merely a robust-looking one: a dropped
        // frame or a sub-reconnect hiccup removes arrivals from a window and can
        // only ever push `measured` UP. Nothing pushes it down — no mechanism
        // delivers more samples than the scale sent. So the smallest recent window
        // is the one least contaminated, and taking it discards a bad window
        // outright instead of blending its error in.
        //
        // The alternative tried first was rejecting the contaminated GAP before it
        // reached the average, by restarting the window on anything more than a few
        // times the cadence. That cannot work on a bunching transport: the
        // inter-burst gap (~490 ms against a 100 ms cadence) is itself many times
        // the cadence, so the window would restart on every burst and never close.
        // Averaging ACROSS bursts is the whole point; the gap is signal, not noise.
        //
        // Simulated on a 10 Hz feed with one 1.5 s hiccup: an EMA pulled the
        // estimate to 145 ms and ~1 s of pour read 1.38-1.53 g/s against a true
        // 2.00; the minimum holds 100 ms and 2.00 g/s throughout.
        //
        // A genuine rate change still lands, ASYMMETRICALLY, which is worth stating
        // because the obvious reading is wrong: a scale speeding UP takes effect on
        // the very next closed window, since a smaller value wins the minimum
        // immediately. Only a slowdown waits for the smaller stale entries to age
        // out, i.e. up to kRateRecentWindows windows. That is the safe way round —
        // an interval briefly too SMALL leaves synthetic behind wall-clock, where
        // the non-batched branch corrects it, while one too large is what builds a
        // runaway lead.
        m_rateRecent[m_rateRecentNext] = measured;
        m_rateRecentNext = (m_rateRecentNext + 1) % kRateRecentWindows;
        if (m_rateRecentCount < kRateRecentWindows) ++m_rateRecentCount;
        int lowest = m_rateRecent[0];
        for (int i = 1; i < m_rateRecentCount; ++i) {
            lowest = qMin(lowest, m_rateRecent[i]);
        }
        m_estimatedIntervalMs = lowest;
        m_rateWindowStartMs = wallClock;
        m_rateWindowCount = 1;
    }

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
        SCALEFEED_LOG(
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

    // How many arrivals deep we are into the current burst. Resets on the first
    // non-batched arrival, so it measures THIS burst and cannot ratchet across
    // bursts — which is what keeps the lead allowance below from growing without
    // bound. Counted before the timestamp is assigned because the allowance is an
    // input to that assignment.
    if (sinceLast >= 0 && sinceLast <= kBatchThresholdMs) {
        ++m_burstFrames;
    } else {
        m_burstFrames = 1;
    }

    qint64 sampleTs;
    if (sinceLast < 0 || sinceLast > kBatchThresholdMs) {
        // First call or non-batched: use wall-clock as ground truth.
        // A previous batch may have pushed synthetic timestamps ahead of wall-clock.
        // Instead of clamping by a full interval (which causes runaway drift — each
        // clamp pushes synthetic further ahead, so subsequent events also get clamped),
        // use wall-clock directly and only enforce minimal monotonicity (+1ms).
        // This lets synthetic reconverge with wall-clock within a few samples.
        sampleTs = wallClock;
        if (m_lastSampleTs > 0 && sampleTs <= m_lastSampleTs) {
            // Advance a full CADENCE step, not +1 ms. Synthetic sitting ahead of
            // wall-clock is the normal resting state on a bursty transport, not
            // drift to be squeezed out: a burst hands over a whole period of sample
            // time at one instant of arrival time, so the lead is real and constant.
            // The old +1 ms treated it as an error to unwind, and since the first
            // frame of every burst lands here, one frame per burst advanced 1 ms
            // while its weight advanced a full step — a ~200 g/s spike injected into
            // the LSLR fit once per burst. Simulated against this file's own
            // arithmetic, a true 2.00 g/s pour read 2.25-2.59 g/s with the +1 ms and
            // exactly 2.00 g/s with the cadence step.
            //
            // Nothing reconverges downward any more, and nothing needs to: the
            // interval is measured from arrival RATE, so synthetic advances at real
            // time's pace by construction and the lead cannot grow. It is bounded
            // regardless by the lead cap below.
            //
            // `<=`, not `<`: an equal timestamp is a duplicate, and duplicates are
            // the defect this whole block exists to stop producing.
            sampleTs = m_lastSampleTs + (m_estimatedIntervalMs > 0 ? m_estimatedIntervalMs : 1);
        }
        // NO bootstrap seed here, deliberately. There was one — "first estimate,
        // then the arrival-rate window owns it" — and it was worse than having no
        // estimate at all.
        //
        // On a bursty feed every gap reaching this line is an INTER-BURST gap, so
        // the seed is biased by exactly the factor the rate window exists to
        // remove: ~492 ms for a 100 ms cadence. The next burst then spreads at
        // 492 ms a frame, and the lead allowance below cannot stop it because that
        // allowance is computed from the same wrong interval and inflates in
        // lockstep. Simulated: synthetic ran 1960 ms ahead inside one 8 ms burst,
        // and when the rate window then corrected the interval to 100 ms the
        // allowance collapsed to 700 ms, the cap bit on every frame, and four
        // consecutive samples landed 1 ms apart reading 0.00 g/s — the precise
        // failure this change exists to remove, produced by its own warm-up.
        //
        // With no seed, m_estimatedIntervalMs stays 0 until the first rate window
        // closes and batched frames take the uncalibrated branch below, which
        // stamps wall-clock. That is also wrong for under a second, but it is
        // BOUNDED and self-clearing: no lead accumulates, so nothing is still
        // unwinding after the estimate becomes correct. Measured over the same
        // simulated feed, dropping the seed took post-warm-up bad samples from 9
        // to 0 and the settled lead from 1000 ms to 392 ms.
        //
        // Both windows sit inside SAW's own 5 s suppression, so neither reaches a
        // stop decision; the difference is what the live flow readout and the
        // settling logic see.
    } else if (m_estimatedIntervalMs > 0) {
        // Batched (< 20ms since last) and calibrated: spread using estimated interval.
        //
        // The lead allowance is the LSLR buffer length, not 2 intervals as it was.
        // A burst is a whole cadence period of SAMPLE time delivered at one instant
        // of ARRIVAL time, so synthetic leading wall-clock is the model working, not
        // drifting: five 100 ms samples handed over together need 400 ms of lead to
        // land on the instants they were actually taken at. Allowing only two
        // intervals crushed a five-frame burst into 200 ms and left the tail of it
        // pinned — a compressed span reaching LSLR as a stalled feed.
        //
        // The allowance TRACKS THE BURST rather than being a flat ceiling, because a
        // flat one silently caps burst size: a burst of N frames needs (N-1)
        // intervals of lead, so any fixed value is a limit on N that nothing states
        // and nothing reports. Swept in simulation, a flat 1000 ms was exact up to
        // 11 frames at 10 Hz and then fell off a cliff at 14 — to 0.00 g/s, the
        // identical symptom this change exists to remove. A ceiling whose breach
        // reproduces the bug is not a safety net.
        //
        // m_burstFrames counts the arrivals in the current burst, so the allowance
        // grows exactly as far as the burst in hand requires, +2 intervals of slack
        // for the next frames. The 1000 ms floor keeps it from tightening below what
        // a short burst on a slow scale needs.
        //
        // What still bounds it: the burst counter resets on the first non-batched
        // arrival, so the allowance cannot ratchet up across bursts, and a stream
        // whose gaps exceed kReconnectGapMs restarts calibration entirely. Swept in
        // a development-time simulation of this function's arithmetic to 20 frames
        // at 10 Hz and 8 at 5 Hz — NOT ctest coverage, which reaches 14 frames at
        // 10 Hz (longBurstDoesNotCapTheLeadAllowance). The one shape still wrong is 2 Hz in
        // 5-frame bursts, whose 2.5 s inter-burst silence is longer than
        // kReconnectGapMs AND kScaleStaleMs — a feed the app already calls stalled,
        // so it is out of scope here rather than unhandled.
        const qint64 leadAllowanceMs =
            qMax(qint64(1000),
                 static_cast<qint64>(m_burstFrames + 2) * m_estimatedIntervalMs);
        sampleTs = m_lastSampleTs + m_estimatedIntervalMs;
        qint64 maxAhead = wallClock + leadAllowanceMs;
        if (sampleTs > maxAhead) {
            // STRICTLY monotonic, even when the cap bites. Pinning to `maxAhead`
            // outright was the collapse: once m_lastSampleTs reached the cap, every
            // later frame in the burst computed the same clamped value, and the
            // guard below tested `<`, not `<=`, so nothing separated them. Runs of
            // identical timestamps followed — many samples, no span, and LSLR's
            // fill gate zeroing flowRateShort with SAW mid-pour.
            //
            // With the arrival-rate calibration above this branch should now be
            // unreachable in steady state (synthetic no longer outruns wall-clock
            // systematically). It is kept as the backstop for a genuinely late
            // burst, where +1 ms is the honest statement: we know the ordering, we
            // do not know the spacing, and a degenerate dt is the caller's to
            // reject rather than ours to invent.
            sampleTs = qMax(maxAhead, m_lastSampleTs + 1);
        }
    } else {
        // Batched but uncalibrated: use wall-clock (LSLR may see dt≈0 until calibrated)
        sampleTs = wallClock;
        if (m_active && !m_uncalibratedBatchWarned) {
            // Reworded with the arrival-rate calibration: this used to say "until a
            // non-batched gap is observed", which described the old gaps-only
            // estimator. Every arrival now feeds the window, batched or not, so a
            // feed that never produces a non-batched gap still calibrates — the
            // wait is for wall-clock, not for a particular kind of arrival.
            SAWW_WARN(QStringLiteral("De-jitter: batched event before calibration — LSLR may "
                                     "return 0 until the first arrival-rate window closes "
                                     "(~1 s of samples)"));
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
            qsizetype shortN = 0;
            if (m_weightSamples.size() >= 2) {
                qint64 cutoff = m_weightSamples.last().timestamp - shortWindowMs;
                qsizetype si = m_weightSamples.size() - 1;
                while (si > 0 && m_weightSamples[si - 1].timestamp >= cutoff) --si;
                shortDt = (m_weightSamples.last().timestamp - m_weightSamples[si].timestamp) / 1000.0;
                shortN = m_weightSamples.size() - si;
            }
            // shortN and interval are what make this line diagnostic rather than
            // merely alarming. `samples` counts the whole 1 s buffer, so a short
            // `shortDt` beside a healthy `samples` is ambiguous — it reads the same
            // whether timestamps collapsed onto each other or the feed genuinely
            // went quiet. shortN separates them: many samples in a short span means
            // collapse (a de-jitter fault, ours), few means a delivery gap (the
            // transport's). `interval` says which cadence the de-jitter believes it
            // is spreading to, which is the input that was wrong when this was
            // found. Reading a field log without these two cost a round trip.
            SAWW_LOG(QStringLiteral("Flow too low for stop-at-weight check: flowShort=%1 g/s "
                                    "weight=%2 g target=%3 g samples=%4 shortN=%5 "
                                    "shortWindow=%6 ms shortDt=%7 s gate=%8 s interval=%9 ms")
                         .arg(flowRateShort, 0, 'f', 2).arg(weight, 0, 'f', 2)
                         .arg(m_targetWeight, 0, 'f', 2).arg(m_weightSamples.size())
                         .arg(shortN)
                         .arg(shortWindowMs).arg(shortDt, 0, 'f', 3)
                         .arg(shortWindowMs * 0.65 / 1000.0, 0, 'f', 3)
                         .arg(m_estimatedIntervalMs));
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

void WeightProcessor::resetRateCalibration(qint64 windowStartMs)
{
    // All five move together — see the header. m_rateRecent[]'s CONTENTS are left
    // alone on purpose: m_rateRecentCount is what makes an entry readable, and with
    // the write index back at zero no stale slot is inside [0, count) any more.
    m_rateWindowStartMs = windowStartMs;
    m_rateWindowCount = 0;
    m_rateRecentCount = 0;
    m_rateRecentNext = 0;
    m_burstFrames = 0;
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
    // Drop the arrival-rate window with the clocks it is measured against. Left
    // standing, its start time would be an arbitrary distance in the past and the
    // first window to close after the reset would divide that whole idle gap by a
    // handful of arrivals — a wildly inflated interval, which is the same failure
    // this calibration was written to remove.
    resetRateCalibration();
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
    resetRateCalibration();
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
