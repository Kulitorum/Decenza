#include <QtTest>

#include "ai/shotanalysis.h"
#include "history/shothistorystorage.h"
#include "history/shotbadgeprojection.h"

class tst_ShotAnalysis : public QObject {
    Q_OBJECT

private:
    static HistoryPhaseMarker phase(double time, const QString& label, int frameNumber,
                                     bool isFlowMode = false,
                                     const QString& transitionReason = QString())
    {
        HistoryPhaseMarker marker;
        marker.time = time;
        marker.label = label;
        marker.frameNumber = frameNumber;
        marker.isFlowMode = isFlowMode;
        marker.transitionReason = transitionReason;
        return marker;
    }

    // Build a flat-value (time, value) series sampled at `rate` Hz across [t0, t1].
    static QVector<QPointF> flatSeries(double t0, double t1, double value, double rate = 10.0)
    {
        QVector<QPointF> pts;
        const double dt = 1.0 / rate;
        for (double t = t0; t <= t1 + 1e-9; t += dt) pts.append(QPointF(t, value));
        return pts;
    }

    // Build a ramp (t0,v0) → (t1,v1) series at `rate` Hz.
    static QVector<QPointF> rampSeries(double t0, double t1, double v0, double v1,
                                         double rate = 10.0)
    {
        QVector<QPointF> pts;
        const double dt = 1.0 / rate;
        const double span = t1 - t0;
        for (double t = t0; t <= t1 + 1e-9; t += dt) {
            const double alpha = span > 0 ? (t - t0) / span : 0.0;
            pts.append(QPointF(t, v0 + alpha * (v1 - v0)));
        }
        return pts;
    }

    // Concatenate contiguous series (assumes second starts after first ends).
    static QVector<QPointF> concat(QVector<QPointF> a, const QVector<QPointF>& b)
    {
        a.reserve(a.size() + b.size());
        a.append(b);
        return a;
    }

    static void expectSkipDetection(const QList<HistoryPhaseMarker>& phases,
                                    int expectedFrameCount,
                                    bool expected)
    {
        QCOMPARE(ShotAnalysis::detectSkipFirstFrame(phases, expectedFrameCount), expected);
    }

    static void expectSkipDetection(const QList<HistoryPhaseMarker>& phases,
                                    int expectedFrameCount,
                                    double firstFrameConfiguredSeconds,
                                    bool expected)
    {
        QCOMPARE(ShotAnalysis::detectSkipFirstFrame(phases, expectedFrameCount,
                                                     firstFrameConfiguredSeconds),
                 expected);
    }

private slots:
    void skipFirstFrameDetection()
    {
        expectSkipDetection({}, -1, false);

        expectSkipDetection({
            phase(0.0, "Start", 0),
            phase(0.0, "Fill", 0),
            phase(2.3, "Pour", 1),
        }, 3, false);

        expectSkipDetection({
            phase(0.0, "Start", 0),
            phase(0.0, "Fill", 0),
            phase(1.4, "Pour", 1),
        }, 3, true);

        expectSkipDetection({
            phase(0.0, "Start", 0),
            phase(0.2, "Pour", 1),
        }, 3, true);

        expectSkipDetection({
            phase(0.0, "Start", 0),
            phase(2.0, "Pour", 1),
        }, 3, false);

        expectSkipDetection({
            phase(0.0, "Start", 0),
            phase(0.2, "Pour", 1),
        }, 1, false);

        // expectedFrameCount == 0: also suppresses (< 2 frames, no skip possible)
        expectSkipDetection({
            phase(0.0, "Start", 0),
            phase(0.2, "Pour", 1),
        }, 0, false);

        // FW bug: machine never executed frame 0 — first marker arrives directly at frame 1.
        // No "Start" or frame-0 entry at all. Mirrors the Tcl plugin's skipped_first_step_FW
        // case where step1_registered is never set before a non-zero frame is seen.
        expectSkipDetection({
            phase(0.0, "Pour", 1),
        }, 3, true);

        // FW bug with no expectedFrameCount (default -1, unknown profile)
        expectSkipDetection({
            phase(0.0, "Pour", 1),
        }, -1, true);

        // --- Configured-first-frame-seconds path (option 1 fix) ---

        // 80's Espresso shot 889 regression: profile configures frame[0].seconds = 2,
        // BLE jitter delivers the frame-1 marker at 1.872 s (94 % of plan). The
        // legacy hard 2 s window flagged this; with configured seconds known
        // the cutoff is min(2.0, 0.5*2.0) = 1.0 s, so 1.872 must NOT flag.
        expectSkipDetection({
            phase(0.0, "Start", 0),
            phase(0.0, "preinfusion start", 0),
            phase(1.872, "preinfusion", 1),
        }, 4, /*firstFrameConfiguredSeconds=*/2.0, false);

        // Same profile, frame 0 actually skipped (e.g., transition at 0.3 s):
        // 0.3 < 1.0 s cutoff → flag.
        expectSkipDetection({
            phase(0.0, "Start", 0),
            phase(0.0, "preinfusion start", 0),
            phase(0.3, "preinfusion", 1),
        }, 4, /*firstFrameConfiguredSeconds=*/2.0, true);

        // Profile with a deliberately short first step (0.5 s configured): a
        // 0.4 s actual is 80 % of plan, not "skipped" — must NOT flag.
        expectSkipDetection({
            phase(0.0, "Start", 0),
            phase(0.0, "Fill", 0),
            phase(0.4, "Pour", 1),
        }, 3, /*firstFrameConfiguredSeconds=*/0.5, false);

        // Same short-first-step profile but actual is even shorter (0.2 s on
        // a 0.5 s plan = 40 %): cutoff = 0.25 s, 0.2 < 0.25 → flag.
        expectSkipDetection({
            phase(0.0, "Start", 0),
            phase(0.0, "Fill", 0),
            phase(0.2, "Pour", 1),
        }, 3, /*firstFrameConfiguredSeconds=*/0.5, true);

        // Long configured first frame (4 s): cutoff capped at 2 s. A 2.5 s
        // actual (early exit, e.g., pressure-over) → no flag.
        expectSkipDetection({
            phase(0.0, "Start", 0),
            phase(0.0, "Fill", 0),
            phase(2.5, "Pour", 1),
        }, 3, /*firstFrameConfiguredSeconds=*/4.0, false);

        // Long configured first frame, actual under the 2 s cap → flag.
        expectSkipDetection({
            phase(0.0, "Start", 0),
            phase(0.0, "Fill", 0),
            phase(1.5, "Pour", 1),
        }, 3, /*firstFrameConfiguredSeconds=*/4.0, true);

        // Firmware bug (no frame 0 marker) still flags regardless of
        // configured seconds — frame 0 was genuinely never executed.
        expectSkipDetection({
            phase(0.0, "Pour", 1),
        }, 3, /*firstFrameConfiguredSeconds=*/2.0, true);
    }

    // buildChannelingWindows ---------------------------------------------

    // A pressure-mode phase with flat plateau → steep ramp → flat plateau
    // should produce windows that include the flat regions and exclude the
    // ramp. Mirrors the lever family's "ramp between stationary targets"
    // signature, where only the stationary plateaus are reliable for dC/dt
    // analysis.
    void channelingWindows_excludesRampBetweenPlateaus()
    {
        QList<HistoryPhaseMarker> phases{
            phase(3.0, "Pour", 0, /*isFlowMode=*/false),
        };
        // Goal: flat 8 from 3→10s, ramp 8→5 from 10→11.5s (2 bar/s),
        // flat 5 from 11.5→20s. Actual tracks goal (converged).
        // Late plateau at 5 bar (> WINDOW_MIN_EXTRACTION_BAR = 4.5) so the
        // soak-exclusion gate doesn't suppress it — the test exercises ramp
        // exclusion behaviour, not low-pressure soak exclusion.
        QVector<QPointF> pressureGoal;
        pressureGoal = concat(pressureGoal, flatSeries(3.0, 10.0 - 0.1, 8.0));
        pressureGoal = concat(pressureGoal, rampSeries(10.0, 11.5, 8.0, 5.0));
        pressureGoal = concat(pressureGoal, flatSeries(11.5 + 0.1, 20.0, 5.0));
        auto pressure = pressureGoal;  // actual tracks goal exactly

        auto flow = flatSeries(3.0, 20.0, 1.0);
        QVector<QPointF> flowGoal;

        const auto windows = ShotAnalysis::buildChannelingWindows(
            pressure, flow, pressureGoal, flowGoal, phases,
            /*pourStart=*/5.0, /*pourEnd=*/18.0);

        QVERIFY2(!windows.isEmpty(), "expected plateau windows");

        // The ramp itself (10–11.5 s) must be excluded from every window. At
        // 2 bar/s the ±0.75 s stationarity fringe around the ramp start is
        // already above 15 %, so no early fringe window bleeds into the ramp.
        // The fringe at the ramp end (t ≈ 11.9 s) starts at w.start ≥ 11.5
        // and is therefore not counted as intersecting the ramp core.
        for (const auto& w : windows) {
            const bool intersectsRampCore = !(w.end <= 10.0 || w.start >= 11.5);
            QVERIFY2(!intersectsRampCore,
                     qPrintable(QString("window [%1, %2] intersects ramp core 10–11.5 s")
                                    .arg(w.start).arg(w.end)));
        }

        // At least one window should lie on each flat plateau.
        bool sawEarlyPlateau = false, sawLatePlateau = false;
        for (const auto& w : windows) {
            if (w.start >= 7.0 && w.end <= 10.0) sawEarlyPlateau = true;
            if (w.start <= 14.0 && w.end >= 14.0) sawLatePlateau = true;  // window spans t=14 s (well into late plateau)
        }
        QVERIFY2(sawEarlyPlateau, "expected a window inside the 7–10 s plateau");
        QVERIFY2(sawLatePlateau, "expected a window over the late plateau (containing t=14 s)");
    }

    // A flow-mode phase with a stationary flow goal where actual flow is
    // converged onto it should yield a single inclusion window covering the
    // converged region. Series span 2 s beyond the pour bounds on each side
    // so the ±0.75 s stationarity lookup doesn't fall off the edges.
    void channelingWindows_includesFlowModeStationary()
    {
        QList<HistoryPhaseMarker> phases{
            phase(3.0, "Pour", 0, /*isFlowMode=*/true),
        };
        auto flow = flatSeries(3.0, 17.0, 1.7);
        auto flowGoal = flatSeries(3.0, 17.0, 1.7);
        auto pressure = flatSeries(3.0, 17.0, 6.0);
        QVector<QPointF> pressureGoal;  // not relevant in flow mode

        const auto windows = ShotAnalysis::buildChannelingWindows(
            pressure, flow, pressureGoal, flowGoal, phases, /*pourStart=*/5.0, /*pourEnd=*/15.0);

        QVERIFY2(!windows.isEmpty(), "expected at least one inclusion window");
        // 2 s pour-start skip → window starts ≈ 7 s; 1.5 s pour-end skip →
        // window ends ≈ pourEnd - 1.5 = 13.5 s.
        QVERIFY2(windows.first().start >= 6.8 && windows.first().start <= 7.2,
                 qPrintable(QString("window start: %1").arg(windows.first().start)));
        QVERIFY2(windows.last().end >= 13.3 && windows.last().end <= 13.7,
                 qPrintable(QString("window end: %1").arg(windows.last().end)));
    }

    // When actual pressure has not converged onto goal (> 15% off), samples
    // are excluded even if the goal is stationary.
    void channelingWindows_excludesUnconvergedActual()
    {
        QList<HistoryPhaseMarker> phases{
            phase(10.0, "Hold", 0, /*isFlowMode=*/false),
        };
        auto pressureGoal = flatSeries(10.0, 20.0, 8.0);
        // Actual is at 5 bar — 37% off from 8 bar goal, above 15% tolerance.
        auto pressure = flatSeries(10.0, 20.0, 5.0);
        auto flow = flatSeries(10.0, 20.0, 1.0);
        QVector<QPointF> flowGoal;

        const auto windows = ShotAnalysis::buildChannelingWindows(
            pressure, flow, pressureGoal, flowGoal, phases, 10.0, 20.0);

        QVERIFY2(windows.isEmpty(),
                 "Unconverged actual should produce no inclusion windows");
    }

    // Legacy-data contract: when no phase markers are available (older
    // shots pre-phase-storage), the builder emits a single whole-pour
    // window so detectChannelingFromDerivative runs unrestricted. This
    // preserves detector coverage on historical shots instead of silently
    // disabling. Distinguishes from the "phases exist but no stationary
    // window qualifies" case, which returns empty (detector returns None).
    void channelingWindows_emptyPhasesFallsBackToWholePour()
    {
        QVector<QPointF> empty;
        QList<HistoryPhaseMarker> empty_phases;
        auto flow = flatSeries(0.0, 10.0, 1.0);
        auto pressure = flatSeries(0.0, 10.0, 6.0);

        auto windows = ShotAnalysis::buildChannelingWindows(
            pressure, flow, empty, empty, empty_phases, /*pourStart=*/2.0, /*pourEnd=*/10.0);
        QCOMPARE(windows.size(), 1);
        QCOMPARE(windows.first().start, 2.0);
        QCOMPARE(windows.first().end, 10.0);
    }

    // detectChannelingFromDerivative with empty windows now reports None
    // rather than falling back to unrestricted analysis. This is the
    // "phases exist but no stationary ranges" silent-pass contract.
    void channelingFromDerivative_emptyWindowsReturnsNone()
    {
        QVector<QPointF> dcdt;
        for (double t = 0.0; t <= 20.0; t += 0.1) dcdt.append(QPointF(t, 8.0));
        auto severity = ShotAnalysis::detectChannelingFromDerivative(
            dcdt, /*pourStart=*/2.0, /*pourEnd=*/20.0, /*windows=*/{});
        QCOMPARE(severity, ShotAnalysis::ChannelingSeverity::None);
    }

    // detectChannelingFromDerivative ----------------------------------------

    // When windows mask out a dC/dt spike, the detector must return None.
    // Without a mask (baseline whole-pour window) the same spike should
    // register as Sustained. Validates that mode-aware window exclusion is
    // what's doing the work, not some other property of the detector.
    void channelingFromDerivative_windowMaskSuppressesRampSpike()
    {
        // dC/dt series with a sustained high region during 10–15s (ramp)
        // and quiet elsewhere.
        QVector<QPointF> dcdt;
        for (double t = 0.0; t <= 25.0; t += 0.1) {
            double v = 0.2;
            if (t >= 10.0 && t <= 15.0) v = 8.0;  // 50 samples at |dC/dt|=8 > 3.0
            dcdt.append(QPointF(t, v));
        }

        // Unrestricted-equivalent: single whole-pour window → should flag
        // Sustained. (Empty-windows path now reports None — see
        // channelingFromDerivative_emptyWindowsReturnsNone.)
        {
            QVector<ShotAnalysis::DetectionWindow> fullPour{
                {2.0, 25.0},
            };
            auto severity = ShotAnalysis::detectChannelingFromDerivative(
                dcdt, /*pourStart=*/2.0, /*pourEnd=*/25.0, fullPour);
            QCOMPARE(severity, ShotAnalysis::ChannelingSeverity::Sustained);
        }

        // With a window that excludes 10–15s (the ramp), no elevated samples
        // in mask → None. Use a half-second buffer so the window edges don't
        // sit on elevated samples.
        {
            QVector<ShotAnalysis::DetectionWindow> windows{
                {4.0, 9.5},
                {15.5, 25.0},
            };
            auto severity = ShotAnalysis::detectChannelingFromDerivative(
                dcdt, /*pourStart=*/2.0, /*pourEnd=*/25.0, windows);
            QCOMPARE(severity, ShotAnalysis::ChannelingSeverity::None);
        }
    }

    // Lever-style false-positive suppression: in a flow-mode phase whose
    // flow goal is steady but actual pressure is rising fast (the pump is
    // building toward a pressure-ceiling exit condition), the window
    // builder must exclude those samples — the resulting dC/dt drop is the
    // pressure-rise dynamic, not channeling. Falling pressure (bloom
    // transitions, pump stops) must NOT be masked: those are legitimate
    // channeling-like transients.
    void channelingWindows_flowModeRisingPressure_isExcluded()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "Preinfusion", 0, /*isFlowMode=*/true),
            phase(10.0, "End", -1, /*isFlowMode=*/false),
        };

        // Flow tracks goal cleanly, but pressure ramps from 2 → 10 bar over
        // 4 s (≈ 2 bar/s — matches 80's Espresso preinfusion-late dynamic
        // before the pressure-ceiling exit). After 4 s pressure stays flat
        // at 10 bar so the test isolates the ramp effect.
        const auto flow = flatSeries(0.0, 10.0, 7.5);
        const auto flowGoal = flatSeries(0.0, 10.0, 7.5);
        QVector<QPointF> pressure;
        pressure.append(rampSeries(0.0, 4.0, 2.0, 10.0));
        for (const auto& p : flatSeries(4.1, 10.0, 10.0)) pressure.append(p);

        const auto windows = ShotAnalysis::buildChannelingWindows(
            pressure, flow, /*pressureGoal=*/QVector<QPointF>{},
            flowGoal, phases, /*pourStart=*/0.0, /*pourEnd=*/10.0);

        // The steep part of the ramp must be excluded; once the ±0.75 s
        // lookup spans the post-ramp plateau the relative pressure rise
        // drops below WINDOW_STATIONARY_REL and windows can re-open.
        // Empirically that crossover lands near t = 3.3 s, so any window
        // start before t = 3 indicates the gate stopped firing too early.
        QVERIFY2(!windows.isEmpty(), "expected at least one window during the plateau");
        QVERIFY2(windows.first().start >= 3.0,
                 qPrintable(QString("expected first window past the steep ramp, got start=%1s")
                                .arg(windows.first().start)));
    }

    // Inverse: in a flow-mode phase with falling pressure (the bloom
    // transition shape — pump stops while flow trails to zero), pressure
    // is changing rapidly but in the negative direction, so the lever-rise
    // gate must NOT engage. Validates that the directional rule preserves
    // the legacy detector's coverage of bloom-transition spikes.
    void channelingWindows_flowModeFallingPressure_isIncluded()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "Bloom", 0, /*isFlowMode=*/true),
            phase(10.0, "End", -1, /*isFlowMode=*/false),
        };

        // Same setup but pressure falls from 8 → 2 bar.
        const auto flow = flatSeries(0.0, 10.0, 1.5);
        const auto flowGoal = flatSeries(0.0, 10.0, 1.5);
        const auto pressure = rampSeries(0.0, 10.0, 8.0, 2.0);

        const auto windows = ShotAnalysis::buildChannelingWindows(
            pressure, flow, /*pressureGoal=*/QVector<QPointF>{},
            flowGoal, phases, /*pourStart=*/0.0, /*pourEnd=*/10.0);

        double totalMaskSec = 0.0;
        for (const auto& w : windows) totalMaskSec += (w.end - w.start);
        // Most of the analysis range should be in-window. CHANNELING_DC_POUR_SKIP_SEC
        // (2 s) and CHANNELING_DC_POUR_SKIP_END_SEC (1.5 s) are trimmed by
        // the window builder, leaving 10 - 2 - 1.5 = 6.5 s of analysis range.
        QVERIFY2(totalMaskSec > 5.0,
                 qPrintable(QString("expected ~6.5 s of windows during pressure fall, got %1s")
                                .arg(totalMaskSec)));
    }

    // analyzeFlowVsGoal / detectGrindIssue ----------------------------------

    // 80's Espresso style: pour frames are all pressure-controlled. No
    // flow-mode phase → grind check should report hasData=false and
    // detectGrindIssue should return false regardless of flow delta.
    void flowVsGoal_noFlowModePhaseInPour_returnsHasDataFalse()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "Preinfusion", 0, /*isFlowMode=*/true),   // before pour, excluded
            phase(5.0, "Rise", 1, /*isFlowMode=*/false),
            phase(10.0, "Decline", 2, /*isFlowMode=*/false),
        };
        // Actual flow 1 ml/s, goal 7.5 ml/s across the pour (10-30s).
        // Under legacy logic this would be a -6.5 ml/s delta → huge grind
        // caution. Under mode-aware logic this does not run.
        auto flow = flatSeries(5.0, 30.0, 1.0);
        auto flowGoal = flatSeries(5.0, 30.0, 7.5);

        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, 5.0, 30.0);
        QVERIFY(!r.hasData);
        QVERIFY(!r.skipped);

        QCOMPARE(ShotAnalysis::detectGrindIssue(flow, flowGoal, phases, 5.0, 30.0),
                 false);
    }

    // D-Flow style: pour is flow-controlled. Large delta should fire.
    void flowVsGoal_flowModePourWithDelta_fires()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "Preinfusion", 0, /*isFlowMode=*/true),
            phase(10.0, "Pour", 1, /*isFlowMode=*/true),
        };
        // Actual flow 0.8 ml/s, goal 1.7 ml/s → delta -0.9 ml/s, beyond
        // FLOW_DEVIATION_THRESHOLD (0.4).
        auto flow = flatSeries(10.0, 30.0, 0.8);
        auto flowGoal = flatSeries(10.0, 30.0, 1.7);

        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, 10.0, 30.0);
        QVERIFY(r.hasData);
        QVERIFY(r.delta < -0.4);

        QCOMPARE(ShotAnalysis::detectGrindIssue(flow, flowGoal, phases, 10.0, 30.0),
                 true);
    }

    // grind_check_skip flag short-circuits to skipped=true.
    void flowVsGoal_grindCheckSkipFlag_skips()
    {
        QList<HistoryPhaseMarker> phases{
            phase(10.0, "Pour", 0, /*isFlowMode=*/true),
        };
        auto flow = flatSeries(10.0, 30.0, 0.8);
        auto flowGoal = flatSeries(10.0, 30.0, 1.7);

        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, 10.0, 30.0, /*beverage=*/"", {"grind_check_skip"});
        QVERIFY(r.skipped);
        QVERIFY(!r.hasData);

        QCOMPARE(ShotAnalysis::detectGrindIssue(flow, flowGoal, phases,
                                                 10.0, 30.0, "", {"grind_check_skip"}),
                 false);
    }

    // 80's Espresso style: preinfusion is flow-mode 7.5 ml/s with a pressure
    // ceiling that exits the frame on "pressure" transition once the puck
    // builds. The trailing samples (pressure limiter engaged → controller
    // stops tracking flow goal) and the pump-ramp at the very start must be
    // excluded so the averaged flow matches the actual tracking window.
    void flowVsGoal_flowModeExitingOnPressure_trimsLimiterTailAndPumpRamp()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "Preinfusion", 0, /*isFlowMode=*/true),
            phase(7.0, "Rise", 1, /*isFlowMode=*/false, /*tr=*/"pressure"),
            phase(11.0, "End", -1, /*isFlowMode=*/false),
        };

        // Synthesize the lever preinfusion shape. Without the boundary trims
        // the average pulls under goal by ~0.6 ml/s and FIRES grind issue.
        // With both trims (skip 0-0.5 s ramp, skip 5.5-7 s limiter tail),
        // the steady tracking window from 0.5-5.5 s reads ~7.5 ml/s and
        // the check passes.
        QVector<QPointF> flow;
        QVector<QPointF> flowGoal;
        for (double t = 0.0; t <= 11.0; t += 0.1) {
            double f;
            if (t < 0.5)        f = 4.0 + (7.5 - 4.0) * (t / 0.5);  // pump ramp 4 → 7.5
            else if (t < 5.5)   f = 7.5;                             // steady tracking
            else if (t < 7.0)   f = 6.0;                             // pressure limiter engaging
            else                f = 4.5;                             // pressure-mode pour (excluded by isFlowMode)
            flow.append(QPointF(t, f));
            flowGoal.append(QPointF(t, 7.5));
        }

        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, /*pourStart=*/0.0, /*pourEnd=*/11.0);
        QVERIFY(r.hasData);
        QVERIFY2(std::abs(r.delta) < ShotAnalysis::FLOW_DEVIATION_THRESHOLD,
                 qPrintable(QString("expected |delta| < %1, got %2")
                                .arg(ShotAnalysis::FLOW_DEVIATION_THRESHOLD)
                                .arg(r.delta)));
        QCOMPARE(ShotAnalysis::detectGrindIssue(flow, flowGoal, phases, 0.0, 11.0),
                 false);
    }

    // Extreme puck failure: a flow-mode phase shorter than
    // (GRIND_PUMP_RAMP_SKIP_SEC + kMinPostTrimRangeSec) so the trim guard
    // must take its bypass branch. Without the bypass, the pump-ramp trim
    // (0.5 s) would push start past end (start=0.5, end=0.8 → 3 samples
    // after time-window filter, < 5 → hasData=false → grind silent on a
    // shot that's clearly a gusher). With the bypass, the full 0-0.8 s
    // window stays and the detector fires.
    void flowVsGoal_shortShot_skipsTrimToPreserveSignal()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "Fill", 0, /*isFlowMode=*/true),
            phase(0.8, "Pour", 1, /*isFlowMode=*/false),
            phase(2.0, "End", -1, /*isFlowMode=*/false),
        };
        // Flow gushes (6 ml/s) against goal 4 ml/s during the 0.8 s
        // flow-mode phase — clear coarse-grind signal. The phase length
        // (0.8 s) minus the trim (0.5 s) equals 0.3 s, well below the
        // 1.0 s post-trim minimum, so the bypass must engage.
        auto flow = flatSeries(0.0, 2.0, 6.0);
        auto flowGoal = flatSeries(0.0, 2.0, 4.0);

        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, /*pourStart=*/0.0, /*pourEnd=*/2.0);
        QVERIFY2(r.hasData,
                 "expected the trim bypass to preserve enough samples");
        QVERIFY2(r.delta > ShotAnalysis::FLOW_DEVIATION_THRESHOLD,
                 qPrintable(QString("expected positive delta > %1, got %2")
                                .arg(ShotAnalysis::FLOW_DEVIATION_THRESHOLD).arg(r.delta)));
        QCOMPARE(ShotAnalysis::detectGrindIssue(flow, flowGoal, phases, 0.0, 2.0),
                 true);
    }

    // The limiter-tail trim is gated on transitionReason == "pressure".
    // Same flow shape as the lever test but with the next phase exiting on
    // "time" — the trailing undershoot must remain in the average and the
    // badge must still fire. Without this gate, a copy-paste regression
    // that applied the trim unconditionally would silently suppress real
    // grind signals on D-Flow / A-Flow profiles whose pour phases exit on
    // time.
    void flowVsGoal_flowModeExitingOnTime_keepsTrailingWindow_andFires()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "Preinfusion", 0, /*isFlowMode=*/true),
            phase(7.0, "Rise", 1, /*isFlowMode=*/false, /*tr=*/"time"),
            phase(11.0, "End", -1, /*isFlowMode=*/false),
        };

        // Pump ramp 0-0.5 s, steady tracking 0.5-5.5 s, sustained dip
        // 5.5-7.0 s. Under "pressure" exit the dip is trimmed and the
        // average is clean; under "time" exit the dip stays in and pulls
        // the average ~0.9 ml/s under goal.
        QVector<QPointF> flow;
        QVector<QPointF> flowGoal;
        for (double t = 0.0; t <= 11.0; t += 0.1) {
            double f;
            if (t < 0.5)        f = 4.0 + (7.5 - 4.0) * (t / 0.5);
            else if (t < 5.5)   f = 7.5;
            else if (t < 7.0)   f = 3.5;
            else                f = 4.5;
            flow.append(QPointF(t, f));
            flowGoal.append(QPointF(t, 7.5));
        }

        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, /*pourStart=*/0.0, /*pourEnd=*/11.0);
        QVERIFY(r.hasData);
        QVERIFY2(r.delta < -ShotAnalysis::FLOW_DEVIATION_THRESHOLD,
                 qPrintable(QString("expected delta < -%1, got %2")
                                .arg(ShotAnalysis::FLOW_DEVIATION_THRESHOLD)
                                .arg(r.delta)));
        QCOMPARE(ShotAnalysis::detectGrindIssue(flow, flowGoal, phases, 0.0, 11.0),
                 true);
    }

    // Filter beverage type also short-circuits.
    void flowVsGoal_filterBeverage_skips()
    {
        QList<HistoryPhaseMarker> phases{
            phase(10.0, "Pour", 0, /*isFlowMode=*/true),
        };
        auto flow = flatSeries(10.0, 30.0, 0.8);
        auto flowGoal = flatSeries(10.0, 30.0, 1.7);

        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, 10.0, 30.0, /*beverage=*/"filter");
        QVERIFY(r.skipped);
    }

    // End-of-pour skip ------------------------------------------------------

    // Elevated samples inside the final CHANNELING_DC_POUR_SKIP_END_SEC
    // window must be excluded. Mirrors the existing 2 s start-skip.
    void channelingFromDerivative_endSkipSuppressesTailSpike()
    {
        // Long clean quiet dC/dt with a single burst of elevated samples in
        // the last 1 s of pour. Without end-skip the burst tallies; with
        // end-skip it's outside the analysis range.
        QVector<QPointF> dcdt;
        for (double t = 0.0; t <= 20.0; t += 0.1) {
            double v = 0.2;
            if (t >= 19.0) v = 8.0;  // tail burst in last second
            dcdt.append(QPointF(t, v));
        }
        QVector<ShotAnalysis::DetectionWindow> fullPour{{2.0, 20.0}};
        auto severity = ShotAnalysis::detectChannelingFromDerivative(
            dcdt, /*pourStart=*/0.0, /*pourEnd=*/20.0, fullPour);
        QCOMPARE(severity, ShotAnalysis::ChannelingSeverity::None);
    }

    // Choked-puck fallback (pressure-mode pours) ---------------------------

    // Shot 890 signature: 80's Espresso (entire pour pressure-controlled),
    // pressure held ~6.6 bar for 50 s with mean flow ~0.2 ml/s, yield 1.1 g.
    // The flow-vs-goal path returns no data (no flow-mode window in the
    // pour); the choked-puck fallback must fire and flag grindIssue.
    void grindIssue_chokedPuckPressureMode_fires()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0,  "preinfusion start", 0, /*isFlowMode=*/true),
            phase(2.0,  "preinfusion",       1, /*isFlowMode=*/true),
            phase(6.0,  "rise and hold",     2, /*isFlowMode=*/false),
            phase(10.0, "decline",           3, /*isFlowMode=*/false),
        };
        // Pressure builds during preinfusion, then sits at 6.6 bar through
        // the pressure-mode pour. Flow stays near zero from t=10 onward.
        QVector<QPointF> pressure;
        pressure = concat(pressure, rampSeries(0.0, 6.0, 0.5, 6.6));
        pressure = concat(pressure, flatSeries(6.1, 60.0, 6.6));

        QVector<QPointF> flow;
        flow = concat(flow, flatSeries(0.0, 6.0, 7.5));        // preinfusion
        flow = concat(flow, flatSeries(6.1, 10.0, 3.0));       // brief rise
        flow = concat(flow, flatSeries(10.1, 60.0, 0.2));      // choked

        QVector<QPointF> flowGoal = flatSeries(0.0, 60.0, 7.5);  // preinfusion goal only

        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, /*pourStart=*/6.0, /*pourEnd=*/60.0,
            /*beverageType=*/"", /*analysisFlags=*/{}, pressure);
        QVERIFY(r.hasData);
        QVERIFY(r.chokedPuck);
        QCOMPARE(ShotAnalysis::detectGrindIssue(
                     flow, flowGoal, phases, 6.0, 60.0,
                     "", {}, pressure), true);
    }

    // Lever-style clean shot (Cremina): pressure-mode pour, but flow is
    // healthy — must NOT fire the choked-puck fallback.
    void grindIssue_chokedPuckPressureMode_normalLeverDoesNotFire()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0,  "preinfusion", 0, /*isFlowMode=*/true),
            phase(5.0,  "pour",        1, /*isFlowMode=*/false),
        };
        auto pressure = concat(rampSeries(0.0, 5.0, 0.0, 8.0),
                                rampSeries(5.1, 30.0, 8.0, 4.0));
        // Mean flow ~1.8 ml/s during the pressurized pour: well above the
        // 0.5 ml/s choked threshold.
        auto flow = concat(flatSeries(0.0, 5.0, 4.0),
                            flatSeries(5.1, 30.0, 1.8));
        QVector<QPointF> flowGoal;  // no flow goal during the pour

        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, /*pourStart=*/5.0, /*pourEnd=*/30.0,
            "", {}, pressure);
        QVERIFY(!r.chokedPuck);
        QCOMPARE(ShotAnalysis::detectGrindIssue(
                     flow, flowGoal, phases, 5.0, 30.0,
                     "", {}, pressure), false);
    }

    // A flow-mode pour with healthy flow must not get tagged as choked,
    // even though the choked-puck check now runs additively on every pour.
    // With this fixture pressureModeRanges is empty (the only phase is
    // isFlowMode=true), so inPressureMode returns true for every sample
    // and the choked loop iterates the whole pour. The 1.7 ml/s flow is
    // above CHOKED_FLOW_MAX_MLPS = 0.5, so chokedPuck stays false. Locks
    // in the per-sample threshold gate as the real safeguard.
    void grindIssue_flowModePourDoesNotTriggerChokedCheck()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "Pour", 0, /*isFlowMode=*/true),
        };
        auto flow = flatSeries(0.0, 30.0, 1.7);
        auto flowGoal = flatSeries(0.0, 30.0, 1.7);
        auto pressure = flatSeries(0.0, 30.0, 6.0);

        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, /*pourStart=*/2.0, /*pourEnd=*/28.0,
            "", {}, pressure);
        QVERIFY(r.hasData);
        QVERIFY(!r.chokedPuck);
        QVERIFY(std::abs(r.delta) < ShotAnalysis::FLOW_DEVIATION_THRESHOLD);
    }

    // Yield-ratio arm: 80's Espresso-style signature — mean pressurized
    // flow ~0.6 ml/s (just over the 0.5 flow threshold), with yield well
    // below target. The flow arm doesn't fire; the yield arm catches this
    // as the same diagnosis (grind too fine, just less severe than the
    // catastrophic ~0.3 ml/s case). Yield ratio 24/36 = 0.67 sits below
    // CHOKED_YIELD_RATIO_MAX (0.70 — tightened from a prior 0.85 by the
    // 500-shot audit).
    void grindIssue_chokedPuckPressureMode_yieldShortfallFires()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0,  "preinfusion start", 0, /*isFlowMode=*/true),
            phase(2.0,  "preinfusion",       1, /*isFlowMode=*/true),
            phase(6.0,  "rise and hold",     2, /*isFlowMode=*/false),
            phase(10.0, "decline",           3, /*isFlowMode=*/false),
        };
        QVector<QPointF> pressure;
        pressure = concat(pressure, rampSeries(0.0, 6.0, 0.5, 6.5));
        pressure = concat(pressure, flatSeries(6.1, 60.0, 6.5));
        // Pressurized mean flow 0.6 ml/s — just above the 0.5 flow threshold.
        QVector<QPointF> flow;
        flow = concat(flow, flatSeries(0.0, 6.0, 7.0));
        flow = concat(flow, flatSeries(6.1, 60.0, 0.6));
        QVector<QPointF> flowGoal = flatSeries(0.0, 60.0, 7.5);

        // Without target/yield, neither arm fires (mean flow 0.6 > 0.5).
        const auto rNoYield = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, 6.0, 60.0, "", {}, pressure);
        QVERIFY(!rNoYield.chokedPuck);

        // With yield 24 / target 36 = 67 %, yield-ratio arm fires.
        const auto rYield = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, 6.0, 60.0, "", {}, pressure,
            /*targetWeightG=*/36.0, /*finalWeightG=*/24.0);
        QVERIFY(rYield.chokedPuck);
        QCOMPARE(ShotAnalysis::detectGrindIssue(
                     flow, flowGoal, phases, 6.0, 60.0, "", {}, pressure,
                     36.0, 24.0), true);
    }

    // Yield-ratio arm must NOT fire on a clean ristretto where yield ~ target.
    // Shot 888 signature: 19.8 g of 20 g target (99 %).
    void grindIssue_chokedPuckPressureMode_cleanRistrettoYieldDoesNotFire()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0,  "preinfusion start", 0, /*isFlowMode=*/true),
            phase(2.0,  "preinfusion",       1, /*isFlowMode=*/true),
            phase(6.0,  "rise and hold",     2, /*isFlowMode=*/false),
            phase(10.0, "decline",           3, /*isFlowMode=*/false),
        };
        QVector<QPointF> pressure;
        pressure = concat(pressure, rampSeries(0.0, 6.0, 0.5, 6.5));
        pressure = concat(pressure, flatSeries(6.1, 50.0, 6.5));
        QVector<QPointF> flow;
        flow = concat(flow, flatSeries(0.0, 6.0, 7.0));
        flow = concat(flow, flatSeries(6.1, 50.0, 1.3));
        QVector<QPointF> flowGoal = flatSeries(0.0, 50.0, 7.5);

        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, 6.0, 50.0, "", {}, pressure,
            /*targetWeightG=*/20.0, /*finalWeightG=*/19.8);
        QVERIFY(!r.chokedPuck);
    }

    // Choked-puck signature with no pressure passed: the fallback must
    // short-circuit silently rather than evaluate findValueAtTime on empty
    // data. Locks in the contract that pressure is optional.
    void grindIssue_chokedPuckPressureMode_emptyPressureSkipsFallback()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0,  "preinfusion start", 0, /*isFlowMode=*/true),
            phase(2.0,  "preinfusion",       1, /*isFlowMode=*/true),
            phase(6.0,  "rise and hold",     2, /*isFlowMode=*/false),
            phase(10.0, "decline",           3, /*isFlowMode=*/false),
        };
        auto flow = concat(flatSeries(0.0, 6.0, 7.5),
                            flatSeries(6.1, 60.0, 0.2));
        QVector<QPointF> flowGoal = flatSeries(0.0, 60.0, 7.5);

        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, /*pourStart=*/6.0, /*pourEnd=*/60.0);
        QVERIFY(!r.hasData);
        QVERIFY(!r.chokedPuck);
    }

    // Yield arm decoupled from the 15s pressurized-duration gate. Shot 745
    // signature: Adaptive v2, 35 s pour, brief pressurized window (~6 s
    // ≥ 4 bar — under the 15 s flow-arm gate), yield 23 / 36 = 0.64.
    // The shared gate previously hid this miss; now the yield arm fires
    // standalone whenever flowSamples ≥ 5 (any meaningful pressurized
    // samples) AND yield ratio < CHOKED_YIELD_RATIO_MAX.
    void grindIssue_yieldArm_firesWithoutSustainedPressurized()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0,  "preinfusion", 0, /*isFlowMode=*/true),
            phase(8.0,  "pour",        1, /*isFlowMode=*/false),
        };
        // Pressure ramps from 0.5 to 6.5 bar across the 8 s preinfusion
        // (passes 4 bar partway up), then holds 9 bar from t=8.1-14.0,
        // then drops to 2.5 bar for the rest of the shot. Total time
        // above 4 bar ~6 s — under the 15 s flow-arm gate but well over
        // the 5-sample minimum so the yield arm can fire.
        QVector<QPointF> pressure;
        pressure = concat(pressure, rampSeries(0.0, 8.0, 0.5, 6.5));
        pressure = concat(pressure, flatSeries(8.1, 14.0, 9.0));
        pressure = concat(pressure, flatSeries(14.1, 35.0, 2.5));
        QVector<QPointF> flow;
        flow = concat(flow, flatSeries(0.0, 8.0, 5.0));
        flow = concat(flow, flatSeries(8.1, 35.0, 1.5));
        QVector<QPointF> flowGoal = flatSeries(0.0, 35.0, 5.0);

        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, /*pourStart=*/8.0, /*pourEnd=*/35.0,
            /*beverageType=*/"", /*analysisFlags=*/{},
            pressure,
            /*targetWeightG=*/36.0, /*finalWeightG=*/23.0);
        QVERIFY2(r.chokedPuck,
                 "yield arm must fire on brief-pressurized + low-yield shot 745");
        QVERIFY(r.hasData);
        QVERIFY2(!r.verifiedClean,
                 "verifiedClean must require sustained pressurized window");
    }

    // Borderline yield ratio between the new threshold (0.70) and the prior
    // value (0.85) must stay silent. Locks in that the threshold tightening
    // doesn't over-flag profiles that legitimately deliver 71-85% of target.
    void grindIssue_yieldArm_doesNotFireOnBorderlineRatio()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0,  "preinfusion", 0, /*isFlowMode=*/true),
            phase(8.0,  "pour",        1, /*isFlowMode=*/false),
        };
        QVector<QPointF> pressure;
        pressure = concat(pressure, rampSeries(0.0, 8.0, 0.5, 6.5));
        pressure = concat(pressure, flatSeries(8.1, 14.0, 9.0));
        pressure = concat(pressure, flatSeries(14.1, 35.0, 2.5));
        QVector<QPointF> flow;
        flow = concat(flow, flatSeries(0.0, 8.0, 5.0));
        flow = concat(flow, flatSeries(8.1, 35.0, 1.5));
        QVector<QPointF> flowGoal = flatSeries(0.0, 35.0, 5.0);

        // Yield 27 / 36 = 0.75 — above the 0.70 threshold, below the prior 0.85.
        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, /*pourStart=*/8.0, /*pourEnd=*/35.0,
            /*beverageType=*/"", /*analysisFlags=*/{},
            pressure,
            /*targetWeightG=*/36.0, /*finalWeightG=*/27.0);
        QVERIFY2(!r.chokedPuck,
                 "0.75 yield ratio must not fire under the 0.70 threshold");
    }

    // Flow arm still requires the 15s sustained pressurized gate — only the
    // yield arm decoupled. A low-mean-flow shot with insufficient pressurized
    // duration must NOT fire chokedPuck via the flow arm; the yield arm only
    // fires when target/final-weight metadata is present.
    void grindIssue_flowArm_stillRequiresFifteenSecondsPressurized()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0,  "preinfusion", 0, /*isFlowMode=*/true),
            phase(8.0,  "pour",        1, /*isFlowMode=*/false),
        };
        // 10 s of sustained pressure (under the 15 s gate), with very low
        // mean flow (0.3 mL/s, well under the 0.5 flow threshold).
        QVector<QPointF> pressure;
        pressure = concat(pressure, rampSeries(0.0, 8.0, 0.5, 6.5));
        pressure = concat(pressure, flatSeries(8.1, 18.0, 9.0));
        pressure = concat(pressure, flatSeries(18.1, 25.0, 2.0));
        QVector<QPointF> flow;
        flow = concat(flow, flatSeries(0.0, 8.0, 5.0));
        flow = concat(flow, flatSeries(8.1, 25.0, 0.3));
        QVector<QPointF> flowGoal = flatSeries(0.0, 25.0, 5.0);

        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, /*pourStart=*/8.0, /*pourEnd=*/25.0,
            /*beverageType=*/"", /*analysisFlags=*/{},
            pressure);
        QVERIFY2(!r.chokedPuck,
                 "flow arm must keep its 15s pressurized gate — must not fire on 10s sustained");
    }

    // generateSummary on a choked-puck shot must emit the dedicated warning
    // line and the "Puck choked — grind way too fine" verdict, NOT the
    // generic "Puck integrity issue" verdict. Locks in the verdict ordering
    // (chokedPuck branch must come before the hasWarning branch in the
    // verdict if/else cascade).
    void generateSummary_chokedPuck_emitsTailoredVerdict()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0,  "preinfusion start", 0, /*isFlowMode=*/true),
            phase(2.0,  "preinfusion",       1, /*isFlowMode=*/true),
            phase(6.0,  "rise and hold",     2, /*isFlowMode=*/false),
            phase(10.0, "decline",           3, /*isFlowMode=*/false),
        };
        // Mirror the real shot 890: pressure stays well under 4 bar during
        // preinfusion, then jumps into the pressure-mode pour at 6.6 bar.
        // Otherwise the preinfusion flow (7.5 ml/s) drags the choked-window
        // mean above the 0.5 threshold.
        QVector<QPointF> pressure = concat(flatSeries(0.0, 5.9, 1.7),
                                            rampSeries(5.9, 6.0, 1.7, 6.6));
        pressure = concat(pressure, flatSeries(6.1, 60.0, 6.6));
        QVector<QPointF> flow = concat(flatSeries(0.0, 6.0, 7.5),
                                        flatSeries(6.1, 60.0, 0.2));
        QVector<QPointF> flowGoal = flatSeries(0.0, 60.0, 7.5);
        QVector<QPointF> temperature = flatSeries(0.0, 60.0, 92.0);
        QVector<QPointF> temperatureGoal = flatSeries(0.0, 60.0, 92.0);
        QVector<QPointF> dCdt = flatSeries(0.0, 60.0, 0.0);
        QVector<QPointF> weight;  // unused

        const QVariantList lines = ShotAnalysis::generateSummary(
            pressure, flow, weight, temperature, temperatureGoal,
            dCdt, phases, /*beverageType=*/"espresso", /*duration=*/60.0,
            /*pressureGoal=*/{}, flowGoal, /*analysisFlags=*/{});

        bool sawChokedWarning = false;
        QString verdictText;
        for (const QVariant& v : lines) {
            const QVariantMap m = v.toMap();
            const QString type = m["type"].toString();
            const QString text = m["text"].toString();
            if (type == "warning" && text.contains("puck choked", Qt::CaseInsensitive))
                sawChokedWarning = true;
            if (type == "verdict")
                verdictText = text;
        }
        QVERIFY2(sawChokedWarning, "expected the choked-puck warning line");
        QVERIFY2(verdictText.contains("Puck choked", Qt::CaseInsensitive),
                 qPrintable("verdict was: " + verdictText));
        QVERIFY2(!verdictText.contains("Puck integrity issue", Qt::CaseInsensitive),
                 qPrintable("verdict fell through to puck-integrity branch: " + verdictText));
    }

    // Yield-overshoot ("gusher") arm ---------------------------------------

    // Shot 835 signature: 18 g → 40 g target, 56.3 g actual (1.41 ratio) on
    // grind 11. The puck offered too little resistance; the existing detectors
    // are silent because flow tracks goal in flow-mode, the pressure-mode choke
    // arms gate on 15 s × 4 bar (which a gusher never reaches), and detectPour-
    // Truncated needs peak < 2.5 bar (a gusher can briefly exceed). The yield-
    // overshoot arm fires on yield/target alone.
    void grindIssue_yieldOvershoot_fires()
    {
        // Trivial flow/pressure curves — the arm runs purely off final/target.
        auto flow = flatSeries(0.0, 35.0, 1.6);
        auto flowGoal = flatSeries(0.0, 35.0, 1.8);
        auto pressure = flatSeries(0.0, 35.0, 4.5);
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "preinfusion", 0, /*isFlowMode=*/true),
            phase(8.0, "pour",        1, /*isFlowMode=*/true),
        };

        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, 8.0, 35.0, "espresso", {}, pressure,
            /*targetWeightG=*/40.0, /*finalWeightG=*/56.3);
        QVERIFY(r.hasData);
        QVERIFY(r.yieldOvershoot);
        QVERIFY(!r.chokedPuck);
        QCOMPARE(ShotAnalysis::detectGrindIssue(
                     flow, flowGoal, phases, 8.0, 35.0, "espresso", {},
                     pressure, 40.0, 56.3), true);
    }

    // Boundary: 1.20 ratio is the threshold (open). Just under (1.19) must
    // not fire; well over (1.41) must fire.
    void grindIssue_yieldOvershoot_thresholdBoundary()
    {
        auto flow = flatSeries(0.0, 30.0, 1.5);
        auto flowGoal = flatSeries(0.0, 30.0, 1.8);
        auto pressure = flatSeries(0.0, 30.0, 6.0);
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "pour", 0, /*isFlowMode=*/true),
        };

        // 47.6 / 40 = 1.19 — just under the 1.20 threshold, must NOT fire.
        const auto rUnder = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, 0.0, 30.0, "espresso", {}, pressure,
            40.0, 47.6);
        QVERIFY(!rUnder.yieldOvershoot);

        // 48.5 / 40 = 1.2125 — just over, must fire.
        const auto rOver = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, 0.0, 30.0, "espresso", {}, pressure,
            40.0, 48.5);
        QVERIFY(rOver.yieldOvershoot);
    }

    // Yield equal to target (clean shot) must not fire.
    void grindIssue_yieldOvershoot_cleanShotDoesNotFire()
    {
        auto flow = flatSeries(0.0, 30.0, 1.6);
        auto flowGoal = flatSeries(0.0, 30.0, 1.8);
        auto pressure = flatSeries(0.0, 30.0, 6.0);
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "pour", 0, /*isFlowMode=*/true),
        };

        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, 0.0, 30.0, "espresso", {}, pressure,
            40.0, 40.0);
        QVERIFY(!r.yieldOvershoot);
    }

    // Imported / partial shot with no target weight: arm correctly stays
    // silent (target=0 disables the precondition).
    void grindIssue_yieldOvershoot_zeroTargetSkips()
    {
        auto flow = flatSeries(0.0, 30.0, 1.6);
        auto flowGoal = flatSeries(0.0, 30.0, 1.8);
        auto pressure = flatSeries(0.0, 30.0, 6.0);
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "pour", 0, /*isFlowMode=*/true),
        };

        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, 0.0, 30.0, "espresso", {}, pressure,
            /*targetWeightG=*/0.0, /*finalWeightG=*/100.0);
        QVERIFY(!r.yieldOvershoot);
    }

    // Non-espresso beverage types (filter / pourover / tea / steam / cleaning)
    // legitimately yield much more than "target" weight relative to dose. The
    // arm must skip — same hard-skip rule the rest of the grind detector uses.
    void grindIssue_yieldOvershoot_nonEspressoSkips()
    {
        auto flow = flatSeries(0.0, 60.0, 5.0);
        auto flowGoal = flatSeries(0.0, 60.0, 5.0);
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "pour", 0, /*isFlowMode=*/true),
        };
        QVector<QPointF> pressure;

        for (const QString& bev : {"filter", "pourover", "tea", "steam", "cleaning"}) {
            const auto r = ShotAnalysis::analyzeFlowVsGoal(
                flow, flowGoal, phases, 0.0, 60.0, bev, {}, pressure,
                40.0, 200.0);
            QVERIFY2(r.skipped, qPrintable(QString("expected skipped for %1").arg(bev)));
            QVERIFY2(!r.yieldOvershoot,
                     qPrintable(QString("yieldOvershoot must not fire for %1").arg(bev)));
        }
    }

    // Empty pressure / empty flow — the arm runs off yield only and must
    // still fire even with no curve data. Locks in the "no pressurized
    // window gate" contract documented in shotanalysis.h.
    void grindIssue_yieldOvershoot_firesWithEmptyCurves()
    {
        QVector<QPointF> flow;       // empty
        QVector<QPointF> flowGoal;   // empty
        QVector<QPointF> pressure;   // empty
        QList<HistoryPhaseMarker> phases;

        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, 0.0, 0.0, "espresso", {}, pressure,
            40.0, 56.3);
        QVERIFY(r.yieldOvershoot);
        QVERIFY(r.hasData);
    }

    // analysisFlags("grind_check_skip") suppresses the arm — same as the
    // rest of the grind detector.
    void grindIssue_yieldOvershoot_grindCheckSkipFlagSuppresses()
    {
        auto flow = flatSeries(0.0, 30.0, 1.6);
        auto flowGoal = flatSeries(0.0, 30.0, 1.8);
        auto pressure = flatSeries(0.0, 30.0, 6.0);
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "pour", 0, /*isFlowMode=*/true),
        };

        const auto r = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, 0.0, 30.0, "espresso",
            QStringList{"grind_check_skip"}, pressure, 40.0, 56.3);
        QVERIFY(r.skipped);
        QVERIFY(!r.yieldOvershoot);
    }

    // generateSummary on a gusher must emit the dedicated warning line and
    // the "Pour gushed past target" verdict, NOT the generic "Puck integrity
    // issue" or "Grind appears too coarse" caution. Locks in the verdict
    // ordering: yieldOvershoot tier sits above hasWarning.
    void generateSummary_yieldOvershoot_emitsTailoredVerdict()
    {
        // Mirror shot 835: 35 s duration, peak ~6 bar (briefly), gusher.
        QList<HistoryPhaseMarker> phases{
            phase(0.0,  "preinfusion start", 0, /*isFlowMode=*/true),
            phase(2.0,  "preinfusion",       1, /*isFlowMode=*/true),
            phase(8.0,  "pour",              2, /*isFlowMode=*/true),
        };
        QVector<QPointF> pressure;
        pressure = concat(pressure, rampSeries(0.0, 4.0, 0.0, 6.0));
        pressure = concat(pressure, rampSeries(4.1, 8.0, 6.0, 3.0));
        pressure = concat(pressure, flatSeries(8.1, 35.0, 2.5));
        QVector<QPointF> flow = flatSeries(0.0, 35.0, 1.8);
        QVector<QPointF> flowGoal = flatSeries(0.0, 35.0, 1.8);
        QVector<QPointF> temperature = flatSeries(0.0, 35.0, 92.0);
        QVector<QPointF> temperatureGoal = flatSeries(0.0, 35.0, 92.0);
        QVector<QPointF> dCdt = flatSeries(0.0, 35.0, 0.0);
        QVector<QPointF> weight;

        const QVariantList lines = ShotAnalysis::generateSummary(
            pressure, flow, weight, temperature, temperatureGoal,
            dCdt, phases, /*beverageType=*/"espresso", /*duration=*/35.4,
            /*pressureGoal=*/{}, flowGoal, /*analysisFlags=*/{},
            /*firstFrameConfiguredSeconds=*/-1.0,
            /*targetWeightG=*/40.0, /*finalWeightG=*/56.3);

        bool sawOvershootWarning = false;
        QString verdictText;
        for (const QVariant& v : lines) {
            const QVariantMap m = v.toMap();
            const QString type = m["type"].toString();
            const QString text = m["text"].toString();
            if (type == "warning" && text.contains("over target", Qt::CaseInsensitive))
                sawOvershootWarning = true;
            if (type == "verdict")
                verdictText = text;
        }
        QVERIFY2(sawOvershootWarning, "expected the yield-overshoot warning line");
        QVERIFY2(verdictText.contains("gushed past target", Qt::CaseInsensitive),
                 qPrintable("verdict was: " + verdictText));
        QVERIFY2(!verdictText.contains("Puck integrity issue", Qt::CaseInsensitive),
                 qPrintable("verdict fell through to puck-integrity branch: " + verdictText));
    }

    // Pour-truncated detection ---------------------------------------------

    // Shot 868 signature: 7 s duration, pressure never exceeded ~0.6 bar
    // because the puck offered zero resistance. Must flag as truncated.
    void pourTruncated_lowPeakPressure_fires()
    {
        auto pressure = flatSeries(0.0, 7.0, 0.5);
        QCOMPARE(ShotAnalysis::detectPourTruncated(pressure, 0.0, 7.0), true);
    }

    // Normal shot with a real 9-bar peak must not flag.
    void pourTruncated_normalPeak_doesNotFire()
    {
        QVector<QPointF> pressure;
        // Ramp 0 -> 9 bar, sustain, decline. Peak 9 bar well above the
        // PRESSURE_FLOOR_BAR threshold (2.5).
        pressure = concat(pressure, rampSeries(0.0, 5.0, 0.0, 9.0));
        pressure = concat(pressure, flatSeries(5.1, 25.0, 9.0));
        pressure = concat(pressure, rampSeries(25.1, 30.0, 9.0, 3.0));
        QCOMPARE(ShotAnalysis::detectPourTruncated(pressure, 0.0, 30.0), false);
    }

    // Non-espresso beverage types (tea, filter, cleaning, steam) are
    // legitimately low-pressure — must not flag.
    void pourTruncated_nonEspressoBeverage_skips()
    {
        auto pressure = flatSeries(0.0, 10.0, 0.5);
        for (const QString& bev : {"filter", "pourover", "tea", "steam", "cleaning"}) {
            QCOMPARE(ShotAnalysis::detectPourTruncated(pressure, 0.0, 10.0, bev), false);
        }
        // Unknown / empty beverage type defaults to espresso-style check.
        QCOMPARE(ShotAnalysis::detectPourTruncated(pressure, 0.0, 10.0, ""), true);
    }

    // Peak happening outside the pour window (e.g. during fill) should not
    // count — we're diagnosing extraction pressure.
    void pourTruncated_peakOutsidePourWindow_fires()
    {
        // Short fill spike at t=1 (10 bar), then pour at 0.5 bar.
        QVector<QPointF> pressure;
        pressure.append(QPointF(0.0, 0.0));
        pressure.append(QPointF(0.5, 5.0));
        pressure.append(QPointF(1.0, 10.0));
        pressure.append(QPointF(1.5, 5.0));
        for (double t = 2.0; t <= 8.0; t += 0.1) pressure.append(QPointF(t, 0.5));
        // Pour window starts at 2 s — misses the fill spike.
        QCOMPARE(ShotAnalysis::detectPourTruncated(pressure, 2.0, 8.0), true);
    }

    // ---- Suppression cascade in analyzeShot ----
    //
    // When pourTruncated fires, the channeling / flow-trend / temp-stability /
    // grind blocks all read off curves the failed puck didn't produce, so
    // their output is unreliable. The summary path suppresses those blocks
    // entirely and emits a single "Pour never pressurized" warning + the
    // "Don't tune off this shot" verdict instead. These tests lock in that
    // behaviour so a future tweak to one of the suppressed blocks can't
    // accidentally re-introduce a wrong-diagnosis line on a puck-failure
    // shot. Mirror of issue #903 — see the issue for the user-visible bug
    // (shot 868 firing "Temp unstable" while the actual signal was a 0.63
    // bar peak puck failure).

    // Warning line must include the actual peak pressure inside the pour
    // window (the value the user looks at) and the verdict must lead with
    // "Don't tune off this shot" rather than the old "Puck failed" wording.
    void pourTruncated_summary_warningIncludesPeakAndVerdictNamesMetaAction()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "preinfusion start", 0, /*isFlowMode=*/true),
            phase(2.0, "pour",              1, /*isFlowMode=*/true),
        };
        // Flat 0.6 bar across the pour — well below PRESSURE_FLOOR_BAR (2.5).
        QVector<QPointF> pressure = flatSeries(0.0, 7.0, 0.6);
        QVector<QPointF> flow = flatSeries(0.0, 7.0, 7.0);
        QVector<QPointF> flowGoal = flatSeries(0.0, 7.0, 7.5);
        QVector<QPointF> temperature = flatSeries(0.0, 7.0, 80.0);
        QVector<QPointF> temperatureGoal = flatSeries(0.0, 7.0, 82.0);
        QVector<QPointF> dCdt = flatSeries(0.0, 7.0, 0.0);
        QVector<QPointF> weight;

        const QVariantList lines = ShotAnalysis::generateSummary(
            pressure, flow, weight, temperature, temperatureGoal,
            dCdt, phases, /*beverageType=*/"espresso", /*duration=*/7.0,
            /*pressureGoal=*/{}, flowGoal, /*analysisFlags=*/{});

        bool sawTruncatedWarning = false;
        QString warningText;
        QString verdictText;
        for (const QVariant& v : lines) {
            const QVariantMap m = v.toMap();
            const QString type = m["type"].toString();
            const QString text = m["text"].toString();
            if (type == "warning" && text.contains("never pressurized", Qt::CaseInsensitive)) {
                sawTruncatedWarning = true;
                warningText = text;
            }
            if (type == "verdict")
                verdictText = text;
        }
        QVERIFY2(sawTruncatedWarning, "expected the pour-truncated warning line");
        QVERIFY2(warningText.contains("peak 0.6 bar"),
                 qPrintable("warning was: " + warningText));
        QVERIFY2(verdictText.contains("Don't tune off this shot", Qt::CaseInsensitive),
                 qPrintable("verdict was: " + verdictText));
        QVERIFY2(verdictText.contains("unreliable", Qt::CaseInsensitive),
                 qPrintable("verdict should name the unreliable signals: " + verdictText));
    }

    // Temperature drift well above the 2°C threshold must NOT produce a
    // "Temperature drifted" caution line when pourTruncated fires. This is
    // the exact misdiagnosis from shot 868 — fix is the suppression gate in
    // analyzeShot.
    void pourTruncated_summary_suppressesTempDriftLine()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "preinfusion start", 0, /*isFlowMode=*/true),
            phase(2.0, "pour",              1, /*isFlowMode=*/true),
        };
        QVector<QPointF> pressure = flatSeries(0.0, 7.0, 0.6);  // puck failure
        QVector<QPointF> flow = flatSeries(0.0, 7.0, 7.0);
        QVector<QPointF> flowGoal = flatSeries(0.0, 7.0, 7.5);
        // 5°C below goal — would normally trip the temp-unstable detector.
        QVector<QPointF> temperature = flatSeries(0.0, 7.0, 77.0);
        QVector<QPointF> temperatureGoal = flatSeries(0.0, 7.0, 82.0);
        QVector<QPointF> dCdt = flatSeries(0.0, 7.0, 0.0);
        QVector<QPointF> weight;

        const QVariantList lines = ShotAnalysis::generateSummary(
            pressure, flow, weight, temperature, temperatureGoal,
            dCdt, phases, /*beverageType=*/"espresso", /*duration=*/7.0,
            /*pressureGoal=*/{}, flowGoal, /*analysisFlags=*/{});

        for (const QVariant& v : lines) {
            const QVariantMap m = v.toMap();
            const QString text = m["text"].toString();
            QVERIFY2(!text.contains("Temperature drifted", Qt::CaseInsensitive),
                     qPrintable("temp-drift line leaked through suppression: " + text));
        }
    }

    // Sustained dC/dt elevation must NOT produce a channeling line (or its
    // green "Puck stable — no channeling spikes" companion) when
    // pourTruncated fires — the conductance signal is unreliable when peak
    // pressure stayed below floor.
    void pourTruncated_summary_suppressesChannelingLines()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "preinfusion start", 0, /*isFlowMode=*/true),
            phase(2.0, "pour",              1, /*isFlowMode=*/true),
        };
        QVector<QPointF> pressure = flatSeries(0.0, 7.0, 0.6);  // puck failure
        QVector<QPointF> flow = flatSeries(0.0, 7.0, 7.0);
        QVector<QPointF> flowGoal = flatSeries(0.0, 7.0, 7.5);
        QVector<QPointF> temperature = flatSeries(0.0, 7.0, 82.0);
        QVector<QPointF> temperatureGoal = flatSeries(0.0, 7.0, 82.0);
        // Synthetic dC/dt with a sustained run above the elevated threshold.
        QVector<QPointF> dCdt;
        for (double t = 2.0; t <= 7.0; t += 0.05)
            dCdt.append(QPointF(t, 4.5));  // > CHANNELING_DC_ELEVATED (3.0)
        QVector<QPointF> weight;

        const QVariantList lines = ShotAnalysis::generateSummary(
            pressure, flow, weight, temperature, temperatureGoal,
            dCdt, phases, /*beverageType=*/"espresso", /*duration=*/7.0,
            /*pressureGoal=*/{}, flowGoal, /*analysisFlags=*/{});

        for (const QVariant& v : lines) {
            const QVariantMap m = v.toMap();
            const QString text = m["text"].toString();
            QVERIFY2(!text.contains("channeling", Qt::CaseInsensitive)
                     || text.contains("never pressurized", Qt::CaseInsensitive)
                     || text.contains("Don't tune off", Qt::CaseInsensitive),
                     qPrintable("channeling line leaked through suppression: " + text));
            QVERIFY2(!text.contains("Puck stable", Qt::CaseInsensitive),
                     qPrintable("green puck-stable line leaked through suppression: " + text));
        }
    }
    // ---- Structured detector results (analyzeShot) ----
    //
    // The structured `DetectorResults` struct exists so external consumers
    // (MCP `shots_get_detail`, regression harnesses) can read the same
    // signals the in-app dialog renders without parsing prose. These tests
    // lock in the contract that `lines` and `detectors` describe the same
    // evaluation — a detector flip moves both fields together. If you
    // change a verdict string or detector phrasing, the matching field in
    // DetectorResults must change with it (or these tests will fail).

    // Choked-puck shot: structured fields must mirror the prose verdict.
    void analyzeShot_chokedPuck_structuredFieldsMatchProse()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0,  "preinfusion start", 0, /*isFlowMode=*/true),
            phase(2.0,  "preinfusion",       1, /*isFlowMode=*/true),
            phase(6.0,  "rise and hold",     2, /*isFlowMode=*/false),
            phase(10.0, "decline",           3, /*isFlowMode=*/false),
        };
        QVector<QPointF> pressure = concat(flatSeries(0.0, 5.9, 1.7),
                                            rampSeries(5.9, 6.0, 1.7, 6.6));
        pressure = concat(pressure, flatSeries(6.1, 60.0, 6.6));
        QVector<QPointF> flow = concat(flatSeries(0.0, 6.0, 7.5),
                                        flatSeries(6.1, 60.0, 0.2));
        QVector<QPointF> flowGoal = flatSeries(0.0, 60.0, 7.5);
        QVector<QPointF> temperature = flatSeries(0.0, 60.0, 92.0);
        QVector<QPointF> temperatureGoal = flatSeries(0.0, 60.0, 92.0);
        QVector<QPointF> dCdt = flatSeries(0.0, 60.0, 0.0);
        QVector<QPointF> weight;

        const auto result = ShotAnalysis::analyzeShot(
            pressure, flow, weight, temperature, temperatureGoal,
            dCdt, phases, "espresso", 60.0,
            /*pressureGoal=*/{}, flowGoal, /*analysisFlags=*/{});

        const auto& d = result.detectors;
        QVERIFY(d.grindChecked);
        QVERIFY(d.grindHasData);
        QVERIFY(d.grindChokedPuck);
        QCOMPARE(d.grindDirection, QStringLiteral("chokedPuck"));
        QCOMPARE(d.verdictCategory, QStringLiteral("chokedPuck"));
        QVERIFY(!d.pourTruncated);

        // Cross-check: prose still names the same diagnosis. If this
        // assertion fails alongside grindDirection still being "chokedPuck",
        // the lines and detectors have drifted — analyzeShot must keep them
        // in lockstep.
        bool sawChokedVerdict = false;
        for (const QVariant& v : result.lines) {
            const QVariantMap m = v.toMap();
            if (m["type"].toString() == "verdict"
                && m["text"].toString().contains("Puck choked", Qt::CaseInsensitive))
                sawChokedVerdict = true;
        }
        QVERIFY2(sawChokedVerdict, "structured chokedPuck verdict must match prose");
    }

    // Pour-truncated cascade: when pourTruncated fires, downstream detectors
    // must report `checked == false` so MCP consumers don't read their
    // default fields as "clean signal."
    void analyzeShot_pourTruncated_suppressedDetectorsReportNotChecked()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "preinfusion start", 0, /*isFlowMode=*/true),
            phase(2.0, "pour",              1, /*isFlowMode=*/true),
        };
        QVector<QPointF> pressure = flatSeries(0.0, 7.0, 0.6);  // puck failure
        QVector<QPointF> flow = flatSeries(0.0, 7.0, 7.0);
        QVector<QPointF> flowGoal = flatSeries(0.0, 7.0, 7.5);
        QVector<QPointF> temperature = flatSeries(0.0, 7.0, 82.0);
        QVector<QPointF> temperatureGoal = flatSeries(0.0, 7.0, 82.0);
        QVector<QPointF> dCdt;
        for (double t = 2.0; t <= 7.0; t += 0.05)
            dCdt.append(QPointF(t, 4.5));  // would normally trigger sustained channeling
        QVector<QPointF> weight;

        const auto result = ShotAnalysis::analyzeShot(
            pressure, flow, weight, temperature, temperatureGoal,
            dCdt, phases, "espresso", 7.0,
            /*pressureGoal=*/{}, flowGoal, /*analysisFlags=*/{});

        const auto& d = result.detectors;
        QVERIFY(d.pourTruncated);
        QVERIFY(d.peakPressureBar > 0.0);
        QCOMPARE(d.verdictCategory, QStringLiteral("puckTruncated"));
        // Cascade: channeling/grind suppressed — must report not-checked
        // (distinct from "checked, no signal"). Flow trend is also
        // suppressed but its `checked` flag may stay false for the same
        // reason; we don't require a specific value, only that the prose
        // and structured fields agree.
        QVERIFY2(!d.channelingChecked,
                 "pourTruncated must suppress channeling check");
        QVERIFY2(!d.grindChecked,
                 "pourTruncated must suppress grind check");
    }

    // Clean shot: structured `verdictCategory == "clean"` must match the
    // "Clean shot. Puck held well." prose verdict.
    void analyzeShot_cleanShot_verdictCategoryMatches()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "preinfusion start", 0, /*isFlowMode=*/true),
            phase(8.0, "pour",              1, /*isFlowMode=*/false),
        };
        QVector<QPointF> pressure = concat(rampSeries(0.0, 8.0, 1.0, 9.0),
                                            flatSeries(8.1, 30.0, 9.0));
        QVector<QPointF> flow = flatSeries(0.0, 30.0, 1.8);
        QVector<QPointF> pressureGoal = pressure;
        QVector<QPointF> flowGoal = flatSeries(0.0, 30.0, 1.8);
        QVector<QPointF> temperature = flatSeries(0.0, 30.0, 92.0);
        QVector<QPointF> temperatureGoal = flatSeries(0.0, 30.0, 92.0);
        QVector<QPointF> dCdt = flatSeries(0.0, 30.0, 0.0);
        QVector<QPointF> weight = rampSeries(0.0, 30.0, 0.0, 36.0);

        const auto result = ShotAnalysis::analyzeShot(
            pressure, flow, weight, temperature, temperatureGoal,
            dCdt, phases, "espresso", 30.0,
            pressureGoal, flowGoal, /*analysisFlags=*/{},
            /*firstFrameConfiguredSeconds=*/-1.0,
            /*targetWeightG=*/36.0, /*finalWeightG=*/36.0);

        const auto& d = result.detectors;
        QCOMPARE(d.verdictCategory, QStringLiteral("clean"));
        QVERIFY(!d.pourTruncated);
        QVERIFY(!d.skipFirstFrame);

        bool sawCleanVerdict = false;
        for (const QVariant& v : result.lines) {
            const QVariantMap m = v.toMap();
            if (m["type"].toString() == "verdict"
                && m["text"].toString().contains("Clean shot", Qt::CaseInsensitive))
                sawCleanVerdict = true;
        }
        QVERIFY2(sawCleanVerdict, "structured clean verdict must match prose");
    }

    // Grind coverage signal — verified clean: a healthy pressurized pour
    // with no choke / no overshoot / on-target flow must set
    // grindVerifiedClean=true, emit `grindCoverage="verified"`, and append
    // a [good] line confirming the puck tracked goal. Without this signal
    // the dialog falls back on inferring "Clean" from no-data, which is
    // exactly the long-running gap on simple two-marker profiles.
    void analyzeShot_grindCoverage_verifiedCleanEmitsPositiveLine()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "preinfusion start", 0, /*isFlowMode=*/true),
            phase(8.0, "pour",              1, /*isFlowMode=*/false),
        };
        QVector<QPointF> pressure = concat(rampSeries(0.0, 8.0, 1.0, 9.0),
                                            flatSeries(8.1, 30.0, 9.0));
        QVector<QPointF> flow = flatSeries(0.0, 30.0, 2.0);
        QVector<QPointF> pressureGoal = pressure;
        QVector<QPointF> flowGoal = flatSeries(0.0, 30.0, 2.0);
        QVector<QPointF> temperature = flatSeries(0.0, 30.0, 92.0);
        QVector<QPointF> temperatureGoal = flatSeries(0.0, 30.0, 92.0);
        QVector<QPointF> dCdt = flatSeries(0.0, 30.0, 0.0);
        QVector<QPointF> weight;

        const auto result = ShotAnalysis::analyzeShot(
            pressure, flow, weight, temperature, temperatureGoal,
            dCdt, phases, "espresso", 30.0,
            pressureGoal, flowGoal, /*analysisFlags=*/{},
            /*firstFrameConfiguredSeconds=*/-1.0,
            /*targetWeightG=*/36.0, /*finalWeightG=*/36.0);

        const auto& d = result.detectors;
        QVERIFY(d.grindHasData);
        QVERIFY(d.grindVerifiedClean);
        QVERIFY(!d.grindChokedPuck);
        QVERIFY(!d.grindYieldOvershoot);
        QCOMPARE(d.grindCoverage, QStringLiteral("verified"));
        QCOMPARE(d.verdictCategory, QStringLiteral("clean"));

        bool sawVerifiedLine = false;
        for (const QVariant& v : result.lines) {
            const QVariantMap m = v.toMap();
            if (m["type"].toString() == "good"
                && m["text"].toString().contains("Grind tracked goal", Qt::CaseInsensitive))
                sawVerifiedLine = true;
        }
        QVERIFY2(sawVerifiedLine, "verified-clean shot must emit [good] line");
    }

    // Grind coverage signal — not analyzable: a two-marker profile whose
    // pour-mode phase never sustains 4 bar long enough to satisfy Arm 2
    // (and whose flow-mode windows are entirely before pourStart, so Arm 1
    // sees no qualifying samples) must produce a notAnalyzable coverage
    // signal, the [observation] line, and the alternate verdict text.
    // Regression for the 248-shot silent population in the audit.
    void analyzeShot_grindCoverage_notAnalyzableEmitsObservation()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "preinfusion", 0, /*isFlowMode=*/true),
            phase(20.0, "pour",       1, /*isFlowMode=*/false),
        };
        // Pressure stays under 4 bar (CHOKED_PRESSURE_MIN_BAR) throughout
        // — Arm 2 never accumulates pressurizedDuration. Arm 1's flow-mode
        // window is [0, 20] entirely before pourStart=20. Use 3.5 bar to
        // sit comfortably above pourTruncated's 2.5 bar floor and below
        // Arm 2's 4 bar gate, avoiding boundary-value fragility.
        QVector<QPointF> pressure = flatSeries(0.0, 25.0, 3.5);
        QVector<QPointF> flow = flatSeries(0.0, 25.0, 1.5);
        QVector<QPointF> pressureGoal = pressure;
        QVector<QPointF> flowGoal = flatSeries(0.0, 25.0, 1.5);
        QVector<QPointF> temperature = flatSeries(0.0, 25.0, 92.0);
        QVector<QPointF> temperatureGoal = flatSeries(0.0, 25.0, 92.0);
        QVector<QPointF> dCdt = flatSeries(0.0, 25.0, 0.0);
        QVector<QPointF> weight;

        const auto result = ShotAnalysis::analyzeShot(
            pressure, flow, weight, temperature, temperatureGoal,
            dCdt, phases, "espresso", 25.0,
            pressureGoal, flowGoal, /*analysisFlags=*/{},
            /*firstFrameConfiguredSeconds=*/-1.0,
            /*targetWeightG=*/0.0, /*finalWeightG=*/30.0);

        const auto& d = result.detectors;
        QVERIFY(!d.pourTruncated);  // 3.5 bar peak is above PRESSURE_FLOOR_BAR (2.5)
        QVERIFY(!d.grindHasData);
        QVERIFY(!d.grindVerifiedClean);
        QCOMPARE(d.grindCoverage, QStringLiteral("notAnalyzable"));
        QCOMPARE(d.verdictCategory, QStringLiteral("cleanGrindNotAnalyzable"));

        bool sawObservation = false;
        bool sawAlternateVerdict = false;
        for (const QVariant& v : result.lines) {
            const QVariantMap m = v.toMap();
            if (m["type"].toString() == "observation"
                && m["text"].toString().contains("Could not analyze grind", Qt::CaseInsensitive))
                sawObservation = true;
            if (m["type"].toString() == "verdict"
                && m["text"].toString().contains("grind could not be evaluated", Qt::CaseInsensitive))
                sawAlternateVerdict = true;
        }
        QVERIFY2(sawObservation, "notAnalyzable shot must emit [observation] line");
        QVERIFY2(sawAlternateVerdict, "notAnalyzable shot must emit alternate verdict");
    }

    // Grind coverage signal — pourTruncated cascade: when the pour never
    // pressurized, the grind coverage signal must be absent (empty string),
    // not "verified" or "notAnalyzable" — the cascade dominator already
    // explains why the grind block was skipped, and emitting a coverage
    // value would imply the detector ran.
    void analyzeShot_grindCoverage_pourTruncatedSuppresses()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "preinfusion start", 0, /*isFlowMode=*/true),
            phase(2.0, "pour",              1, /*isFlowMode=*/true),
        };
        QVector<QPointF> pressure = flatSeries(0.0, 7.0, 0.6);  // puck failure
        QVector<QPointF> flow = flatSeries(0.0, 7.0, 7.0);
        QVector<QPointF> flowGoal = flatSeries(0.0, 7.0, 7.5);
        QVector<QPointF> temperature = flatSeries(0.0, 7.0, 92.0);
        QVector<QPointF> temperatureGoal = flatSeries(0.0, 7.0, 92.0);
        QVector<QPointF> dCdt = flatSeries(0.0, 7.0, 0.0);
        QVector<QPointF> weight;

        const auto result = ShotAnalysis::analyzeShot(
            pressure, flow, weight, temperature, temperatureGoal,
            dCdt, phases, "espresso", 7.0,
            /*pressureGoal=*/{}, flowGoal, /*analysisFlags=*/{});

        const auto& d = result.detectors;
        QVERIFY(d.pourTruncated);
        QVERIFY2(d.grindCoverage.isEmpty(),
                 "pourTruncated cascade must omit grindCoverage");
        QVERIFY(!d.grindVerifiedClean);

        for (const QVariant& v : result.lines) {
            const QVariantMap m = v.toMap();
            const QString text = m["text"].toString();
            QVERIFY2(!text.contains("Grind tracked goal", Qt::CaseInsensitive),
                     "pourTruncated cascade must not emit verified-clean line");
            QVERIFY2(!text.contains("Could not analyze grind", Qt::CaseInsensitive),
                     "pourTruncated cascade must not emit notAnalyzable line");
        }
    }

    // Pour window exposure: `pourStartSec`/`pourEndSec` must reflect the
    // phase-boundary range analyzeShot computed internally. MCP consumers
    // (`shots_get_detail`) read these directly instead of re-deriving the
    // window from phase markers; the previous `ShotSummarizer::computePourWindow`
    // re-derivation is what let drift creep in (PR #944 deleted it).
    // ShotSummarizer's per-phase temperature instability check iterates
    // `summary.phases` directly and doesn't read these fields — only the
    // `pourTruncatedDetected` cascade gate couples it to analyzeShot.
    void analyzeShot_pourWindow_matchesPhaseBoundaries()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "preinfusion start", 0, /*isFlowMode=*/true),
            phase(8.0, "pour",              1, /*isFlowMode=*/false),
            phase(28.0, "end",              2, /*isFlowMode=*/false),
        };
        QVector<QPointF> pressure = concat(rampSeries(0.0, 8.0, 1.0, 9.0),
                                            flatSeries(8.1, 30.0, 9.0));
        QVector<QPointF> flow = flatSeries(0.0, 30.0, 1.8);
        QVector<QPointF> pressureGoal = pressure;
        QVector<QPointF> flowGoal = flatSeries(0.0, 30.0, 1.8);
        QVector<QPointF> temperature = flatSeries(0.0, 30.0, 92.0);
        QVector<QPointF> temperatureGoal = flatSeries(0.0, 30.0, 92.0);
        QVector<QPointF> dCdt = flatSeries(0.0, 30.0, 0.0);
        QVector<QPointF> weight = rampSeries(0.0, 30.0, 0.0, 36.0);

        const auto result = ShotAnalysis::analyzeShot(
            pressure, flow, weight, temperature, temperatureGoal,
            dCdt, phases, "espresso", 30.0,
            pressureGoal, flowGoal);
        const auto& d = result.detectors;
        QCOMPARE(d.pourStartSec, 8.0);
        QCOMPARE(d.pourEndSec, 28.0);
    }

    // No-marker whole-shot fallback: when phases is empty but pressure data
    // is present, analyzeShot still runs but the boundary loop produces no
    // hits, so pourStart stays at 0 and pourEnd stays at the full shot
    // duration. ShotSummarizer's per-phase temp gate sees this as "whole
    // shot is the pour window" — same behavior as the deleted
    // computePourWindow's `pourEnd = summary.totalDuration` default.
    void analyzeShot_pourWindow_noMarkers_spansWholeShot()
    {
        const double duration = 30.0;
        QVector<QPointF> pressure = flatSeries(0.0, duration, 9.0);
        QVector<QPointF> flow = flatSeries(0.0, duration, 1.8);
        QVector<QPointF> temperature = flatSeries(0.0, duration, 92.0);
        QVector<QPointF> temperatureGoal = flatSeries(0.0, duration, 92.0);
        QVector<QPointF> dCdt = flatSeries(0.0, duration, 0.0);
        QVector<QPointF> weight = rampSeries(0.0, duration, 0.0, 36.0);

        const auto result = ShotAnalysis::analyzeShot(
            pressure, flow, weight, temperature, temperatureGoal,
            dCdt, /*phases=*/{}, "espresso", duration);
        const auto& d = result.detectors;
        QCOMPARE(d.pourStartSec, 0.0);
        QCOMPARE(d.pourEndSec, duration);
    }

    // Insufficient-data early return: when pressure.size() < 10, analyzeShot
    // returns immediately with default detector fields. pourStartSec and
    // pourEndSec stay at their `0.0` defaults — consumers must treat that
    // as "no analysis was possible," not "valid window starting at 0."
    void analyzeShot_pourWindow_insufficientData_defaultsToZero()
    {
        QVector<QPointF> pressure;  // empty — triggers early return
        const auto result = ShotAnalysis::analyzeShot(
            pressure, /*flow=*/{}, /*weight=*/{}, /*temperature=*/{},
            /*tempGoal=*/{}, /*dCdt=*/{}, /*phases=*/{}, "espresso", 30.0);
        const auto& d = result.detectors;
        QCOMPARE(d.pourStartSec, 0.0);
        QCOMPARE(d.pourEndSec, 0.0);
    }

    // Preinfusion-only fallback: when no "pour" phase is present, analyzeShot
    // uses the first preinfusion/start boundary as pourStart. MCP consumers
    // read this directly instead of re-deriving the window from phase markers
    // (the previous `ShotSummarizer::computePourWindow` did exactly that and
    // was the drift hazard PR #944 closed).
    void analyzeShot_pourWindow_preinfusionOnly_usesPreinfusionBoundary()
    {
        const double duration = 30.0;
        QList<HistoryPhaseMarker> phases{
            phase(2.0, "preinfusion", 0, /*isFlowMode=*/true),
        };
        QVector<QPointF> pressure = flatSeries(0.0, duration, 9.0);
        QVector<QPointF> flow = flatSeries(0.0, duration, 1.8);
        QVector<QPointF> temperature = flatSeries(0.0, duration, 92.0);
        QVector<QPointF> temperatureGoal = flatSeries(0.0, duration, 92.0);
        QVector<QPointF> dCdt = flatSeries(0.0, duration, 0.0);
        QVector<QPointF> weight = rampSeries(0.0, duration, 0.0, 36.0);

        const auto result = ShotAnalysis::analyzeShot(
            pressure, flow, weight, temperature, temperatureGoal,
            dCdt, phases, "espresso", duration);
        const auto& d = result.detectors;
        QCOMPARE(d.pourStartSec, 2.0);
        QCOMPARE(d.pourEndSec, duration);
    }

    // Backwards compatibility: the legacy generateSummary() wrapper must
    // return the same line list as analyzeShot(...).lines. If this fails,
    // the wrapper has drifted — every QML/AI consumer downstream is
    // affected.
    void generateSummary_isThinWrapperOverAnalyzeShot()
    {
        QList<HistoryPhaseMarker> phases{
            phase(0.0, "preinfusion start", 0, /*isFlowMode=*/true),
            phase(8.0, "pour",              1, /*isFlowMode=*/false),
        };
        QVector<QPointF> pressure = concat(rampSeries(0.0, 8.0, 1.0, 9.0),
                                            flatSeries(8.1, 30.0, 9.0));
        QVector<QPointF> flow = flatSeries(0.0, 30.0, 1.8);
        QVector<QPointF> flowGoal = flatSeries(0.0, 30.0, 1.8);
        QVector<QPointF> temperature = flatSeries(0.0, 30.0, 92.0);
        QVector<QPointF> temperatureGoal = flatSeries(0.0, 30.0, 92.0);
        QVector<QPointF> dCdt = flatSeries(0.0, 30.0, 0.0);
        QVector<QPointF> weight = rampSeries(0.0, 30.0, 0.0, 36.0);

        // Pass non-default expectedFrameCount on both call sites — locks in
        // that the wrapper threads it through to analyzeShot identically.
        // Without this assertion, a future change that drops the parameter
        // from the wrapper would silently regress 1-frame-profile callers
        // (the dialog and the live AI advisor). Pre-merge of #934/#935 the
        // wrapper hardcoded -1 and the dialog/AI advisor diverged from
        // save/load/MCP for 1-frame profiles — see SHOT_REVIEW.md §4.
        const int frameCount = 2;
        const QVariantList legacy = ShotAnalysis::generateSummary(
            pressure, flow, weight, temperature, temperatureGoal,
            dCdt, phases, "espresso", 30.0,
            /*pressureGoal=*/{}, flowGoal, /*analysisFlags=*/{},
            /*firstFrameConfiguredSeconds=*/-1.0,
            /*targetWeightG=*/0.0, /*finalWeightG=*/0.0,
            frameCount);
        const auto fresh = ShotAnalysis::analyzeShot(
            pressure, flow, weight, temperature, temperatureGoal,
            dCdt, phases, "espresso", 30.0,
            /*pressureGoal=*/{}, flowGoal, /*analysisFlags=*/{},
            /*firstFrameConfiguredSeconds=*/-1.0,
            /*targetWeightG=*/0.0, /*finalWeightG=*/0.0,
            frameCount);

        QCOMPARE(legacy.size(), fresh.lines.size());
        for (qsizetype i = 0; i < legacy.size(); ++i) {
            QCOMPARE(legacy[i].toMap()["text"].toString(),
                     fresh.lines[i].toMap()["text"].toString());
            QCOMPARE(legacy[i].toMap()["type"].toString(),
                     fresh.lines[i].toMap()["type"].toString());
        }
    }
    // ---- Badge projection (decenza::deriveBadgesFromAnalysis) ----
    //
    // The five boolean quality-badge columns are now a deterministic
    // projection of ShotAnalysis::DetectorResults via the helper in
    // src/history/shotbadgeprojection.h. saveShot and loadShotRecordStatic
    // both call analyzeShot once and apply the projection — the cascade
    // lives in exactly one place. These table-driven cases lock in the
    // projection contract documented in SHOT_REVIEW.md §4. If any cell of
    // the mapping table is changed, the matching test case must change with
    // it (or the test fails — which is the whole point of these locks).

    void badgeProjection_cleanShot_allFalse()
    {
        // No detectors fired, no warnings — the canonical clean shot.
        ShotAnalysis::DetectorResults d;
        d.channelingChecked = true;
        d.channelingSeverity = QStringLiteral("none");
        d.flowTrendChecked = true;
        d.flowTrend = QStringLiteral("stable");
        d.tempStabilityChecked = true;
        d.tempUnstable = false;
        d.grindChecked = true;
        d.grindHasData = true;
        d.grindDirection = QStringLiteral("onTarget");
        d.grindFlowDeltaMlPerSec = 0.0;
        d.verdictCategory = QStringLiteral("clean");

        const auto flags = decenza::deriveBadgesFromAnalysis(d);
        QVERIFY(!flags.pourTruncatedDetected);
        QVERIFY(!flags.channelingDetected);
        QVERIFY(!flags.temperatureUnstable);
        QVERIFY(!flags.grindIssueDetected);
        QVERIFY(!flags.skipFirstFrameDetected);
    }

    // Badge projection — verifiedClean: the new positive grind signal must
    // NOT fire grindIssueDetected. The badge column gates on actual issues
    // (chokedPuck, yieldOvershoot, large delta), not on hasData. This locks
    // in the invariant that adding the verified-clean branch can't sneak
    // a false-positive grind badge into the cascade.
    void badgeProjection_grindVerifiedClean_doesNotFireBadge()
    {
        ShotAnalysis::DetectorResults d;
        d.channelingChecked = true;
        d.channelingSeverity = QStringLiteral("none");
        d.tempStabilityChecked = true;
        d.tempUnstable = false;
        d.grindChecked = true;
        d.grindHasData = true;
        d.grindVerifiedClean = true;          // new positive signal
        d.grindChokedPuck = false;
        d.grindYieldOvershoot = false;
        d.grindFlowDeltaMlPerSec = 0.0;
        d.grindDirection = QStringLiteral("onTarget");
        d.grindCoverage = QStringLiteral("verified");
        d.verdictCategory = QStringLiteral("clean");

        const auto flags = decenza::deriveBadgesFromAnalysis(d);
        QVERIFY2(!flags.grindIssueDetected,
                 "verifiedClean must not fire grindIssueDetected — only "
                 "chokedPuck/yieldOvershoot/large-delta should");
    }

    void badgeProjection_pourTruncated_onlyTruncatedFires()
    {
        // Cascade dominator: when pourTruncated fires, the suppressed
        // detectors leave their flags at default. skipFirstFrameDetected is
        // explicitly NOT suppressed by the cascade per the SHOT_REVIEW.md
        // contract — it can independently fire on the same shot.
        ShotAnalysis::DetectorResults d;
        d.pourTruncated = true;
        d.peakPressureBar = 1.2;
        // analyzeShot would leave channelingChecked / grindChecked / etc.
        // false in the truncated cascade (that's the cascade contract).
        d.verdictCategory = QStringLiteral("puckTruncated");

        const auto flags = decenza::deriveBadgesFromAnalysis(d);
        QVERIFY2(flags.pourTruncatedDetected,
                 "pourTruncated must project to pourTruncatedDetected");
        QVERIFY2(!flags.channelingDetected,
                 "cascade must leave channelingDetected at false");
        QVERIFY2(!flags.temperatureUnstable,
                 "cascade must leave temperatureUnstable at false");
        QVERIFY2(!flags.grindIssueDetected,
                 "cascade must leave grindIssueDetected at false");
        QVERIFY2(!flags.skipFirstFrameDetected,
                 "skipFirstFrame is independent — must be false when not flagged");
    }

    void badgeProjection_pourTruncatedAndSkipFirstFrame_bothFire()
    {
        // skipFirstFrameDetected is NOT suppressed by the pourTruncated
        // cascade — they can co-fire. Locks in the PR #922 invariant.
        ShotAnalysis::DetectorResults d;
        d.pourTruncated = true;
        d.skipFirstFrame = true;
        d.verdictCategory = QStringLiteral("puckTruncated");

        const auto flags = decenza::deriveBadgesFromAnalysis(d);
        QVERIFY(flags.pourTruncatedDetected);
        QVERIFY(flags.skipFirstFrameDetected);
        QVERIFY(!flags.channelingDetected);
        QVERIFY(!flags.temperatureUnstable);
        QVERIFY(!flags.grindIssueDetected);
    }

    void badgeProjection_sustainedChanneling_firesBadge()
    {
        ShotAnalysis::DetectorResults d;
        d.channelingChecked = true;
        d.channelingSeverity = QStringLiteral("sustained");
        d.channelingSpikeTimeSec = 18.2;
        d.verdictCategory = QStringLiteral("puckIntegrity");

        const auto flags = decenza::deriveBadgesFromAnalysis(d);
        QVERIFY2(flags.channelingDetected,
                 "Sustained channeling must fire the badge");
    }

    void badgeProjection_transientChanneling_doesNotFireBadge()
    {
        // Critical regression lock: Transient channeling shows in the dialog
        // and in MCP detectorResults, but the boolean badge stays false.
        // Matches PR #922's invariant. If this test ever fails alongside
        // a "transient" severity in DetectorResults, the projection drifted.
        ShotAnalysis::DetectorResults d;
        d.channelingChecked = true;
        d.channelingSeverity = QStringLiteral("transient");
        d.channelingSpikeTimeSec = 8.4;
        d.verdictCategory = QStringLiteral("minorIssues");

        const auto flags = decenza::deriveBadgesFromAnalysis(d);
        QVERIFY2(!flags.channelingDetected,
                 "Transient channeling must NOT fire the badge (PR #922 invariant)");
    }

    void badgeProjection_chokedPuck_firesGrindBadge()
    {
        ShotAnalysis::DetectorResults d;
        d.grindChecked = true;
        d.grindHasData = true;
        d.grindChokedPuck = true;
        d.grindDirection = QStringLiteral("chokedPuck");
        d.verdictCategory = QStringLiteral("chokedPuck");

        const auto flags = decenza::deriveBadgesFromAnalysis(d);
        QVERIFY(flags.grindIssueDetected);
    }

    void badgeProjection_yieldOvershoot_firesGrindBadge()
    {
        ShotAnalysis::DetectorResults d;
        d.grindChecked = true;
        d.grindHasData = true;
        d.grindYieldOvershoot = true;
        d.grindDirection = QStringLiteral("yieldOvershoot");
        d.verdictCategory = QStringLiteral("yieldOvershoot");

        const auto flags = decenza::deriveBadgesFromAnalysis(d);
        QVERIFY(flags.grindIssueDetected);
    }

    void badgeProjection_grindDeltaAboveThreshold_firesBadge()
    {
        ShotAnalysis::DetectorResults d;
        d.grindChecked = true;
        d.grindHasData = true;
        d.grindFlowDeltaMlPerSec = ShotAnalysis::FLOW_DEVIATION_THRESHOLD + 0.1;
        d.grindDirection = QStringLiteral("tooCoarse");
        d.verdictCategory = QStringLiteral("minorIssuesGrindCoarse");

        const auto flags = decenza::deriveBadgesFromAnalysis(d);
        QVERIFY2(flags.grindIssueDetected,
                 "delta above FLOW_DEVIATION_THRESHOLD must fire the grind badge");
    }

    void badgeProjection_grindDeltaBelowThresholdNegative_firesBadge()
    {
        // Negative delta means flow ran below goal (grind too fine). The
        // |delta| > threshold check uses absolute value.
        ShotAnalysis::DetectorResults d;
        d.grindChecked = true;
        d.grindHasData = true;
        d.grindFlowDeltaMlPerSec = -(ShotAnalysis::FLOW_DEVIATION_THRESHOLD + 0.1);
        d.grindDirection = QStringLiteral("tooFine");
        d.verdictCategory = QStringLiteral("minorIssuesGrindFine");

        const auto flags = decenza::deriveBadgesFromAnalysis(d);
        QVERIFY2(flags.grindIssueDetected,
                 "negative delta below threshold (|delta| > threshold) must fire the grind badge");
    }

    void badgeProjection_grindDeltaWithinTolerance_doesNotFireBadge()
    {
        ShotAnalysis::DetectorResults d;
        d.grindChecked = true;
        d.grindHasData = true;
        d.grindFlowDeltaMlPerSec = 0.1;  // well within tolerance
        d.grindDirection = QStringLiteral("onTarget");
        d.verdictCategory = QStringLiteral("clean");

        const auto flags = decenza::deriveBadgesFromAnalysis(d);
        QVERIFY2(!flags.grindIssueDetected,
                 "delta within tolerance must NOT fire the grind badge");
    }

    void badgeProjection_grindNoData_doesNotFireBadge()
    {
        // hasData=false must short-circuit the grind projection — even if
        // by some strange path one of the sub-flags is set, the badge stays
        // false because the projection ANDs hasData with the OR of the arms.
        ShotAnalysis::DetectorResults d;
        d.grindChecked = true;
        d.grindHasData = false;
        d.grindChokedPuck = true;  // sub-flag set without hasData — defensive
        d.grindFlowDeltaMlPerSec = 5.0;  // way above threshold

        const auto flags = decenza::deriveBadgesFromAnalysis(d);
        QVERIFY2(!flags.grindIssueDetected,
                 "grind badge requires hasData=true (defensive against partial state)");
    }

    void badgeProjection_skipFirstFrame_firesBadge()
    {
        ShotAnalysis::DetectorResults d;
        d.skipFirstFrame = true;
        d.verdictCategory = QStringLiteral("skipFirstFrame");

        const auto flags = decenza::deriveBadgesFromAnalysis(d);
        QVERIFY(flags.skipFirstFrameDetected);
    }

    void badgeProjection_tempUnstable_firesBadge()
    {
        // tempUnstable already encodes the gates inside analyzeShot
        // (tempStabilityChecked && !tempIntentionalStepping && avgDev > threshold);
        // the projection just reads the flag.
        ShotAnalysis::DetectorResults d;
        d.tempStabilityChecked = true;
        d.tempIntentionalStepping = false;
        d.tempAvgDeviationC = 3.0;
        d.tempUnstable = true;
        d.verdictCategory = QStringLiteral("minorIssues");

        const auto flags = decenza::deriveBadgesFromAnalysis(d);
        QVERIFY(flags.temperatureUnstable);
    }

    void badgeProjection_tempIntentionalStepping_doesNotFireBadge()
    {
        // D-Flow style profile with intentional temp stepping: tempUnstable
        // stays false even with a large measured deviation, because the
        // gate is held inside analyzeShot. Locks in that the projection
        // doesn't second-guess the struct.
        ShotAnalysis::DetectorResults d;
        d.tempStabilityChecked = true;
        d.tempIntentionalStepping = true;
        d.tempAvgDeviationC = 8.0;  // would normally trip threshold
        d.tempUnstable = false;
        d.verdictCategory = QStringLiteral("clean");

        const auto flags = decenza::deriveBadgesFromAnalysis(d);
        QVERIFY2(!flags.temperatureUnstable,
                 "intentional temp stepping must NOT fire the temp badge");
    }

    void badgeProjection_applyBadgesToTarget_writesAllFiveFields()
    {
        // Compile-time + runtime check that the templated apply helper
        // writes all five fields onto a target struct shape. Uses a local
        // throwaway struct that mimics ShotSaveData / ShotRecord's badge
        // surface — keeps this test independent of the storage TU layout.
        struct FakeTarget {
            bool pourTruncatedDetected = false;
            bool channelingDetected = false;
            bool temperatureUnstable = false;
            bool grindIssueDetected = false;
            bool skipFirstFrameDetected = false;
        };
        FakeTarget t;
        ShotAnalysis::DetectorResults d;
        d.pourTruncated = true;
        d.skipFirstFrame = true;
        d.channelingSeverity = QStringLiteral("sustained");
        d.tempUnstable = true;
        d.grindHasData = true;
        d.grindChokedPuck = true;

        decenza::applyBadgesToTarget(t, d);
        QVERIFY(t.pourTruncatedDetected);
        QVERIFY(t.channelingDetected);
        QVERIFY(t.temperatureUnstable);
        QVERIFY(t.grindIssueDetected);
        QVERIFY(t.skipFirstFrameDetected);
    }

    // avgTempDeviation -------------------------------------------------------

    void avgTempDeviation_coldStartWarmupSkipped()
    {
        // First 3s: actual = 84°C, goal = 88°C → delta = 4°C > TEMP_WARMUP_SKIP_C (3.0)
        // → those samples are skipped. Remaining 7s: actual = 87°C → delta = 1°C < threshold.
        // Average over the non-skipped samples should be ~1°C → well below 2°C badge threshold.
        const double goal = 88.0;
        const double warmSamples = 7.0 * 10; // 7s at 10 Hz

        QVector<QPointF> temp, tempGoal;
        for (int i = 0; i < 30; ++i) {          // 3s cold: 84°C
            temp.append(QPointF(i * 0.1, 84.0));
            tempGoal.append(QPointF(i * 0.1, goal));
        }
        for (int i = 30; i < 130; ++i) {        // 10s warm: 87°C
            temp.append(QPointF(i * 0.1, 87.0));
            tempGoal.append(QPointF(i * 0.1, goal));
        }

        const double dev = ShotAnalysis::avgTempDeviation(temp, tempGoal, 0.0, 13.0);
        QVERIFY2(dev < ShotAnalysis::TEMP_UNSTABLE_THRESHOLD,
                 "cold-start leading samples should be skipped; stable tail should not badge");
        QCOMPARE_LT(std::abs(dev - 1.0), 0.01); // should average to exactly 1.0°C
    }

    void avgTempDeviation_midShotDropCountedAfterLatch()
    {
        // 2s stable at goal, then 3s of a 5°C below-goal drop mid-shot.
        // Latch fires on the stable prefix; the drop is counted and pushes avg above threshold.
        const double goal = 93.0;

        QVector<QPointF> temp, tempGoal;
        for (int i = 0; i < 20; ++i) {          // 2s stable: 93°C (delta=0)
            temp.append(QPointF(i * 0.1, 93.0));
            tempGoal.append(QPointF(i * 0.1, goal));
        }
        for (int i = 20; i < 50; ++i) {         // 3s drop: 88°C (delta=5°C)
            temp.append(QPointF(i * 0.1, 88.0));
            tempGoal.append(QPointF(i * 0.1, goal));
        }

        const double dev = ShotAnalysis::avgTempDeviation(temp, tempGoal, 0.0, 5.0);
        QVERIFY2(dev > ShotAnalysis::TEMP_UNSTABLE_THRESHOLD,
                 "mid-shot temperature drop must be counted after warmup latch fires");
    }

    void avgTempDeviation_allSamplesColdReturnsZero()
    {
        // Every pour sample is 5°C below goal throughout — machine never warmed up.
        // count stays 0; function must return 0.0 (no usable data → no badge).
        const double goal = 90.0;

        QVector<QPointF> temp, tempGoal;
        for (int i = 0; i < 50; ++i) {
            temp.append(QPointF(i * 0.1, 85.0));    // delta = 5°C > TEMP_WARMUP_SKIP_C
            tempGoal.append(QPointF(i * 0.1, goal));
        }

        const double dev = ShotAnalysis::avgTempDeviation(temp, tempGoal, 0.0, 5.0);
        QCOMPARE(dev, 0.0); // no usable data → no badge (intentional)
    }
};

QTEST_MAIN(tst_ShotAnalysis)
#include "tst_shotanalysis.moc"
