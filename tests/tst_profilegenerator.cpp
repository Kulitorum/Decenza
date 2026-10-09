#include <QtTest>

#include "profile/profile.h"
#include "profile/profileframe.h"
#include "profile/profilegenerator.h"
#include "profile/profileparams.h"

// Test ProfileGenerator frame generation against de1app behavior.
// D-Flow/A-Flow profiles in de1app are EDITED in-place by update_D-Flow/update_A-Flow,
// not regenerated from scratch. So tests compare generator output against the stored
// profile params + de1app formulas, NOT against the saved frame values.
//
// Not comparing against saved frames is right, but the reason once given here —
// "may have been manually tweaked in de1app's UI" — is wrong, and it matters
// because it made a stale array sound like a legitimate user preference worth
// preserving. All 12 of the stock simple profiles that carry an advanced_shot
// ship `read_only 1`, so no user edited any of them. The stored array is stale
// for a structural reason: de1app's legacy save dumps a fixed key list out of
// the GLOBAL ::settings array (vars.tcl:3305) and `advanced_shot` is in that
// list, so a simple profile saved after another profile was loaded is written
// with that other profile's frames.
//
// It is measurable, not theoretical. 10 of those 12 hold frames contradicting
// their own espresso_temperature, and five — Traditional lever machine, Trendy
// 6 bar low pressure shot, Two spring lever machine to 9 bar, Preinfuse then
// 45ml of water, Test/temperature calibration — share one byte-identical
// advanced_shot: a pour-over frame list (Prewet / Pause / Main water, on
// `sensor water`) that belongs to none of them, and to no profile in the
// corpus. It is not `Pour over.tcl`'s either — an earlier revision of this
// comment said so, having matched on the first four frame temperatures alone.
//
// This is why Profile::loadFromTclString() discards a simple profile's stored
// advanced_shot and regenerates, matching de1app's own dispatch. The formulas
// below are therefore the only oracle for that generator; the saved frames
// cannot serve as one.

class tst_ProfileGenerator : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    // ==========================================
    // D-Flow frame structure
    // (de1app D_Flow_Espresso_Profile/plugin.tcl update_D-Flow)
    // ==========================================

    void dflowAlways3Frames() {
        ProfileParams params;
        params.editorType = EditorType::DFlow;
        QList<ProfileFrame> frames = ProfileGenerator::generateFrames(params);
        QCOMPARE(frames.size(), 3);
    }

    void dflowFrameStructure() {
        // de1app D-Flow: Filling (pressure), Infusing (pressure), Pouring (flow)
        ProfileParams params;
        params.editorType = EditorType::DFlow;
        params.infusePressure = 3.0;
        params.fillTemperature = 88.0;
        params.pourTemperature = 88.0;
        params.pourFlow = 1.7;
        params.pourPressure = 8.5;
        params.infuseTime = 60.0;
        params.infuseWeight = 4.0;

        QList<ProfileFrame> frames = ProfileGenerator::generateFrames(params);

        // Frame 0: Filling — pressure pump, exit on pressure_over
        QCOMPARE(frames[0].name, QString("Filling"));
        QCOMPARE(frames[0].pump, QString("pressure"));
        QVERIFY(frames[0].exitIf);
        QCOMPARE(frames[0].exitType, QString("pressure_over"));
        QCOMPARE(frames[0].exitWeight, 5.0);  // de1app default fill weight exit

        // Frame 1: Infusing — pressure pump, NO machine exit (weight via app-side)
        QCOMPARE(frames[1].name, QString("Infusing"));
        QCOMPARE(frames[1].pump, QString("pressure"));
        QVERIFY(!frames[1].exitIf);  // de1app: exit_if 0 on infuse frame
        QCOMPARE(frames[1].seconds, 60.0);
        QCOMPARE(frames[1].exitWeight, 4.0);  // App-side weight exit

        // Frame 2: Pouring — flow pump with pressure limiter
        QCOMPARE(frames[2].name, QString("Pouring"));
        QCOMPARE(frames[2].pump, QString("flow"));
        QCOMPARE(frames[2].flow, 1.7);
        QCOMPARE(frames[2].maxFlowOrPressure, 8.5);  // Pressure cap
    }

    // ===== Fill exit pressure formula (de1app upstream D_Flow_Espresso_Profile) =====
    // if pressure < 2.8: exitP = pressure (min 1.2)
    // if pressure >= 2.8: exitP = round_to_one_digits((pressure/2) + 0.6) (min 1.2)

    void dflowFillExitPressure_data() {
        QTest::addColumn<double>("infusePressure");
        QTest::addColumn<double>("expectedExitP");

        // Below 2.8: use pressure directly (clamped to min 1.2)
        QTest::newRow("p=0.5 clamped")  << 0.5 << 1.2;
        QTest::newRow("p=1.2 at min")   << 1.2 << 1.2;
        QTest::newRow("p=2.0 direct")   << 2.0 << 2.0;
        QTest::newRow("p=2.7 below")    << 2.7 << 2.7;
        // At/above 2.8: formula path
        QTest::newRow("p=2.8 boundary") << 2.8 << 2.0;  // (2.8/2+0.6)=2.0
        QTest::newRow("p=3.0 standard") << 3.0 << 2.1;  // (3.0/2+0.6)=2.1
        QTest::newRow("p=4.0")          << 4.0 << 2.6;
        QTest::newRow("p=6.0")          << 6.0 << 3.6;
        QTest::newRow("p=8.0")          << 8.0 << 4.6;
    }

    void dflowFillExitPressure() {
        QFETCH(double, infusePressure);
        QFETCH(double, expectedExitP);

        ProfileParams params;
        params.editorType = EditorType::DFlow;
        params.infusePressure = infusePressure;

        QList<ProfileFrame> frames = ProfileGenerator::generateFrames(params);
        QVERIFY2(qAbs(frames[0].exitPressureOver - expectedExitP) < 0.01,
                 qPrintable(QString("Expected %1 but got %2 for p=%3")
                            .arg(expectedExitP).arg(frames[0].exitPressureOver).arg(infusePressure)));
    }

    void dflowInfuseDisabledZeroSeconds() {
        // "No soak" is infuseTime 0 — the machine skips a zero-length frame, and
        // it is how the plugins express a disabled step everywhere (2nd_fill,
        // pause, ramp_down). The separate infuseEnabled boolean is gone.
        ProfileParams params;
        params.editorType = EditorType::DFlow;
        params.infuseTime = 0.0;

        QList<ProfileFrame> frames = ProfileGenerator::generateFrames(params);
        QCOMPARE(frames.size(), 3);  // Still 3 frames
        QCOMPARE(frames[1].name, QString("Infusing"));
        QCOMPARE(frames[1].seconds, 0.0);
    }

    void dflowInfuseDisabledNoWeightExit() {
        ProfileParams params;
        params.editorType = EditorType::DFlow;
        params.infuseTime = 0.0;
        params.infuseWeight = 0.0;

        QList<ProfileFrame> frames = ProfileGenerator::generateFrames(params);
        QCOMPARE(frames[1].exitWeight, 0.0);  // No weight exit without a target
    }

    // ==========================================
    // A-Flow frame structure
    // (de1app Jan3kJ/A_Flow plugin)
    // ==========================================

    void aflowFrameCountDefault() {
        // de1app A_Flow/plugin.tcl update_A-Flow: always 9 frames:
        //   Pre Fill, Fill, Infuse, 2nd Fill, Pause,
        //   Ramp Up, Ramp Down, Pouring Start, Pouring
        ProfileParams params;
        params.editorType = EditorType::AFlow;
        params.applyEditorDefaults();
        QList<ProfileFrame> frames = ProfileGenerator::generateFrames(params);
        QCOMPARE(frames.size(), 9);
    }

    void aflowSecondFillActivatesFrames() {
        // A-Flow always emits the same frame count; secondFillEnabled changes
        // the "2nd Fill" and "Pause" frame seconds from 0 to non-zero
        // (machine skips frames with seconds=0)
        ProfileParams params;
        params.editorType = EditorType::AFlow;
        params.applyEditorDefaults();
        params.secondFillEnabled = false;

        QList<ProfileFrame> framesOff = ProfileGenerator::generateFrames(params);

        params.secondFillEnabled = true;
        QList<ProfileFrame> framesOn = ProfileGenerator::generateFrames(params);

        QCOMPARE(framesOff.size(), framesOn.size());  // Same count

        // Find the "2nd Fill" frame and verify seconds changes
        bool foundSecondFill = false;
        for (int i = 0; i < framesOn.size(); ++i) {
            if (framesOn[i].name == "2nd Fill") {
                QVERIFY(framesOn[i].seconds > 0);   // Active when enabled
                QCOMPARE(framesOff[i].seconds, 0.0); // Inactive when disabled
                foundSecondFill = true;
                break;
            }
        }
        QVERIFY(foundSecondFill);
    }

    // ==========================================
    // Pressure profile (de1app pressure_to_advanced_list)
    // ==========================================

    void pressureProfileStructure() {
        // de1app profile.tcl pressure_to_advanced_list:
        // preinfusion(flow) + forced_rise(3s,no limiter) + hold(remaining,limiter) + decline(smooth,limiter)
        ProfileParams params;
        params.editorType = EditorType::Pressure;
        params.preinfusionTime = 5.0;
        params.preinfusionFlowRate = 4.0;
        params.preinfusionStopPressure = 4.0;
        params.holdTime = 10.0;
        params.espressoPressure = 9.2;
        params.simpleDeclineTime = 25.0;
        params.pressureEnd = 4.0;
        params.limiterValue = 6.0;
        params.limiterRange = 1.0;

        QList<ProfileFrame> frames = ProfileGenerator::generateFrames(params);

        // 4 frames: preinfusion + forced_rise(3s) + hold(7s) + decline
        QCOMPARE(frames.size(), 4);

        // de1app: preinfusion exit_flow_over 6 (not 0)
        QCOMPARE(frames[0].pump, QString("flow"));
        QCOMPARE(frames[0].exitFlowOver, 6.0);

        // de1app: forced rise = 3s, limited like the hold and decline below
        QCOMPARE(frames[1].name, QString("forced rise"));
        QCOMPARE(frames[1].seconds, 3.0);
        QCOMPARE(frames[1].maxFlowOrPressure, 6.0);

        // de1app: hold = remaining time with limiter
        QCOMPARE(frames[2].seconds, 7.0);  // 10-3
        QCOMPARE(frames[2].maxFlowOrPressure, 6.0);

        // de1app: decline smooth with limiter
        QCOMPARE(frames[3].transition, QString("smooth"));
        QCOMPARE(frames[3].pressure, 4.0);
        QCOMPARE(frames[3].maxFlowOrPressure, 6.0);
    }

    // ==========================================
    // Flow profile (de1app flow_to_advanced_list)
    // ==========================================

    void flowProfileStructure() {
        // de1app profile.tcl flow_to_advanced_list:
        // preinfusion(flow) + hold(flow) + decline(flow,smooth)
        // NO forced rise for flow profiles
        ProfileParams params;
        params.editorType = EditorType::Flow;
        params.preinfusionTime = 5.0;
        params.preinfusionFlowRate = 4.0;
        params.preinfusionStopPressure = 4.0;
        params.holdTime = 8.0;
        params.holdFlow = 2.2;
        params.simpleDeclineTime = 17.0;
        params.flowEnd = 1.8;
        params.limiterValue = 9.0;
        params.limiterRange = 0.9;

        QList<ProfileFrame> frames = ProfileGenerator::generateFrames(params);

        // 3 frames: preinfusion + hold + decline (no forced rise)
        QCOMPARE(frames.size(), 3);

        // de1app: flow preinfusion has exit_flow_over 0 (unlike pressure which has 6)
        QCOMPARE(frames[0].pump, QString("flow"));
        QCOMPARE(frames[0].exitFlowOver, 0.0);

        // de1app: hold is flow pump with limiter
        QCOMPARE(frames[1].pump, QString("flow"));
        QCOMPARE(frames[1].flow, 2.2);
        QCOMPARE(frames[1].maxFlowOrPressure, 9.0);

        // de1app: decline gated by holdTime > 0, smooth transition
        QCOMPARE(frames[2].transition, QString("smooth"));
        QCOMPARE(frames[2].flow, 1.8);
    }

    void flowProfileNoDeclineWhenNoHold() {
        // de1app flow_to_advanced_list: decline only generated when holdTime > 0
        ProfileParams params;
        params.editorType = EditorType::Flow;
        params.preinfusionTime = 5.0;
        params.holdTime = 0.0;
        params.simpleDeclineTime = 17.0;

        QList<ProfileFrame> frames = ProfileGenerator::generateFrames(params);
        QCOMPARE(frames.size(), 1);  // Only preinfusion
    }

    // ==========================================
    // createProfile metadata
    // ==========================================

    void createProfileSetsCorrectType_data() {
        QTest::addColumn<int>("editorType");
        QTest::addColumn<QString>("expectedProfileType");
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("expectedEditorType");

        // de1app: settings_2a = pressure, settings_2b = flow, settings_2c = advanced
        QTest::newRow("Pressure") << int(EditorType::Pressure) << "settings_2a" << "Pressure Test" << "pressure";
        QTest::newRow("Flow")     << int(EditorType::Flow)     << "settings_2b" << "Flow Test" << "flow";
        QTest::newRow("DFlow")    << int(EditorType::DFlow)    << "settings_2c" << "D-Flow / Test" << "dflow";
        QTest::newRow("AFlow")    << int(EditorType::AFlow)    << "settings_2c" << "A-Flow / Test" << "aflow";
    }

    void createProfileSetsCorrectType() {
        QFETCH(int, editorType);
        QFETCH(QString, expectedProfileType);
        QFETCH(QString, title);
        QFETCH(QString, expectedEditorType);

        ProfileParams params;
        params.editorType = static_cast<EditorType>(editorType);
        Profile p = ProfileGenerator::createProfile(params, title);
        QCOMPARE(p.profileType(), expectedProfileType);
        QCOMPARE(p.editorType(), expectedEditorType);
    }

    void createProfilePreservesParams() {
        ProfileParams params;
        params.editorType = EditorType::DFlow;
        params.targetWeight = 42.0;
        params.pourFlow = 3.0;
        params.infusePressure = 4.0;

        Profile p = ProfileGenerator::createProfile(params, "D-Flow / Roundtrip");
        QCOMPARE(p.profileParams().targetWeight, 42.0);
        QCOMPARE(p.profileParams().pourFlow, 3.0);
        QCOMPARE(p.profileParams().infusePressure, 4.0);
        QCOMPARE(p.targetWeight(), 42.0);
    }

    void createProfileSetsPreinfuseFrameCount() {
        ProfileParams params;
        params.editorType = EditorType::DFlow;
        params.preinfuseFrameCount = 2;

        Profile p = ProfileGenerator::createProfile(params, "DFlow Test");
        QCOMPARE(p.preinfuseFrameCount(), 2);
    }

    void pressureProfileCountsForcedRiseAsPreinfusion() {
        // Same shape as pressureProfileStructure(): preinfusion + one forced-rise + hold +
        // decline. The forced-rise frame fills headspace before any coffee pours, so it must
        // be excluded from Stop-at-Volume's pour count — matching de1app commit 13a30463.
        ProfileParams params;
        params.editorType = EditorType::Pressure;
        params.preinfusionTime = 5.0;
        params.preinfusionFlowRate = 4.0;
        params.preinfusionStopPressure = 4.0;
        params.holdTime = 10.0;
        params.espressoPressure = 9.2;
        params.simpleDeclineTime = 25.0;
        params.pressureEnd = 4.0;

        Profile p = ProfileGenerator::createProfile(params, "Pressure Test");
        // 1 leading preinfusion frame (exitIf==true) + 1 forced-rise frame before Hold
        QCOMPARE(p.preinfuseFrameCount(), 2);
    }

    void pressureProfileCountsBothForcedRiseFrames() {
        // holdTime > 3 triggers a forced-rise before Hold; after the 3s decrement holdTime
        // drops below 3, and declineTime > 3 triggers a second forced-rise before Decline.
        // Both must be excluded from the pour count.
        ProfileParams params;
        params.editorType = EditorType::Pressure;
        params.preinfusionTime = 5.0;
        params.preinfusionFlowRate = 4.0;
        params.preinfusionStopPressure = 4.0;
        params.holdTime = 3.5;
        params.espressoPressure = 9.2;
        params.simpleDeclineTime = 25.0;
        params.pressureEnd = 4.0;

        Profile p = ProfileGenerator::createProfile(params, "Pressure Double Rise Test");
        // 1 leading preinfusion frame + 2 forced-rise frames (before Hold, before Decline)
        QCOMPARE(p.preinfuseFrameCount(), 3);
    }

    void flowProfilePreinfuseCountUnaffectedByForcedRiseFix() {
        // Flow-type profiles generate no forced-rise frame, so this fix must not change
        // their preinfuse count.
        ProfileParams params;
        params.editorType = EditorType::Flow;
        params.preinfusionTime = 5.0;
        params.preinfusionFlowRate = 4.0;
        params.preinfusionStopPressure = 4.0;
        params.holdTime = 8.0;
        params.holdFlow = 2.2;
        params.simpleDeclineTime = 17.0;
        params.flowEnd = 1.8;

        Profile p = ProfileGenerator::createProfile(params, "Flow Test");
        QCOMPARE(p.preinfuseFrameCount(), 1);
    }

    void createProfileUsesFirstFrameTemp() {
        // de1app: espresso_temperature matches first frame temp
        ProfileParams params;
        params.editorType = EditorType::DFlow;
        params.fillTemperature = 85.0;  // First frame temp

        Profile p = ProfileGenerator::createProfile(params, "Temp Test");
        QCOMPARE(p.espressoTemperature(), 85.0);
    }
};

QTEST_GUILESS_MAIN(tst_ProfileGenerator)
#include "tst_profilegenerator.moc"
