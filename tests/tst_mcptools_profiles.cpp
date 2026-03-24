#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>

#include "mocks/McpTestFixture.h"
#include "ble/protocol/de1characteristics.h"
#include "profile/recipeparams.h"

using namespace DE1::Characteristic;

// Forward declaration — implemented in mcptools_profiles.cpp
class ProfileManager;
class McpToolRegistry;
void registerProfileTools(McpToolRegistry* registry, ProfileManager* profileManager);

// Test MCP profile tools against ProfileManager + MockTransport.
// Critical regression: profiles_edit_params must trigger BLE upload (PR #561).

class tst_McpToolsProfiles : public QObject {
    Q_OBJECT

private:
    // Load a minimal D-Flow profile into the fixture's ProfileManager
    static void loadDFlowProfile(McpTestFixture& f, const QString& title = "D-Flow / Test") {
        QJsonObject json;
        json["title"] = title;
        json["author"] = "test";
        json["notes"] = "";
        json["beverage_type"] = "espresso";
        json["version"] = "2";
        json["legacy_profile_type"] = "settings_2c";
        json["target_weight"] = 36.0;
        json["target_volume"] = 0.0;
        json["espresso_temperature"] = 93.0;
        json["maximum_pressure"] = 12.0;
        json["maximum_flow"] = 6.0;
        json["minimum_pressure"] = 0.0;
        json["is_recipe_mode"] = true;

        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        recipe.targetWeight = 36.0;
        recipe.fillTemperature = 93.0;
        recipe.pourTemperature = 93.0;
        recipe.fillPressure = 6.0;
        recipe.fillFlow = 4.0;
        recipe.pourFlow = 2.0;
        json["recipe"] = recipe.toJson();

        // Build a single preinfusion + pour frame
        QJsonArray steps;
        QJsonObject frame1;
        frame1["name"] = "fill";
        frame1["temperature"] = 93.0;
        frame1["sensor"] = "coffee";
        frame1["pump"] = "flow";
        frame1["transition"] = "fast";
        frame1["pressure"] = 6.0;
        frame1["flow"] = 4.0;
        frame1["seconds"] = 25.0;
        frame1["volume"] = 0.0;
        frame1["exit"] = QJsonObject{{"type", "pressure"}, {"condition", "over"}, {"value", 4.0}};
        frame1["limiter"] = QJsonObject{{"value", 0.0}, {"range", 0.6}};
        steps.append(frame1);

        QJsonObject frame2;
        frame2["name"] = "pour";
        frame2["temperature"] = 93.0;
        frame2["sensor"] = "coffee";
        frame2["pump"] = "flow";
        frame2["transition"] = "smooth";
        frame2["pressure"] = 6.0;
        frame2["flow"] = 2.0;
        frame2["seconds"] = 60.0;
        frame2["volume"] = 0.0;
        frame2["exit"] = QJsonObject{{"type", "pressure"}, {"condition", "over"}, {"value", 11.0}};
        frame2["limiter"] = QJsonObject{{"value", 0.0}, {"range", 0.6}};
        steps.append(frame2);

        json["steps"] = steps;
        json["number_of_preinfuse_frames"] = 1;

        QString jsonStr = QJsonDocument(json).toJson(QJsonDocument::Compact);
        f.profileManager.loadProfileFromJson(jsonStr);
    }

    // Load a minimal advanced profile
    static void loadAdvancedProfile(McpTestFixture& f, const QString& title = "Test Advanced") {
        QJsonObject json;
        json["title"] = title;
        json["author"] = "test";
        json["notes"] = "";
        json["beverage_type"] = "espresso";
        json["version"] = "2";
        json["legacy_profile_type"] = "settings_2c";
        json["target_weight"] = 36.0;
        json["target_volume"] = 0.0;
        json["espresso_temperature"] = 93.0;
        json["maximum_pressure"] = 12.0;
        json["maximum_flow"] = 6.0;
        json["minimum_pressure"] = 0.0;
        json["number_of_preinfuse_frames"] = 1;

        QJsonObject frame;
        frame["name"] = "preinfusion";
        frame["temperature"] = 93.0;
        frame["sensor"] = "coffee";
        frame["pump"] = "flow";
        frame["transition"] = "fast";
        frame["pressure"] = 1.0;
        frame["flow"] = 4.0;
        frame["seconds"] = 20.0;
        frame["volume"] = 0.0;
        frame["exit"] = QJsonObject{{"type", "pressure"}, {"condition", "over"}, {"value", 4.0}};
        frame["limiter"] = QJsonObject{{"value", 0.0}, {"range", 0.6}};
        json["steps"] = QJsonArray{frame};

        QString jsonStr = QJsonDocument(json).toJson(QJsonDocument::Compact);
        f.profileManager.loadProfileFromJson(jsonStr);
    }

private slots:

    // ===== profiles_list =====

    void profilesListReturnsArray()
    {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager);

        QJsonObject result = f.callTool("profiles_list", {});
        QVERIFY(result.contains("profiles"));
        QVERIFY(result["profiles"].isArray());
        QVERIFY(result.contains("count"));
    }

    // ===== profiles_get_active =====

    void profilesGetActiveReturnsFilename()
    {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager);
        loadDFlowProfile(f);

        QJsonObject result = f.callTool("profiles_get_active", {});
        QVERIFY(!result.contains("error"));
        QVERIFY(result.contains("filename"));
        QVERIFY(result.contains("targetWeightG"));
        QCOMPARE(result["targetWeightG"].toDouble(), 36.0);
    }

    // ===== profiles_get_params =====

    void profilesGetParamsReturnsDFlowFields()
    {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager);
        loadDFlowProfile(f);

        QJsonObject result = f.callTool("profiles_get_params", {});
        QCOMPARE(result["editorType"].toString(), QString("dflow"));
        QVERIFY(result.contains("fillTemperature"));
        QVERIFY(result.contains("pourFlow"));
        QVERIFY(result.contains("targetWeight"));
    }

    void profilesGetParamsReturnsAdvancedFields()
    {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager);
        loadAdvancedProfile(f);

        QJsonObject result = f.callTool("profiles_get_params", {});
        QCOMPARE(result["editorType"].toString(), QString("advanced"));
        QVERIFY(result.contains("steps"));
    }

    // ===== profiles_edit_params — PR #561 regression test =====

    void editParamsDFlowTriggersBleUpload()
    {
        // The critical test: editing recipe params must write frames to BLE.
        // PR #561 was a regression where this path silently stopped uploading.
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager);
        loadDFlowProfile(f);
        f.transport.clearWrites();

        QJsonObject args;
        args["targetWeight"] = 40.0;
        args["pourFlow"] = 2.5;
        QJsonObject result = f.callTool("profiles_edit_params", args);

        QVERIFY(result["success"].toBool());
        QCOMPARE(result["editorType"].toString(), QString("dflow"));

        // Verify BLE writes occurred (HEADER_WRITE + FRAME_WRITE)
        auto headerWrites = f.writesTo(HEADER_WRITE);
        auto frameWrites = f.writesTo(FRAME_WRITE);
        QVERIFY2(!headerWrites.isEmpty(), "profiles_edit_params must write shot header to BLE");
        QVERIFY2(!frameWrites.isEmpty(), "profiles_edit_params must write shot frames to BLE");
    }

    void editParamsAdvancedTriggersBleUpload()
    {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager);
        loadAdvancedProfile(f);
        f.transport.clearWrites();

        QJsonObject args;
        args["espresso_temperature"] = 95.0;
        QJsonObject result = f.callTool("profiles_edit_params", args);

        QVERIFY(result["success"].toBool());
        QCOMPARE(result["editorType"].toString(), QString("advanced"));

        auto headerWrites = f.writesTo(HEADER_WRITE);
        QVERIFY2(!headerWrites.isEmpty(), "profiles_edit_params (advanced) must write to BLE");
    }

    void editParamsUpdatesProfileState()
    {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager);
        loadDFlowProfile(f);

        QJsonObject args;
        args["targetWeight"] = 42.0;
        f.callTool("profiles_edit_params", args);

        // Verify the profile object was updated
        QCOMPARE(f.profileManager.profileTargetWeight(), 42.0);
        QVERIFY(f.profileManager.isProfileModified());
    }

    // ===== profiles_get_detail =====

    void profilesGetDetailRequiresFilename()
    {
        McpTestFixture f;
        registerProfileTools(&f.registry, &f.profileManager);

        QJsonObject result = f.callTool("profiles_get_detail", {{"filename", ""}});
        QVERIFY(result.contains("error"));
    }
};

QTEST_MAIN(tst_McpToolsProfiles)
#include "tst_mcptools_profiles.moc"
