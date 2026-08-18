#include <QtTest>
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
    // autoFlowCalWindowMissedTarget() decides whether a flow-classified window
    // gets reclassified to the pressure-branch formula because the pump didn't
    // reach the frame's target flow (e.g. a pressure-capped flow frame like
    // D-Flow / D-Flow-Q — see Kulitorum/Decenza#1823). isFlowProfile flipping to
    // false is the only effect in computeAutoFlowCalibration(); everything
    // downstream (ratio guard basis, ideal formula) is the existing, already
    // relied-upon pressure branch, unchanged by this fix.

    // Target essentially achieved (1.4% deviation) — must NOT reclassify. This
    // is the common case: every window sampled on a real D-Flow/Q shot in this
    // repo's own dial-in history hit 99-101% of target.
    void flowWindowAchievesTarget_notReclassified() {
        QVERIFY(!autoFlowCalWindowMissedTarget(1.825, 1.8, 0.10));
        QVERIFY(!autoFlowCalWindowMissedTarget(1.69, 1.7, 0.10));
    }

    // Target badly missed (37% deviation) — the exact #1823 shot538 numbers
    // (meanMachineFlow=1.0688 against target=1.7). Must reclassify.
    void flowWindowMissesTarget_reclassified() {
        QVERIFY(autoFlowCalWindowMissedTarget(1.0688, 1.7, 0.10));
    }

    // Threshold boundary: clearly-inside deviation does not count as "missed";
    // clearly-outside does, on both sides of target. (Not testing the exact
    // 10.000...% tie itself — 2.20 - 2.00 isn't exactly representable as 0.20
    // in `double`, so an exact-boundary assertion is a floating-point trap,
    // not a meaningful spec of the strict `>` comparison.)
    void flowTargetDeviationThreshold_boundaryIsExclusive() {
        QVERIFY(!autoFlowCalWindowMissedTarget(1.85, 2.00, 0.10));  // 7.5% low
        QVERIFY(autoFlowCalWindowMissedTarget(1.75, 2.00, 0.10));   // 12.5% low
        QVERIFY(!autoFlowCalWindowMissedTarget(2.15, 2.00, 0.10));  // 7.5% high
        QVERIFY(autoFlowCalWindowMissedTarget(2.25, 2.00, 0.10));   // 12.5% high
    }

    // A non-positive target flow has nothing to compare against — must not
    // claim a deviation (and must not divide by zero).
    void flowTargetDeviation_nonPositiveTargetNeverMisses() {
        QVERIFY(!autoFlowCalWindowMissedTarget(1.5, 0.0, 0.10));
        QVERIFY(!autoFlowCalWindowMissedTarget(1.5, -1.0, 0.10));
    }

    // Reproduces the real 8-window batch from Kulitorum/Decenza#1823's attached
    // debug log for d_flow_20g_2_50_91 (target 1.7 ml/s throughout). 3 windows
    // achieved target (shots 530, 536, 543 — 99-100%); 5 did not (shots 529,
    // 531, 537, 538, 541 — 57-67%, the frame's pressure limiter holding the pump
    // below target). At the 10% threshold, the predicate must split them exactly
    // along that line — this is the empirical evidence the threshold was chosen
    // from, pinned as a regression.
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
            QCOMPARE(autoFlowCalWindowMissedTarget(w.meanMachineFlow, kTargetFlow, 0.10),
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
    // primitive the migration calls; the migration's own gating (first-construction-
    // in-process) has no existing test coverage for v2/v3 either, so this doesn't
    // introduce a new gap.
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
};

QTEST_GUILESS_MAIN(tst_AutoFlowCal)
#include "tst_autoflowcal.moc"
