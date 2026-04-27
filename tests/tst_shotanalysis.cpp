#include <QtTest>

#include "ai/shotanalysis.h"
#include "history/shothistorystorage.h"

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
        // Goal: flat 8 from 3→10s, ramp 8→3 from 10→12s (2.5 bar/s),
        // flat 3 from 12→20s. Actual tracks goal (converged).
        QVector<QPointF> pressureGoal;
        pressureGoal = concat(pressureGoal, flatSeries(3.0, 10.0 - 0.1, 8.0));
        pressureGoal = concat(pressureGoal, rampSeries(10.0, 12.0, 8.0, 3.0));
        pressureGoal = concat(pressureGoal, flatSeries(12.0 + 0.1, 20.0, 3.0));
        auto pressure = pressureGoal;  // actual tracks goal exactly

        auto flow = flatSeries(3.0, 20.0, 1.0);
        QVector<QPointF> flowGoal;

        const auto windows = ShotAnalysis::buildChannelingWindows(
            pressure, flow, pressureGoal, flowGoal, phases,
            /*pourStart=*/5.0, /*pourEnd=*/18.0);

        QVERIFY2(!windows.isEmpty(), "expected plateau windows");

        // The ramp itself (10–12 s) must be excluded from every window. The
        // ±0.75 s lookup fringes around each ramp boundary can still qualify
        // when the magnitude change stays under 15 %, so do not assert on
        // them — only the ramp core.
        for (const auto& w : windows) {
            const bool intersectsRampCore = !(w.end <= 10.0 || w.start >= 12.0);
            QVERIFY2(!intersectsRampCore,
                     qPrintable(QString("window [%1, %2] intersects ramp core 10–12 s")
                                    .arg(w.start).arg(w.end)));
        }

        // At least one window should sit entirely on each flat plateau.
        bool sawEarlyPlateau = false, sawLatePlateau = false;
        for (const auto& w : windows) {
            if (w.start >= 7.0 && w.end <= 10.0) sawEarlyPlateau = true;
            if (w.start >= 12.0 && w.end <= 18.5) sawLatePlateau = true;
        }
        QVERIFY2(sawEarlyPlateau, "expected a window inside the 7–10 s plateau");
        QVERIFY2(sawLatePlateau, "expected a window inside the 12–18 s plateau");
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

    // Yield-ratio arm: shot 883 signature — 80's Espresso, mean pressurized
    // flow ~0.6 ml/s (just over the 0.5 flow threshold), but yield 25.3 g
    // of 36 g target is 70 %. The flow arm doesn't fire; the yield arm
    // must catch this as the same diagnosis (grind too fine, just less
    // severe than the catastrophic 1.1 g case).
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

        // With yield 25.3 / target 36 = 70 %, yield-ratio arm fires.
        const auto rYield = ShotAnalysis::analyzeFlowVsGoal(
            flow, flowGoal, phases, 6.0, 60.0, "", {}, pressure,
            /*targetWeightG=*/36.0, /*finalWeightG=*/25.3);
        QVERIFY(rYield.chokedPuck);
        QCOMPARE(ShotAnalysis::detectGrindIssue(
                     flow, flowGoal, phases, 6.0, 60.0, "", {}, pressure,
                     36.0, 25.3), true);
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
};

QTEST_MAIN(tst_ShotAnalysis)
#include "tst_shotanalysis.moc"
