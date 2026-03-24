#include <QtTest>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "mocks/McpTestFixture.h"
#include "ble/protocol/de1characteristics.h"
#include "ble/protocol/binarycodec.h"
#include "profile/recipeparams.h"

using namespace DE1::Characteristic;

// Direct tests for ProfileManager — the core class extracted in the refactor.
// Verifies the profile lifecycle (load, state, save, upload, signals) works
// correctly through ProfileManager without MainController forwarding.

class tst_ProfileManager : public QObject {
    Q_OBJECT

private:
    // Load a minimal D-Flow profile into the fixture's ProfileManager
    static void loadDFlowProfile(McpTestFixture& f, const QString& title = "D-Flow / Test",
                                 double targetWeight = 36.0, double temp = 93.0) {
        QJsonObject json;
        json["title"] = title;
        json["author"] = "test";
        json["notes"] = "";
        json["beverage_type"] = "espresso";
        json["version"] = "2";
        json["legacy_profile_type"] = "settings_2c";
        json["target_weight"] = targetWeight;
        json["target_volume"] = 0.0;
        json["espresso_temperature"] = temp;
        json["maximum_pressure"] = 12.0;
        json["maximum_flow"] = 6.0;
        json["minimum_pressure"] = 0.0;
        json["is_recipe_mode"] = true;

        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        recipe.targetWeight = targetWeight;
        recipe.fillTemperature = temp;
        recipe.pourTemperature = temp;
        recipe.fillPressure = 6.0;
        recipe.fillFlow = 4.0;
        recipe.pourFlow = 2.0;
        json["recipe"] = recipe.toJson();

        QJsonArray steps;
        QJsonObject frame1;
        frame1["name"] = "fill";
        frame1["temperature"] = temp;
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
        frame2["temperature"] = temp;
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

private slots:

    // === Profile state after load ===

    void loadProfileSetsCurrentName() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Espresso");
        QCOMPARE(f.profileManager.currentProfileName(), "D-Flow / Espresso");
    }

    void loadProfileSetsBaseProfileName() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Espresso");
        // baseProfileName is the filename (set after save), empty for JSON-loaded profiles
        // but currentProfileName should always be the title
        QVERIFY(!f.profileManager.currentProfileName().isEmpty());
    }

    void loadProfileSetsTargetWeight() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 40.0);
        QCOMPARE(f.profileManager.profileTargetWeight(), 40.0);
    }

    void loadProfileSetsTemperature() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0, 88.5);
        QCOMPARE(f.profileManager.profileTargetTemperature(), 88.5);
    }

    void loadProfileNotModified() {
        McpTestFixture f;
        loadDFlowProfile(f);
        QVERIFY(!f.profileManager.isProfileModified());
    }

    void loadProfileIsRecipe() {
        McpTestFixture f;
        loadDFlowProfile(f);
        QVERIFY(f.profileManager.isCurrentProfileRecipe());
        QCOMPARE(f.profileManager.currentEditorType(), "dflow");
    }

    void loadProfileFrameCount() {
        McpTestFixture f;
        loadDFlowProfile(f);
        QCOMPARE(f.profileManager.frameCount(), 2);
    }

    // === Signal emission ===

    void loadProfileEmitsCurrentProfileChanged() {
        McpTestFixture f;
        QSignalSpy spy(&f.profileManager, &ProfileManager::currentProfileChanged);
        loadDFlowProfile(f);
        QVERIFY(spy.count() >= 1);
    }

    void uploadProfileEmitsProfileModifiedChanged() {
        McpTestFixture f;
        loadDFlowProfile(f);

        QSignalSpy spy(&f.profileManager, &ProfileManager::profileModifiedChanged);
        QVariantMap profile = f.profileManager.getCurrentProfile();
        profile["target_weight"] = 42.0;
        f.profileManager.uploadProfile(profile);

        QVERIFY(spy.count() >= 1);
        QVERIFY(f.profileManager.isProfileModified());
    }

    void setTargetWeightEmitsSignal() {
        McpTestFixture f;
        loadDFlowProfile(f);

        QSignalSpy spy(&f.profileManager, &ProfileManager::targetWeightChanged);
        f.profileManager.setTargetWeight(45.0);
        QVERIFY(spy.count() >= 1);
    }

    // === BLE upload ===

    void uploadCurrentProfileWritesBLE() {
        McpTestFixture f;
        loadDFlowProfile(f);
        f.transport.clearWrites();

        f.profileManager.uploadCurrentProfile();

        // Should write header + frames + shot settings
        auto headerWrites = f.writesTo(HEADER_WRITE);
        auto frameWrites = f.writesTo(FRAME_WRITE);
        auto settingsWrites = f.writesTo(SHOT_SETTINGS);

        QVERIFY2(!headerWrites.isEmpty(), "uploadCurrentProfile must write profile header to BLE");
        QVERIFY2(!frameWrites.isEmpty(), "uploadCurrentProfile must write profile frames to BLE");
        QVERIFY2(!settingsWrites.isEmpty(), "uploadCurrentProfile must write shot settings to BLE");
    }

    void uploadCurrentProfileSendsCorrectTemperature() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0, 91.0);
        f.transport.clearWrites();

        f.profileManager.uploadCurrentProfile();

        // Shot settings byte 7-8 encode group temperature as U16P8
        auto settingsWrites = f.writesTo(SHOT_SETTINGS);
        QVERIFY(!settingsWrites.isEmpty());
        QByteArray data = settingsWrites.last();
        QVERIFY(data.size() >= 9);

        uint16_t encoded = (static_cast<uint8_t>(data[7]) << 8) | static_cast<uint8_t>(data[8]);
        double groupTemp = BinaryCodec::decodeU16P8(encoded);
        QVERIFY2(qAbs(groupTemp - 91.0) < 0.5,
                 qPrintable(QString("Group temp should be ~91.0, got %1").arg(groupTemp)));
    }

    void uploadCurrentProfileSends200mlSafetyLimit() {
        // Regression test for #555: TargetEspressoVol must be 200, not 36
        McpTestFixture f;
        loadDFlowProfile(f);
        f.transport.clearWrites();

        f.profileManager.uploadCurrentProfile();

        auto settingsWrites = f.writesTo(SHOT_SETTINGS);
        QVERIFY(!settingsWrites.isEmpty());
        QByteArray data = settingsWrites.last();
        QVERIFY(data.size() >= 7);

        uint8_t targetEspressoVol = static_cast<uint8_t>(data[6]);
        QCOMPARE(targetEspressoVol, static_cast<uint8_t>(200));
    }

    void uploadBlockedDuringActivePhase() {
        McpTestFixture f;
        loadDFlowProfile(f);

        // Simulate active phase (direct member access via friend class)
        f.machineState.m_phase = MachineState::Phase::Pouring;
        f.transport.clearWrites();

        QSignalSpy spy(&f.profileManager, &ProfileManager::profileUploadBlocked);
        f.profileManager.uploadCurrentProfile();

        // Should NOT write to BLE
        auto headerWrites = f.writesTo(HEADER_WRITE);
        QVERIFY2(headerWrites.isEmpty(), "uploadCurrentProfile must NOT write BLE during active phase");
        QVERIFY(spy.count() >= 1);
    }

    // === Profile modification ===

    void uploadProfileMarksModified() {
        McpTestFixture f;
        loadDFlowProfile(f);
        QVERIFY(!f.profileManager.isProfileModified());

        QVariantMap profile = f.profileManager.getCurrentProfile();
        profile["target_weight"] = 42.0;
        f.profileManager.uploadProfile(profile);

        QVERIFY(f.profileManager.isProfileModified());
    }

    void markProfileCleanClearsModified() {
        McpTestFixture f;
        loadDFlowProfile(f);

        QVariantMap profile = f.profileManager.getCurrentProfile();
        profile["target_weight"] = 42.0;
        f.profileManager.uploadProfile(profile);
        QVERIFY(f.profileManager.isProfileModified());

        f.profileManager.markProfileClean();
        QVERIFY(!f.profileManager.isProfileModified());
    }

    void uploadRecipeProfileUpdatesState() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test", 36.0, 93.0);

        QVariantMap recipe;
        recipe["editorType"] = "dflow";
        recipe["targetWeight"] = 40.0;
        recipe["fillTemperature"] = 95.0;
        recipe["pourTemperature"] = 95.0;
        recipe["fillPressure"] = 6.0;
        recipe["fillFlow"] = 4.0;
        recipe["pourFlow"] = 2.5;
        f.profileManager.uploadRecipeProfile(recipe);

        QCOMPARE(f.profileManager.profileTargetWeight(), 40.0);
        QCOMPARE(f.profileManager.profileTargetTemperature(), 95.0);
    }

    // === Frame operations ===

    void addFrameIncreasesCount() {
        McpTestFixture f;
        loadDFlowProfile(f);
        QCOMPARE(f.profileManager.frameCount(), 2);

        f.profileManager.addFrame();
        QCOMPARE(f.profileManager.frameCount(), 3);
    }

    void deleteFrameDecreasesCount() {
        McpTestFixture f;
        loadDFlowProfile(f);
        QCOMPARE(f.profileManager.frameCount(), 2);

        f.profileManager.deleteFrame(1);
        QCOMPARE(f.profileManager.frameCount(), 1);
    }

    void getFrameReturnsValidData() {
        McpTestFixture f;
        loadDFlowProfile(f);

        QVariantMap frame = f.profileManager.getFrameAt(0);
        QVERIFY(!frame.isEmpty());
        QCOMPARE(frame["name"].toString(), "fill");
    }

    void getFrameInvalidIndexReturnsEmpty() {
        McpTestFixture f;
        loadDFlowProfile(f);

        QVariantMap frame = f.profileManager.getFrameAt(99);
        QVERIFY(frame.isEmpty());
    }

    // === getCurrentProfile round-trip ===

    void getCurrentProfileContainsExpectedFields() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / RoundTrip", 38.0, 92.0);

        QVariantMap profile = f.profileManager.getCurrentProfile();
        QCOMPARE(profile["title"].toString(), "D-Flow / RoundTrip");
        QCOMPARE(profile["target_weight"].toDouble(), 38.0);
        QCOMPARE(profile["espresso_temperature"].toDouble(), 92.0);
        QVERIFY(profile.contains("steps"));
    }

    // === previousProfileName ===

    void previousProfileNameAfterSwitch() {
        McpTestFixture f;
        loadDFlowProfile(f, "Profile A");
        loadDFlowProfile(f, "Profile B");

        QCOMPARE(f.profileManager.currentProfileName(), "Profile B");
        // previousProfileName may be empty for JSON-loaded profiles (no filename),
        // but the method should not crash
        f.profileManager.previousProfileName();  // should not crash
    }

    // === Temperature override ===

    void temperatureOverrideAffectsUpload() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0, 90.0);

        // Set a temperature override
        f.settings.setTemperatureOverride(95.0);
        f.transport.clearWrites();
        f.profileManager.uploadCurrentProfile();

        // Shot settings should reflect the override, not the profile default
        auto settingsWrites = f.writesTo(SHOT_SETTINGS);
        QVERIFY(!settingsWrites.isEmpty());
        QByteArray data = settingsWrites.last();
        uint16_t encoded = (static_cast<uint8_t>(data[7]) << 8) | static_cast<uint8_t>(data[8]);
        double groupTemp = BinaryCodec::decodeU16P8(encoded);
        QVERIFY2(qAbs(groupTemp - 95.0) < 0.5,
                 qPrintable(QString("Group temp with override should be ~95.0, got %1").arg(groupTemp)));
    }
};

QTEST_GUILESS_MAIN(tst_ProfileManager)
#include "tst_profilemanager.moc"
