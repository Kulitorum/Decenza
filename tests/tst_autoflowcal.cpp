#include <QtTest>

#include "controllers/autoflowcalclassifier.h"
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
};

QTEST_GUILESS_MAIN(tst_AutoFlowCal)
#include "tst_autoflowcal.moc"
