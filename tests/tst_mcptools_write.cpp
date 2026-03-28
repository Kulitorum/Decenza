#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>

#include "mocks/McpTestFixture.h"
#include "ble/protocol/de1characteristics.h"
#include "profile/recipeparams.h"

using namespace DE1::Characteristic;

// Forward declarations — implemented in mcptools_write.cpp
class ProfileManager;
class McpToolRegistry;
class ShotHistoryStorage;
class Settings;
class AccessibilityManager;
class ScreensaverVideoManager;
class TranslationManager;
class BatteryManager;
void registerWriteTools(McpToolRegistry* registry, ProfileManager* profileManager,
                        ShotHistoryStorage* shotHistory, Settings* settings,
                        AccessibilityManager* accessibility,
                        ScreensaverVideoManager* screensaver,
                        TranslationManager* translation,
                        BatteryManager* battery);

// Test MCP write tools (settings_set, profiles_set_active) against ProfileManager + MockTransport.
// Critical regression: settings_set temperature/weight must trigger BLE upload.

class tst_McpToolsWrite : public QObject {
    Q_OBJECT

private:
    // Load a minimal D-Flow profile
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
        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        recipe.targetWeight = 36.0;
        recipe.fillTemperature = 93.0;
        recipe.pourTemperature = 93.0;
        recipe.fillPressure = 6.0;
        recipe.fillFlow = 4.0;
        recipe.pourFlow = 2.0;
        json["recipe"] = recipe.toJson();

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
    static void loadAdvancedProfile(McpTestFixture& f) {
        QJsonObject json;
        json["title"] = "Test Advanced";
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

    void registerTools(McpTestFixture& f)
    {
        // Pass nullptr for dependencies not needed by the profile paths under test
        registerWriteTools(&f.registry, &f.profileManager, nullptr, &f.settings,
                          nullptr, nullptr, nullptr, nullptr);
    }

private slots:

    // ===== settings_set temperature triggers BLE upload =====

    void settingsSetTemperatureDFlowTriggersBleUpload()
    {
        McpTestFixture f;
        registerTools(f);
        loadDFlowProfile(f);
        f.transport.clearWrites();

        QJsonObject args;
        args["espressoTemperature"] = 95.0;
        QJsonObject result = f.callAsyncTool("settings_set", args);

        QVERIFY(result.contains("updated"));

        // Verify BLE writes occurred
        auto headerWrites = f.writesTo(HEADER_WRITE);
        auto frameWrites = f.writesTo(FRAME_WRITE);
        QVERIFY2(!headerWrites.isEmpty(), "settings_set temperature must write shot header to BLE");
        QVERIFY2(!frameWrites.isEmpty(), "settings_set temperature must write shot frames to BLE");
    }

    void settingsSetTemperatureAdvancedTriggersBleUpload()
    {
        McpTestFixture f;
        registerTools(f);
        loadAdvancedProfile(f);
        f.transport.clearWrites();

        QJsonObject args;
        args["espressoTemperature"] = 95.0;
        QJsonObject result = f.callAsyncTool("settings_set", args);

        QVERIFY(result.contains("updated"));

        auto headerWrites = f.writesTo(HEADER_WRITE);
        QVERIFY2(!headerWrites.isEmpty(), "settings_set temperature (advanced) must write to BLE");
    }

    // ===== settings_set targetWeight triggers BLE upload =====

    void settingsSetWeightTriggersBleUpload()
    {
        McpTestFixture f;
        registerTools(f);
        loadDFlowProfile(f);
        f.transport.clearWrites();

        QJsonObject args;
        args["targetWeight"] = 40.0;
        QJsonObject result = f.callAsyncTool("settings_set", args);

        QVERIFY(result.contains("updated"));

        auto headerWrites = f.writesTo(HEADER_WRITE);
        QVERIFY2(!headerWrites.isEmpty(), "settings_set targetWeight must write to BLE");
    }

    // ===== settings_set non-profile settings don't require profile =====

    void settingsSetSteamNoProfileNeeded()
    {
        McpTestFixture f;
        registerTools(f);

        QJsonObject args;
        args["steamTemperature"] = 155.0;
        QJsonObject result = f.callAsyncTool("settings_set", args);

        QVERIFY(result.contains("updated"));
        QJsonArray updated = result["updated"].toArray();
        bool found = false;
        for (const auto& v : updated) {
            if (v.toString() == "steamTemperature") found = true;
        }
        QVERIFY2(found, "steamTemperature should be in updated list");
    }
};

QTEST_MAIN(tst_McpToolsWrite)
#include "tst_mcptools_write.moc"
