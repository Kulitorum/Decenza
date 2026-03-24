#include <QtTest>

#include "profile/profile.h"
#include "profile/profileframe.h"
#include "profile/recipegenerator.h"
#include "profile/recipeparams.h"

// Test RecipeGenerator frame generation against de1app behavior.
// D-Flow/A-Flow profiles in de1app are EDITED in-place by update_D-Flow/update_A-Flow,
// not regenerated from scratch. So tests compare generator output against the stored
// recipe params + de1app formulas, not against saved frame values (which may have been
// manually tweaked in de1app's UI).

class tst_RecipeGenerator : public QObject {
    Q_OBJECT

private slots:

    // ==========================================
    // D-Flow frame structure
    // (de1app D_Flow_Espresso_Profile/plugin.tcl update_D-Flow)
    // ==========================================

    void dflowAlways3Frames() {
        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        QList<ProfileFrame> frames = RecipeGenerator::generateFrames(recipe);
        QCOMPARE(frames.size(), 3);
    }

    void dflowFrameStructure() {
        // de1app D-Flow: Filling (pressure), Infusing (pressure), Pouring (flow)
        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        recipe.infusePressure = 3.0;
        recipe.fillTemperature = 88.0;
        recipe.pourTemperature = 88.0;
        recipe.pourFlow = 1.7;
        recipe.pourPressure = 8.5;
        recipe.infuseTime = 60.0;
        recipe.infuseWeight = 4.0;

        QList<ProfileFrame> frames = RecipeGenerator::generateFrames(recipe);

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

        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        recipe.infusePressure = infusePressure;

        QList<ProfileFrame> frames = RecipeGenerator::generateFrames(recipe);
        QVERIFY2(qAbs(frames[0].exitPressureOver - expectedExitP) < 0.01,
                 qPrintable(QString("Expected %1 but got %2 for p=%3")
                            .arg(expectedExitP).arg(frames[0].exitPressureOver).arg(infusePressure)));
    }

    void dflowInfuseDisabledZeroSeconds() {
        // de1app: when infuse disabled, seconds=0 causes machine to skip frame
        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        recipe.infuseEnabled = false;

        QList<ProfileFrame> frames = RecipeGenerator::generateFrames(recipe);
        QCOMPARE(frames.size(), 3);  // Still 3 frames
        QCOMPARE(frames[1].name, QString("Infusing"));
        QCOMPARE(frames[1].seconds, 0.0);
    }

    void dflowInfuseDisabledNoWeightExit() {
        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        recipe.infuseEnabled = false;
        recipe.infuseWeight = 4.0;

        QList<ProfileFrame> frames = RecipeGenerator::generateFrames(recipe);
        QCOMPARE(frames[1].exitWeight, 0.0);  // No weight exit when disabled
    }

    // ==========================================
    // A-Flow frame structure
    // (de1app Jan3kJ/A_Flow plugin)
    // ==========================================

    void aflowFrameCountDefault() {
        // de1app A_Flow/plugin.tcl update_A-Flow: always 9 frames:
        //   Pre Fill, Fill, Infuse, 2nd Fill, Pause,
        //   Ramp Up, Ramp Down, Pouring Start, Pouring
        RecipeParams recipe;
        recipe.editorType = EditorType::AFlow;
        recipe.applyEditorDefaults();
        QList<ProfileFrame> frames = RecipeGenerator::generateFrames(recipe);
        QCOMPARE(frames.size(), 9);
    }

    void aflowSecondFillActivatesFrames() {
        // A-Flow always emits the same frame count; secondFillEnabled changes
        // the "2nd Fill" and "Pause" frame seconds from 0 to non-zero
        // (machine skips frames with seconds=0)
        RecipeParams recipe;
        recipe.editorType = EditorType::AFlow;
        recipe.applyEditorDefaults();
        recipe.secondFillEnabled = false;

        QList<ProfileFrame> framesOff = RecipeGenerator::generateFrames(recipe);

        recipe.secondFillEnabled = true;
        QList<ProfileFrame> framesOn = RecipeGenerator::generateFrames(recipe);

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
        RecipeParams recipe;
        recipe.editorType = EditorType::Pressure;
        recipe.preinfusionTime = 5.0;
        recipe.preinfusionFlowRate = 4.0;
        recipe.preinfusionStopPressure = 4.0;
        recipe.holdTime = 10.0;
        recipe.espressoPressure = 9.2;
        recipe.simpleDeclineTime = 25.0;
        recipe.pressureEnd = 4.0;
        recipe.limiterValue = 6.0;
        recipe.limiterRange = 1.0;

        QList<ProfileFrame> frames = RecipeGenerator::generateFrames(recipe);

        // 4 frames: preinfusion + forced_rise(3s) + hold(7s) + decline
        QCOMPARE(frames.size(), 4);

        // de1app: preinfusion exit_flow_over 6 (not 0)
        QCOMPARE(frames[0].pump, QString("flow"));
        QCOMPARE(frames[0].exitFlowOver, 6.0);

        // de1app: forced rise = 3s, no limiter
        QCOMPARE(frames[1].name, QString("forced rise without limit"));
        QCOMPARE(frames[1].seconds, 3.0);
        QCOMPARE(frames[1].maxFlowOrPressure, 0.0);

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
        RecipeParams recipe;
        recipe.editorType = EditorType::Flow;
        recipe.preinfusionTime = 5.0;
        recipe.preinfusionFlowRate = 4.0;
        recipe.preinfusionStopPressure = 4.0;
        recipe.holdTime = 8.0;
        recipe.holdFlow = 2.2;
        recipe.simpleDeclineTime = 17.0;
        recipe.flowEnd = 1.8;
        recipe.limiterValue = 9.0;
        recipe.limiterRange = 0.9;

        QList<ProfileFrame> frames = RecipeGenerator::generateFrames(recipe);

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
        RecipeParams recipe;
        recipe.editorType = EditorType::Flow;
        recipe.preinfusionTime = 5.0;
        recipe.holdTime = 0.0;
        recipe.simpleDeclineTime = 17.0;

        QList<ProfileFrame> frames = RecipeGenerator::generateFrames(recipe);
        QCOMPARE(frames.size(), 1);  // Only preinfusion
    }

    // ==========================================
    // createProfile metadata
    // ==========================================

    void createProfileSetsCorrectType_data() {
        QTest::addColumn<int>("editorType");
        QTest::addColumn<QString>("expectedProfileType");

        // de1app: settings_2a = pressure, settings_2b = flow, settings_2c = advanced
        QTest::newRow("Pressure") << int(EditorType::Pressure) << "settings_2a";
        QTest::newRow("Flow")     << int(EditorType::Flow)     << "settings_2b";
        QTest::newRow("DFlow")    << int(EditorType::DFlow)    << "settings_2c";
        QTest::newRow("AFlow")    << int(EditorType::AFlow)    << "settings_2c";
    }

    void createProfileSetsCorrectType() {
        QFETCH(int, editorType);
        QFETCH(QString, expectedProfileType);

        RecipeParams recipe;
        recipe.editorType = static_cast<EditorType>(editorType);
        Profile p = RecipeGenerator::createProfile(recipe, "Test");
        QCOMPARE(p.profileType(), expectedProfileType);
        QVERIFY(p.isRecipeMode());
    }

    void createProfilePreservesRecipe() {
        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        recipe.targetWeight = 42.0;
        recipe.pourFlow = 3.0;
        recipe.infusePressure = 4.0;

        Profile p = RecipeGenerator::createProfile(recipe, "Roundtrip");
        QCOMPARE(p.recipeParams().targetWeight, 42.0);
        QCOMPARE(p.recipeParams().pourFlow, 3.0);
        QCOMPARE(p.recipeParams().infusePressure, 4.0);
        QCOMPARE(p.targetWeight(), 42.0);
    }

    void createProfileSetsPreinfuseFrameCount() {
        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        recipe.preinfuseFrameCount = 2;

        Profile p = RecipeGenerator::createProfile(recipe, "DFlow Test");
        QCOMPARE(p.preinfuseFrameCount(), 2);
    }

    void createProfileUsesFirstFrameTemp() {
        // de1app: espresso_temperature matches first frame temp
        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        recipe.fillTemperature = 85.0;  // First frame temp

        Profile p = RecipeGenerator::createProfile(recipe, "Temp Test");
        QCOMPARE(p.espressoTemperature(), 85.0);
    }
};

QTEST_GUILESS_MAIN(tst_RecipeGenerator)
#include "tst_recipegenerator.moc"
