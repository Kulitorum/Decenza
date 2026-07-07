#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>

#include "mocks/McpTestFixture.h"
#include "ble/protocol/de1characteristics.h"
#include "core/dbutils.h"
#include "core/settings_visualizer.h"
#include "history/shothistorystorage.h"
#include "history/coffeebagstorage.h"
#include "profile/recipeparams.h"
#include "ai/aimanager.h"

using namespace DE1::Characteristic;

// Stub for AIManager::availableModels — mcptools_write.cpp references it for
// aiModel validation, but this test passes a null AIManager (no aiModel paths
// under test) and does not link aimanager.cpp (which would drag in the whole
// AI subsystem). Defining it here satisfies the linker; it is never called.
QVariantList AIManager::availableModels(const QString&) const { return {}; }

// Forward declarations — implemented in mcptools_write.cpp
class ProfileManager;
class McpToolRegistry;
class ShotHistoryStorage;
class Settings;
class VisualizerUploader;
class AccessibilityManager;
class ScreensaverVideoManager;
class TranslationManager;
class BatteryManager;
class CoffeeBagStorage;
void registerWriteTools(McpToolRegistry* registry, ProfileManager* profileManager,
                        ShotHistoryStorage* shotHistory, Settings* settings,
                        VisualizerUploader* visualizerUploader,
                        CoffeeBagStorage* bagStorage,
                        AccessibilityManager* accessibility,
                        ScreensaverVideoManager* screensaver,
                        TranslationManager* translation,
                        BatteryManager* battery,
                        AIManager* aiManager);

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
        emit f.device.profileUploaded(true, QString());
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
        emit f.device.profileUploaded(true, QString());
    }

    void registerTools(McpTestFixture& f)
    {
        // Pass nullptr for dependencies not needed by the profile paths under test
        // (visualizer, bagStorage, accessibility, screensaver, translation, battery, aiManager).
        registerWriteTools(&f.registry, &f.profileManager, nullptr, &f.settings,
                          nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
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

    // Verifies that settings_set persists visualizerAutoUpdate through the MCP
    // tool surface. Does NOT exercise the shots_update auto-update gate inside
    // the QMetaObject::invokeMethod lambda in registerWriteTools — that path
    // requires a real VisualizerUploader, and registerTools passes nullptr here.
    // The gate currently has no automated test coverage; adding it would require
    // either a mock VisualizerUploader or a live-network integration harness.
    void settingsSetVisualizerAutoUpdateRoundTrip()
    {
        McpTestFixture f;
        registerTools(f);

        bool orig = f.settings.visualizer()->visualizerAutoUpdate();
        QJsonObject args;
        args["visualizerAutoUpdate"] = !orig;
        QJsonObject result = f.callAsyncTool("settings_set", args);

        QVERIFY(result.contains("updated"));
        QJsonArray updated = result["updated"].toArray();
        bool found = false;
        for (const auto& v : updated) {
            if (v.toString() == "visualizerAutoUpdate") found = true;
        }
        QVERIFY2(found, "visualizerAutoUpdate should be in updated list");
        QCOMPARE(f.settings.visualizer()->visualizerAutoUpdate(), !orig);

        // Restore
        f.settings.visualizer()->setVisualizerAutoUpdate(orig);
    }

    // shots_upload_to_visualizer needs both a real ShotHistoryStorage and a real
    // VisualizerUploader to exercise the upload-dispatch path; the test fixture
    // wires both as nullptr, so what we cover here is the synchronous input and
    // dependency guards. The dispatch path (load shot, detect existing upload,
    // pre-flight credentials/maintenance/duration, call uploadShotFromHistoryWithOverrides)
    // currently has no automated test coverage; adding it would require a real
    // ShotHistoryStorage plus either a mock VisualizerUploader or a live-network
    // integration harness.
    void shotsUploadToVisualizerRejectsInvalidShotId()
    {
        McpTestFixture f;
        registerTools(f);

        QJsonObject args;
        args["shotId"] = 0;
        QJsonObject result = f.callAsyncTool("shots_upload_to_visualizer", args);

        QVERIFY2(result.contains("error"), "expected error for shotId <= 0");
        QCOMPARE(result["error"].toString(), QString("Valid shotId is required"));
    }

    void shotsUploadToVisualizerRejectsMissingShotHistory()
    {
        McpTestFixture f;
        registerTools(f);

        QJsonObject args;
        args["shotId"] = 42;
        QJsonObject result = f.callAsyncTool("shots_upload_to_visualizer", args);

        QVERIFY2(result.contains("error"), "expected error when shotHistory is null");
        QCOMPARE(result["error"].toString(), QString("Shot history not available"));
    }

    // ===== bag_update bean-detail fields (add-bag-detail-editing) =====
    // Detail params merge into the beanBaseData blob via the shared
    // BeanBaseBlob helper (merge semantics themselves are unit-tested in
    // tst_beanbaseclient); this covers the MCP arg mapping, the clear-on-empty
    // contract, the identity mirror into the blob's working keys, and the
    // beanBase echo in the response. No CoffeeBagStorage instance is passed,
    // so the fallback static-write path runs (headless, no signals).
    void bagUpdateMergesDetailFieldsIntoBlob()
    {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("bagupd.db")));
        registerWriteTools(&f.registry, &f.profileManager, &storage, &f.settings,
                          nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        qint64 bagId = -1;
        withTempDb(storage.databasePath(), "bagupd_seed", [&](QSqlDatabase& db) {
            CoffeeBag bag;
            bag.roasterName = "Prodigal";
            bag.coffeeName = "First Batch";
            bagId = CoffeeBagStorage::insertBagStatic(db, bag);
        });
        QVERIFY(bagId > 0);

        // Details on a manual bag: keys land in the blob, echoed as beanBase.
        QJsonObject args;
        args["bagId"] = bagId;
        args["origin"] = "Ethiopia";
        args["tastingNotes"] = "floral, bergamot";
        args["link"] = "https://example.com/bag";
        QJsonObject result = f.callAsyncTool("bag_update", args);
        QVERIFY2(result["success"].toBool(), qPrintable(QJsonDocument(result).toJson()));
        QJsonObject beanBase = result["bag"].toObject()["beanBase"].toObject();
        QCOMPARE(beanBase["origin"].toString(), QString("Ethiopia"));
        QCOMPARE(beanBase["tastingNotes"].toString(), QString("floral, bergamot"));
        QCOMPARE(beanBase["link"].toString(), QString("https://example.com/bag"));
        // Manual bag: no canonical id was conjured.
        QVERIFY(!beanBase.contains("id"));

        // Empty string clears a key; identity edits mirror into the working
        // keys of the (now-existing) blob.
        QJsonObject args2;
        args2["bagId"] = bagId;
        args2["origin"] = "";
        args2["coffeeName"] = "First Batch 2026";
        QJsonObject result2 = f.callAsyncTool("bag_update", args2);
        QVERIFY(result2["success"].toBool());
        QJsonObject bag2 = result2["bag"].toObject();
        QCOMPARE(bag2["coffeeName"].toString(), QString("First Batch 2026"));
        QJsonObject beanBase2 = bag2["beanBase"].toObject();
        QVERIFY(!beanBase2.contains("origin"));
        QCOMPARE(beanBase2["roastName"].toString(), QString("First Batch 2026"));
        QCOMPARE(beanBase2["tastingNotes"].toString(), QString("floral, bergamot"));

        // A column-only edit on a detail-less bag must NOT conjure a blob.
        qint64 plainBagId = -1;
        withTempDb(storage.databasePath(), "bagupd_seed2", [&](QSqlDatabase& db) {
            CoffeeBag bag;
            bag.roasterName = "Other";
            bag.coffeeName = "Roast";
            plainBagId = CoffeeBagStorage::insertBagStatic(db, bag);
        });
        QJsonObject args3;
        args3["bagId"] = plainBagId;
        args3["coffeeName"] = "Renamed";
        QJsonObject result3 = f.callAsyncTool("bag_update", args3);
        QVERIFY(result3["success"].toBool());
        QVERIFY(!result3["bag"].toObject().contains("beanBase"));

        storage.close();
        for (int i = 0; i < 20; i++) {
            QCoreApplication::processEvents();
            QThread::msleep(25);
        }
    }
};

QTEST_MAIN(tst_McpToolsWrite)
#include "tst_mcptools_write.moc"
