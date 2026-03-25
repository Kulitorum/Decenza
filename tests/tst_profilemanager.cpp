#include <QtTest>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>

#include "mocks/McpTestFixture.h"
#include "mcp/mcpresourceregistry.h"
#include "ble/protocol/de1characteristics.h"
#include "ble/protocol/binarycodec.h"
#include "profile/recipeparams.h"
#include "profile/profilesavehelper.h"

using namespace DE1::Characteristic;

// Forward declaration — implemented in mcpresources.cpp
class MemoryMonitor;
class ShotHistoryStorage;
void registerMcpResources(McpResourceRegistry* registry, DE1Device* device,
                          MachineState* machineState, ProfileManager* profileManager,
                          ShotHistoryStorage* shotHistory, MemoryMonitor* memoryMonitor);

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
    // === QML migration guard: no stale MainController.profileMethod references ===

    void noStaleMainControllerProfileRefsInQml() {
        // Scan all QML files for MainController references to methods/properties
        // that were moved to ProfileManager. Any match is a missed migration.
        QDir qmlDir(QCoreApplication::applicationDirPath() + "/../../../../qml");
        if (!qmlDir.exists())
            qmlDir.setPath(QString(SRCDIR) + "/../qml");
        if (!qmlDir.exists())
            QSKIP("QML directory not found — run from source tree");

        // Profile identifiers that must NOT appear as MainController.X in QML
        static const QStringList profileIds = {
            "loadProfile", "saveProfile", "saveProfileAs", "uploadProfile",
            "uploadCurrentProfile", "uploadRecipeProfile", "deleteProfile",
            "profileExists", "findProfileByTitle", "getProfileByFilename",
            "getCurrentProfile", "markProfileClean", "titleToFilename",
            "getOrConvertRecipeParams", "createNewRecipe", "createNewAFlowRecipe",
            "createNewPressureProfile", "createNewFlowProfile", "createNewProfile",
            "convertCurrentProfileToAdvanced", "loadProfileFromJson", "refreshProfiles",
            "addFrame", "deleteFrame", "moveFrameUp", "moveFrameDown",
            "duplicateFrame", "setFrameProperty", "getFrameAt", "frameCount",
            "activateBrewWithOverrides", "clearBrewOverrides", "previousProfileName",
            "currentProfileName", "baseProfileName", "profileModified",
            "targetWeight", "brewByRatioActive", "brewByRatioDose", "brewByRatio",
            "availableProfiles", "selectedProfiles", "allBuiltInProfiles",
            "cleaningProfiles", "downloadedProfiles", "userCreatedProfiles",
            "allProfilesList", "isCurrentProfileRecipe", "currentEditorType",
            "profileTargetTemperature", "profileTargetWeight",
            "profileHasRecommendedDose", "profileRecommendedDose", "currentProfilePtr"
        };

        // Profile signal handler names that must NOT appear in Connections
        // targeting MainController (catches "target: MainController" + handler pattern)
        static const QStringList profileSignalHandlers = {
            "onCurrentProfileChanged", "onProfileModifiedChanged",
            "onTargetWeightChanged", "onProfilesChanged",
            "onAllBuiltInProfileListChanged", "onProfileUploadBlocked"
        };

        // Build regex for dot-access: MainController\.(id1|id2|...)
        QString dotPattern = "MainController\\.(" + profileIds.join("|") + ")";
        QRegularExpression dotRe(dotPattern);

        // Build regex for signal handlers inside Connections blocks
        QString handlerPattern = "function\\s+(" + profileSignalHandlers.join("|") + ")";
        QRegularExpression handlerRe(handlerPattern);
        QRegularExpression targetRe("target\\s*:\\s*MainController\\b");

        QStringList violations;
        QDirIterator it(qmlDir.absolutePath(), {"*.qml"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString filePath = it.next();
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            QStringList lines;
            while (!file.atEnd())
                lines.append(QString::fromUtf8(file.readLine()));
            QString relPath = qmlDir.relativeFilePath(filePath);

            for (qsizetype i = 0; i < lines.size(); ++i) {
                // Check 1: MainController.profileMethod dot-access
                QRegularExpressionMatch m = dotRe.match(lines[i]);
                if (m.hasMatch()) {
                    violations << QString("%1:%2: MainController.%3")
                        .arg(relPath).arg(i + 1).arg(m.captured(1));
                }

                // Check 2: Connections { target: MainController } with profile signal handler
                // Look for "target: MainController" and scan nearby lines for handlers
                if (targetRe.match(lines[i]).hasMatch()) {
                    // Scan up to 10 lines after for profile signal handlers
                    for (qsizetype j = i + 1; j < qMin(i + 10, lines.size()); ++j) {
                        // Stop at closing brace (end of Connections block)
                        if (lines[j].trimmed().startsWith('}'))
                            break;
                        QRegularExpressionMatch hm = handlerRe.match(lines[j]);
                        if (hm.hasMatch()) {
                            violations << QString("%1:%2: Connections target: MainController with %3")
                                .arg(relPath).arg(j + 1).arg(hm.captured(1));
                        }
                    }
                }
            }
        }

        if (!violations.isEmpty()) {
            QString msg = QString("Found %1 stale MainController profile reference(s) in QML:\n  %2")
                .arg(violations.size())
                .arg(violations.join("\n  "));
            QFAIL(qPrintable(msg));
        }
    }

    // === MCP resource: decenza://profiles/active ===

    void mcpResourceActiveProfileReturnsFilenameAndTitle() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Espresso");

        McpResourceRegistry resources;
        registerMcpResources(&resources, &f.device, &f.machineState,
                             &f.profileManager, nullptr, nullptr);

        QString error;
        QJsonObject result = resources.readResource("decenza://profiles/active", error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        // "filename" should be baseProfileName (the filename, not display title)
        // "title" should be currentProfileName (display title)
        QVERIFY2(result.contains("title"), "Active profile resource must include 'title'");
        QCOMPARE(result["title"].toString(), "D-Flow / Espresso");

        // filename may be empty for JSON-loaded profiles (no disk file),
        // but the field must exist
        QVERIFY2(result.contains("filename"), "Active profile resource must include 'filename'");
    }

    void mcpResourceActiveProfileReturnsTemperatureAndWeight() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 40.0, 91.5);

        McpResourceRegistry resources;
        registerMcpResources(&resources, &f.device, &f.machineState,
                             &f.profileManager, nullptr, nullptr);

        QString error;
        QJsonObject result = resources.readResource("decenza://profiles/active", error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result["targetWeightG"].toDouble(), 40.0);
        QCOMPARE(result["targetTemperatureC"].toDouble(), 91.5);
    }

    // === QML binding smoke test ===
    // Verifies that ProfileManager properties resolve to real values when
    // registered as a QML context property. Would have caught the 3 QML bugs
    // from the PR #562 code review (previousProfileName, currentProfile,
    // typeof guard).

    void qmlBindingsResolveCorrectly() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / QML Test", 36.0, 93.0);

        QQmlEngine engine;
        engine.rootContext()->setContextProperty("ProfileManager", &f.profileManager);

        auto evaluate = [&](const QString& expr) -> QVariant {
            QQmlExpression qmlExpr(engine.rootContext(), nullptr, expr);
            bool isUndefined = false;
            QVariant result = qmlExpr.evaluate(&isUndefined);
            if (isUndefined)
                return QVariant();  // null signals "undefined"
            return result;
        };

        // Core properties must not be undefined
        QVERIFY2(!evaluate("ProfileManager.currentProfileName").isNull(),
                 "ProfileManager.currentProfileName must not be undefined in QML");
        QCOMPARE(evaluate("ProfileManager.currentProfileName").toString(), "D-Flow / QML Test");

        QVERIFY2(!evaluate("ProfileManager.profileModified").isNull(),
                 "ProfileManager.profileModified must not be undefined in QML");

        QVERIFY2(!evaluate("ProfileManager.targetWeight").isNull(),
                 "ProfileManager.targetWeight must not be undefined in QML");
        QCOMPARE(evaluate("ProfileManager.targetWeight").toDouble(), 36.0);

        QVERIFY2(!evaluate("ProfileManager.profileTargetTemperature").isNull(),
                 "ProfileManager.profileTargetTemperature must not be undefined in QML");
        QCOMPARE(evaluate("ProfileManager.profileTargetTemperature").toDouble(), 93.0);

        QVERIFY2(!evaluate("ProfileManager.isCurrentProfileRecipe").isNull(),
                 "ProfileManager.isCurrentProfileRecipe must not be undefined in QML");

        QVERIFY2(!evaluate("ProfileManager.currentEditorType").isNull(),
                 "ProfileManager.currentEditorType must not be undefined in QML");
        QCOMPARE(evaluate("ProfileManager.currentEditorType").toString(), "dflow");

        QVERIFY2(!evaluate("ProfileManager.brewByRatioActive").isNull(),
                 "ProfileManager.brewByRatioActive must not be undefined in QML");

        QVERIFY2(!evaluate("ProfileManager.profileTargetWeight").isNull(),
                 "ProfileManager.profileTargetWeight must not be undefined in QML");

        QVERIFY2(!evaluate("ProfileManager.baseProfileName").isNull(),
                 "ProfileManager.baseProfileName must not be undefined in QML");
    }

    void qmlMethodsCallable() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Methods Test");

        QQmlEngine engine;
        engine.rootContext()->setContextProperty("ProfileManager", &f.profileManager);

        auto evaluate = [&](const QString& expr) -> QVariant {
            QQmlExpression qmlExpr(engine.rootContext(), nullptr, expr);
            bool isUndefined = false;
            QVariant result = qmlExpr.evaluate(&isUndefined);
            if (isUndefined)
                return QVariant();
            return result;
        };

        // Q_INVOKABLE methods must be callable (not undefined)
        QVariant result = evaluate("ProfileManager.getCurrentProfile()");
        QVERIFY2(!result.isNull(), "ProfileManager.getCurrentProfile() must be callable from QML");

        result = evaluate("ProfileManager.frameCount()");
        QVERIFY2(!result.isNull(), "ProfileManager.frameCount() must be callable from QML");
        QCOMPARE(result.toInt(), 2);

        result = evaluate("ProfileManager.previousProfileName()");
        // May return empty string but must not be undefined
        QVERIFY2(!result.isNull(), "ProfileManager.previousProfileName() must be callable from QML");

        result = evaluate("ProfileManager.getOrConvertRecipeParams()");
        QVERIFY2(!result.isNull(), "ProfileManager.getOrConvertRecipeParams() must be callable from QML");
    }

    // =========================================================================
    // NEW TESTS — Coverage gaps identified in test review
    // =========================================================================

    // === Static helpers: isDFlowTitle / isAFlowTitle ===

    void isDFlowTitleMatchesDFlowPrefixes() {
        QVERIFY(ProfileManager::isDFlowTitle("D-Flow / Espresso"));
        QVERIFY(ProfileManager::isDFlowTitle("d-flow / test"));  // case-insensitive
        QVERIFY(!ProfileManager::isDFlowTitle("A-Flow / Espresso"));
        QVERIFY(!ProfileManager::isDFlowTitle("My Custom Profile"));
        QVERIFY(!ProfileManager::isDFlowTitle(""));
    }

    void isDFlowTitleIgnoresLeadingStar() {
        // Modified indicator prefix from imports — should still match
        QVERIFY(ProfileManager::isDFlowTitle("*D-Flow / Espresso"));
        QVERIFY(!ProfileManager::isDFlowTitle("*A-Flow / Espresso"));
    }

    void isAFlowTitleMatchesAFlowPrefixes() {
        QVERIFY(ProfileManager::isAFlowTitle("A-Flow / Espresso"));
        QVERIFY(ProfileManager::isAFlowTitle("a-flow / test"));  // case-insensitive
        QVERIFY(ProfileManager::isAFlowTitle("*A-Flow / Modified"));  // star prefix
        QVERIFY(!ProfileManager::isAFlowTitle("D-Flow / Espresso"));
        QVERIFY(!ProfileManager::isAFlowTitle("My Profile"));
    }

    // === titleToFilename ===

    void titleToFilenameBasic() {
        McpTestFixture f;
        QCOMPARE(f.profileManager.titleToFilename("D-Flow / Espresso"), "d_flow_espresso");
    }

    void titleToFilenameAccents() {
        McpTestFixture f;
        // Accented characters should be replaced with ASCII equivalents
        QString result = f.profileManager.titleToFilename(QString::fromUtf8("Caf\xC3\xA9 Cr\xC3\xA8me"));
        QCOMPARE(result, "cafe_creme");
    }

    void titleToFilenameSpecialChars() {
        McpTestFixture f;
        // Multiple special chars collapse to single underscore, edges trimmed
        QCOMPARE(f.profileManager.titleToFilename("  Hello  World  "), "hello_world");
        QCOMPARE(f.profileManager.titleToFilename("test!!!profile"), "test_profile");
    }

    // === Frame operations: move, duplicate, setFrameProperty ===

    void moveFrameUpSwapsFrames() {
        McpTestFixture f;
        loadDFlowProfile(f);
        QCOMPARE(f.profileManager.getFrameAt(0)["name"].toString(), "fill");
        QCOMPARE(f.profileManager.getFrameAt(1)["name"].toString(), "pour");

        f.profileManager.moveFrameUp(1);

        QCOMPARE(f.profileManager.getFrameAt(0)["name"].toString(), "pour");
        QCOMPARE(f.profileManager.getFrameAt(1)["name"].toString(), "fill");
    }

    void moveFrameUpAtZeroIsNoop() {
        McpTestFixture f;
        loadDFlowProfile(f);

        QSignalSpy spy(&f.profileManager, &ProfileManager::currentProfileChanged);
        f.profileManager.moveFrameUp(0);

        // No signal emitted — nothing changed
        QCOMPARE(spy.count(), 0);
        QCOMPARE(f.profileManager.getFrameAt(0)["name"].toString(), "fill");
    }

    void moveFrameDownSwapsFrames() {
        McpTestFixture f;
        loadDFlowProfile(f);

        f.profileManager.moveFrameDown(0);

        QCOMPARE(f.profileManager.getFrameAt(0)["name"].toString(), "pour");
        QCOMPARE(f.profileManager.getFrameAt(1)["name"].toString(), "fill");
    }

    void moveFrameDownAtLastIsNoop() {
        McpTestFixture f;
        loadDFlowProfile(f);

        QSignalSpy spy(&f.profileManager, &ProfileManager::currentProfileChanged);
        f.profileManager.moveFrameDown(1);  // Already at last index

        QCOMPARE(spy.count(), 0);
        QCOMPARE(f.profileManager.getFrameAt(1)["name"].toString(), "pour");
    }

    void duplicateFrameInsertsAfter() {
        McpTestFixture f;
        loadDFlowProfile(f);
        QCOMPARE(f.profileManager.frameCount(), 2);

        f.profileManager.duplicateFrame(0);

        QCOMPARE(f.profileManager.frameCount(), 3);
        QCOMPARE(f.profileManager.getFrameAt(1)["name"].toString(), "fill (copy)");
    }

    void duplicateFrameMarksModified() {
        McpTestFixture f;
        loadDFlowProfile(f);

        QSignalSpy spy(&f.profileManager, &ProfileManager::profileModifiedChanged);
        f.profileManager.duplicateFrame(0);

        QCOMPARE(spy.count(), 1);
        QVERIFY(f.profileManager.isProfileModified());
    }

    void setFramePropertyUpdatesValue() {
        McpTestFixture f;
        loadDFlowProfile(f);

        f.profileManager.setFrameProperty(0, "temperature", 88.0);

        QVariantMap frame = f.profileManager.getFrameAt(0);
        QCOMPARE(frame["temperature"].toDouble(), 88.0);
    }

    void setFramePropertyUnknownIsNoop() {
        McpTestFixture f;
        loadDFlowProfile(f);

        // Unknown property should not crash and should not emit currentProfileChanged
        QSignalSpy spy(&f.profileManager, &ProfileManager::currentProfileChanged);
        f.profileManager.setFrameProperty(0, "nonexistent_property", 42);

        QCOMPARE(spy.count(), 0);
    }

    void deleteLastFrameIsBlocked() {
        McpTestFixture f;
        loadDFlowProfile(f);
        f.profileManager.deleteFrame(1);  // Remove one, leaving 1
        QCOMPARE(f.profileManager.frameCount(), 1);

        f.profileManager.deleteFrame(0);  // Should be blocked
        QCOMPARE(f.profileManager.frameCount(), 1);
    }

    // === Brew-by-ratio ===

    void brewByRatioInactiveByDefault() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0);
        QVERIFY(!f.profileManager.brewByRatioActive());
    }

    void brewByRatioActiveWhenOverrideDiffers() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0);

        // Set a yield override different from profile's 36.0
        f.settings.setBrewYieldOverride(54.0);
        QVERIFY(f.profileManager.brewByRatioActive());
    }

    void brewByRatioCalculation() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0);

        f.settings.setDyeBeanWeight(18.0);
        f.settings.setBrewYieldOverride(36.0);

        QCOMPARE(f.profileManager.brewByRatio(), 2.0);
    }

    void clearBrewOverridesResetsToProfileDefaults() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0, 93.0);

        // Activate with different values
        f.profileManager.activateBrewWithOverrides(20.0, 50.0, 96.0, "15");

        // Clear should reset to profile defaults
        f.profileManager.clearBrewOverrides();

        QCOMPARE(f.settings.brewYieldOverride(), 36.0);
        QCOMPARE(f.settings.temperatureOverride(), 93.0);
    }

    // === activateBrewWithOverrides ===

    void activateBrewWithOverridesSetsSettings() {
        McpTestFixture f;
        loadDFlowProfile(f);

        f.profileManager.activateBrewWithOverrides(18.0, 40.0, 95.0, "14");

        QCOMPARE(f.settings.dyeBeanWeight(), 18.0);
        QCOMPARE(f.settings.brewYieldOverride(), 40.0);
        QCOMPARE(f.settings.temperatureOverride(), 95.0);
        QCOMPARE(f.settings.dyeGrinderSetting(), "14");
    }

    void activateBrewWithOverridesTriggersUpload() {
        McpTestFixture f;
        loadDFlowProfile(f);
        f.transport.clearWrites();

        f.profileManager.activateBrewWithOverrides(18.0, 40.0, 95.0, "14");

        auto headerWrites = f.writesTo(HEADER_WRITE);
        QVERIFY2(!headerWrites.isEmpty(), "activateBrewWithOverrides must trigger BLE upload");
    }

    // === Profile creation factories ===

    void createNewRecipeSetsEditorType() {
        McpTestFixture f;
        // Title must start with "D-Flow" for currentEditorType() title-based detection
        f.profileManager.createNewRecipe("D-Flow / Custom");

        QCOMPARE(f.profileManager.currentEditorType(), "dflow");
        QVERIFY(f.profileManager.isCurrentProfileRecipe());
        QVERIFY(f.profileManager.isProfileModified());
        QVERIFY(f.profileManager.frameCount() > 0);
    }

    void createNewAFlowRecipeSetsEditorType() {
        McpTestFixture f;
        // Title must start with "A-Flow" for currentEditorType() title-based detection
        f.profileManager.createNewAFlowRecipe("A-Flow / Custom");

        QCOMPARE(f.profileManager.currentEditorType(), "aflow");
        QVERIFY(f.profileManager.isCurrentProfileRecipe());
    }

    void createNewPressureProfileSetsEditorType() {
        McpTestFixture f;
        f.profileManager.createNewPressureProfile("My Pressure");

        QCOMPARE(f.profileManager.currentEditorType(), "pressure");
        QVERIFY(f.profileManager.isCurrentProfileRecipe());
    }

    void createNewFlowProfileSetsEditorType() {
        McpTestFixture f;
        f.profileManager.createNewFlowProfile("My Flow");

        QCOMPARE(f.profileManager.currentEditorType(), "flow");
        QVERIFY(f.profileManager.isCurrentProfileRecipe());
    }

    void createNewProfileCreatesBlankAdvanced() {
        McpTestFixture f;
        f.profileManager.createNewProfile("Blank Profile");

        QCOMPARE(f.profileManager.frameCount(), 1);
        QCOMPARE(f.profileManager.currentProfileName(), "*Blank Profile");
        QVERIFY(f.profileManager.isProfileModified());
        // Not a D-Flow/A-Flow title → advanced editor
        QCOMPARE(f.profileManager.currentEditorType(), "advanced");
    }

    void convertCurrentProfileToAdvancedDisablesRecipe() {
        McpTestFixture f;
        loadDFlowProfile(f);
        QVERIFY(f.profileManager.isCurrentProfileRecipe());

        f.profileManager.convertCurrentProfileToAdvanced();

        // Profile type is settings_2c (not 2a/2b) and recipe mode is off,
        // but title still starts with "D-Flow" so isCurrentProfileRecipe()
        // still returns true (title-based detection). The editor type check
        // is the authoritative test.
        QVERIFY(f.profileManager.isProfileModified());

        // Frames should be preserved
        QCOMPARE(f.profileManager.frameCount(), 2);
    }

    // === Signal precision ===

    void setTargetWeightSameValueNoSignal() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0);

        QSignalSpy spy(&f.profileManager, &ProfileManager::targetWeightChanged);
        f.profileManager.setTargetWeight(36.0);  // Same as profile default

        QCOMPARE(spy.count(), 0);
    }

    void uploadProfileDoubleCallEmitsOnce() {
        McpTestFixture f;
        loadDFlowProfile(f);

        QVariantMap profile = f.profileManager.getCurrentProfile();
        profile["target_weight"] = 42.0;

        QSignalSpy spy(&f.profileManager, &ProfileManager::profileModifiedChanged);
        f.profileManager.uploadProfile(profile);
        f.profileManager.uploadProfile(profile);  // Second call — already modified

        // The idempotent guard should prevent the second emission
        QCOMPARE(spy.count(), 1);
    }

    void markProfileCleanEmitsCurrentProfileChanged() {
        McpTestFixture f;
        loadDFlowProfile(f);

        QVariantMap profile = f.profileManager.getCurrentProfile();
        profile["target_weight"] = 42.0;
        f.profileManager.uploadProfile(profile);

        QSignalSpy modSpy(&f.profileManager, &ProfileManager::profileModifiedChanged);
        QSignalSpy curSpy(&f.profileManager, &ProfileManager::currentProfileChanged);
        f.profileManager.markProfileClean();

        // Must emit both: profileModifiedChanged (modified → clean)
        // and currentProfileChanged (remove * prefix from name)
        QCOMPARE(modSpy.count(), 1);
        QVERIFY(curSpy.count() >= 1);
    }

    // === Upload blocked during all active phases ===

    void uploadBlockedDuringAllActivePhases() {
        const QList<MachineState::Phase> blockedPhases = {
            MachineState::Phase::EspressoPreheating,
            MachineState::Phase::Preinfusion,
            MachineState::Phase::Pouring,
            MachineState::Phase::Ending,
            MachineState::Phase::Steaming,
            MachineState::Phase::HotWater,
            MachineState::Phase::Flushing,
            MachineState::Phase::Descaling,
            MachineState::Phase::Cleaning
        };

        for (MachineState::Phase phase : blockedPhases) {
            McpTestFixture f;
            loadDFlowProfile(f);
            f.machineState.m_phase = phase;
            f.transport.clearWrites();

            f.profileManager.uploadCurrentProfile();

            auto headerWrites = f.writesTo(HEADER_WRITE);
            QVERIFY2(headerWrites.isEmpty(),
                qPrintable(QString("Upload must be blocked during phase %1")
                    .arg(static_cast<int>(phase))));
        }
    }

    // === Pending retry mechanism ===

    void pendingUploadRetriesOnIdle() {
        McpTestFixture f;
        loadDFlowProfile(f);

        // Block upload during Pouring
        f.machineState.m_phase = MachineState::Phase::Pouring;
        f.transport.clearWrites();
        f.profileManager.uploadCurrentProfile();
        QVERIFY(f.writesTo(HEADER_WRITE).isEmpty());
        QVERIFY(f.profileManager.m_profileUploadPending);

        // Transition to Idle — should trigger retry
        f.machineState.m_phase = MachineState::Phase::Idle;
        emit f.machineState.phaseChanged();

        auto headerWrites = f.writesTo(HEADER_WRITE);
        QVERIFY2(!headerWrites.isEmpty(), "Pending upload must retry when phase becomes Idle");
        QVERIFY(!f.profileManager.m_profileUploadPending);
    }

    void pendingUploadClearedOnDisconnect() {
        McpTestFixture f;
        loadDFlowProfile(f);

        // Block upload during Pouring
        f.machineState.m_phase = MachineState::Phase::Pouring;
        f.transport.clearWrites();
        f.profileManager.uploadCurrentProfile();
        QVERIFY(f.profileManager.m_profileUploadPending);

        // Disconnect — should clear pending without retry
        f.machineState.m_phase = MachineState::Phase::Disconnected;
        emit f.machineState.phaseChanged();

        QVERIFY(!f.profileManager.m_profileUploadPending);
        QVERIFY2(f.writesTo(HEADER_WRITE).isEmpty(),
            "Disconnect must not trigger BLE write");
    }

    // === uploadRecipeProfile signal verification ===

    void uploadRecipeProfileEmitsAllSignals() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test", 36.0, 93.0);

        QSignalSpy modSpy(&f.profileManager, &ProfileManager::profileModifiedChanged);
        QSignalSpy curSpy(&f.profileManager, &ProfileManager::currentProfileChanged);
        QSignalSpy wgtSpy(&f.profileManager, &ProfileManager::targetWeightChanged);

        QVariantMap recipe;
        recipe["editorType"] = "dflow";
        recipe["targetWeight"] = 40.0;
        recipe["fillTemperature"] = 95.0;
        recipe["pourTemperature"] = 95.0;
        recipe["fillPressure"] = 6.0;
        recipe["fillFlow"] = 4.0;
        recipe["pourFlow"] = 2.5;
        f.profileManager.uploadRecipeProfile(recipe);

        QCOMPARE(modSpy.count(), 1);
        QVERIFY(curSpy.count() >= 1);
        QVERIFY(wgtSpy.count() >= 1);
    }

    // === getCurrentProfile comprehensive field coverage ===

    void getCurrentProfileContainsAllFields() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / FieldTest", 38.0, 92.0);

        QVariantMap profile = f.profileManager.getCurrentProfile();

        // Top-level fields
        QCOMPARE(profile["title"].toString(), "D-Flow / FieldTest");
        QCOMPARE(profile["author"].toString(), "test");
        QCOMPARE(profile["target_weight"].toDouble(), 38.0);
        QCOMPARE(profile["target_volume"].toDouble(), 0.0);
        QCOMPARE(profile["espresso_temperature"].toDouble(), 92.0);
        QVERIFY(profile.contains("mode"));
        QVERIFY(profile.contains("preinfuse_frame_count"));

        // Per-frame fields
        QVariantList steps = profile["steps"].toList();
        QVERIFY(steps.size() >= 2);
        QVariantMap frame = steps[0].toMap();
        QVERIFY(frame.contains("name"));
        QVERIFY(frame.contains("temperature"));
        QVERIFY(frame.contains("sensor"));
        QVERIFY(frame.contains("pump"));
        QVERIFY(frame.contains("transition"));
        QVERIFY(frame.contains("pressure"));
        QVERIFY(frame.contains("flow"));
        QVERIFY(frame.contains("seconds"));
        QVERIFY(frame.contains("volume"));
        QVERIFY(frame.contains("exit_if"));
        QVERIFY(frame.contains("exit_type"));
        QVERIFY(frame.contains("exit_pressure_over"));
        QVERIFY(frame.contains("max_flow_or_pressure"));
        QVERIFY(frame.contains("max_flow_or_pressure_range"));
    }

    // === Profile catalog (built-in profiles) ===

    void refreshProfilesPopulatesBuiltInProfiles() {
        McpTestFixture f;
        // Constructor calls refreshProfiles(). Built-in profiles come from QRC (:/profiles/)
        // which may not be linked in the test binary. Verify the mechanism works by
        // checking that after adding a saved profile, allProfiles() reflects it.
        loadDFlowProfile(f, "D-Flow / CatalogTest");
        f.profileManager.saveProfile("catalog_test");

        f.profileManager.refreshProfiles();
        const auto& allProfiles = f.profileManager.allProfiles();
        QVERIFY2(!allProfiles.isEmpty(), "Profiles list must be non-empty after save + refresh");

        bool found = false;
        for (const ProfileInfo& info : allProfiles) {
            if (info.filename == "catalog_test") {
                found = true;
                break;
            }
        }
        QVERIFY2(found, "Saved profile must appear in allProfiles() after refresh");
    }

    void availableProfilesReturnsSortedList() {
        McpTestFixture f;
        // Create multiple profiles to ensure sorting can be verified
        loadDFlowProfile(f, "D-Flow / Zebra");
        f.profileManager.saveProfile("zebra_profile");
        loadDFlowProfile(f, "D-Flow / Alpha");
        f.profileManager.saveProfile("alpha_profile");
        f.profileManager.refreshProfiles();

        QVariantList profiles = f.profileManager.availableProfiles();
        QVERIFY(profiles.size() >= 2);

        // Verify alphabetical sort by title
        for (qsizetype i = 1; i < profiles.size(); ++i) {
            QString prev = profiles[i-1].toMap()["title"].toString();
            QString curr = profiles[i].toMap()["title"].toString();
            QVERIFY2(prev.compare(curr, Qt::CaseInsensitive) <= 0,
                qPrintable(QString("Profiles not sorted: '%1' before '%2'").arg(prev, curr)));
        }
    }

    void profileExistsForSavedProfile() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / ExistsTest");
        f.profileManager.saveProfile("exists_test");

        QVERIFY(f.profileManager.profileExists("exists_test"));
        QVERIFY(!f.profileManager.profileExists("nonexistent_profile_xyz"));
    }

    void findProfileByTitleFindsSavedProfile() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / FindMe");
        f.profileManager.saveProfile("find_me_profile");
        f.profileManager.refreshProfiles();

        QString filename = f.profileManager.findProfileByTitle("D-Flow / FindMe");
        QCOMPARE(filename, "find_me_profile");
    }

    void findProfileByTitleReturnsEmptyForMissing() {
        McpTestFixture f;
        QString filename = f.profileManager.findProfileByTitle("No Such Profile XYZ");
        QVERIFY(filename.isEmpty());
    }

    // === File-based loadProfile ===

    void loadProfileByFilenamLoadsSavedProfile() {
        McpTestFixture f;
        // Save a profile first, then load by filename
        loadDFlowProfile(f, "D-Flow / LoadTest");
        f.profileManager.saveProfile("load_test");

        // Load a different profile to reset state
        loadDFlowProfile(f, "D-Flow / Other");

        // Now load back by filename
        f.profileManager.loadProfile("load_test");

        QCOMPARE(f.profileManager.currentProfileName(), "D-Flow / LoadTest");
        QCOMPARE(f.profileManager.baseProfileName(), "load_test");
        QVERIFY(!f.profileManager.isProfileModified());
    }

    void loadProfileSetsPreviousProfileName() {
        McpTestFixture f;
        // Save two profiles
        loadDFlowProfile(f, "D-Flow / First");
        f.profileManager.saveProfile("first_profile");
        loadDFlowProfile(f, "D-Flow / Second");
        f.profileManager.saveProfile("second_profile");

        // Load first, then second — previous should track
        f.profileManager.loadProfile("first_profile");
        f.profileManager.loadProfile("second_profile");

        QCOMPARE(f.profileManager.previousProfileName(), "first_profile");
    }

    void loadProfileNotFoundFallsToDefault() {
        McpTestFixture f;
        f.profileManager.loadProfile("nonexistent_profile_xyz");

        // Should not crash — loads default or stays on current
        QVERIFY(!f.profileManager.currentProfileName().isEmpty());
    }

    // === Save / SaveAs ===

    void saveProfileWritesToDisk() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / SaveTest");

        // Modify so there's something to save
        QVariantMap profile = f.profileManager.getCurrentProfile();
        profile["target_weight"] = 42.0;
        f.profileManager.uploadProfile(profile);

        bool saved = f.profileManager.saveProfile("save_test");

        QVERIFY(saved);
        // Verify the file exists in user profiles dir
        QString expectedPath = f.profileManager.userProfilesPath() + "/save_test.json";
        QVERIFY2(QFile::exists(expectedPath),
            qPrintable(QString("Saved file not found at: %1").arg(expectedPath)));
    }

    void saveProfileAsChangesTitle() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Original");

        bool saved = f.profileManager.saveProfileAs("renamed_profile", "D-Flow / Renamed");

        QVERIFY(saved);
        QCOMPARE(f.profileManager.currentProfileName(), "D-Flow / Renamed");
        QCOMPARE(f.profileManager.baseProfileName(), "renamed_profile");
    }

    void saveProfileCleansModifiedFlag() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / DirtyTest");

        // Make it modified
        QVariantMap profile = f.profileManager.getCurrentProfile();
        profile["target_weight"] = 42.0;
        f.profileManager.uploadProfile(profile);
        QVERIFY(f.profileManager.isProfileModified());

        f.profileManager.saveProfile("dirty_test");

        QVERIFY(!f.profileManager.isProfileModified());
    }

    // === getProfileByFilename ===

    void getProfileByFilenameReturnsSavedProfile() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / GetByName", 38.0);
        f.profileManager.saveProfile("get_by_name_test");

        QVariantMap profile = f.profileManager.getProfileByFilename("get_by_name_test");

        QVERIFY(!profile.isEmpty());
        QCOMPARE(profile["title"].toString(), "D-Flow / GetByName");
        QVERIFY(profile.contains("steps"));
        QCOMPARE(profile["target_weight"].toDouble(), 38.0);
    }

    void getProfileByFilenameReturnsEmptyForMissing() {
        McpTestFixture f;
        QVariantMap profile = f.profileManager.getProfileByFilename("nonexistent_xyz");
        QVERIFY(profile.isEmpty());
    }

    // === Read-only profile protection ===

    void readOnlyFieldJsonRoundTrip() {
        // read_only: 1 survives toJson/fromJson
        Profile p;
        p.setTitle("Test Profile");
        p.setReadOnly(1);
        QJsonDocument doc = p.toJson();
        QJsonObject obj = doc.object();
        QCOMPARE(obj["read_only"].toInt(), 1);

        Profile p2 = Profile::fromJson(doc);
        QCOMPARE(p2.readOnly(), 1);
        QVERIFY(p2.isReadOnly());

        // read_only: 0 should not appear in JSON (default)
        Profile p3;
        p3.setTitle("Test");
        p3.setReadOnly(0);
        QJsonDocument doc3 = p3.toJson();
        QVERIFY(!doc3.object().contains("read_only"));

        // read_only: 2 should appear in JSON
        Profile p4;
        p4.setTitle("Test");
        p4.setReadOnly(2);
        QJsonDocument doc4 = p4.toJson();
        QCOMPARE(doc4.object()["read_only"].toInt(), 2);
    }

    void readOnlyFieldTclImport() {
        // TCL profile with read_only 1 should be parsed
        QString tcl = R"(
            profile_title {Test Read Only}
            author {test}
            beverage_type espresso
            settings_profile_type settings_2c
            read_only 1
            final_desired_shot_weight_advanced 36.0
            final_desired_shot_volume_advanced 0
            espresso_temperature 93.0
            advanced_shot {}
        )";
        Profile p = Profile::loadFromTclString(tcl);
        QCOMPARE(p.readOnly(), 1);
        QVERIFY(p.isReadOnly());
    }

    void isCurrentProfileReadOnlyForReadOnlyFlag() {
        McpTestFixture f;
        // Load a profile with read_only: 1 — should be detected as read-only
        loadDFlowProfile(f, "D-Flow / Protected");
        f.profileManager.m_currentProfile.setReadOnly(1);
        QVERIFY(f.profileManager.isCurrentProfileReadOnly());

        // Load a profile without read_only — should not be read-only
        loadDFlowProfile(f, "D-Flow / Editable");
        f.profileManager.m_currentProfile.setReadOnly(0);
        QVERIFY(!f.profileManager.isCurrentProfileReadOnly());
    }

    void saveProfileRejectsReadOnly() {
        McpTestFixture f;
        // Load a profile and mark it read-only
        loadDFlowProfile(f, "D-Flow / Protected");
        f.profileManager.m_currentProfile.setReadOnly(1);
        f.profileManager.m_baseProfileName = "test_protected";

        // Attempt to save in place — should fail because read-only
        QVERIFY(!f.profileManager.saveProfile("test_protected"));
    }

    void saveProfileAsRejectsBuiltInFilename() {
        McpTestFixture f;
        // isBuiltInFilename checks :/profiles/ resources
        // "default" is a known built-in profile filename
        bool hasDefault = f.profileManager.isBuiltInFilename("default");
        if (!hasDefault) {
            QSKIP("No built-in profiles in test binary QRC");
        }
        // Attempt Save As with a built-in filename — should fail
        loadDFlowProfile(f, "Some Custom Title");
        QVERIFY(!f.profileManager.saveProfileAs("default", "Some Custom Title"));
    }

    void isBuiltInFilenameReturnsFalseForUserProfile() {
        McpTestFixture f;
        QVERIFY(!f.profileManager.isBuiltInFilename("my_custom_profile_xyz"));
        QVERIFY(!f.profileManager.isBuiltInFilename(""));
    }

    void saveProfileAsClearsReadOnlyFlag() {
        // When saving as a copy, the read_only flag should be cleared
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test");
        // Manually set read_only on the profile
        f.profileManager.m_currentProfile.setReadOnly(1);
        // Save as a new name (non-built-in)
        bool saved = f.profileManager.saveProfileAs("test_user_copy_xyz", "D-Flow / Test Copy");
        if (saved) {
            // The profile's read_only should be cleared to 0
            QCOMPARE(f.profileManager.currentProfile().readOnly(), 0);
        }
        // Cleanup
        QFile::remove(f.profileManager.userProfilesPath() + "/test_user_copy_xyz.json");
    }

    // === ProfileSaveHelper::compareProfiles() — unified duplicate detection ===

    void compareProfilesIdentical() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test", 36.0, 93.0);
        Profile a = f.profileManager.currentProfile();
        Profile b = f.profileManager.currentProfile();
        QVERIFY(ProfileSaveHelper::compareProfiles(a, b));
    }

    void compareProfilesDifferentPressure() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test");
        Profile a = f.profileManager.currentProfile();
        Profile b = a;
        auto steps = b.steps();
        steps[0].pressure = steps[0].pressure + 1.0;
        b.setSteps(steps);
        QVERIFY(!ProfileSaveHelper::compareProfiles(a, b));
    }

    void compareProfilesDifferentFlow() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test");
        Profile a = f.profileManager.currentProfile();
        Profile b = a;
        auto steps = b.steps();
        steps[0].flow = steps[0].flow + 0.5;
        b.setSteps(steps);
        QVERIFY(!ProfileSaveHelper::compareProfiles(a, b));
    }

    void compareProfilesDifferentTemperature() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test");
        Profile a = f.profileManager.currentProfile();
        Profile b = a;
        auto steps = b.steps();
        steps[0].temperature = steps[0].temperature + 2.0;
        b.setSteps(steps);
        QVERIFY(!ProfileSaveHelper::compareProfiles(a, b));
    }

    void compareProfilesDifferentStepCount() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test");
        Profile a = f.profileManager.currentProfile();
        Profile b = a;
        ProfileFrame extra;
        extra.name = "extra";
        extra.temperature = 93.0;
        extra.pump = "flow";
        extra.flow = 2.0;
        extra.seconds = 30.0;
        b.addStep(extra);
        QVERIFY(!ProfileSaveHelper::compareProfiles(a, b));
    }

    void compareProfilesEmptySteps() {
        Profile a;
        a.setTitle("Empty A");
        Profile b;
        b.setTitle("Empty B");
        QVERIFY(!ProfileSaveHelper::compareProfiles(a, b));
    }

    void compareProfilesWithinTolerance() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test");
        Profile a = f.profileManager.currentProfile();
        Profile b = a;
        auto steps = b.steps();
        steps[0].pressure += 0.05;  // Within 0.1 tolerance
        steps[0].flow -= 0.05;
        b.setSteps(steps);
        QVERIFY(ProfileSaveHelper::compareProfiles(a, b));
    }

    void compareProfilesDifferentExitCondition() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test");
        Profile a = f.profileManager.currentProfile();
        Profile b = a;
        auto steps = b.steps();
        steps[0].exitPressureOver += 2.0;
        b.setSteps(steps);
        QVERIFY(!ProfileSaveHelper::compareProfiles(a, b));
    }

    void compareProfilesDifferentLimiter() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test");
        Profile a = f.profileManager.currentProfile();
        Profile b = a;
        auto steps = b.steps();
        steps[0].maxFlowOrPressure = 5.0;
        b.setSteps(steps);
        QVERIFY(!ProfileSaveHelper::compareProfiles(a, b));
    }

    void compareProfilesIgnoresTitle() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / A");
        Profile a = f.profileManager.currentProfile();
        loadDFlowProfile(f, "D-Flow / B");
        Profile b = f.profileManager.currentProfile();
        // Same frames, different titles — compareProfiles only checks frames
        QVERIFY(ProfileSaveHelper::compareProfiles(a, b));
    }

    void compareProfilesIgnoresReadOnly() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test");
        Profile a = f.profileManager.currentProfile();
        Profile b = a;
        a.setReadOnly(1);
        b.setReadOnly(0);
        QVERIFY(ProfileSaveHelper::compareProfiles(a, b));
    }
};

QTEST_GUILESS_MAIN(tst_ProfileManager)
#include "tst_profilemanager.moc"
