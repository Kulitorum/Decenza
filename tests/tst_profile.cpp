#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

#include "profile/profile.h"
#include "profile/profileframe.h"
#include "profile/recipegenerator.h"
#include "profile/recipeparams.h"

// Test Profile JSON/TCL parsing, frame generation, and BLE encoding.
// Expected values derived from de1app procs. Profile is not a QObject — no friend access needed.

class tst_Profile : public QObject {
    Q_OBJECT

private:
    // Build a minimal advanced profile JSON object
    static QJsonObject makeAdvancedProfileJson(const QString& title = "Test Profile") {
        QJsonObject obj;
        obj["title"] = title;
        obj["author"] = "test";
        obj["notes"] = "test notes";
        obj["beverage_type"] = "espresso";
        obj["version"] = "2";
        obj["legacy_profile_type"] = "settings_2c";
        obj["target_weight"] = 36.0;
        obj["target_volume"] = 0.0;
        obj["espresso_temperature"] = 93.0;
        obj["maximum_pressure"] = 12.0;
        obj["maximum_flow"] = 6.0;
        obj["minimum_pressure"] = 0.0;
        obj["number_of_preinfuse_frames"] = 1;

        // One simple frame with nested exit
        QJsonObject frame;
        frame["name"] = "preinfusion";
        frame["temperature"] = 88.0;
        frame["sensor"] = "coffee";
        frame["pump"] = "flow";
        frame["transition"] = "fast";
        frame["pressure"] = 1.0;
        frame["flow"] = 4.0;
        frame["seconds"] = 20.0;
        frame["volume"] = 0.0;

        QJsonObject exitObj;
        exitObj["type"] = "pressure";
        exitObj["condition"] = "over";
        exitObj["value"] = 4.0;
        frame["exit"] = exitObj;

        QJsonObject limiterObj;
        limiterObj["value"] = 0.0;
        limiterObj["range"] = 0.6;
        frame["limiter"] = limiterObj;

        QJsonArray steps;
        steps.append(frame);
        obj["steps"] = steps;

        return obj;
    }

    // Build a frame JSON with specific exit type
    static QJsonObject makeFrameJson(const QString& exitType, const QString& exitCondition, double exitValue) {
        QJsonObject frame;
        frame["name"] = "test frame";
        frame["temperature"] = 93.0;
        frame["sensor"] = "coffee";
        frame["pump"] = (exitType == "flow") ? "flow" : "pressure";
        frame["transition"] = "fast";
        frame["pressure"] = 9.0;
        frame["flow"] = 2.0;
        frame["seconds"] = 30.0;
        frame["volume"] = 0.0;

        QJsonObject exitObj;
        exitObj["type"] = exitType;
        exitObj["condition"] = exitCondition;
        exitObj["value"] = exitValue;
        frame["exit"] = exitObj;

        return frame;
    }

private slots:

    // ==========================================
    // JSON Round-Trip Tests
    // ==========================================

    void jsonRoundTripAdvanced() {
        QJsonObject obj = makeAdvancedProfileJson();
        QJsonDocument doc(obj);

        Profile p = Profile::fromJson(doc);
        QJsonDocument serialized = p.toJson();
        Profile p2 = Profile::fromJson(serialized);

        QCOMPARE(p2.title(), QString("Test Profile"));
        QCOMPARE(p2.author(), QString("test"));
        QCOMPARE(p2.profileNotes(), QString("test notes"));
        QCOMPARE(p2.profileType(), QString("settings_2c"));
        QCOMPARE(p2.targetWeight(), 36.0);
        QCOMPARE(p2.targetVolume(), 0.0);
        QCOMPARE(p2.espressoTemperature(), 88.0);  // Synced from first frame for advanced profiles
        QCOMPARE(p2.steps().size(), 1);
        QCOMPARE(p2.preinfuseFrameCount(), 1);
    }

    void jsonLegacyFlatFieldsFallback() {
        // Old Decenza format: profile_notes, profile_type, preinfuse_frame_count, flat exit fields
        QJsonObject obj;
        obj["title"] = "Legacy";
        obj["profile_notes"] = "legacy notes";        // Not "notes"
        obj["profile_type"] = "settings_2c";           // Not "legacy_profile_type"
        obj["preinfuse_frame_count"] = 2;              // Not "number_of_preinfuse_frames"

        QJsonObject frame;
        frame["name"] = "test";
        frame["temperature"] = 93.0;
        frame["pump"] = "flow";
        frame["flow"] = 4.0;
        frame["seconds"] = 20.0;
        frame["exit_if"] = true;                       // Flat field
        frame["exit_type"] = "pressure_over";          // Flat field
        frame["exit_pressure_over"] = 4.0;             // Flat field
        frame["max_flow_or_pressure"] = 2.5;           // Flat limiter field
        frame["max_flow_or_pressure_range"] = 0.8;

        QJsonArray steps;
        steps.append(frame);
        obj["steps"] = steps;

        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.profileNotes(), QString("legacy notes"));
        QCOMPARE(p.profileType(), QString("settings_2c"));
        QCOMPARE(p.preinfuseFrameCount(), 2);
        QCOMPARE(p.steps().size(), 1);
        QVERIFY(p.steps()[0].exitIf);
        QCOMPARE(p.steps()[0].exitType, QString("pressure_over"));
        QCOMPARE(p.steps()[0].exitPressureOver, 4.0);
        QCOMPARE(p.steps()[0].maxFlowOrPressure, 2.5);
        QCOMPARE(p.steps()[0].maxFlowOrPressureRange, 0.8);
    }

    void jsonStringEncodedNumbers() {
        // de1app encodes numbers as strings — jsonToDouble must handle this
        QJsonObject obj;
        obj["title"] = "De1App Strings";
        obj["legacy_profile_type"] = "settings_2c";
        obj["target_weight"] = QString("36.0");        // String, not number
        obj["target_volume"] = QString("100");
        obj["espresso_temperature"] = QString("93.5");
        obj["number_of_preinfuse_frames"] = QString("2");

        QJsonObject frame;
        frame["name"] = "test";
        frame["temperature"] = QString("88.0");
        frame["pressure"] = QString("9.0");
        frame["flow"] = QString("2.0");
        frame["seconds"] = QString("30.0");
        QJsonArray steps;
        steps.append(frame);
        obj["steps"] = steps;

        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.targetWeight(), 36.0);
        QCOMPARE(p.targetVolume(), 100.0);
        QCOMPARE(p.espressoTemperature(), 88.0);  // Synced from first frame
        QCOMPARE(p.preinfuseFrameCount(), 2);
        QCOMPARE(p.steps()[0].temperature, 88.0);
        QCOMPARE(p.steps()[0].pressure, 9.0);
    }

    // ===== Nested exit conditions (de1app v2 format) =====

    void jsonNestedExitPressureOver() {
        QJsonObject frame = makeFrameJson("pressure", "over", 3.0);
        ProfileFrame pf = ProfileFrame::fromJson(frame);
        QVERIFY(pf.exitIf);
        QCOMPARE(pf.exitType, QString("pressure_over"));
        QCOMPARE(pf.exitPressureOver, 3.0);
    }

    void jsonNestedExitPressureUnder() {
        QJsonObject frame = makeFrameJson("pressure", "under", 2.0);
        ProfileFrame pf = ProfileFrame::fromJson(frame);
        QVERIFY(pf.exitIf);
        QCOMPARE(pf.exitType, QString("pressure_under"));
        QCOMPARE(pf.exitPressureUnder, 2.0);
    }

    void jsonNestedExitFlowOver() {
        QJsonObject frame = makeFrameJson("flow", "over", 4.0);
        ProfileFrame pf = ProfileFrame::fromJson(frame);
        QVERIFY(pf.exitIf);
        QCOMPARE(pf.exitType, QString("flow_over"));
        QCOMPARE(pf.exitFlowOver, 4.0);
    }

    void jsonNestedExitFlowUnder() {
        QJsonObject frame = makeFrameJson("flow", "under", 1.5);
        ProfileFrame pf = ProfileFrame::fromJson(frame);
        QVERIFY(pf.exitIf);
        QCOMPARE(pf.exitType, QString("flow_under"));
        QCOMPARE(pf.exitFlowUnder, 1.5);
    }

    void jsonWeightExitIndependent() {
        // Weight exit is independent of the exit object — both can coexist.
        // A frame can have exit_if=false (no machine-side exit) with weight > 0.
        QJsonObject frame;
        frame["name"] = "test";
        frame["temperature"] = 93.0;
        frame["pump"] = "pressure";
        frame["pressure"] = 3.0;
        frame["seconds"] = 20.0;
        // No "exit" object → exitIf should be false
        frame["weight"] = 4.0;  // App-side weight exit

        ProfileFrame pf = ProfileFrame::fromJson(frame);
        QVERIFY(!pf.exitIf);           // No machine-side exit
        QCOMPARE(pf.exitWeight, 4.0);  // But weight exit IS set
    }

    void jsonLimiterNestedRoundTrip() {
        // D-Flow pattern: limiter value=0, range=0.2 (always saved for fidelity)
        QJsonObject frame;
        frame["name"] = "pour";
        frame["temperature"] = 93.0;
        frame["pump"] = "flow";
        frame["flow"] = 2.0;
        frame["seconds"] = 30.0;

        QJsonObject limiter;
        limiter["value"] = 0.0;
        limiter["range"] = 0.2;
        frame["limiter"] = limiter;

        ProfileFrame pf = ProfileFrame::fromJson(frame);
        QCOMPARE(pf.maxFlowOrPressure, 0.0);
        QCOMPARE(pf.maxFlowOrPressureRange, 0.2);

        // Serialize back and verify limiter round-trips
        QJsonObject serialized = pf.toJson();
        QVERIFY(serialized.contains("limiter"));
        QJsonObject limOut = serialized["limiter"].toObject();
        QCOMPARE(limOut["value"].toDouble(), 0.0);
        QCOMPARE(limOut["range"].toDouble(), 0.2);
    }

    // ===== Bug #425: preinfuseFrameCount preserved from JSON =====

    void preinfuseFrameCountPreserved() {
        QJsonObject obj = makeAdvancedProfileJson();
        obj["number_of_preinfuse_frames"] = 2;  // Explicitly set

        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.preinfuseFrameCount(), 2);    // Must NOT recompute from frames

        // Round-trip
        QJsonDocument serialized = p.toJson();
        Profile p2 = Profile::fromJson(serialized);
        QCOMPARE(p2.preinfuseFrameCount(), 2);
    }

    void preinfuseFrameCountExplicitOverridesAutoCount() {
        // When number_of_preinfuse_frames is explicitly set, it overrides auto-counting.
        // Even if the frames have different exit patterns, the explicit value wins.
        QJsonObject obj;
        obj["title"] = "Explicit Count";
        obj["legacy_profile_type"] = "settings_2c";
        obj["number_of_preinfuse_frames"] = 3;

        // Only 1 frame with exit, but explicit count says 3
        QJsonObject f0;
        f0["name"] = "preinfusion";
        f0["temperature"] = 88.0;
        f0["pump"] = "flow";
        f0["flow"] = 4.0;
        f0["seconds"] = 20.0;
        QJsonObject exit0;
        exit0["type"] = "pressure";
        exit0["condition"] = "over";
        exit0["value"] = 4.0;
        f0["exit"] = exit0;

        QJsonObject f1;
        f1["name"] = "pouring";
        f1["temperature"] = 93.0;
        f1["pump"] = "flow";
        f1["flow"] = 2.0;
        f1["seconds"] = 30.0;

        QJsonArray steps;
        steps.append(f0);
        steps.append(f1);
        obj["steps"] = steps;

        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.preinfuseFrameCount(), 3);  // Explicit value wins over auto-count
    }

    // ===== Bug #517: simple profiles derive editorType from profileType, not is_recipe_mode =====

    void simpleProfileAutoFixRecipeMode() {
        QJsonObject obj;
        obj["title"] = "Simple Pressure";
        obj["legacy_profile_type"] = "settings_2a";
        obj["is_recipe_mode"] = true;  // Legacy flag — should be ignored for settings_2a
        obj["espresso_temperature"] = 93.0;
        obj["preinfusion_time"] = 5.0;
        obj["preinfusion_flow_rate"] = 4.0;
        obj["preinfusion_stop_pressure"] = 4.0;
        obj["espresso_hold_time"] = 10.0;
        obj["espresso_pressure"] = 9.2;
        obj["espresso_decline_time"] = 25.0;
        obj["pressure_end"] = 4.0;

        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.editorType(), QString("pressure"));
    }

    // ===== Editor type inference from title =====

    void editorTypeInferenceAFlow() {
        QJsonObject obj = makeAdvancedProfileJson("A-Flow Medium Roast");
        obj["is_recipe_mode"] = true;
        // Remove editorType from recipe JSON to trigger title-based inference
        QJsonObject recipeJson = RecipeParams().toJson();
        recipeJson.remove("editorType");
        obj["recipe"] = recipeJson;

        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.editorType(), QString("aflow"));
        QCOMPARE(p.recipeParams().editorType, EditorType::AFlow);
    }

    void editorTypeInferenceDFlowDefault() {
        QJsonObject obj = makeAdvancedProfileJson("D-Flow Default");
        obj["is_recipe_mode"] = true;
        obj["recipe"] = RecipeParams().toJson();

        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.editorType(), QString("dflow"));
    }

    void editorTypeInferencePressure() {
        QJsonObject obj;
        obj["title"] = "My Pressure Profile";
        obj["legacy_profile_type"] = "settings_2a";
        obj["is_recipe_mode"] = true;  // Legacy flag — overridden by profileType
        obj["recipe"] = RecipeParams().toJson();
        obj["steps"] = QJsonArray();  // Empty, will be generated

        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.editorType(), QString("pressure"));
    }

    // ===== editorType changes when title changes (fully derived) =====

    void editorTypeChangesWithTitle() {
        // D-Flow profile renamed → becomes advanced (matches de1app behavior)
        QJsonObject obj = makeAdvancedProfileJson("My Morning Shot");
        obj["recipe"] = RecipeParams().toJson();

        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.editorType(), QString("advanced"));  // No D-Flow title → advanced
    }

    // ===== editorType round-trip serialization =====

    void editorTypeRoundTrip() {
        // D-Flow title → dflow, round-trip preserves it (title preserved)
        QJsonObject obj = makeAdvancedProfileJson("D-Flow Test");
        obj["recipe"] = RecipeParams().toJson();

        Profile p1 = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p1.editorType(), QString("dflow"));

        // Round-trip through toJson/fromJson
        QJsonDocument doc = p1.toJson();
        Profile p2 = Profile::fromJson(doc);
        QCOMPARE(p2.editorType(), QString("dflow"));

        // Verify is_recipe_mode and editor_type are NOT in the output (fully derived)
        QJsonObject out = doc.object();
        QVERIFY(!out.contains("is_recipe_mode"));
        QVERIFY(!out.contains("editor_type"));
    }

    void editorTypeDeriveFromTitleNoDFlags() {
        // de1app import: D-Flow title, no is_recipe_mode, no editor_type
        QJsonObject obj = makeAdvancedProfileJson("D-Flow La Pavoni");
        // No editor_type, no is_recipe_mode — must derive from title

        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.editorType(), QString("dflow"));
    }

    void editorTypeDeriveFromTitleAFlow() {
        QJsonObject obj = makeAdvancedProfileJson("A-Flow Medium Roast");

        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.editorType(), QString("aflow"));
    }

    void editorTypeDeriveSettings2b() {
        // Pure settings_2b with no recipe flags
        QJsonObject obj;
        obj["title"] = "Flow Profile";
        obj["legacy_profile_type"] = "settings_2b";
        obj["steps"] = QJsonArray();

        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.editorType(), QString("flow"));
    }

    void editorTypeAdvancedDefault() {
        // settings_2c profile with no flags → should be "advanced"
        QJsonObject obj = makeAdvancedProfileJson("Some Advanced Profile");

        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.editorType(), QString("advanced"));
    }

    void editorTypeStarPrefixedAFlow() {
        // Star-prefixed A-Flow title → derived as "aflow"
        QJsonObject obj = makeAdvancedProfileJson("*A-Flow My Profile");

        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.editorType(), QString("aflow"));
    }

    void regenerateFromRecipeGuardAdvanced() {
        // Advanced profile must NOT regenerate frames
        QJsonObject obj = makeAdvancedProfileJson("My Advanced");

        Profile p = Profile::fromJson(QJsonDocument(obj));
        int framesBefore = p.steps().size();
        p.regenerateFromRecipe();
        QCOMPARE(p.steps().size(), framesBefore);  // Unchanged
    }

    void toJsonAdvancedNoRecipeBlock() {
        // Advanced profile should not emit "recipe" in JSON
        QJsonObject obj = makeAdvancedProfileJson("Advanced Profile");

        Profile p = Profile::fromJson(QJsonDocument(obj));
        QJsonDocument doc = p.toJson();
        QJsonObject out = doc.object();
        QVERIFY(!out.contains("recipe"));
        QVERIFY(!out.contains("is_recipe_mode"));
        QVERIFY(!out.contains("editor_type"));
    }

    void toJsonDFlowIncludesRecipeBlock() {
        // D-Flow profile should emit "recipe" in JSON
        QJsonObject obj = makeAdvancedProfileJson("D-Flow Test");
        obj["recipe"] = RecipeParams().toJson();

        Profile p = Profile::fromJson(QJsonDocument(obj));
        QJsonDocument doc = p.toJson();
        QJsonObject out = doc.object();
        QVERIFY(out.contains("recipe"));
        QVERIFY(!out.contains("editor_type"));  // Never stored
    }

    void legacyRecipePressureOnSettings2c() {
        // Legacy: is_recipe_mode=true, settings_2c, recipe.editorType=pressure
        // With fully-derived editorType, settings_2c + non-matching title → "advanced"
        // The recipe's editorType should be respected (unusual but valid)
        QJsonObject obj = makeAdvancedProfileJson("Pressure Recipe");
        obj["is_recipe_mode"] = true;
        QJsonObject recipeJson = RecipeParams().toJson();
        recipeJson["editorType"] = "pressure";
        obj["recipe"] = recipeJson;

        Profile p = Profile::fromJson(QJsonDocument(obj));
        // settings_2c + title "Pressure Recipe" → derived as "advanced"
        // (legacy recipe.editorType is ignored — editorType is fully derived from content)
        QCOMPARE(p.editorType(), QString("advanced"));
    }

    // ===== editorType with empty/unusual titles =====

    void editorTypeEmptyTitle() {
        QJsonObject obj = makeAdvancedProfileJson("");
        Profile p = Profile::fromJson(QJsonDocument(obj));
        // Empty title + settings_2c → "advanced"
        QCOMPARE(p.editorType(), QString("advanced"));
    }

    void editorTypeCaseInsensitive() {
        QJsonObject obj = makeAdvancedProfileJson("d-flow lowercase test");
        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.editorType(), QString("dflow"));
    }

    void editorTypeDFlowSubstring() {
        // Title contains "D-Flow" but not at the start → should NOT match
        QJsonObject obj = makeAdvancedProfileJson("My D-Flow Profile");
        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.editorType(), QString("advanced"));
    }

    void editorTypeNoSpuriousRecipeForPressure() {
        // Pressure profiles (settings_2a) without explicit recipe data
        // should NOT emit a recipe block with default DFlow params
        QJsonObject obj;
        obj["title"] = "My Pressure";
        obj["legacy_profile_type"] = "settings_2a";
        obj["espresso_temperature"] = 93.0;
        obj["preinfusion_time"] = 5.0;
        obj["preinfusion_flow_rate"] = 4.0;
        obj["preinfusion_stop_pressure"] = 4.0;
        obj["espresso_pressure"] = 9.0;
        obj["pressure_end"] = 6.0;
        obj["steps"] = QJsonArray();

        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.editorType(), QString("pressure"));

        QJsonDocument doc = p.toJson();
        QJsonObject out = doc.object();
        // No recipe block (no explicit recipe data was set)
        QVERIFY(!out.contains("recipe"));
        QVERIFY(!out.contains("is_recipe_mode"));
    }

    // ===== Issue #1: toJson output must allow editorType derivation =====
    // These tests verify that the JSON written by toJson() contains enough
    // information for a consumer (like summarizeFromHistory) to derive the
    // editor type without reading is_recipe_mode or editor_type fields.

    void toJsonDFlowDerivableFromOutput() {
        // A D-Flow profile's toJson() must include "title" starting with "D-Flow"
        // so consumers can derive editorType without is_recipe_mode
        QJsonObject obj = makeAdvancedProfileJson("D-Flow / Test");
        obj["recipe"] = RecipeParams().toJson();
        Profile p = Profile::fromJson(QJsonDocument(obj));

        QJsonDocument doc = p.toJson();
        QJsonObject out = doc.object();

        // Derive editorType from output JSON (same logic as Profile::editorType)
        QString title = out["title"].toString();
        QString t = title.startsWith(QLatin1Char('*')) ? title.mid(1) : title;
        QString derived;
        if (t.startsWith(QStringLiteral("D-Flow"), Qt::CaseInsensitive))
            derived = "dflow";
        else if (t.startsWith(QStringLiteral("A-Flow"), Qt::CaseInsensitive))
            derived = "aflow";
        else {
            QString pt = out["legacy_profile_type"].toString();
            if (pt == "settings_2a") derived = "pressure";
            else if (pt == "settings_2b") derived = "flow";
            else derived = "advanced";
        }
        QCOMPARE(derived, QString("dflow"));
    }

    void toJsonPressureDerivableFromOutput() {
        // A settings_2a profile must include legacy_profile_type in toJson output
        QJsonObject obj;
        obj["title"] = "My Pressure";
        obj["legacy_profile_type"] = "settings_2a";
        obj["espresso_temperature"] = 93.0;
        obj["steps"] = QJsonArray();
        Profile p = Profile::fromJson(QJsonDocument(obj));

        QJsonDocument doc = p.toJson();
        QJsonObject out = doc.object();

        // Consumer must be able to derive "pressure" from legacy_profile_type
        QString pt = out["legacy_profile_type"].toString();
        QCOMPARE(pt, QString("settings_2a"));
        // profile_type key should NOT be relied on (writer uses legacy_profile_type)
        // But legacy_profile_type MUST be present
        QVERIFY(!pt.isEmpty());
    }

    // ===== Issue #3: simple profile round-trip recipe params =====

    void simpleProfileRoundTripRecipeEditorType() {
        // A settings_2a profile that never had recipe data should NOT gain
        // a recipe block with mismatched editorType after round-trip
        QJsonObject obj;
        obj["title"] = "My Pressure";
        obj["legacy_profile_type"] = "settings_2a";
        obj["espresso_temperature"] = 93.0;
        obj["steps"] = QJsonArray();
        // No recipe block — simulates a simple profile

        Profile p1 = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p1.editorType(), QString("pressure"));

        // Round-trip through toJson/fromJson
        QJsonDocument doc = p1.toJson();
        Profile p2 = Profile::fromJson(doc);

        // After round-trip, recipeParams.editorType must match editorType()
        // i.e. it should be Pressure, not DFlow (the default)
        if (doc.object().contains("recipe")) {
            // If a recipe block was written, the editorType inside it must be correct
            QString recipeEt = doc.object()["recipe"].toObject()["editorType"].toString();
            QVERIFY2(recipeEt != "dflow",
                "settings_2a profile must not have recipe.editorType=dflow after round-trip");
        }
        QCOMPARE(p2.editorType(), QString("pressure"));
    }

    void regenerateFromRecipeDFlowRegenerates() {
        // D-Flow profile with recipe params should regenerate frames
        QJsonObject obj = makeAdvancedProfileJson("D-Flow Test");
        QJsonObject recipeJson = RecipeParams().toJson();
        recipeJson["editorType"] = "dflow";
        obj["recipe"] = recipeJson;

        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.editorType(), QString("dflow"));

        // Set valid recipe params so regeneration produces frames
        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        recipe.fillPressure = 6.0;
        recipe.fillFlow = 4.0;
        recipe.pourFlow = 2.0;
        recipe.fillTemperature = 93.0;
        recipe.pourTemperature = 93.0;
        recipe.targetWeight = 36.0;
        p.setRecipeParams(recipe);
        p.regenerateFromRecipe();

        // Should have regenerated frames (D-Flow produces 3 frames)
        QVERIFY(p.steps().size() > 0);
    }

    // ===== Title strips leading star (de1app modified indicator) =====

    void titleStripsStar() {
        QJsonObject obj = makeAdvancedProfileJson("*D-Flow Default");
        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.title(), QString("D-Flow Default"));
    }

    void titleNoStarUnchanged() {
        QJsonObject obj = makeAdvancedProfileJson("D-Flow Default");
        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.title(), QString("D-Flow Default"));
    }

    // ===== Espresso temperature sync =====

    void espressoTempSyncFromFirstFrame() {
        // For advanced profiles, espresso_temperature syncs from first frame
        QJsonObject obj = makeAdvancedProfileJson();
        obj["espresso_temperature"] = 90.0;
        // First frame has temp 88.0
        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.espressoTemperature(), 88.0);  // Synced from first frame
    }

    void espressoTempNoSyncForSimple() {
        // For simple profiles (settings_2a/2b), espresso_temperature stays authoritative
        QJsonObject obj;
        obj["title"] = "Simple";
        obj["legacy_profile_type"] = "settings_2a";
        obj["espresso_temperature"] = 90.0;
        obj["preinfusion_time"] = 5.0;
        obj["preinfusion_flow_rate"] = 4.0;
        obj["preinfusion_stop_pressure"] = 4.0;
        obj["espresso_hold_time"] = 10.0;
        obj["espresso_pressure"] = 9.2;
        obj["espresso_decline_time"] = 25.0;
        obj["pressure_end"] = 4.0;

        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.espressoTemperature(), 90.0);  // NOT synced from frames
    }

    // ==========================================
    // TCL Import Tests
    // ==========================================

    void tclFrameParsing() {
        // Parse a single de1app TCL frame string
        QString tcl = "{name {preinfusion} temperature 88.0 sensor coffee "
                      "pump flow transition fast pressure 1.0 flow 4.0 "
                      "seconds 20.0 volume 0.0 exit_if 1 exit_type pressure_over "
                      "exit_pressure_over 4.0 exit_pressure_under 0.0 "
                      "exit_flow_over 6.0 exit_flow_under 0.0 "
                      "max_flow_or_pressure 0.0 max_flow_or_pressure_range 0.6 "
                      "weight 0.0 popup {}}";

        ProfileFrame pf = ProfileFrame::fromTclList(tcl);
        QCOMPARE(pf.name, QString("preinfusion"));
        QCOMPARE(pf.temperature, 88.0);
        QCOMPARE(pf.sensor, QString("coffee"));
        QCOMPARE(pf.pump, QString("flow"));
        QCOMPARE(pf.transition, QString("fast"));
        QCOMPARE(pf.pressure, 1.0);
        QCOMPARE(pf.flow, 4.0);
        QCOMPARE(pf.seconds, 20.0);
        QCOMPARE(pf.volume, 0.0);
        QVERIFY(pf.exitIf);
        QCOMPARE(pf.exitType, QString("pressure_over"));
        QCOMPARE(pf.exitPressureOver, 4.0);
        QCOMPARE(pf.exitFlowOver, 6.0);
        QCOMPARE(pf.maxFlowOrPressure, 0.0);
        QCOMPARE(pf.maxFlowOrPressureRange, 0.6);
    }

    void tclBracedValues() {
        // Braced values: name with spaces
        QString tcl = "{name {rise and hold} temperature 93.0 pump pressure "
                      "pressure 9.0 flow 2.0 seconds 30.0 volume 0.0 "
                      "exit_if 0 transition fast sensor coffee}";

        ProfileFrame pf = ProfileFrame::fromTclList(tcl);
        QCOMPARE(pf.name, QString("rise and hold"));
    }

    void tclTransitionSlowToSmooth() {
        // de1app uses "slow" which maps to "smooth" in Decenza
        QString tcl = "{name decline temperature 93.0 pump pressure "
                      "pressure 4.0 flow 2.0 seconds 25.0 volume 0.0 "
                      "exit_if 0 transition slow sensor coffee}";

        ProfileFrame pf = ProfileFrame::fromTclList(tcl);
        QCOMPARE(pf.transition, QString("smooth"));
    }

    void tclWeightIndependentOfExitIf() {
        // Weight exit is independent of exit_if (de1app behavior)
        // exit_if 0 means no machine-side exit, but weight > 0 means app-side weight exit
        QString tcl = "{name {infuse} temperature 93.0 pump pressure "
                      "pressure 3.0 flow 8.0 seconds 20.0 volume 100.0 "
                      "exit_if 0 weight 4.0 transition fast sensor coffee}";

        ProfileFrame pf = ProfileFrame::fromTclList(tcl);
        QVERIFY(!pf.exitIf);           // Machine-side exit is OFF
        QCOMPARE(pf.exitWeight, 4.0);  // App-side weight exit IS set
    }

    void tclRoundTrip() {
        // Create a frame, serialize to TCL, parse back, compare
        ProfileFrame original;
        original.name = "rise and hold";
        original.temperature = 93.0;
        original.sensor = "coffee";
        original.pump = "pressure";
        original.transition = "smooth";
        original.pressure = 9.0;
        original.flow = 2.0;
        original.seconds = 30.0;
        original.volume = 0.0;
        original.exitIf = true;
        original.exitType = "pressure_over";
        original.exitPressureOver = 4.0;
        original.maxFlowOrPressure = 6.0;
        original.maxFlowOrPressureRange = 1.0;
        original.exitWeight = 3.5;

        QString tcl = original.toTclList();
        ProfileFrame parsed = ProfileFrame::fromTclList(tcl);

        QCOMPARE(parsed.name, original.name);
        QCOMPARE(parsed.temperature, original.temperature);
        QCOMPARE(parsed.sensor, original.sensor);
        QCOMPARE(parsed.pump, original.pump);
        QCOMPARE(parsed.transition, original.transition);
        QCOMPARE(parsed.pressure, original.pressure);
        QCOMPARE(parsed.flow, original.flow);
        QCOMPARE(parsed.seconds, original.seconds);
        QVERIFY(parsed.exitIf);
        QCOMPARE(parsed.exitType, original.exitType);
        QCOMPARE(parsed.exitPressureOver, original.exitPressureOver);
        QCOMPARE(parsed.maxFlowOrPressure, original.maxFlowOrPressure);
        QCOMPARE(parsed.exitWeight, original.exitWeight);
    }

    // ==========================================
    // Simple Profile Frame Generation
    // (de1app pressure_to_advanced_list / flow_to_advanced_list)
    // ==========================================

    void pressureProfileFrameGeneration() {
        // settings_2a with holdTime > 3: expect preinfusion + forced rise + hold + decline
        // de1app: pressure_to_advanced_list()
        QJsonObject obj;
        obj["title"] = "Pressure Test";
        obj["legacy_profile_type"] = "settings_2a";
        obj["espresso_temperature"] = 93.0;
        obj["preinfusion_time"] = 5.0;
        obj["preinfusion_flow_rate"] = 4.0;
        obj["preinfusion_stop_pressure"] = 4.0;
        obj["espresso_hold_time"] = 10.0;     // > 3s: generates forced rise + hold
        obj["espresso_pressure"] = 9.2;
        obj["espresso_decline_time"] = 25.0;
        obj["pressure_end"] = 4.0;
        obj["maximum_flow"] = 6.0;
        obj["maximum_flow_range_default"] = 1.0;

        Profile p = Profile::fromJson(QJsonDocument(obj));

        // Expected: preinfusion(flow,exit) + forced_rise(pressure,3s) + hold(pressure,7s) + decline(pressure,smooth)
        QCOMPARE(p.steps().size(), 4);

        // Frame 0: preinfusion (flow pump, exit_pressure_over)
        // de1app profile.tcl pressure_to_advanced_list: single preinfusion frame
        // with exit_flow_over 6 (when no temp stepping)
        QCOMPARE(p.steps()[0].pump, QString("flow"));
        QVERIFY(p.steps()[0].exitIf);
        QCOMPARE(p.steps()[0].exitType, QString("pressure_over"));
        QCOMPARE(p.steps()[0].exitPressureOver, 4.0);
        QCOMPARE(p.steps()[0].flow, 4.0);
        QCOMPARE(p.steps()[0].seconds, 5.0);
        QCOMPARE(p.steps()[0].exitFlowOver, 6.0);  // de1app: exit_flow_over 6

        // Frame 1: forced rise without limit (pressure pump, 3s)
        QCOMPARE(p.steps()[1].pump, QString("pressure"));
        QCOMPARE(p.steps()[1].pressure, 9.2);
        QCOMPARE(p.steps()[1].seconds, 3.0);
        QVERIFY(!p.steps()[1].exitIf);
        QCOMPARE(p.steps()[1].maxFlowOrPressure, 0.0);  // No limiter on forced rise

        // Frame 2: hold (pressure pump, remaining time)
        QCOMPARE(p.steps()[2].pump, QString("pressure"));
        QCOMPARE(p.steps()[2].pressure, 9.2);
        QCOMPARE(p.steps()[2].seconds, 7.0);  // 10 - 3 = 7
        QCOMPARE(p.steps()[2].maxFlowOrPressure, 6.0);  // Limiter active

        // Frame 3: decline (pressure pump, smooth transition)
        QCOMPARE(p.steps()[3].pump, QString("pressure"));
        QCOMPARE(p.steps()[3].transition, QString("smooth"));
        QCOMPARE(p.steps()[3].pressure, 4.0);   // pressureEnd
        QCOMPARE(p.steps()[3].seconds, 25.0);
    }

    void pressureProfileShortHold() {
        // de1app edge case: holdTime <= 3, declineTime > 3 → forced rise in decline
        QJsonObject obj;
        obj["title"] = "Short Hold";
        obj["legacy_profile_type"] = "settings_2a";
        obj["preinfusion_time"] = 5.0;
        obj["preinfusion_flow_rate"] = 4.0;
        obj["preinfusion_stop_pressure"] = 4.0;
        obj["espresso_hold_time"] = 2.0;      // <= 3s: no forced rise before hold
        obj["espresso_pressure"] = 9.2;
        obj["espresso_decline_time"] = 20.0;   // > 3s: forced rise before decline
        obj["pressure_end"] = 4.0;
        obj["maximum_flow"] = 6.0;
        obj["maximum_flow_range_default"] = 1.0;

        Profile p = Profile::fromJson(QJsonDocument(obj));

        // Expected: preinfusion + hold(2s) + forced_rise(3s) + decline(17s)
        QCOMPARE(p.steps().size(), 4);

        // Frame 1: hold (short, no forced rise before it)
        QCOMPARE(p.steps()[1].pump, QString("pressure"));
        QCOMPARE(p.steps()[1].seconds, 2.0);

        // Frame 2: forced rise (inserted before decline because hold was short)
        QCOMPARE(p.steps()[2].seconds, 3.0);
        QVERIFY(!p.steps()[2].exitIf);

        // Frame 3: decline (time reduced by 3s for forced rise)
        QCOMPARE(p.steps()[3].transition, QString("smooth"));
        QCOMPARE(p.steps()[3].seconds, 17.0);  // 20 - 3 = 17
    }

    void flowProfileDeclineGating() {
        // de1app flow_to_advanced_list profile.tcl line 301:
        //   if {$temp_advanced(espresso_hold_time) > 0} { set decline ... }
        // Decline is gated by hold time, NOT decline time.
        QJsonObject obj;
        obj["title"] = "Flow No Hold";
        obj["legacy_profile_type"] = "settings_2b";
        obj["preinfusion_time"] = 5.0;
        obj["preinfusion_flow_rate"] = 4.0;
        obj["preinfusion_stop_pressure"] = 4.0;
        obj["espresso_hold_time"] = 0.0;          // Zero hold time
        obj["flow_profile_hold"] = 2.0;
        obj["espresso_decline_time"] = 17.0;       // Non-zero, but hold is 0
        obj["flow_profile_decline"] = 1.2;

        Profile p = Profile::fromJson(QJsonDocument(obj));

        // de1app: only preinfusion frame — no hold, no decline
        QCOMPARE(p.steps().size(), 1);
        QCOMPARE(p.steps()[0].pump, QString("flow"));
    }

    void flowProfileWithHoldDe1appOracle() {
        // de1app flow_to_advanced_list: full flow profile with hold + decline
        // Verify field-by-field against de1app Tcl source
        QJsonObject obj;
        obj["title"] = "Flow Oracle";
        obj["legacy_profile_type"] = "settings_2b";
        obj["espresso_temperature"] = 93.0;
        obj["preinfusion_time"] = 5.0;
        obj["preinfusion_flow_rate"] = 4.0;
        obj["preinfusion_stop_pressure"] = 4.0;
        obj["espresso_hold_time"] = 8.0;
        obj["flow_profile_hold"] = 2.0;
        obj["espresso_decline_time"] = 17.0;
        obj["flow_profile_decline"] = 1.2;
        obj["maximum_pressure"] = 9.0;
        obj["maximum_pressure_range_default"] = 0.9;

        Profile p = Profile::fromJson(QJsonDocument(obj));

        // de1app: preinfusion + hold + decline = 3 frames (no forced rise for flow profiles)
        QCOMPARE(p.steps().size(), 3);

        // Frame 0: preinfusion
        // de1app flow_to_advanced_list: exit_flow_over 0 (NOT 6 like pressure profiles)
        QCOMPARE(p.steps()[0].pump, QString("flow"));
        QVERIFY(p.steps()[0].exitIf);
        QCOMPARE(p.steps()[0].exitType, QString("pressure_over"));
        QCOMPARE(p.steps()[0].exitPressureOver, 4.0);
        QCOMPARE(p.steps()[0].exitFlowOver, 0.0);  // de1app: exit_flow_over 0 for flow profiles

        // Frame 1: hold
        // de1app: exit_flow_over 6, flow pump, fast transition
        QCOMPARE(p.steps()[1].pump, QString("flow"));
        QCOMPARE(p.steps()[1].flow, 2.0);
        QCOMPARE(p.steps()[1].seconds, 8.0);
        QCOMPARE(p.steps()[1].exitFlowOver, 6.0);  // de1app: exit_flow_over 6
        QCOMPARE(p.steps()[1].maxFlowOrPressure, 9.0);  // Pressure limiter

        // Frame 2: decline
        // de1app: exit_flow_over 0, smooth transition
        QCOMPARE(p.steps()[2].pump, QString("flow"));
        QCOMPARE(p.steps()[2].transition, QString("smooth"));
        QCOMPARE(p.steps()[2].flow, 1.2);
        QCOMPARE(p.steps()[2].seconds, 17.0);
        QCOMPARE(p.steps()[2].exitFlowOver, 0.0);  // de1app: exit_flow_over 0 on decline
        QCOMPARE(p.steps()[2].maxFlowOrPressure, 9.0);  // Pressure limiter
    }

    void tempSteppingPressure() {
        // Temp stepping: preinfusion splits into boost(2s,temp0) + main(remaining,temp1)
        // de1app: espresso_temperature_steps_list / temp_bump_time_seconds=2
        QJsonObject obj;
        obj["title"] = "Temp Stepping";
        obj["legacy_profile_type"] = "settings_2a";
        obj["temp_steps_enabled"] = true;
        obj["preinfusion_time"] = 5.0;
        obj["preinfusion_flow_rate"] = 4.0;
        obj["preinfusion_stop_pressure"] = 4.0;
        obj["espresso_hold_time"] = 10.0;
        obj["espresso_pressure"] = 9.2;
        obj["espresso_decline_time"] = 25.0;
        obj["pressure_end"] = 4.0;
        obj["maximum_flow"] = 6.0;

        QJsonArray temps;
        temps.append(85.0);  // temp0: boost
        temps.append(88.0);  // temp1: preinfusion
        temps.append(93.0);  // temp2: hold
        temps.append(90.0);  // temp3: decline
        obj["temperature_presets"] = temps;

        Profile p = Profile::fromJson(QJsonDocument(obj));

        // Expected: boost(2s,85C) + preinfusion(3s,88C) + forced_rise(3s,93C) + hold(7s,93C) + decline(25s,90C)
        QCOMPARE(p.steps().size(), 5);

        // Frame 0: temp boost at 85C (2s)
        // de1app profile.tcl: exit_flow_over 0 (no flow exit during temp boost)
        QCOMPARE(p.steps()[0].temperature, 85.0);
        QCOMPARE(p.steps()[0].seconds, 2.0);
        QCOMPARE(p.steps()[0].exitFlowOver, 0.0);  // de1app: exit_flow_over 0

        // Frame 1: preinfusion at 88C (3s remaining)
        // de1app profile.tcl: exit_flow_over 6 (flow exit on main preinfusion frame)
        QCOMPARE(p.steps()[1].temperature, 88.0);
        QCOMPARE(p.steps()[1].seconds, 3.0);
        QCOMPARE(p.steps()[1].exitFlowOver, 6.0);  // de1app: exit_flow_over 6

        // Frame 2: forced rise at 93C
        QCOMPARE(p.steps()[2].temperature, 93.0);

        // Frame 4: decline at 90C
        // de1app: exit_flow_over 6 on decline
        QCOMPARE(p.steps()[4].temperature, 90.0);
        QCOMPARE(p.steps()[4].exitFlowOver, 6.0);  // de1app: exit_flow_over 6
    }

    void emptyFrameFallback() {
        // All times zero → single empty frame (safety net)
        QJsonObject obj;
        obj["title"] = "Empty";
        obj["legacy_profile_type"] = "settings_2a";
        obj["preinfusion_time"] = 0.0;
        obj["espresso_hold_time"] = 0.0;
        obj["espresso_decline_time"] = 0.0;

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("all time parameters are zero"));
        Profile p = Profile::fromJson(QJsonDocument(obj));
        QCOMPARE(p.steps().size(), 1);
        QCOMPARE(p.steps()[0].name, QString("empty"));
    }

    // ==========================================
    // computeFlags (de1app calculate_frame_flag)
    // ==========================================

    void flagsPressureNoExit() {
        ProfileFrame pf;
        pf.pump = "pressure";
        pf.exitIf = false;
        pf.transition = "fast";
        pf.sensor = "coffee";

        // de1app: IgnoreLimit only (0x40)
        QCOMPARE(pf.computeFlags(), uint8_t(0x40));
    }

    void flagsFlowNoExit() {
        ProfileFrame pf;
        pf.pump = "flow";
        pf.exitIf = false;
        pf.transition = "fast";
        pf.sensor = "coffee";

        // de1app: CtrlF | IgnoreLimit (0x01 | 0x40 = 0x41)
        QCOMPARE(pf.computeFlags(), uint8_t(0x41));
    }

    void flagsFlowPressureOver() {
        ProfileFrame pf;
        pf.pump = "flow";
        pf.exitIf = true;
        pf.exitType = "pressure_over";
        pf.transition = "fast";
        pf.sensor = "coffee";

        // CtrlF | DoCompare | DC_GT | IgnoreLimit (0x01 | 0x02 | 0x04 | 0x40 = 0x47)
        QCOMPARE(pf.computeFlags(), uint8_t(0x47));
    }

    void flagsPressureUnder() {
        ProfileFrame pf;
        pf.pump = "pressure";
        pf.exitIf = true;
        pf.exitType = "pressure_under";
        pf.transition = "fast";
        pf.sensor = "coffee";

        // DoCompare | IgnoreLimit (0x02 | 0x40 = 0x42)
        // DC_GT=0 (less than), DC_CompF=0 (pressure)
        QCOMPARE(pf.computeFlags(), uint8_t(0x42));
    }

    void flagsFlowUnder() {
        ProfileFrame pf;
        pf.pump = "flow";
        pf.exitIf = true;
        pf.exitType = "flow_under";
        pf.transition = "fast";
        pf.sensor = "coffee";

        // CtrlF | DoCompare | DC_CompF | IgnoreLimit (0x01 | 0x02 | 0x08 | 0x40 = 0x4B)
        QCOMPARE(pf.computeFlags(), uint8_t(0x4B));
    }

    void flagsFlowOver() {
        ProfileFrame pf;
        pf.pump = "pressure";
        pf.exitIf = true;
        pf.exitType = "flow_over";
        pf.transition = "fast";
        pf.sensor = "coffee";

        // DoCompare | DC_GT | DC_CompF | IgnoreLimit (0x02 | 0x04 | 0x08 | 0x40 = 0x4E)
        QCOMPARE(pf.computeFlags(), uint8_t(0x4E));
    }

    void flagsSmoothTransition() {
        ProfileFrame pf;
        pf.pump = "pressure";
        pf.exitIf = false;
        pf.transition = "smooth";
        pf.sensor = "coffee";

        // Interpolate | IgnoreLimit (0x20 | 0x40 = 0x60)
        QCOMPARE(pf.computeFlags(), uint8_t(0x60));
    }

    void flagsWaterSensor() {
        ProfileFrame pf;
        pf.pump = "pressure";
        pf.exitIf = false;
        pf.transition = "fast";
        pf.sensor = "water";

        // TMixTemp | IgnoreLimit (0x10 | 0x40 = 0x50)
        QCOMPARE(pf.computeFlags(), uint8_t(0x50));
    }

    // ==========================================
    // getSetVal / getTriggerVal
    // ==========================================

    void getSetValFlow() {
        ProfileFrame pf;
        pf.pump = "flow";
        pf.flow = 2.5;
        pf.pressure = 9.0;
        QCOMPARE(pf.getSetVal(), 2.5);
    }

    void getSetValPressure() {
        ProfileFrame pf;
        pf.pump = "pressure";
        pf.flow = 2.5;
        pf.pressure = 9.0;
        QCOMPARE(pf.getSetVal(), 9.0);
    }

    void getTriggerValEachType() {
        ProfileFrame pf;
        pf.exitIf = true;

        pf.exitType = "pressure_over";
        pf.exitPressureOver = 3.0;
        QCOMPARE(pf.getTriggerVal(), 3.0);

        pf.exitType = "pressure_under";
        pf.exitPressureUnder = 2.0;
        QCOMPARE(pf.getTriggerVal(), 2.0);

        pf.exitType = "flow_over";
        pf.exitFlowOver = 4.0;
        QCOMPARE(pf.getTriggerVal(), 4.0);

        pf.exitType = "flow_under";
        pf.exitFlowUnder = 1.5;
        QCOMPARE(pf.getTriggerVal(), 1.5);
    }

    void getTriggerValNoExit() {
        ProfileFrame pf;
        pf.exitIf = false;
        pf.exitType = "pressure_over";
        pf.exitPressureOver = 99.0;
        QCOMPARE(pf.getTriggerVal(), 0.0);  // exitIf=false → 0
    }

    void withSetpointImmutability() {
        ProfileFrame original;
        original.pump = "flow";
        original.flow = 2.0;
        original.pressure = 9.0;
        original.temperature = 93.0;

        ProfileFrame copy = original.withSetpoint(3.5, 88.0);
        QCOMPARE(copy.flow, 3.5);
        QCOMPARE(copy.temperature, 88.0);

        // Original unchanged
        QCOMPARE(original.flow, 2.0);
        QCOMPARE(original.temperature, 93.0);
    }

    // ==========================================
    // D-Flow Recipe Generator (de1app dflow_generate_frames)
    // ==========================================

    void dflowDefaultFrameCount() {
        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        QList<ProfileFrame> frames = RecipeGenerator::generateFrames(recipe);
        QCOMPARE(frames.size(), 3);  // Always: Filling, Infusing, Pouring
    }

    void dflowFrameNames() {
        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        QList<ProfileFrame> frames = RecipeGenerator::generateFrames(recipe);
        QCOMPARE(frames[0].name, QString("Filling"));
        QCOMPARE(frames[1].name, QString("Infusing"));
        QCOMPARE(frames[2].name, QString("Pouring"));
    }

    void dflowFillExitFormula() {
        // de1app upstream D_Flow_Espresso_Profile/plugin.tcl (Damian-AU/D_Flow_Espresso_Profile):
        //   if pressure < 2.8: exit_pressure_over = pressure
        //   else: exit_pressure_over = round_to_one_digits((pressure / 2) + 0.6)
        //   if exit_pressure_over < 1.2: exit_pressure_over = 1.2
        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        recipe.infusePressure = 3.0;  // >= 2.8 → formula path

        QList<ProfileFrame> frames = RecipeGenerator::generateFrames(recipe);
        // (3.0/2 + 0.6) = 2.1
        QCOMPARE(frames[0].exitPressureOver, 2.1);
    }

    void dflowFillExitClamp() {
        // de1app upstream: minimum exit pressure is 1.2
        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        recipe.infusePressure = 0.5;  // < 2.8 → use directly → 0.5 < 1.2 → clamp

        QList<ProfileFrame> frames = RecipeGenerator::generateFrames(recipe);
        QCOMPARE(frames[0].exitPressureOver, 1.2);
    }

    // de1app oracle: D-Flow fill exit pressure across the formula boundary
    void dflowFillExitDe1appOracle_data() {
        QTest::addColumn<double>("infusePressure");
        QTest::addColumn<double>("expected");

        // de1app upstream formula (Damian-AU/D_Flow_Espresso_Profile):
        //   p < 2.8: exitP = p (clamped to min 1.2)
        //   p >= 2.8: exitP = round_to_one_digits((p/2) + 0.6) (clamped to min 1.2)
        QTest::newRow("p=0.5 below min")  << 0.5  << 1.2;  // 0.5 → clamp to 1.2
        QTest::newRow("p=1.0 below 2.8")  << 1.0  << 1.2;  // 1.0 → clamp to 1.2
        QTest::newRow("p=2.0 below 2.8")  << 2.0  << 2.0;  // 2.0 direct
        QTest::newRow("p=2.7 below 2.8")  << 2.7  << 2.7;  // 2.7 direct (just below threshold)
        QTest::newRow("p=2.8 at boundary") << 2.8  << 2.0;  // (2.8/2+0.6) = 2.0
        QTest::newRow("p=3.0 standard")   << 3.0  << 2.1;  // (3.0/2+0.6) = 2.1
        QTest::newRow("p=4.0")            << 4.0  << 2.6;  // (4.0/2+0.6) = 2.6
        QTest::newRow("p=6.0")            << 6.0  << 3.6;  // (6.0/2+0.6) = 3.6
        QTest::newRow("p=8.0")            << 8.0  << 4.6;  // (8.0/2+0.6) = 4.6
    }

    void dflowFillExitDe1appOracle() {
        QFETCH(double, infusePressure);
        QFETCH(double, expected);

        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        recipe.infusePressure = infusePressure;

        QList<ProfileFrame> frames = RecipeGenerator::generateFrames(recipe);
        QVERIFY2(qAbs(frames[0].exitPressureOver - expected) < 0.01,
                 qPrintable(QString("Expected %1 but got %2 for infusePressure=%3")
                            .arg(expected).arg(frames[0].exitPressureOver).arg(infusePressure)));
    }

    void dflowInfuseDisabled() {
        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        recipe.infuseEnabled = false;

        QList<ProfileFrame> frames = RecipeGenerator::generateFrames(recipe);
        QCOMPARE(frames.size(), 3);       // Still 3 frames
        QCOMPARE(frames[1].seconds, 0.0); // Infuse frame has 0 seconds (machine skips it)
    }

    void dflowPourFrameIsFlow() {
        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        recipe.pourFlow = 2.5;
        recipe.pourPressure = 9.0;

        QList<ProfileFrame> frames = RecipeGenerator::generateFrames(recipe);
        // Pour frame: flow pump with pressure limiter
        QCOMPARE(frames[2].pump, QString("flow"));
        QCOMPARE(frames[2].flow, 2.5);
        QCOMPARE(frames[2].maxFlowOrPressure, 9.0);  // Pressure cap
    }

    // ==========================================
    // Recipe Generator: createProfile metadata
    // ==========================================

    void createProfilePressureType() {
        RecipeParams recipe;
        recipe.editorType = EditorType::Pressure;
        Profile p = RecipeGenerator::createProfile(recipe, "My Pressure");
        QCOMPARE(p.profileType(), QString("settings_2a"));
        QCOMPARE(p.editorType(), QString("pressure"));
    }

    void createProfileFlowType() {
        RecipeParams recipe;
        recipe.editorType = EditorType::Flow;
        Profile p = RecipeGenerator::createProfile(recipe, "My Flow");
        QCOMPARE(p.profileType(), QString("settings_2b"));
    }

    void createProfileDFlowType() {
        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        Profile p = RecipeGenerator::createProfile(recipe, "D-Flow / My Recipe");
        QCOMPARE(p.profileType(), QString("settings_2c"));
        QCOMPARE(p.editorType(), QString("dflow"));
    }

    void createProfilePreservesRecipeParams() {
        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        recipe.targetWeight = 42.0;
        recipe.pourFlow = 3.0;

        Profile p = RecipeGenerator::createProfile(recipe, "D-Flow / Test");
        QCOMPARE(p.editorType(), QString("dflow"));
        QCOMPARE(p.recipeParams().targetWeight, 42.0);
        QCOMPARE(p.recipeParams().pourFlow, 3.0);
    }

    // ==========================================
    // BLE Header/Frame Bytes
    // ==========================================

    void headerBytesLength() {
        QJsonObject obj = makeAdvancedProfileJson();
        Profile p = Profile::fromJson(QJsonDocument(obj));
        QByteArray header = p.toHeaderBytes();
        QCOMPARE(header.size(), 5);
    }

    void headerBytesVersion() {
        QJsonObject obj = makeAdvancedProfileJson();
        Profile p = Profile::fromJson(QJsonDocument(obj));
        QByteArray header = p.toHeaderBytes();
        QCOMPARE(uint8_t(header[0]), uint8_t(1));  // HeaderV = 1
    }

    void frameBytesCount() {
        QJsonObject obj = makeAdvancedProfileJson();
        Profile p = Profile::fromJson(QJsonDocument(obj));
        QList<QByteArray> frames = p.toFrameBytes();
        // 1 frame + possible extension frames + 1 tail frame
        QVERIFY(frames.size() >= 2);  // At minimum: 1 frame + 1 tail
    }

    void frameBytesSize() {
        QJsonObject obj = makeAdvancedProfileJson();
        Profile p = Profile::fromJson(QJsonDocument(obj));
        QList<QByteArray> frames = p.toFrameBytes();
        // Each frame is 8 bytes
        for (const QByteArray& frame : frames) {
            QCOMPARE(frame.size(), 8);
        }
    }

    // ==========================================
    // ProfileFrame JSON round-trip (all fields)
    // ==========================================

    void profileFrameFullRoundTrip() {
        ProfileFrame original;
        original.name = "test frame";
        original.temperature = 88.5;
        original.sensor = "water";
        original.pump = "flow";
        original.transition = "smooth";
        original.pressure = 3.0;
        original.flow = 4.5;
        original.seconds = 15.0;
        original.volume = 100.0;
        original.exitIf = true;
        original.exitType = "pressure_over";
        original.exitPressureOver = 4.0;
        original.exitWeight = 5.0;
        original.maxFlowOrPressure = 6.0;
        original.maxFlowOrPressureRange = 0.8;
        original.popup = "$weight";

        QJsonObject json = original.toJson();
        ProfileFrame parsed = ProfileFrame::fromJson(json);

        QCOMPARE(parsed.name, original.name);
        QCOMPARE(parsed.temperature, original.temperature);
        QCOMPARE(parsed.sensor, original.sensor);
        QCOMPARE(parsed.pump, original.pump);
        QCOMPARE(parsed.transition, original.transition);
        QCOMPARE(parsed.pressure, original.pressure);
        QCOMPARE(parsed.flow, original.flow);
        QCOMPARE(parsed.seconds, original.seconds);
        QCOMPARE(parsed.volume, original.volume);
        QCOMPARE(parsed.exitIf, original.exitIf);
        QCOMPARE(parsed.exitType, original.exitType);
        QCOMPARE(parsed.exitPressureOver, original.exitPressureOver);
        QCOMPARE(parsed.exitWeight, original.exitWeight);
        QCOMPARE(parsed.maxFlowOrPressure, original.maxFlowOrPressure);
        QCOMPARE(parsed.maxFlowOrPressureRange, original.maxFlowOrPressureRange);
        QCOMPARE(parsed.popup, original.popup);
    }

};

QTEST_GUILESS_MAIN(tst_Profile)
#include "tst_profile.moc"
