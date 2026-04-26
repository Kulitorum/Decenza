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

    // Channeling is a positive-dC/dt signature. A sustained negative-dC/dt
    // run (the lever pressure-rise / decline shape: pressure climbing or
    // falling faster than flow can follow → conductance dropping) must not
    // be flagged. The magnitudes here would all trip the |v| > 3 check that
    // the legacy std::abs() loop used.
    void channelingFromDerivative_negativeSpikeNotFlagged()
    {
        QVector<QPointF> dcdt;
        for (double t = 0.0; t <= 25.0; t += 0.1) {
            double v = 0.2;
            if (t >= 7.0 && t <= 12.0) v = -8.0;  // 50 samples at -8 (|v|=8)
            dcdt.append(QPointF(t, v));
        }

        QVector<ShotAnalysis::DetectionWindow> fullPour{{2.0, 25.0}};
        auto severity = ShotAnalysis::detectChannelingFromDerivative(
            dcdt, /*pourStart=*/2.0, /*pourEnd=*/25.0, fullPour);
        QCOMPARE(severity, ShotAnalysis::ChannelingSeverity::None);
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
