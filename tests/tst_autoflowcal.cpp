#include <QtTest>
#include <cmath>
#include <QRegularExpression>

#include "controllers/autoflowcalclassifier.h"
#include "core/settings.h"
#include "core/settings_calibration.h"
#include "profile/profileframe.h"

// Regression tests for classifyAutoFlowCalWindow(), the function that decides
// whether the auto flow calibration steady window is flow- or pressure-
// controlled based on which profile frames were actually active during the
// window (via the shot's PhaseMarker/FrameTransition stream).
//
// The core bug this guards against is hybrid profiles — e.g. ASL9-3, which
// has pressure declines followed by a flow-controlled "maintain flow" tail.
// The old profile-level scan flagged the whole thing as a flow profile the
// moment any extraction frame used flow control; the window-level classifier
// correctly routes each shot's window to its actual mode.

class tst_AutoFlowCal : public QObject {
    Q_OBJECT

private:
    // --- Frame builders ---

    static ProfileFrame flowFrame(const QString& name, double targetFlow) {
        ProfileFrame pf;
        pf.name = name;
        pf.pump = "flow";
        pf.flow = targetFlow;
        pf.pressure = 0.0;
        return pf;
    }

    static ProfileFrame pressureFrame(const QString& name, double targetPressure) {
        ProfileFrame pf;
        pf.name = name;
        pf.pump = "pressure";
        pf.pressure = targetPressure;
        pf.flow = 0.0;
        return pf;
    }

    // --- Profile builders (matching real profile shapes) ---

    // D-Flow / Q: preinfuse flow frames + single flow-controlled pouring frame.
    // Every extraction window lands in the Pouring frame at 1.8 ml/s.
    static QList<ProfileFrame> buildFlowOnlyProfile() {
        return {
            flowFrame("Filling", 8.0),      // 0: preinfuse
            flowFrame("Infusing", 4.0),     // 1: preinfuse
            flowFrame("Pouring", 1.8),      // 2: extraction (flow)
        };
    }

    // Spring Lever shape: flow preinfuse + pressure rise + pressure decline.
    // Every extraction window lands in decline at 6 bar.
    static QList<ProfileFrame> buildPressureOnlyProfile() {
        return {
            flowFrame("preinfusion", 4.0),        // 0
            pressureFrame("rise and hold", 9.0),  // 1
            pressureFrame("decline", 6.0),        // 2
        };
    }

    // ASL9-3 shape: flow preinfuse, pressure rise+declines, then a flow tail.
    // Windows usually sit in the declines; rarely does a window reach the tail.
    static QList<ProfileFrame> buildHybridAsl9Profile() {
        return {
            flowFrame("2s infuse", 8.0),          // 0: preinfuse
            flowFrame("infuse", 8.0),             // 1: preinfuse
            pressureFrame("rise and hold", 9.0),  // 2
            pressureFrame("decline1", 8.0),       // 3
            pressureFrame("decline2", 7.0),       // 4
            pressureFrame("decline3", 6.0),       // 5
            pressureFrame("decline4", 5.0),       // 6
            pressureFrame("decline5", 4.0),       // 7
            pressureFrame("decline6", 3.0),       // 8
            flowFrame("maintain flow", 1.5),      // 9: flow-controlled tail
        };
    }

private slots:
    void init() { QTest::failOnWarning(); }

    // D-Flow / Q: window sits entirely inside the Pouring flow frame.
    // isFlowProfile=true, target=1.8. No regression from today's behavior.
    void flowOnlyProfile_windowInFlowFrame() {
        auto steps = buildFlowOnlyProfile();
        QList<FrameTransition> transitions = {
            {0.0, 0},   // Filling
            {2.1, 1},   // Infusing
            {7.5, 2},   // Pouring
        };

        auto cls = classifyAutoFlowCalWindow(steps, transitions,
                                             /*winStart*/ 10.0, /*winEnd*/ 30.0,
                                             /*meanMachineFlow*/ 1.82);

        QVERIFY(!cls.fallbackToProfileScan);
        QVERIFY(!cls.mixedMode);
        QVERIFY(cls.isFlowProfile);
        QCOMPARE(cls.targetFlow, 1.8);
        QCOMPARE(cls.firstFrameInWindow, 2);
        QCOMPARE(cls.lastFrameInWindow, 2);
    }

    // Spring Lever: window sits in the pressure decline. Must route to
    // pressure mode even though frame 0 is flow-controlled preinfusion.
    void pressureOnlyProfile_windowInPressureFrame() {
        auto steps = buildPressureOnlyProfile();
        QList<FrameTransition> transitions = {
            {0.0, 0},   // preinfusion
            {6.5, 1},   // rise and hold
            {9.0, 2},   // decline
        };

        auto cls = classifyAutoFlowCalWindow(steps, transitions,
                                             10.0, 30.0, 0.80);

        QVERIFY(!cls.fallbackToProfileScan);
        QVERIFY(!cls.mixedMode);
        QVERIFY(!cls.isFlowProfile);
        QCOMPARE(cls.firstFrameInWindow, 2);
        QCOMPARE(cls.lastFrameInWindow, 2);
    }

    // PRIMARY REGRESSION: ASL9-3 window in pressure declines must NOT be
    // classified as a flow profile just because the profile has a flow tail.
    // This is the exact shot shape from debug-4.log shot 175 (window 16.6-37.7,
    // entirely inside decline2..decline4).
    void hybridProfile_windowInPressureDeclines() {
        auto steps = buildHybridAsl9Profile();
        QList<FrameTransition> transitions = {
            {0.00, 0},     // 2s infuse
            {2.07, 1},     // infuse
            {4.35, 2},     // rise and hold
            {7.26, 3},     // decline1
            {17.06, 4},    // decline2
            {26.43, 5},    // decline3
            {35.42, 6},    // decline4
        };

        auto cls = classifyAutoFlowCalWindow(steps, transitions,
                                             16.6, 37.7, 0.97);

        QVERIFY(!cls.fallbackToProfileScan);
        QVERIFY(!cls.mixedMode);
        QVERIFY2(!cls.isFlowProfile,
                 "Hybrid profile with window in pressure declines must NOT be "
                 "classified as flow — that was the #739 bug");
        // Window covers frames 3,4,5,6 (decline1..decline4).
        QCOMPARE(cls.firstFrameInWindow, 3);
        QCOMPARE(cls.lastFrameInWindow, 6);
    }

    // ASL9-3 but the shot runs all the way into the "maintain flow" tail and
    // the steady window lands there. Must classify as flow with target=1.5.
    void hybridProfile_windowInFlowTail() {
        auto steps = buildHybridAsl9Profile();
        QList<FrameTransition> transitions = {
            {0.00, 0},
            {2.00, 1},
            {4.00, 2},
            {7.00, 3},
            {17.0, 4},
            {27.0, 5},
            {37.0, 6},
            {47.0, 7},
            {57.0, 8},
            {67.0, 9},   // maintain flow starts
        };

        auto cls = classifyAutoFlowCalWindow(steps, transitions,
                                             68.0, 90.0, 1.48);

        QVERIFY(!cls.fallbackToProfileScan);
        QVERIFY(!cls.mixedMode);
        QVERIFY(cls.isFlowProfile);
        QCOMPARE(cls.targetFlow, 1.5);
        QCOMPARE(cls.firstFrameInWindow, 9);
        QCOMPARE(cls.lastFrameInWindow, 9);
    }

    // Window straddles a pressure→flow transition. Skip rather than guess.
    void hybridProfile_mixedModeWindow() {
        auto steps = buildHybridAsl9Profile();
        QList<FrameTransition> transitions = {
            {0.0, 0},
            {2.0, 1},
            {4.0, 2},
            {7.0, 3},
            {17.0, 4},
            {27.0, 5},
            {37.0, 6},
            {47.0, 7},
            {57.0, 8},
            {60.0, 9},  // maintain flow starts
        };

        // Window spans decline6 (pressure, frame 8) + maintain flow (flow, frame 9).
        auto cls = classifyAutoFlowCalWindow(steps, transitions,
                                             58.0, 65.0, 1.80);

        QVERIFY(!cls.fallbackToProfileScan);
        QVERIFY(cls.mixedMode);
    }

    // Multi-frame same-mode window: ASL9-3 shot with window spanning several
    // pressure declines. Must classify as pressure (not mixed), since all
    // frames in the window are pressure-controlled.
    void hybridProfile_multiFrameSameModeWindow() {
        auto steps = buildHybridAsl9Profile();
        QList<FrameTransition> transitions = {
            {0.0, 0},
            {2.0, 1},
            {4.0, 2},
            {7.0, 3},    // decline1
            {17.0, 4},   // decline2
            {27.0, 5},   // decline3
        };

        auto cls = classifyAutoFlowCalWindow(steps, transitions,
                                             10.0, 30.0, 1.1);

        QVERIFY(!cls.fallbackToProfileScan);
        QVERIFY(!cls.mixedMode);
        QVERIFY(!cls.isFlowProfile);
    }

    // Empty transitions → fallback so calibration still runs via the old
    // profile-level scan.
    void missingTransitions_fallsBack() {
        auto steps = buildHybridAsl9Profile();
        QList<FrameTransition> transitions;  // empty

        auto cls = classifyAutoFlowCalWindow(steps, transitions,
                                             10.0, 30.0, 1.0);

        QVERIFY(cls.fallbackToProfileScan);
    }

    // An out-of-range frame index in the transitions stream (e.g. stale
    // profile data) triggers the fallback path rather than a wrong answer.
    void outOfRangeFrame_fallsBack() {
        auto steps = buildPressureOnlyProfile();   // 3 frames
        QList<FrameTransition> transitions = {
            {0.0, 0},
            {5.0, 1},
            {10.0, 99},  // bogus
        };

        auto cls = classifyAutoFlowCalWindow(steps, transitions,
                                             12.0, 20.0, 0.8);

        QVERIFY(cls.fallbackToProfileScan);
    }

    // Multiple flow frames at different targets → picks the one closest to
    // the observed machine flow. This exercises the multi-target flow-frame
    // logic inherited from the original classifier.
    void multipleFlowTargets_picksClosest() {
        QList<ProfileFrame> steps = {
            flowFrame("preinfuse", 4.0),     // 0
            flowFrame("pour A", 2.5),        // 1
            flowFrame("pour B", 1.2),        // 2
        };
        QList<FrameTransition> transitions = {
            {0.0, 0},
            {5.0, 1},
            {15.0, 2},
        };

        // Window spans frames 1 and 2 (both flow). Mean machine flow 1.3 →
        // closest target is frame 2's 1.2.
        auto cls = classifyAutoFlowCalWindow(steps, transitions,
                                             10.0, 20.0, 1.3);

        QVERIFY(!cls.fallbackToProfileScan);
        QVERIFY(!cls.mixedMode);
        QVERIFY(cls.isFlowProfile);
        QCOMPARE(cls.targetFlow, 1.2);
    }

    // Exact distance tie between two flow targets: meanMachineFlow sits
    // precisely equidistant from both. Must deterministically pick the
    // lower-indexed frame (sorted iteration order), not whatever a QSet's
    // hash-bucket order happens to visit first — see the sort added in
    // classifyAutoFlowCalWindow() ahead of this loop.
    void multipleFlowTargets_exactTieResolvesToLowerFrameIndex() {
        QList<ProfileFrame> steps = {
            flowFrame("pour A", 1.0),   // 0
            flowFrame("pour B", 2.0),   // 1
        };
        QList<FrameTransition> transitions = {
            {0.0, 0},
            {5.0, 1},
        };

        // Window spans both frames. Mean machine flow 1.5 is exactly
        // equidistant (0.5) from both 1.0 and 2.0.
        auto cls = classifyAutoFlowCalWindow(steps, transitions,
                                             0.0, 10.0, 1.5);

        QVERIFY(!cls.fallbackToProfileScan);
        QVERIFY(!cls.mixedMode);
        QVERIFY(cls.isFlowProfile);
        QCOMPARE(cls.targetFlow, 1.0);  // frame 0, the lower index
    }

    // --- Per-profile store: what a WRITE does to the pending batch -----------
    //
    // The auto-cal path clears the batch itself before committing, so these cover
    // the writers that do not: the set_flow_calibration MCP tool and settings
    // import. A stale ideal surviving a manual write is invisible until several
    // shots later, when it skews the batch median toward the value the user
    // replaced.

    void manualSetClearsPendingBatch() {
        Settings settings;
        SettingsCalibration* cal = settings.calibration();
        const QString profile = QStringLiteral("tst_autoflowcal_profile");
        cal->clearProfileFlowCalibration(profile);

        cal->appendFlowCalPendingIdeal(profile, 1.10);
        cal->appendFlowCalPendingIdeal(profile, 1.12);
        QCOMPARE(cal->flowCalPendingIdeals(profile).size(), 2);

        QVERIFY(cal->setProfileFlowCalibration(profile, 1.40));
        QCOMPARE(cal->profileFlowCalibration(profile), 1.40);
        QVERIFY(cal->flowCalPendingIdeals(profile).isEmpty());

        cal->clearProfileFlowCalibration(profile);
    }

    // A refused write must leave BOTH the stored value and the batch alone —
    // rejecting the number and still wiping the accumulated shots would cost the
    // user a batch for a typo.
    void outOfBoundsSetIsRefusedAndKeepsBatch() {
        Settings settings;
        SettingsCalibration* cal = settings.calibration();
        const QString profile = QStringLiteral("tst_autoflowcal_profile");
        cal->clearProfileFlowCalibration(profile);

        QVERIFY(cal->setProfileFlowCalibration(profile, 1.20));
        cal->appendFlowCalPendingIdeal(profile, 1.25);

        for (double bad : {SettingsCalibration::kProfileFlowCalMin - 0.01,
                           SettingsCalibration::kProfileFlowCalMax + 0.01}) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(
                QStringLiteral("rejecting per-profile flow calibration")));
            QVERIFY(!cal->setProfileFlowCalibration(profile, bad));
        }
        QCOMPARE(cal->profileFlowCalibration(profile), 1.20);
        QCOMPARE(cal->flowCalPendingIdeals(profile).size(), 1);

        cal->clearProfileFlowCalibration(profile);
    }

    // --- Achieved-flow deviation check --------------------------------------
    //
    // autoFlowCalWindowTargetCheck() decides whether a flow-classified window
    // is SKIPPED because the pump didn't reach the frame's target flow (e.g. a
    // pressure-capped flow frame like D-Flow / D-Flow-Q — see
    // Kulitorum/Decenza#1823 and #1872). Such a window measured the sensor at a
    // flow rate the profile does not pour at, and one per-profile multiplier
    // cannot describe two operating points. It only ever triggers on
    // UNDERSHOOT: a pressure ceiling can hold flow below its setpoint but has
    // no mechanism to push flow above one, so overshoot never counts as missed
    // regardless of magnitude (see flowWindowOvershootsTarget_neverSkipped
    // below) — an overshooting window may still be genuinely PID-locked, and it
    // keeps the target-flow formula that protects flow windows from the v2
    // feedback-loop bug.
    //
    // These slots cover the PREDICATE. What the caller does with a positive
    // result (skip, since this change; re-route through the achieved-flow
    // formula, between 2.0.4 and this change) lives in
    // MainController::computeAutoFlowCalibration(), which no test constructs —
    // there is no MainController harness, and building one to reach this branch
    // would be the fault-injection shape this repo treats as a stop sign. That
    // gap is real and worth naming: it is why the re-route could be swapped for
    // a skip with this whole suite green. The behaviour is instead evidenced by
    // an offline replay of the real algorithm over 75 shots from three machines
    // — openspec/changes/skip-off-target-flow-cal-windows/evidence/.

    // Target essentially achieved — must NOT be skipped. This is the common
    // case: every window sampled on a real D-Flow/Q shot in this repo's own
    // dial-in history hit 99-101% of target.
    void flowWindowAchievesTarget_notSkipped() {
        QVERIFY(!autoFlowCalWindowTargetCheck(1.825, 1.8, kAutoFlowCalDeviationThreshold).missedTarget);  // 1.4% deviation
        QVERIFY(!autoFlowCalWindowTargetCheck(1.69, 1.7, kAutoFlowCalDeviationThreshold).missedTarget);   // 0.6% deviation
    }

    // Threshold boundary on the undershoot side: clearly-inside deviation does
    // not count as "missed"; clearly-outside does. (Not testing the exact
    // 10.000...% tie itself — 1.80 - 2.00 isn't exactly representable as 0.20
    // in `double`, so an exact-boundary assertion is a floating-point trap,
    // not a meaningful spec of the strict `>` comparison.) Also pins the
    // returned `deviation` value, since the log line at the call site depends
    // on it being the actual computed fraction, not a rounded/approximate one.
    void flowTargetDeviationThreshold_boundaryIsExclusive() {
        auto under = autoFlowCalWindowTargetCheck(1.85, 2.00, kAutoFlowCalDeviationThreshold);  // 7.5% low
        QVERIFY(!under.missedTarget);
        QVERIFY(qFuzzyCompare(under.deviation, 0.075));

        auto over = autoFlowCalWindowTargetCheck(1.75, 2.00, kAutoFlowCalDeviationThreshold);   // 12.5% low
        QVERIFY(over.missedTarget);
        QVERIFY(qFuzzyCompare(over.deviation, 0.125));
    }

    // Overshoot is NEVER skipped, no matter how large — the defining asymmetry
    // of this check. A pressure cap has no mechanism to push flow above target,
    // so there's no pressure-cap explanation for an overshoot reading, and
    // treating it as "missed target" would discard a window that is still
    // genuinely flow-controlled (see section comment above).
    void flowWindowOvershootsTarget_neverSkipped() {
        QVERIFY(!autoFlowCalWindowTargetCheck(2.15, 2.00, kAutoFlowCalDeviationThreshold).missedTarget);  // 7.5% high
        QVERIFY(!autoFlowCalWindowTargetCheck(2.25, 2.00, kAutoFlowCalDeviationThreshold).missedTarget);  // 12.5% high
        QVERIFY(!autoFlowCalWindowTargetCheck(4.00, 2.00, kAutoFlowCalDeviationThreshold).missedTarget);  // 100% high
    }

    // A non-positive target flow has nothing to compare against — must not
    // claim a deviation (and must not divide by zero).
    void flowTargetDeviation_nonPositiveTargetNeverMisses() {
        // This path is unreachable in production (see the qWarning it emits
        // in autoFlowCalWindowTargetCheck() — a canary for an upstream
        // invariant break), so calling it directly here is deliberately
        // exercising the "should never happen" guard, not routine input.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(
            QStringLiteral("non-positive target")));
        auto zero = autoFlowCalWindowTargetCheck(1.5, 0.0, kAutoFlowCalDeviationThreshold);
        QVERIFY(!zero.missedTarget);
        QCOMPARE(zero.deviation, 0.0);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(
            QStringLiteral("non-positive target")));
        auto negative = autoFlowCalWindowTargetCheck(1.5, -1.0, kAutoFlowCalDeviationThreshold);
        QVERIFY(!negative.missedTarget);
        QCOMPARE(negative.deviation, 0.0);
    }

    // Reproduces the real 8-window batch from Kulitorum/Decenza#1823's attached
    // debug log for d_flow_20g_2_50_91 (target 1.7 ml/s throughout). 3 windows
    // achieved target (shots 530, 536, 543 — 99-100%, all slightly UNDER target
    // so this data is unaffected by restricting the check to undershoot); 5 did
    // not (shots 529, 531, 537, 538, 541 — 57-67%, the frame's pressure limiter
    // holding the pump below target). At the 10% threshold, the check must
    // split them exactly along that line — this is the empirical evidence the
    // threshold was chosen from, pinned as a regression.
    void issue1823Batch_splitsTargetMetFromTargetMissed() {
        struct Window { double meanMachineFlow; bool expectMissed; };
        const QList<Window> windows = {
            {1.06846, true},   // shot 529
            {1.69798, false},  // shot 530
            {0.996183, true},  // shot 531
            {1.69086, false},  // shot 536
            {0.965689, true},  // shot 537
            {1.0688, true},    // shot 538
            {1.14582, true},   // shot 541
            {1.69011, false},  // shot 543
        };
        constexpr double kTargetFlow = 1.7;
        for (const auto& w : windows) {
            QCOMPARE(autoFlowCalWindowTargetCheck(w.meanMachineFlow, kTargetFlow, kAutoFlowCalDeviationThreshold).missedTarget,
                     w.expectMissed);
        }
    }

    // --- clearAllFlowCalPendingIdeals(): the v4 migration's primitive ---------
    //
    // The v4 migration (Settings constructor, calibration/v4AchievedFlowFormulaReset)
    // clears every profile's pending batch so none straddles the old and new
    // formula-selection logic in one median, but must leave stored multipliers
    // alone — unlike the v2/v3 migrations it sits beside, which reset everything
    // because the STORED value itself was shown corrupted. This covers the
    // primitive the migration calls; the migration's own gating (a persisted,
    // on-disk one-time flag, checked in the Settings constructor) has no
    // existing test coverage for v2/v3 either, so this doesn't introduce a
    // new gap.
    void clearAllFlowCalPendingIdeals_clearsBatchesButNotMultipliers() {
        Settings settings;
        SettingsCalibration* cal = settings.calibration();
        const QString profileA = QStringLiteral("tst_autoflowcal_v4_a");
        const QString profileB = QStringLiteral("tst_autoflowcal_v4_b");
        cal->clearProfileFlowCalibration(profileA);
        cal->clearProfileFlowCalibration(profileB);

        QVERIFY(cal->setProfileFlowCalibration(profileA, 1.23));
        QVERIFY(cal->setProfileFlowCalibration(profileB, 0.87));
        cal->appendFlowCalPendingIdeal(profileA, 0.80);
        cal->appendFlowCalPendingIdeal(profileA, 0.79);
        cal->appendFlowCalPendingIdeal(profileB, 1.10);
        QCOMPARE(cal->flowCalPendingIdeals(profileA).size(), 2);
        QCOMPARE(cal->flowCalPendingIdeals(profileB).size(), 1);

        double globalBefore = cal->flowCalibrationMultiplier();
        cal->clearAllFlowCalPendingIdeals();

        QVERIFY(cal->flowCalPendingIdeals(profileA).isEmpty());
        QVERIFY(cal->flowCalPendingIdeals(profileB).isEmpty());
        QCOMPARE(cal->profileFlowCalibration(profileA), 1.23);
        QCOMPARE(cal->profileFlowCalibration(profileB), 0.87);
        QCOMPARE(cal->flowCalibrationMultiplier(), globalBefore);

        cal->clearProfileFlowCalibration(profileA);
        cal->clearProfileFlowCalibration(profileB);
    }

    // With auto calibration off, a stored per-profile value is deliberately
    // ignored — the machine uses the global multiplier. This is what
    // set_flow_calibration reports as its `warning` case, and the reason the tool
    // does not simply claim success.
    void perProfileValueIsInertWhileAutoCalIsOff() {
        Settings settings;
        SettingsCalibration* cal = settings.calibration();
        const QString profile = QStringLiteral("tst_autoflowcal_profile");
        const bool origAuto = cal->autoFlowCalibration();
        const double origGlobal = cal->flowCalibrationMultiplier();
        cal->clearProfileFlowCalibration(profile);

        cal->setFlowCalibrationMultiplier(1.00);
        cal->setAutoFlowCalibration(true);
        QVERIFY(cal->setProfileFlowCalibration(profile, 1.40));
        QCOMPARE(cal->effectiveFlowCalibration(profile), 1.40);

        cal->setAutoFlowCalibration(false);
        QCOMPARE(cal->effectiveFlowCalibration(profile), 1.00);
        // Still on disk — turning auto back on restores it, so the tool is right
        // to call the value "stored but not in effect" rather than refusing it.
        QCOMPARE(cal->profileFlowCalibration(profile), 1.40);

        cal->clearProfileFlowCalibration(profile);
        cal->setAutoFlowCalibration(origAuto);
        cal->setFlowCalibrationMultiplier(origGlobal);
    }

    // ---- autoFlowCalSensorIdeal (v6: one formula for both control modes) ----
    //
    // These pin the two properties the v6 change rests on. Both were FALSE for
    // the expression the flow branch used up to 2.0.4
    // (`weightFlow / (targetFlow * density)`), which is why that expression
    // converged on the square root of the sensor ratio instead of the ratio;
    // see openspec/changes/skip-off-target-flow-cal-windows/design.md. Breaking
    // either assertion means someone has reintroduced a target-anchored ideal.

    // Property 1: a correctly-calibrated machine is a fixed point. Feed the
    // formula a machine whose reported flow already matches the scale (after
    // density) and it returns the multiplier unchanged, so a converged machine
    // stops moving instead of walking every batch.
    void sensorIdeal_isAFixedPointForACalibratedMachine() {
        const double density = 0.963;
        const double c = 1.35;
        // The overlay condition: reported flow equals the scale's water flow
        // once density is accounted for. That is exactly what Decent's
        // Graphical Flow Calibrator asks the operator to dial by eye, and the
        // formula must agree with it by returning the multiplier untouched.
        const double machineFlow = 1.80;
        const double weightFlow = machineFlow * density;
        QCOMPARE(autoFlowCalSensorIdeal(c, weightFlow, machineFlow, density), c);
    }

    // Property 2: invariance under the current multiplier, on a machine that
    // servos its CALIBRATED flow. Raising c makes the DE1 deliver less water to
    // hold the same REPORTED flow, so machineFlow is unchanged and weightFlow
    // falls as 1/c — and the ideal must come out the same both times. This is
    // the property that makes the update converge in one step rather than
    // oscillating, and the one the old flow-branch expression lacked.
    void sensorIdeal_isInvariantUnderTheCurrentMultiplier() {
        const double density = 0.963;
        const double machineFlow = 1.80;

        const double cLow = 1.00;
        const double weightAtLow = 1.55;
        const double idealAtLow =
            autoFlowCalSensorIdeal(cLow, weightAtLow, machineFlow, density);

        // Same machine, multiplier raised 40%: it now delivers 1/1.4 the water
        // for the same reported flow.
        const double cHigh = 1.40;
        const double weightAtHigh = weightAtLow * (cLow / cHigh);
        const double idealAtHigh =
            autoFlowCalSensorIdeal(cHigh, weightAtHigh, machineFlow, density);

        QVERIFY(qFuzzyCompare(idealAtLow, idealAtHigh));

        // The superseded flow-branch expression, on the same two shots. It is
        // NOT invariant — it moves by the full ratio of the two multipliers,
        // which is what drove the observed drift.
        const double targetFlow = machineFlow;  // window holding target
        const double oldAtLow = weightAtLow / (targetFlow * density);
        const double oldAtHigh = weightAtHigh / (targetFlow * density);
        QVERIFY(!qFuzzyCompare(oldAtLow, oldAtHigh));
        QVERIFY(qFuzzyCompare(oldAtLow / oldAtHigh, cHigh / cLow));
    }

    // The consequence. The applied multiplier is not set to the ideal directly:
    // `computeAutoFlowCalibration()` blends it in with an EMA at alpha 0.5 over
    // the batch median (kBatchEmaAlpha in maincontroller.cpp), so the update is
    //
    //     c := (1 - alpha) * c + alpha * ideal
    //
    // On a window holding target the OLD expression gives ideal = k / c, and at
    // alpha 0.5 that update is exactly Babylonian square-root iteration,
    // c := (c + k/c) / 2 — so it settles on sqrt(k), not k. That is where
    // flow-profile machines were measured to sit. The v6 expression gives
    // ideal = k regardless of c, so the same EMA settles on k.
    void oldFlowBranchUpdate_settlesOnTheSquareRootOfTheSensorRatio() {
        const double density = 0.963;
        const double machineFlow = 1.80;
        const double targetFlow = machineFlow;  // window holding target
        const double sensorRatio = 1.44;
        const double alpha = 0.5;

        // At multiplier c the machine delivers water such that the sensor
        // expression reads `sensorRatio`, i.e. w = sensorRatio * mf * rho / c.
        auto weightFlowAt = [&](double c) {
            return sensorRatio * machineFlow * density / c;
        };

        double c = 1.00;
        for (int i = 0; i < 100; ++i) {
            const double ideal = weightFlowAt(c) / (targetFlow * density);
            c = (1.0 - alpha) * c + alpha * ideal;
        }
        QVERIFY(qAbs(c - std::sqrt(sensorRatio)) < 1e-6);
        // ...and sqrt(k) is a materially different place from k.
        QVERIFY(qAbs(c - sensorRatio) > 0.2);

        // Same machine, same EMA, v6 expression: settles on the ratio itself.
        double c2 = 1.00;
        for (int i = 0; i < 100; ++i) {
            const double ideal = autoFlowCalSensorIdeal(
                c2, weightFlowAt(c2), machineFlow, density);
            c2 = (1.0 - alpha) * c2 + alpha * ideal;
        }
        QVERIFY(qAbs(c2 - sensorRatio) < 1e-6);
    }

    // The damping is load-bearing, not incidental. Applied undamped the old
    // expression does not converge at all: c := k/c is a period-2 map that
    // oscillates between its starting value and k/start forever. Worth pinning
    // so nobody "simplifies" the EMA away on the assumption that it only
    // affects convergence SPEED.
    void oldFlowBranchUpdate_undampedOscillatesRatherThanConverging() {
        const double sensorRatio = 1.44;
        const double start = 1.00;

        double c = start;
        for (int i = 0; i < 50; ++i) {
            c = sensorRatio / c;  // alpha = 1.0
        }
        QVERIFY(qFuzzyCompare(c, start));               // even iteration count
        QVERIFY(qAbs(c - std::sqrt(sensorRatio)) > 0.1);
    }
};

QTEST_GUILESS_MAIN(tst_AutoFlowCal)
#include "tst_autoflowcal.moc"
