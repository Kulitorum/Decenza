#include <QtTest>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QJsonObject>
#include <QJsonArray>

#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QDir>

#include "core/settings_calibration.h"
#include <QDirIterator>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThread>

#include "mocks/McpTestFixture.h"
#include "core/settings_app.h"
#include "core/settings_brew.h"
#include "core/settings_dye.h"
#include "mcp/mcpresourceregistry.h"
#include "network/webdebuglogger.h"
#include "ble/protocol/de1characteristics.h"
#include "ble/protocol/binarycodec.h"
#include "core/dbutils.h"
#include "history/coffeebagstorage.h"
#include "history/shothistorystorage.h"
#include "profile/recipeparams.h"
#include "profile/profilesavehelper.h"

using namespace DE1::Characteristic;

// Forward declaration — implemented in mcpresources.cpp
class MemoryMonitor;
class ShotHistoryStorage;
class Settings;
void registerMcpResources(McpResourceRegistry* registry, DE1Device* device,
                          MachineState* machineState, ProfileManager* profileManager,
                          ShotHistoryStorage* shotHistory, MemoryMonitor* memoryMonitor,
                          Settings* settings);
void registerDebugTools(McpToolRegistry* registry, MemoryMonitor* memoryMonitor);

// RAII: points WebDebugLogger::instance() at a test-owned logger backed by an
// explicit file for the guard's lifetime, restoring no-singleton on
// destruction — debug_get_log's handler resolves the real WebDebugLogger::instance().
struct WebDebugLoggerTestGuard {
    WebDebugLogger logger;
    explicit WebDebugLoggerTestGuard(const QString& logFilePath) : logger(logFilePath) {
        WebDebugLogger::installForTesting(&logger);
    }
    ~WebDebugLoggerTestGuard() { WebDebugLogger::installForTesting(nullptr); }
};

// Direct tests for ProfileManager — the core class extracted in the refactor.
// Verifies the profile lifecycle (load, state, save, upload, signals) works
// correctly through ProfileManager without MainController forwarding.

class tst_ProfileManager : public QObject {
    Q_OBJECT

private:
    // Load a minimal D-Flow profile into the fixture's ProfileManager
    // withInfuse: a real D-Flow profile is ALWAYS three frames — Filling /
    // Infusing / Pouring — because that is what the plugin's `prep` indexes
    // (0/1/2, no pattern matching). Most tests here only need *a* profile to
    // manipulate frames on and hardcode a count of two, so the third frame is
    // opt-in; any test that reads recipe PARAMETERS needs it.
    static void loadDFlowProfile(McpTestFixture& f, const QString& title = "D-Flow / Test",
                                 double targetWeight = 36.0, double temp = 93.0,
                                 bool withInfuse = false) {
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
        RecipeParams recipe;
        recipe.editorType = EditorType::DFlow;
        recipe.targetWeight = targetWeight;
        recipe.fillTemperature = temp;
        recipe.pourTemperature = temp;
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

        if (withInfuse) {
        QJsonObject frameSoak;
        frameSoak["name"] = "infuse";
        frameSoak["temperature"] = temp;
        frameSoak["sensor"] = "coffee";
        frameSoak["pump"] = "pressure";
        frameSoak["transition"] = "fast";
        frameSoak["pressure"] = 3.0;
        frameSoak["flow"] = 8.0;
        frameSoak["seconds"] = 20.0;
        frameSoak["volume"] = 100.0;
        frameSoak["weight"] = 4.0;
        frameSoak["exit"] = QJsonObject{{"type", "pressure"}, {"condition", "over"}, {"value", 3.0}};
        frameSoak["limiter"] = QJsonObject{{"value", 0.0}, {"range", 0.6}};
        steps.append(frameSoak);
        }

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
        // Simulate the setup upload completing so m_uploadInFlight is cleared.
        // MockTransport never ACKs writes, so tests that call uploadCurrentProfile()
        // after this helper would otherwise find the gate permanently blocked.
        emit f.device.profileUploaded(true, QString());
    }

private slots:
    void initTestCase() {
        // Redirect AppDataLocation, which ProfileManager::profilesPath() reads.
        // Two reasons: migrateRecipeFrames below writes real files, and without
        // this the whole suite has been reading and writing the developer's own
        // ~/Library/Application Support profiles directory.
        QStandardPaths::setTestModeEnabled(true);
    }

    void init() { QTest::failOnWarning(); }

    // The dye store is PID-scoped but shared across every test in this file, and
    // the active bag/recipe ids persist into it. The dose-ladder tests set them,
    // and restoring on the last line of each only works when the test reaches
    // that line — a failed QCOMPARE aborts the function and would leave an
    // active bag armed for every test that follows, turning one red into
    // several. Clear it here instead, where an abort cannot skip it.
    void cleanup() {
        QSettings raw(Settings::testQSettingsPath(), QSettings::IniFormat);
        raw.remove(QStringLiteral("dye/activeBagId"));
        raw.remove(QStringLiteral("dye/activeRecipeId"));
        raw.sync();
    }

    // === stripStoredRecipeBlocks: runs at startup, rewrites files on disk ===
    // Minimal valid D-Flow profile JSON, for the strip-pass cases below. Frame
    // values are deliberately NOT RecipeParams' defaults, so a pass that rebuilt
    // frames instead of just removing a key would be visible.
    // The QStandardPaths test-mode store persists across runs, and several tests in
    // this file leave deliberately-broken fixtures behind. A pass that walks every
    // profile at ProfileManager construction then reports them, which has nothing to
    // do with what these cases are asserting — so start from a known-empty store.
    static void clearTestProfileStore() {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                             + QStringLiteral("/profiles");
        for (const QString& sub : {QStringLiteral(""), QStringLiteral("/user"),
                                   QStringLiteral("/downloaded")}) {
            QDir d(base + sub);
            for (const QString& f : d.entryList({QStringLiteral("*.json")}, QDir::Files))
                QFile::remove(d.filePath(f));
        }
    }

    // Writes a 3-frame D-Flow profile to the store and loads it. D-Flow's editor
    // reads frames positionally and refuses to regenerate below 3, so a 2-frame
    // fixture cannot exercise the save guard at all.
    static void loadThreeFrameDFlow(McpTestFixture& f, const QString& fileName,
                                    const QString& title) {
        const QString path = f.profileManager.userProfilesPath() + "/" + fileName + ".json";
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(QJsonDocument(makeDFlowJson(title)).toJson());
        out.close();
        f.profileManager.refreshProfiles();
        f.profileManager.loadProfile(fileName);
    }

    static QJsonObject makeDFlowJson(const QString& title) {
        QJsonObject p;
        p["title"] = title;
        p["author"] = "test";
        p["beverage_type"] = "espresso";
        p["version"] = "2";
        p["legacy_profile_type"] = "settings_2c";
        p["target_weight"] = 36.0;
        p["target_volume"] = 0.0;
        p["target_volume_count_start"] = 2;
        p["tank_temperature"] = 0.0;
        p["espresso_temperature"] = 84.0;
        QJsonArray steps;
        for (const char* name : {"Filling", "Infusing", "Pouring"}) {
            QJsonObject fr;
            fr["name"] = name;
            fr["temperature"] = 84.0;
            fr["sensor"] = "coffee";
            fr["pump"] = "pressure";
            fr["transition"] = "fast";
            fr["pressure"] = 3.0;
            fr["flow"] = 8.0;
            fr["seconds"] = 25.0;
            fr["volume"] = 60.0;
            fr["exit"] = QJsonObject{{"type", "pressure"}, {"condition", "over"}, {"value", 3.0}};
            fr["limiter"] = QJsonObject{{"value", 0.0}, {"range", 0.6}};
            steps.append(fr);
        }
        p["steps"] = steps;
        return p;
    }


    void stripLeavesProfilesWithoutABlockByteIdentical() {
        // The pass runs ONCE, reads every profile on disk and rewrites the ones that
        // qualify, so a mistake here is permanent and silent. A profile with no
        // `recipe` key has nothing to strip and must not be touched at all — not
        // even re-serialized, which would quietly renormalise a user's file under
        // the banner of a migration.
        //
        // The legacy `is_recipe_mode` flag is deliberately included: the pass this
        // replaced once keyed off it and rebuilt frames from RecipeParams' defaults
        // for profiles that had no parameters to rebuild from (REC-1).
        clearTestProfileStore();
        McpTestFixture f;
        f.settings.setValue("recipe_blocks_stripped", false);

        const QString dir = f.profileManager.userProfilesPath();

        QJsonObject legacy;
        legacy["title"] = "D-Flow / Legacy Flag";
        legacy["author"] = "test";
        legacy["beverage_type"] = "espresso";
        legacy["version"] = "2";
        legacy["legacy_profile_type"] = "settings_2c";
        legacy["is_recipe_mode"] = true;      // the legacy flag...
        legacy["target_weight"] = 36.0;
        legacy["espresso_temperature"] = 84.0;
        QJsonArray steps;
        for (const char* name : {"Filling", "Infusing", "Pouring"}) {
            QJsonObject fr;
            fr["name"] = name;
            fr["temperature"] = 84.0;   // distinctive: NOT RecipeParams' 88.0 default
            fr["sensor"] = "coffee";
            fr["pump"] = "pressure";
            fr["transition"] = "fast";
            fr["pressure"] = 3.0;
            fr["flow"] = 8.0;
            fr["seconds"] = 25.0;
            fr["volume"] = 60.0;
            fr["exit"] = QJsonObject{{"type", "pressure"}, {"condition", "over"}, {"value", 3.0}};
            fr["limiter"] = QJsonObject{{"value", 0.0}, {"range", 0.6}};
            steps.append(fr);
        }
        legacy["steps"] = steps;
        // ...and no "recipe" block, which is the whole point.

        const QString path = dir + "/legacy_flag_only.json";
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        const QByteArray before = QJsonDocument(legacy).toJson();
        QCOMPARE(out.write(before), qint64(before.size()));
        out.close();

        f.profileManager.stripStoredRecipeBlocks();

        QFile back(path);
        QVERIFY(back.open(QIODevice::ReadOnly));
        const QByteArray after = back.readAll();
        back.close();
        QFile::remove(path);

        QVERIFY2(before == after,
                 "a profile with no recipe block was rewritten by the strip pass — it "
                 "has nothing to strip, so touching it can only lose something");
    }

    void stripRemovesTheBlockAndLeavesEverythingElseAlone() {
        clearTestProfileStore();
        McpTestFixture f;
        f.settings.setValue("recipe_blocks_stripped", false);
        const QString dir = f.profileManager.userProfilesPath();

        QJsonObject p = makeDFlowJson("D-Flow / Has Block");
        // A block whose values CONTRADICT the frames, which is the real-world case:
        // five shipped A-Flow built-ins carried exactly that.
        p["recipe"] = QJsonObject{
            {"editorType", "dflow"}, {"dose", 18.0},
            {"fillTemperature", 88.0}, {"pourTemperature", 93.0}, {"pourPressure", 9.0}};

        const QString path = dir + "/has_block.json";
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(QJsonDocument(p).toJson());
        out.close();

        f.profileManager.stripStoredRecipeBlocks();

        QFile back(path);
        QVERIFY(back.open(QIODevice::ReadOnly));
        const QJsonObject after = QJsonDocument::fromJson(back.readAll()).object();
        back.close();
        QFile::remove(path);

        QVERIFY2(!after.contains("recipe"), "the recipe block survived the strip pass");
        // Everything else intact — the frames especially, which the block disagreed with.
        QCOMPARE(after.value("title").toString(), QStringLiteral("D-Flow / Has Block"));
        QCOMPARE(after.value("steps").toArray().size(), p.value("steps").toArray().size());
        // dose was the struct default, so no recommendation is switched on.
        QVERIFY2(!after.value("has_recommended_dose").toBool(false),
                 "a default dose of 18 enabled a recommendation the user never set");
    }

    void stripPromotesAUserSetDose() {
        clearTestProfileStore();
        McpTestFixture f;
        f.settings.setValue("recipe_blocks_stripped", false);
        const QString dir = f.profileManager.userProfilesPath();

        QJsonObject p = makeDFlowJson("D-Flow / Real Dose");
        p["recipe"] = QJsonObject{{"editorType", "dflow"}, {"dose", 20.5}};

        const QString path = dir + "/real_dose.json";
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(QJsonDocument(p).toJson());
        out.close();

        f.profileManager.stripStoredRecipeBlocks();

        QFile back(path);
        QVERIFY(back.open(QIODevice::ReadOnly));
        const QJsonObject after = QJsonDocument::fromJson(back.readAll()).object();
        back.close();
        QFile::remove(path);

        QVERIFY(!after.contains("recipe"));
        QVERIFY2(after.value("has_recommended_dose").toBool(false),
                 "a dose the user set was dropped with the block instead of promoted");
        QCOMPARE(after.value("recommended_dose").toVariant().toDouble(), 20.5);
    }

    void stripRunsOnlyOnce() {
        clearTestProfileStore();
        McpTestFixture f;
        f.settings.setValue("recipe_blocks_stripped", false);
        f.profileManager.stripStoredRecipeBlocks();
        QVERIFY2(f.settings.value("recipe_blocks_stripped", false).toBool(),
                 "the pass did not record completion, so it would rewrite every "
                 "profile on every launch");

        // Second run must be a no-op: a profile placed afterwards is left alone.
        const QString dir = f.profileManager.userProfilesPath();
        QJsonObject p = makeDFlowJson("D-Flow / After");
        p["recipe"] = QJsonObject{{"editorType", "dflow"}, {"dose", 18.0}};
        const QString path = dir + "/after.json";
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        const QByteArray before = QJsonDocument(p).toJson();
        out.write(before);
        out.close();

        f.profileManager.stripStoredRecipeBlocks();

        QFile back(path);
        QVERIFY(back.open(QIODevice::ReadOnly));
        const QByteArray after = back.readAll();
        back.close();
        QFile::remove(path);
        QCOMPARE(before, after);   // the gate held; loadProfile handles late arrivals
    }

    // === strip-on-load: the block must not survive on disk ===

    void loadStripsAndPersistsAStoredBlock() {
        // The one-time upgrade covers what is already stored. This covers everything
        // that arrives afterwards — an import, a share code, a SAF sync, a restored
        // backup. Dropping the block in memory alone would leave it on disk forever
        // on a profile the user never re-saves.
        clearTestProfileStore();
        McpTestFixture f;
        const QString dir = f.profileManager.userProfilesPath();
        const QString path = dir + "/late_arrival.json";

        QJsonObject p = makeDFlowJson("D-Flow / Late Arrival");
        p["recipe"] = QJsonObject{{"editorType", "dflow"}, {"dose", 19.5}};
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(QJsonDocument(p).toJson());
        out.close();

        f.profileManager.refreshProfiles();
        f.profileManager.loadProfile("late_arrival");

        QFile back(path);
        QVERIFY(back.open(QIODevice::ReadOnly));
        const QByteArray afterFirst = back.readAll();
        back.close();
        const QJsonObject stripped = QJsonDocument::fromJson(afterFirst).object();

        QVERIFY2(!stripped.contains("recipe"),
                 "the block was only dropped in memory — it survives on disk");
        QVERIFY2(stripped.value("has_recommended_dose").toBool(false),
                 "the dose went with the block instead of being promoted");
        QCOMPARE(stripped.value("recommended_dose").toVariant().toDouble(), 19.5);

        // Second load must not rewrite: there is no longer a block to strip.
        f.profileManager.loadProfile("late_arrival");
        QFile again(path);
        QVERIFY(again.open(QIODevice::ReadOnly));
        const QByteArray afterSecond = again.readAll();
        again.close();
        QFile::remove(path);
        QCOMPARE(afterFirst, afterSecond);
    }

    void stripRefusesAProfileItCannotRewriteLosslessly() {
        // The guard that stops the pass mangling a profile stored in an encoding the
        // canonical serializer cannot reproduce. Every other fixture round-trips
        // cleanly, so without a deliberately lossy one this branch is never entered.
        clearTestProfileStore();
        McpTestFixture f;
        f.settings.setValue("recipe_blocks_stripped", false);

        QJsonObject p = makeDFlowJson("D-Flow / Lossy");
        p["recipe"] = QJsonObject{{"editorType", "dflow"}, {"dose", 18.0}};
        // A key the serializer models and re-emits with a DIFFERENT value: the parity
        // check reports a change, so the rewrite is refused.
        p["target_weight"] = 36.5555;

        const QString path = f.profileManager.userProfilesPath() + "/lossy.json";
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        const QByteArray before = QJsonDocument(p).toJson();
        out.write(before);
        out.close();

        f.profileManager.stripStoredRecipeBlocks();

        QFile back(path);
        QVERIFY(back.open(QIODevice::ReadOnly));
        const QByteArray after = back.readAll();
        back.close();
        QFile::remove(path);

        QCOMPARE(before, after);   // untouched, block and all
        QVERIFY2(QJsonDocument::fromJson(after).object().contains("recipe"),
                 "a profile that could not be rewritten losslessly was stripped anyway");
        // A refusal must NOT block completion — it can never succeed on a later run.
        QVERIFY2(f.settings.value("recipe_blocks_stripped", false).toBool(),
                 "a refusal blocked the completion flag, so the pass would repeat forever");
    }

    void stripCountsAnUnparseableProfileAsFailedAndRetries() {
        // Three outcomes used to collapse into one silent `false`, so a corrupt file
        // was invisible on every launch AND the flag was set anyway.
        clearTestProfileStore();
        McpTestFixture f;
        f.settings.setValue("recipe_blocks_stripped", false);

        const QString path = f.profileManager.userProfilesPath() + "/corrupt.json";
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write("{ \"title\": \"D-Flow / Truncated\", \"recipe\": {");   // half a document
        out.close();

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("cannot parse .*corrupt\\.json"));
        // ...and the run summary, which is a warning precisely because failed > 0 —
        // that is the outcome being asserted, so it must be expected, not silenced.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("Recipe block strip incomplete"));
        f.profileManager.stripStoredRecipeBlocks();
        QFile::remove(path);

        QVERIFY2(!f.settings.value("recipe_blocks_stripped", false).toBool(),
                 "an unparseable profile did not block the flag, so it is never revisited");
    }

    // === no-op-save guard ===

    void uneditedSaveLeavesDFlowFramesUntouched() {
        // A regeneration is NOT a no-op for D-Flow: it derives exit_pressure_over
        // from the soak pressure (soak < 2.8 ? soak : soak/2 + 0.6, floored 1.2), so a
        // profile whose stored value is off-formula is silently rewritten by an
        // open-and-close save.
        //
        // makeDFlowJson stores exit_pressure_over 3.0 against a soak pressure of 3.0,
        // whose formula value is 3.0/2 + 0.6 = 2.1. Keep that margin if you retune the
        // fixture — equal values would make a spurious regeneration invisible here.
        McpTestFixture f;
        clearTestProfileStore();
        loadThreeFrameDFlow(f, "guard_noop", "D-Flow / Guard");
        const QList<ProfileFrame> before = f.profileManager.currentProfile().steps();
        QCOMPARE(before.size(), 3);   // fewer and the editor refuses to regenerate at all
        QVERIFY(!before.isEmpty());

        // Save exactly what the editor was populated with — no edit.
        f.profileManager.uploadRecipeProfile(f.profileManager.getOrConvertRecipeParams());

        const QList<ProfileFrame> after = f.profileManager.currentProfile().steps();
        QCOMPARE(after.size(), before.size());
        for (qsizetype i = 0; i < before.size(); ++i) {
            QCOMPARE(after[i].temperature, before[i].temperature);
            QCOMPARE(after[i].pressure, before[i].pressure);
            QCOMPARE(after[i].seconds, before[i].seconds);
            QCOMPARE(after[i].exitPressureOver, before[i].exitPressureOver);
        }
    }

    void uneditedSaveLeavesAFlowFramesUntouched() {
        // A-Flow is the harder half of the derivesFromFrames split: prepAFlow overwrites
        // 12 fields across a 9-frame layout to D-Flow's 8 across 3. The guard rests on
        // extractRecipeParams(profile) equalling getOrConvertRecipeParams(), and that
        // equality was only ever demonstrated for the simpler generator.
        McpTestFixture f;
        clearTestProfileStore();
        const QString src = QStringLiteral(":/profiles/a_flow_default_medium.json");
        Profile aflow = Profile::loadFromFile(src);
        QVERIFY(!aflow.steps().isEmpty());
        const QString path = f.profileManager.userProfilesPath() + "/aflow_guard.json";
        QVERIFY(aflow.saveToFile(path));
        f.profileManager.refreshProfiles();
        f.profileManager.loadProfile("aflow_guard");
        QCOMPARE(f.profileManager.currentEditorType(), QStringLiteral("aflow"));

        const QList<ProfileFrame> before = f.profileManager.currentProfile().steps();
        f.profileManager.uploadRecipeProfile(f.profileManager.getOrConvertRecipeParams());
        const QList<ProfileFrame> after = f.profileManager.currentProfile().steps();

        QCOMPARE(after.size(), before.size());
        for (qsizetype i = 0; i < before.size(); ++i) {
            QCOMPARE(after[i].temperature, before[i].temperature);
            QCOMPARE(after[i].pressure, before[i].pressure);
            QCOMPARE(after[i].flow, before[i].flow);
            QCOMPARE(after[i].seconds, before[i].seconds);
        }
        QFile::remove(path);
    }

    void editedSaveDoesRegenerate() {
        // The converse: the guard must not be so eager that a real edit is skipped.
        McpTestFixture f;
        clearTestProfileStore();
        loadThreeFrameDFlow(f, "guard_edit", "D-Flow / Guard Edit");
        QCOMPARE(f.profileManager.currentProfile().steps().size(), 3);
        QVariantMap params = f.profileManager.getOrConvertRecipeParams();
        const double oldTemp = params.value("pourTemperature").toDouble();
        params["pourTemperature"] = oldTemp + 4.0;
        f.profileManager.uploadRecipeProfile(params);

        const QList<ProfileFrame>& steps = f.profileManager.currentProfile().steps();
        QVERIFY(!steps.isEmpty());
        QCOMPARE(steps.last().temperature, oldTemp + 4.0);
    }

    void advancedProfileTargetWeightEditStillApplies() {
        // Advanced profiles share uploadRecipeProfile's non-simple branch. Sourcing
        // their comparison baseline from the frames would make needFrameRegen
        // permanently true; regenerateFromRecipe() early-returns for advanced, and
        // the else-branch that applies target weight/volume would be skipped — so a
        // target edit would silently do nothing.
        McpTestFixture f;
        clearTestProfileStore();
        // Advanced = a title with no D-Flow/A-Flow prefix on a settings_2c profile.
        QJsonObject adv = makeDFlowJson("Advanced Guard");
        const QString advPath = f.profileManager.userProfilesPath() + "/advanced_guard.json";
        QFile advFile(advPath);
        QVERIFY(advFile.open(QIODevice::WriteOnly));
        advFile.write(QJsonDocument(adv).toJson());
        advFile.close();
        f.profileManager.refreshProfiles();
        f.profileManager.loadProfile("advanced_guard");
        QCOMPARE(f.profileManager.currentEditorType(), QStringLiteral("advanced"));

        QVariantMap params = f.profileManager.getOrConvertRecipeParams();
        params["targetWeight"] = 42.0;
        f.profileManager.uploadRecipeProfile(params);

        QCOMPARE(f.profileManager.currentProfile().targetWeight(), 42.0);
        QFile::remove(advPath);
    }

    // loadProfile REFUSES a profile it cannot read and keeps the previously
    // active one. It used to be `void`, so no caller could tell that apart from
    // a load that worked — and profiles_set_active reported "Profile activated"
    // while the machine went on brewing the old profile. Its `profileExists`
    // guard cannot see this: the file is present, it just does not parse.
    //
    // Both halves are asserted here. `false` alone would pass if the function
    // also failed to keep the old profile, and keeping the old profile alone was
    // already true before the return value existed.
    void loadProfileReportsRefusalAndKeepsTheActiveProfile() {
        McpTestFixture f;
        clearTestProfileStore();
        loadThreeFrameDFlow(f, "keeper", "D-Flow / Keeper");
        const QString activeBefore = f.profileManager.baseProfileName();
        QCOMPARE(activeBefore, QStringLiteral("keeper"));

        // Valid JSON, not a valid profile — no frames, no type, nothing to brew.
        //
        // Removed by a scope guard, not by a statement at the end of the test.
        // The QStandardPaths store PERSISTS ACROSS RUNS, and a failing QVERIFY
        // below aborts this function — so an end-of-test cleanup runs only when
        // the test passes, which is exactly when it is not needed. A file left
        // here makes migrateProfileFormat() warn at every later ProfileManager
        // construction (and never stamp profile_format_migrated, so it retries
        // forever); neither warning is in McpTestFixture's filter, so one red
        // test would turn the whole file red on the NEXT run, pointing nowhere
        // near here. This file already documents that hazard for the dye
        // settings at the `cleanup()` slot above.
        const QString brokenPath = f.profileManager.userProfilesPath() + "/broken.json";
        const auto removeBroken = qScopeGuard([&brokenPath] { QFile::remove(brokenPath); });

        QFile broken(brokenPath);
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write(QJsonDocument(QJsonObject{{"title", "Broken"}}).toJson());
        broken.close();
        f.profileManager.refreshProfiles();

        bool loaded = true;
        {
            ScopedWarningFilter refusalFilter("loadProfile: refusing");
            loaded = f.profileManager.loadProfile("broken");
        }

        QVERIFY2(!loaded, "an unreadable profile must be reported as not loaded");
        QCOMPARE(f.profileManager.baseProfileName(), activeBefore);

        // The OTHER false-returning path: a name that resolves to nothing at all.
        // loadProfile loads the default instead, so the requested profile did not
        // become active either — one line, and it pins the half of the contract
        // the refusal case cannot reach.
        {
            ScopedWarningFilter notFoundFilter("Profile not found");
            QVERIFY2(!f.profileManager.loadProfile("no_such_profile_anywhere"),
                     "a name that matches nothing must also report not loaded");
        }

        clearTestProfileStore();
    }

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

    // === Temperature override anchor (bug fix) ===

    // applyTemperatureToProfile (the "Update Profile" save path) must shift every
    // frame by the delta from espressoTemperature — the SAME anchor as the
    // live-brew override path — not from the first frame. Build a profile where
    // espressoTemperature (90) differs from steps[0] (88) so the two anchors give
    // different results, and assert the espressoTemperature anchor is used.
    void applyTemperatureUsesEspressoTemperatureAnchor() {
        McpTestFixture f;
        QJsonObject obj;
        obj["title"] = "Anchor Test";
        obj["legacy_profile_type"] = "settings_2c";
        obj["espresso_temperature"] = 90.0;  // in [88,93] range → not healed
        QJsonArray steps;
        for (double t : {88.0, 93.0}) {
            QJsonObject fr;
            fr["name"] = "f";
            fr["temperature"] = t;
            fr["pump"] = "flow";
            fr["flow"] = 2.0;
            fr["seconds"] = 10.0;
            steps.append(fr);
        }
        obj["steps"] = steps;
        f.profileManager.loadProfileFromJson(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        QCOMPARE(f.profileManager.currentProfile().espressoTemperature(), 90.0);
        QCOMPARE(f.profileManager.currentProfile().steps()[0].temperature, 88.0);

        // New temp 92 → delta = 92 - 90 = +2 (NOT 92 - 88 = +4).
        QSignalSpy changedSpy(&f.profileManager, &ProfileManager::currentProfileChanged);
        f.profileManager.applyTemperatureToProfile(92.0);
        QCOMPARE(f.profileManager.currentProfile().espressoTemperature(), 92.0);
        QCOMPARE(f.profileManager.currentProfile().steps()[0].temperature, 90.0);  // 88 + 2
        QCOMPARE(f.profileManager.currentProfile().steps()[1].temperature, 95.0);  // 93 + 2
        QVERIFY(changedSpy.count() >= 1);  // QML bindings depend on this signal
    }

    // currentProfileBeverageType() feeds the Shot Plan's beverage word (Espresso/
    // coffee/tea) and its cleaning-run warning — must reflect whatever beverage_type
    // the loaded profile carries, and normalize a missing one to "espresso" rather
    // than surfacing an empty string that would render no beverage word at all.
    void currentProfileBeverageTypeReflectsLoadedProfile() {
        McpTestFixture f;
        QJsonObject obj;
        obj["title"] = "Filter Test";
        obj["legacy_profile_type"] = "settings_2c";
        obj["beverage_type"] = "filter";
        QJsonArray steps;
        QJsonObject fr;
        fr["name"] = "f";
        fr["temperature"] = 92.0;
        fr["pump"] = "flow";
        fr["flow"] = 2.0;
        fr["seconds"] = 10.0;
        steps.append(fr);
        obj["steps"] = steps;
        f.profileManager.loadProfileFromJson(QJsonDocument(obj).toJson(QJsonDocument::Compact));

        QCOMPARE(f.profileManager.currentProfileBeverageType(), QStringLiteral("filter"));
        QVERIFY(!f.profileManager.currentProfileIsMaintenance());
    }

    void currentProfileBeverageTypeDefaultsToEspresso() {
        McpTestFixture f;
        QJsonObject obj;
        obj["title"] = "No Beverage Type Test";
        obj["legacy_profile_type"] = "settings_2c";
        // No "beverage_type" key at all.
        QJsonArray steps;
        QJsonObject fr;
        fr["name"] = "f";
        fr["temperature"] = 92.0;
        fr["pump"] = "flow";
        fr["flow"] = 2.0;
        fr["seconds"] = 10.0;
        steps.append(fr);
        obj["steps"] = steps;
        f.profileManager.loadProfileFromJson(QJsonDocument(obj).toJson(QJsonDocument::Compact));

        QCOMPARE(f.profileManager.currentProfileBeverageType(), QStringLiteral("espresso"));
        QVERIFY(!f.profileManager.currentProfileIsMaintenance());
    }

    void currentProfileBeverageTypeNormalizesCaseAndWhitespace() {
        // The trim+lowercase is the accessor's whole reason to exist: a community-
        // authored " Cleaning " must still match the lowercase comparisons (and trip
        // the maintenance tier's no-coffee warning), not fall through to "coffee".
        McpTestFixture f;
        QJsonObject obj;
        obj["title"] = "Odd-cased Cleaning Test";
        obj["legacy_profile_type"] = "settings_2c";
        obj["beverage_type"] = " Cleaning ";
        QJsonArray steps;
        QJsonObject fr;
        fr["name"] = "f";
        fr["temperature"] = 92.0;
        fr["pump"] = "flow";
        fr["flow"] = 2.0;
        fr["seconds"] = 10.0;
        steps.append(fr);
        obj["steps"] = steps;
        f.profileManager.loadProfileFromJson(QJsonDocument(obj).toJson(QJsonDocument::Compact));

        QCOMPARE(f.profileManager.currentProfileBeverageType(), QStringLiteral("cleaning"));
        QVERIFY(f.profileManager.currentProfileIsMaintenance());
    }

    void currentProfileIsMaintenanceCoversWholeTier() {
        // descale and calibrate belong to the same no-coffee tier as cleaning —
        // the grouping shared with maincontroller/visualizeruploader/mcptools_write.
        for (const char* bev : {"descale", "calibrate"}) {
            McpTestFixture f;
            QJsonObject obj;
            obj["title"] = "Maintenance Tier Test";
            obj["legacy_profile_type"] = "settings_2c";
            obj["beverage_type"] = bev;
            QJsonArray steps;
            QJsonObject fr;
            fr["name"] = "f";
            fr["temperature"] = 92.0;
            fr["pump"] = "flow";
            fr["flow"] = 2.0;
            fr["seconds"] = 10.0;
            steps.append(fr);
            obj["steps"] = steps;
            f.profileManager.loadProfileFromJson(QJsonDocument(obj).toJson(QJsonDocument::Compact));

            QVERIFY2(f.profileManager.currentProfileIsMaintenance(), bev);
        }
    }

    // "Update Profile" must clear any active temperature override before
    // re-uploading. Otherwise uploadCurrentProfile() re-applies the now-stale
    // override as a second delta, making the uploaded shot disagree with the saved
    // profile (the bug class the applyTemperatureToProfile change fixed).
    void applyTemperatureClearsActiveOverride() {
        McpTestFixture f;
        QJsonObject obj;
        obj["title"] = "Override Clear Test";
        obj["legacy_profile_type"] = "settings_2c";
        obj["espresso_temperature"] = 90.0;
        QJsonArray steps;
        for (double t : {88.0, 93.0}) {
            QJsonObject fr;
            fr["name"] = "f";
            fr["temperature"] = t;
            fr["pump"] = "flow";
            fr["flow"] = 2.0;
            fr["seconds"] = 10.0;
            steps.append(fr);
        }
        obj["steps"] = steps;
        f.profileManager.loadProfileFromJson(QJsonDocument(obj).toJson(QJsonDocument::Compact));

        f.settings.brew()->setTemperatureOverride(92.0);  // a standing override
        QVERIFY(f.settings.brew()->hasTemperatureOverride());

        // Bake +4 (94 from the 90 anchor). The override must be cleared so it isn't
        // double-applied; frames must reflect only the bake.
        f.profileManager.applyTemperatureToProfile(94.0);
        QVERIFY(!f.settings.brew()->hasTemperatureOverride());
        QCOMPARE(f.profileManager.currentProfile().espressoTemperature(), 94.0);
        QCOMPARE(f.profileManager.currentProfile().steps()[0].temperature, 92.0);  // 88 + 4
        QCOMPARE(f.profileManager.currentProfile().steps()[1].temperature, 97.0);  // 93 + 4
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
        // loadProfileFromJson already uploaded (same payload), so the next
        // setShotSettings would be deduped. Clear the cache so this test can
        // observe the SHOT_SETTINGS write.
        f.device.m_lastShotSettingsPayload.clear();

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
        f.device.m_lastShotSettingsPayload.clear();

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
        f.device.m_lastShotSettingsPayload.clear();

        f.profileManager.uploadCurrentProfile();

        auto settingsWrites = f.writesTo(SHOT_SETTINGS);
        QVERIFY(!settingsWrites.isEmpty());
        QByteArray data = settingsWrites.last();
        QVERIFY(data.size() >= 7);

        uint8_t targetEspressoVol = static_cast<uint8_t>(data[6]);
        QCOMPARE(targetEspressoVol, static_cast<uint8_t>(200));
    }

    // steam-heater-policy, the REPORTED FIELD BUG. uploadCurrentProfile() used
    // to hand-roll its own steam-target rule that knew only two of the five
    // inputs, so a profile upload re-sent steam = 0 for anyone with the
    // keep-warm setting off — silently undoing the heater state a recipe
    // activation had just applied, because the upload it triggered was deferred
    // behind m_uploadInFlight and landed AFTERWARDS. Byte 0-1 of the ShotSettings
    // payload is TargetSteamTemp (U8P0 in byte 1 for the DE1's layout; byte 0 is
    // the steam-flow/settings bitmask), so assert on what actually went out.
    void uploadCurrentProfileDoesNotClobberAPermittedSteamHeater() {
        McpTestFixture f;
        loadDFlowProfile(f);
        // The precondition that used to break: no STATE permission at all.
        f.settings.brew()->setKeepWarmWhenIdle(false);
        f.settings.brew()->setLetRecipeDecide(true);
        f.settings.brew()->setSteamTemperature(152.0);
        f.settings.brew()->setSteamDisabled(false);
        // ...and a live steam event, which is what a milk recipe's shot start
        // grants. The upload must respect it rather than re-deriving from the
        // settings and finding nothing.
        f.steamHeaterPolicy.setEventPermission(true);

        f.transport.clearWrites();
        f.device.m_lastShotSettingsPayload.clear();
        f.profileManager.uploadCurrentProfile();

        auto settingsWrites = f.writesTo(SHOT_SETTINGS);
        QVERIFY(!settingsWrites.isEmpty());
        const QByteArray data = settingsWrites.last();
        QVERIFY(data.size() >= 2);
        QCOMPARE(static_cast<uint8_t>(data[1]), static_cast<uint8_t>(152));
    }

    // The other direction, so the test above cannot pass by simply always
    // sending the configured temperature: with nothing permitting the heater the
    // upload must still command 0.
    void uploadCurrentProfileSendsZeroWhenNothingPermitsTheHeater() {
        McpTestFixture f;
        loadDFlowProfile(f);
        f.settings.brew()->setKeepWarmWhenIdle(false);
        f.settings.brew()->setLetRecipeDecide(true);
        f.settings.brew()->setSteamTemperature(152.0);
        f.settings.brew()->setSteamDisabled(false);
        f.steamHeaterPolicy.setEventPermission(false);

        f.transport.clearWrites();
        f.device.m_lastShotSettingsPayload.clear();
        f.profileManager.uploadCurrentProfile();

        auto settingsWrites = f.writesTo(SHOT_SETTINGS);
        QVERIFY(!settingsWrites.isEmpty());
        const QByteArray data = settingsWrites.last();
        QVERIFY(data.size() >= 2);
        QCOMPARE(static_cast<uint8_t>(data[1]), static_cast<uint8_t>(0));
    }

    void uploadCurrentProfileRespectsWaterVolumeMode() {
        // Regression: profile upload must match MainController::sendMachineSettings
        // on hot water volume, otherwise two back-to-back writes with different
        // `vol` values race at the BLE layer and falsely trip the drift detector.
        // Byte 4 of the ShotSettings payload is TargetHotWaterVol (U8P0 ml).

        // --- Weight mode: vol byte must be 0 ---
        {
            McpTestFixture f;
            loadDFlowProfile(f);
            f.settings.brew()->setWaterVolumeMode("weight");
            f.settings.brew()->setWaterVolume(65);
            f.transport.clearWrites();
            f.device.m_lastShotSettingsPayload.clear();

            f.profileManager.uploadCurrentProfile();

            auto settingsWrites = f.writesTo(SHOT_SETTINGS);
            QVERIFY(!settingsWrites.isEmpty());
            QByteArray data = settingsWrites.last();
            QVERIFY(data.size() >= 5);
            QCOMPARE(static_cast<uint8_t>(data[4]), static_cast<uint8_t>(0));
        }

        // --- Volume mode: vol byte must echo settings.brew()->waterVolume() ---
        {
            McpTestFixture f;
            loadDFlowProfile(f);
            f.settings.brew()->setWaterVolumeMode("volume");
            f.settings.brew()->setWaterVolume(65);
            f.transport.clearWrites();
            f.device.m_lastShotSettingsPayload.clear();

            f.profileManager.uploadCurrentProfile();

            auto settingsWrites = f.writesTo(SHOT_SETTINGS);
            QVERIFY(!settingsWrites.isEmpty());
            QByteArray data = settingsWrites.last();
            QVERIFY(data.size() >= 5);
            QCOMPARE(static_cast<uint8_t>(data[4]), static_cast<uint8_t>(65));
        }
    }

    void uploadBlockedDuringActivePhase() {
        McpTestFixture f;
        loadDFlowProfile(f);

        // Simulate active phase (direct member access via friend class)
        f.machineState.m_phase = MachineState::Phase::Pouring;
        f.transport.clearWrites();

        QSignalSpy spy(&f.profileManager, &ProfileManager::profileUploadBlocked);
        ScopedWarningFilter filter("BLOCKED during active phase|^  #");
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
        // Three frames: uploadRecipeProfile now refuses to regenerate a profile
        // whose frames its editor cannot read positionally, and a two-frame
        // "D-Flow" profile is not one.
        loadDFlowProfile(f, "D-Flow / Test", 36.0, 93.0, /*withInfuse=*/true);

        QVariantMap recipe;
        recipe["editorType"] = "dflow";
        recipe["targetWeight"] = 40.0;
        recipe["fillTemperature"] = 95.0;
        recipe["pourTemperature"] = 95.0;
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
        f.settings.brew()->setTemperatureOverride(95.0);
        f.transport.clearWrites();
        f.device.m_lastShotSettingsPayload.clear();
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

    // === Knowledge indicator follows the CANDIDATE SET, not the identity ===

    void ambiguousShapeMatchStillOffersItsKnowledge() {
        // A profile whose frame structure matches several documented profiles
        // has no single identity — but its badges and summary are still shaped
        // by what those entries agree on (union suppression, unanimous facts).
        // So the indicator must light and the dialog must show all of them.
        //
        // The failure this prevents is silent and one-directional: findings
        // suppressed by KB knowledge, with nothing on screen saying knowledge
        // was involved. That is worse than a dark sparkle over nothing, which
        // at least matches what the user is told.
        McpTestFixture f;

        // D-Flow / default shares its shape with D-Flow / La Pavoni, so a
        // renamed copy resolves to both. Pinned in tst_shotsummarizer as
        // resolveProfileKb_ambiguousShapeWithholdsIdentityButNotAnalysis.
        QFile src(QStringLiteral(":/profiles/d_flow_default.json"));
        QVERIFY(src.open(QIODevice::ReadOnly));
        QJsonObject obj = QJsonDocument::fromJson(src.readAll()).object();
        const QString renamed = QStringLiteral("Zzz Renamed Copy");
        obj[QStringLiteral("title")] = renamed;
        obj.remove(QStringLiteral("read_only"));

        // userProfilesPath(), not profilesPath() — the catalog scan reads the
        // user/ and downloaded/ subdirectories, so a file dropped in the base
        // directory is never seen and the resolution comes back empty.
        const QString dir = f.profileManager.userProfilesPath();
        QVERIFY(QDir().mkpath(dir));
        QFile out(dir + QStringLiteral("/zzz_renamed_copy.json"));
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(QJsonDocument(obj).toJson());
        out.close();
        f.profileManager.refreshProfiles();

        const QStringList names = f.profileManager.profileKbCandidateNames(renamed);
        QVERIFY2(names.size() > 1,
                 qPrintable(QStringLiteral("expected an ambiguous match, got: %1")
                                .arg(names.join(QLatin1Char(',')))));

        QVERIFY2(f.profileManager.profileHasKnowledge(renamed),
                 "an ambiguous match must still light the indicator");

        // Every named entry's body is present — not one member standing in for
        // the set.
        const QString content = f.profileManager.profileKnowledgeContent(renamed);
        QVERIFY(!content.isEmpty());
        for (const QString& n : names)
            QVERIFY2(content.contains(n),
                     qPrintable(QStringLiteral("dialog body omits candidate %1").arg(n)));

        // ...but no identity is claimed: nothing to put after "Based on".
        QVERIFY2(f.profileManager.profileKbDerivedFrom(renamed).isEmpty(),
                 "an ambiguous match must not name a single source");
    }

    // === QML guard: no visibility gated on a shot's persisted profileKbId ===

    void noQmlVisibilityGatedOnPersistedProfileKbId() {
        // `shotData.profileKbId` is the column written at save time. It is the
        // wrong input for any "do we know something about this profile" gate,
        // and was wrong at two sites before resolve-profile-kb-by-shape:
        //
        //  - the QualityBadges row, where it hid the Shot Summary chip on a
        //    clean shot, i.e. the affordance opening an analysis computed
        //    entirely from the shot's own curves;
        //  - the KB sparkle, which then disagreed with the dialog it opens,
        //    since profileKnowledgeContent() resolves through the catalog.
        //
        // The column is empty for every row saved before that change and for
        // any profile whose shape matched several KB entries (a candidate set
        // establishes no identity, so nothing is persisted), and it says
        // nothing about whether the profile is still in the catalog. Ask
        // ProfileManager instead — profileHasKnowledge() / the catalog's
        // hasKnowledgeBase — so the indicator and its dialog cannot disagree.
        //
        // Reading the field for other purposes is fine; only a visibility
        // binding is flagged.
        QDir qmlDir(QCoreApplication::applicationDirPath() + "/../../../../qml");
        if (!qmlDir.exists())
            qmlDir.setPath(QString(SRCDIR) + "/../qml");
        if (!qmlDir.exists())
            QSKIP("QML directory not found — run from source tree");

        static const QRegularExpression visibleRe(
            QStringLiteral("^\\s*visible\\s*:"));
        static const QRegularExpression kbIdRe(QStringLiteral("\\bprofileKbId\\b"));

        QStringList violations;
        QDirIterator it(qmlDir.absolutePath(), {"*.qml"}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString filePath = it.next();
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            QStringList lines;
            while (!file.atEnd())
                lines.append(QString::fromUtf8(file.readLine()));
            const QString relPath = qmlDir.relativeFilePath(filePath);

            for (qsizetype i = 0; i < lines.size(); ++i) {
                if (!visibleRe.match(lines[i]).hasMatch())
                    continue;
                // A visible binding may wrap; scan it to its last line. Cheap
                // approximation of "the binding": keep going while the line
                // does not close its parenthesis balance.
                int depth = 0;
                for (qsizetype j = i; j < lines.size(); ++j) {
                    const QString& l = lines[j];
                    // A commented-out line is not a binding. Crude but
                    // sufficient: the flagged shape never carries a trailing
                    // comment on the same line.
                    if (!l.contains(QStringLiteral("//"))
                        && kbIdRe.match(l).hasMatch()) {
                        violations << QStringLiteral("%1:%2: visible gated on profileKbId")
                                          .arg(relPath).arg(j + 1);
                    }
                    for (const QChar c : l) {
                        if (c == u'(') ++depth;
                        else if (c == u')') --depth;
                    }
                    if (depth <= 0) break;
                }
            }
        }

        if (!violations.isEmpty()) {
            QFAIL(qPrintable(QStringLiteral("Visibility gated on the persisted "
                                            "profileKbId in %1 place(s):\n  %2")
                                 .arg(violations.size())
                                 .arg(violations.join(QStringLiteral("\n  ")))));
        }
    }

    // === MCP resource: decenza://profiles/active ===

    void mcpResourceActiveProfileReturnsFilenameAndTitle() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Espresso");

        McpResourceRegistry resources;
        registerMcpResources(&resources, &f.device, &f.machineState,
                             &f.profileManager, nullptr, nullptr, nullptr);

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
                             &f.profileManager, nullptr, nullptr, nullptr);

        QString error;
        QJsonObject result = resources.readResource("decenza://profiles/active", error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result["targetWeightG"].toDouble(), 40.0);
        QCOMPARE(result["targetTemperatureC"].toDouble(), 91.5);
    }

    // === MCP tools: debug_get_fds / debug_get_log ===

    void debugGetFds_returnsExplicitDescriptorCensus() {
        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        const QJsonObject result = f.callTool("debug_get_fds", QJsonObject{});
        QVERIFY(result.contains("supported"));
        if (!result.value("supported").toBool()) {
            QVERIFY(!result.value("error").toString().isEmpty());
            return;
        }

        QVERIFY(result.value("openFdCount").toInt() > 0);
        QVERIFY(result.value("descriptorKinds").isObject());
        QVERIFY(result.value("descriptors").isArray());
        const QJsonArray descriptors = result.value("descriptors").toArray();
        QVERIFY(!descriptors.isEmpty());
        const QJsonObject descriptor = descriptors.first().toObject();
        QVERIFY(descriptor.contains("fd"));
        QVERIFY(descriptor.contains("kind"));
        QVERIFY(descriptor.contains("target"));
    }

    static void writeLogFile(const QString& path, const QString& content) {
        QFile f(path);
        QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text), "failed to write test log file");
        QTextStream(&f) << content;
    }

    void debugGetLog_noNewParamsReproducesPriorShape() {
        QTemporaryDir dir;
        writeLogFile(dir.filePath("debug.log"),
            "[   0.100] INFO  line one\n"
            "[   0.200] WARN  line two\n"
            "[   0.300] ERROR line three\n");
        WebDebugLoggerTestGuard guard(dir.filePath("debug.log"));

        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        QJsonObject result = f.callTool("debug_get_log", QJsonObject{{"offset", 0}, {"limit", 500}});
        QCOMPARE(result["totalLines"].toInt(), 3);
        QCOMPARE(result["returnedLines"].toInt(), 3);
        QVERIFY(!result["hasMore"].toBool());
        QVERIFY(result["log"].toString().contains("line one"));
        QVERIFY(result["log"].toString().contains("line three"));
        // Additive-only contract: no qualifyingLines/lines when nothing narrowed the range.
        QVERIFY(!result.contains("qualifyingLines"));
    }

    void debugGetLog_substringFilterIsCaseInsensitive() {
        QTemporaryDir dir;
        writeLogFile(dir.filePath("debug.log"),
            "[   0.100] INFO  connecting to R2\n"
            "[   0.200] INFO  scale ready\n"
            "[   0.300] WARN  r2 error 0/2\n");
        WebDebugLoggerTestGuard guard(dir.filePath("debug.log"));

        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        QJsonObject result = f.callTool("debug_get_log", QJsonObject{{"filter", "R2"}});
        QCOMPARE(result["qualifyingLines"].toInt(), 2);
        QJsonArray lines = result["lines"].toArray();
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines[0].toObject()["line"].toInt(), 0);
        QCOMPARE(lines[1].toObject()["line"].toInt(), 2);
    }

    void debugGetLog_regexFilter() {
        QTemporaryDir dir;
        writeLogFile(dir.filePath("debug.log"),
            "[   0.100] INFO  SAW trigger at 34g\n"
            "[   0.200] INFO  nothing here\n"
            "[   0.300] INFO  SAW trigger at 36g\n");
        WebDebugLoggerTestGuard guard(dir.filePath("debug.log"));

        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        QJsonObject result = f.callTool("debug_get_log", QJsonObject{{"filter", "SAW.*trigger"}, {"regex", true}});
        QCOMPARE(result["qualifyingLines"].toInt(), 2);
    }

    void debugGetLog_minLevelAlone() {
        QTemporaryDir dir;
        writeLogFile(dir.filePath("debug.log"),
            "[   0.100] DEBUG chatter\n"
            "[   0.200] WARN  low water\n"
            "[   0.300] ERROR BLE write failed\n");
        WebDebugLoggerTestGuard guard(dir.filePath("debug.log"));

        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        QJsonObject result = f.callTool("debug_get_log", QJsonObject{{"minLevel", "WARN"}});
        QCOMPARE(result["qualifyingLines"].toInt(), 2);
        QVERIFY(!result["log"].toString().contains("chatter"));
    }

    void debugGetLog_minLevelCombinedWithFilter() {
        QTemporaryDir dir;
        writeLogFile(dir.filePath("debug.log"),
            "[   0.100] ERROR unrelated failure\n"
            "[   0.200] DEBUG BLE chatter\n"
            "[   0.300] ERROR BLE write failed\n");
        WebDebugLoggerTestGuard guard(dir.filePath("debug.log"));

        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        QJsonObject result = f.callTool("debug_get_log", QJsonObject{{"filter", "BLE"}, {"minLevel", "ERROR"}});
        QCOMPARE(result["qualifyingLines"].toInt(), 1);
        QVERIFY(result["log"].toString().contains("BLE write failed"));
    }

    void debugGetLog_tailReturnsLastNAndOverridesOffset() {
        QTemporaryDir dir;
        QString content;
        for (int i = 0; i < 10; ++i)
            content += QString("[  %1.000] INFO  line %2\n").arg(i, 2, 10, QChar('0')).arg(i);
        writeLogFile(dir.filePath("debug.log"), content);
        WebDebugLoggerTestGuard guard(dir.filePath("debug.log"));

        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        // offset supplied alongside tail — tail must win.
        QJsonObject result = f.callTool("debug_get_log", QJsonObject{{"offset", 2}, {"tail", 3}});
        QJsonArray lines = result["lines"].toArray();
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines[0].toObject()["line"].toInt(), 7);
        QCOMPARE(lines[2].toObject()["line"].toInt(), 9);
        QVERIFY(!result["hasMore"].toBool());
    }

    void debugGetLog_sessionScopedFilterUsesAbsoluteLineNumbers() {
        QTemporaryDir dir;
        writeLogFile(dir.filePath("debug.log"),
            "========== SESSION START: 2026-01-01T09:00:00 ==========\n"
            "[   0.100] INFO  first session line\n"
            "[   0.200] WARN  first session warning\n"
            "========== SESSION START: 2026-01-01T10:00:00 ==========\n"
            "[   0.100] INFO  second session line\n"
            "[   0.200] WARN  second session warning\n");
        WebDebugLoggerTestGuard guard(dir.filePath("debug.log"));

        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        // Most recent session only, filtered to WARN — absolute line number
        // must reflect position in the WHOLE file, not the session-relative offset.
        QJsonObject result = f.callTool("debug_get_log", QJsonObject{{"session", -1}, {"minLevel", "WARN"}});
        QJsonArray lines = result["lines"].toArray();
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines[0].toObject()["line"].toInt(), 5);
        QVERIFY(lines[0].toObject()["text"].toString().contains("second session warning"));
    }

    void debugGetLog_dedupeCollapsesRepeatedBurst() {
        QTemporaryDir dir;
        writeLogFile(dir.filePath("debug.log"),
            "[ 101.178] WARN  _derived.text undefined at read\n"
            "[ 101.179] WARN  _derived.text undefined at read\n"
            "[ 101.180] WARN  _derived.text undefined at read\n"
            "[ 101.181] INFO  unrelated line\n");
        WebDebugLoggerTestGuard guard(dir.filePath("debug.log"));

        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        QJsonObject result = f.callTool("debug_get_log", QJsonObject{{"minLevel", "WARN"}, {"dedupe", true}});
        QJsonArray lines = result["lines"].toArray();
        QCOMPARE(lines.size(), 1);
        QJsonObject entry = lines[0].toObject();
        QCOMPARE(entry["line"].toInt(), 0);
        QCOMPARE(entry["count"].toInt(), 3);
        QCOMPARE(entry["lastLine"].toInt(), 2);
        QVERIFY(result["log"].toString().contains("(x3)"));
    }

    void debugGetLog_dedupeCombinesWithTail() {
        // tail:2 (not 1) deliberately discriminates pipeline order: dedupe-then-tail
        // keeps "retrying" collapsed to count:3 as one of the last 2 GROUPED entries;
        // tail-then-dedupe would instead slice the last 2 RAW lines first (one lone
        // "retrying" occurrence + "final failure"), which can no longer collapse to
        // count:3 since the other two "retrying" raw lines were already cut off.
        QTemporaryDir dir;
        QString content;
        for (int i = 0; i < 3; ++i) content += "[   0.100] WARN  retrying\n";
        content += "[   0.200] WARN  final failure\n";
        writeLogFile(dir.filePath("debug.log"), content);
        WebDebugLoggerTestGuard guard(dir.filePath("debug.log"));

        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        QJsonObject result = f.callTool("debug_get_log", QJsonObject{{"dedupe", true}, {"tail", 2}});
        QJsonArray lines = result["lines"].toArray();
        QCOMPARE(lines.size(), 2);
        QVERIFY(lines[0].toObject()["text"].toString().contains("retrying"));
        QCOMPARE(lines[0].toObject()["count"].toInt(), 3);
        QVERIFY(lines[1].toObject()["text"].toString().contains("final failure"));
        QCOMPARE(lines[1].toObject()["count"].toInt(), 1);
    }

    void debugGetLog_sessionScopedDedupe() {
        QTemporaryDir dir;
        writeLogFile(dir.filePath("debug.log"),
            "========== SESSION START: 2026-01-01T09:00:00 ==========\n"
            "[   0.100] WARN  unrelated first session line\n"
            "========== SESSION START: 2026-01-01T10:00:00 ==========\n"
            "[   0.100] WARN  retrying\n"
            "[   0.200] WARN  retrying\n"
            "[   0.300] WARN  retrying\n");
        WebDebugLoggerTestGuard guard(dir.filePath("debug.log"));

        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        // Most recent session only, filtered to the retrying lines (excluding the
        // session-start marker itself) and deduped — absolute line numbers must
        // still reflect position in the WHOLE file (session starts at line 2),
        // not a session-relative offset.
        QJsonObject result = f.callTool("debug_get_log",
            QJsonObject{{"session", -1}, {"filter", "retrying"}, {"dedupe", true}});
        QJsonArray lines = result["lines"].toArray();
        QCOMPARE(lines.size(), 1);
        QJsonObject entry = lines[0].toObject();
        QCOMPARE(entry["line"].toInt(), 3);
        QCOMPARE(entry["count"].toInt(), 3);
        QCOMPARE(entry["lastLine"].toInt(), 5);
    }

    void debugGetLog_dedupeLeavesGenuinelyDifferentLinesAloneEndToEnd() {
        // Different shot ids in an otherwise-identical grab-log template must NOT
        // collapse — the exact case design.md Decision 6 calls out by name.
        QTemporaryDir dir;
        writeLogFile(dir.filePath("debug.log"),
            "[   0.100] INFO  [Background] Shot-chart grab -> source shot 1120 samples 293\n"
            "[   0.200] INFO  [Background] Shot-chart grab -> source shot 1121 samples 292\n");
        WebDebugLoggerTestGuard guard(dir.filePath("debug.log"));

        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        QJsonObject result = f.callTool("debug_get_log",
            QJsonObject{{"filter", "Shot-chart grab"}, {"dedupe", true}});
        QJsonArray lines = result["lines"].toArray();
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines[0].toObject()["count"].toInt(), 1);
        QCOMPARE(lines[1].toObject()["count"].toInt(), 1);
    }

    void debugGetLog_explicitTailZeroDoesNotForceHasMoreFalse() {
        QTemporaryDir dir;
        QString content;
        for (int i = 0; i < 5; ++i) content += QString("[  %1.000] WARN  line %2\n").arg(i).arg(i);
        writeLogFile(dir.filePath("debug.log"), content);
        WebDebugLoggerTestGuard guard(dir.filePath("debug.log"));

        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        // tail:0 must mean "no tail", not "force hasMore false" — there are 5
        // qualifying lines and only 2 fit under this limit, so more remain.
        QJsonObject result = f.callTool("debug_get_log", QJsonObject{{"minLevel", "WARN"}, {"tail", 0}, {"limit", 2}});
        QCOMPARE(result["returnedLines"].toInt(), 2);
        QVERIFY(result["hasMore"].toBool());
    }

    void debugGetLog_invalidMinLevelIsRejected() {
        QTemporaryDir dir;
        writeLogFile(dir.filePath("debug.log"), "[   0.100] WARN  anything\n");
        WebDebugLoggerTestGuard guard(dir.filePath("debug.log"));

        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        QJsonObject result = f.callTool("debug_get_log", QJsonObject{{"minLevel", "WARNING"}});
        QVERIFY(result.contains("error"));
        QVERIFY(!result.contains("lines"));
    }

    void debugGetLog_noDedupeReproducesPriorShape() {
        QTemporaryDir dir;
        writeLogFile(dir.filePath("debug.log"),
            "[   0.100] WARN  repeat\n"
            "[   0.200] WARN  repeat\n");
        WebDebugLoggerTestGuard guard(dir.filePath("debug.log"));

        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        QJsonObject result = f.callTool("debug_get_log", QJsonObject{{"minLevel", "WARN"}});
        QJsonArray lines = result["lines"].toArray();
        QCOMPARE(lines.size(), 2);
        for (const auto& l : lines) {
            QVERIFY(!l.toObject().contains("count"));
            QVERIFY(!l.toObject().contains("lastLine"));
        }
    }

    // === families=true: the prefix census ===

    // Every line is classified into exactly one of the four grammars, and the
    // registered/unregistered split is real.
    //
    // The census is the answer to "which subsystems does this log even contain",
    // so a wrong split is worse than no census: an unregistered family reported
    // as registered tells a reader that one `filter=` search returns that
    // subsystem's WHOLE story, when in fact it returns whatever share happens to
    // carry that one spelling.
    void debugGetLog_familiesCensusSplitsRegisteredFromEverythingElse() {
        QTemporaryDir dir;
        writeLogFile(dir.filePath("debug.log"),
            "[   0.100] INFO  [Scale][BLEManager] connecting\n"
            "[   0.200] DEBUG [Scale][USB Scale] polling started\n"
            "[   0.300] DEBUG [R2-diag] scanForDevices\n"
            "[   0.400] WARN  MqttClient: Connection failed\n"
            "[   0.500] DEBUG Simulation mode: ON\n");
        WebDebugLoggerTestGuard guard(dir.filePath("debug.log"));

        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        const QJsonObject result = f.callTool("debug_get_log", QJsonObject{{"families", true}});
        QCOMPARE(result["linesScanned"].toInt(), 5);

        const QJsonArray registered = result["registeredMarkers"].toArray();
        QCOMPARE(registered.size(), 1);
        QCOMPARE(registered[0].toObject()["prefix"].toString(), QStringLiteral("Scale"));
        QCOMPARE(registered[0].toObject()["lines"].toInt(), 2);
        // The searchWith string is the whole point of the row — it must be
        // pasteable into this same tool's `filter`.
        QCOMPARE(registered[0].toObject()["searchWith"].toString(),
                 QStringLiteral("filter=\"[Scale]\""));

        const QJsonArray unregistered = result["unregisteredBracketPrefixes"].toArray();
        QCOMPARE(unregistered.size(), 1);
        QCOMPARE(unregistered[0].toObject()["prefix"].toString(), QStringLiteral("R2-diag"));

        const QJsonArray classes = result["classPrefixes"].toArray();
        QCOMPARE(classes.size(), 1);
        QCOMPARE(classes[0].toObject()["prefix"].toString(), QStringLiteral("MqttClient"));
        QCOMPARE(classes[0].toObject()["searchWith"].toString(),
                 QStringLiteral("filter=\"MqttClient:\""));

        QCOMPARE(result["linesWithNoPrefix"].toInt(), 1);
        // A readable, non-empty log says nothing about being empty.
        QVERIFY(!result.contains("emptyBecause"));
    }

    // Rows are ordered by volume, because the reason to read a census is to find
    // where the log's mass is. Alphabetical order buries the family that
    // accounts for a third of the file behind one that fired twice.
    void debugGetLog_familiesCensusOrdersByLineCount() {
        QTemporaryDir dir;
        writeLogFile(dir.filePath("debug.log"),
            "[   0.100] DEBUG [Aardvark] rare\n"
            "[   0.200] DEBUG [Zebra] common\n"
            "[   0.300] DEBUG [Zebra] common\n"
            "[   0.400] DEBUG [Zebra] common\n");
        WebDebugLoggerTestGuard guard(dir.filePath("debug.log"));

        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        const QJsonObject result = f.callTool("debug_get_log", QJsonObject{{"families", true}});
        const QJsonArray rows = result["unregisteredBracketPrefixes"].toArray();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0].toObject()["prefix"].toString(), QStringLiteral("Zebra"));
        QCOMPARE(rows[1].toObject()["prefix"].toString(), QStringLiteral("Aardvark"));
    }

    // An empty census must say WHY it is empty.
    //
    // Zeros across the board read as "this log is quiet", and a reader acts on
    // that — when the real state may be that nothing was read at all. That is the
    // same absence-looks-like-evidence mistake the census exists to prevent, so
    // the census committing it would be the worst place for it.
    void debugGetLog_familiesCensusDistinguishesEmptyFromUnreadable() {
        QTemporaryDir dir;
        const QString missing = dir.filePath("never-written.log");
        WebDebugLoggerTestGuard guard(missing);

        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        // getPersistedLogChunk() warns when it cannot open the file, which is
        // correct and is half the point — the census field below is the same
        // fact delivered to the MCP caller, who never sees the app's own log.
        // Permitted rather than asserted: what this test is about is the field.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("failed to open persisted log for reading"));

        const QJsonObject result = f.callTool("debug_get_log", QJsonObject{{"families", true}});
        QCOMPARE(result["linesScanned"].toInt(), 0);
        const QString why = result["emptyBecause"].toString();
        QVERIFY2(!why.isEmpty(), "an empty census must name its cause");
        QVERIFY(why.contains(QStringLiteral("no log file exists")));
        QVERIFY2(why.contains(missing), "the cause must name the path that was checked");
    }

    // The census can be scoped to ONE session, and its searchWith strings say so.
    //
    // Unscoped it sums every app version that ever wrote to the ring buffer, which
    // is honest about the file and cannot answer the question that actually gets
    // asked after a change — "did that subsystem get quieter". The first real use
    // of this tool on a device wanted exactly that comparison and could not make it.
    void debugGetLog_familiesCensusCanBeScopedToOneSession() {
        QTemporaryDir dir;
        writeLogFile(dir.filePath("debug.log"),
            "\n========== SESSION START: 2026-01-01T09:00:00 ==========\n"
            "[   0.100] DEBUG [Loud] a\n"
            "[   0.200] DEBUG [Loud] b\n"
            "[   0.300] DEBUG [Loud] c\n"
            "\n========== SESSION START: 2026-01-02T09:00:00 ==========\n"
            "[   0.100] DEBUG [Loud] a\n"
            "[   0.200] DEBUG [Quiet] x\n");
        WebDebugLoggerTestGuard guard(dir.filePath("debug.log"));

        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        // Whole file: both sessions summed.
        const QJsonObject whole =
            f.callTool("debug_get_log", QJsonObject{{"families", true}});
        QVERIFY(!whole.contains("session"));
        const QJsonArray wholeRows = whole["unregisteredBracketPrefixes"].toArray();
        QCOMPARE(wholeRows[0].toObject()["prefix"].toString(), QStringLiteral("Loud"));
        QCOMPARE(wholeRows[0].toObject()["lines"].toInt(), 4);
        QCOMPARE(wholeRows[0].toObject()["searchWith"].toString(),
                 QStringLiteral("filter=\"[Loud]\""));

        // Most recent session only — the count drops, which is the whole point.
        const QJsonObject scoped =
            f.callTool("debug_get_log", QJsonObject{{"families", true}, {"session", -1}});
        QCOMPARE(scoped["session"].toInt(), 1);
        const QJsonArray scopedRows = scoped["unregisteredBracketPrefixes"].toArray();
        QCOMPARE(scopedRows.size(), 2);
        QCOMPARE(scopedRows[0].toObject()["lines"].toInt(), 1);
        // Pasteable: a filter that addressed the whole log would hand back counts
        // from one session and lines from both.
        QCOMPARE(scopedRows[0].toObject()["searchWith"].toString(),
                 QStringLiteral("filter=\"[Loud]\", session=1"));
    }

    // A session index outside the range is an error, not an empty census.
    void debugGetLog_familiesCensusRejectsABadSessionIndex() {
        QTemporaryDir dir;
        writeLogFile(dir.filePath("debug.log"),
            "\n========== SESSION START: 2026-01-01T09:00:00 ==========\n"
            "[   0.100] DEBUG [Loud] a\n");
        WebDebugLoggerTestGuard guard(dir.filePath("debug.log"));

        McpTestFixture f;
        registerDebugTools(&f.registry, nullptr);

        const QJsonObject r =
            f.callTool("debug_get_log", QJsonObject{{"families", true}, {"session", 7}});
        QVERIFY(r.contains("error"));
        QCOMPARE(r["sessionCount"].toInt(), 1);
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
        // Three-frame: this smoke test calls getOrConvertRecipeParams below, and
        // parameters are derived from the frames.
        loadDFlowProfile(f, "D-Flow / Methods Test", 36.0, 93.0, /*withInfuse=*/true);

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
        QCOMPARE(result.toInt(), 3);

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
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("unknown property"));
        f.profileManager.setFrameProperty(0, "nonexistent_property", 42);

        QCOMPARE(spy.count(), 0);
    }

    void deleteLastFrameIsBlocked() {
        McpTestFixture f;
        loadDFlowProfile(f);
        f.profileManager.deleteFrame(1);  // Remove one, leaving 1
        QCOMPARE(f.profileManager.frameCount(), 1);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Cannot delete the last frame"));
        f.profileManager.deleteFrame(0);  // Should be blocked
        QCOMPARE(f.profileManager.frameCount(), 1);
    }

    // === Brew-by-ratio ===

    void brewByRatioInactiveByDefault() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0);
        QVERIFY(!f.profileManager.brewByRatioActive());
    }

    // brewByRatioActive reads the STORED MODE (add-yield-ratio-anchor): only
    // a ratio anchor makes it true. An absolute override — even one that
    // differs from the profile target — is not "brew by ratio" (the old
    // qAbs(override − profileTarget) inference is retired), and a ratio
    // deriving exactly the profile's target is STILL ratio-anchored (the
    // Bug-A case the inference silently dropped).
    void brewByRatioActiveFollowsStoredMode() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0);

        f.settings.brew()->setBrewYieldOverride(54.0);  // absolute
        QVERIFY(!f.profileManager.brewByRatioActive());

        f.settings.brew()->setBrewRatioAnchor(2.0);
        QVERIFY(f.profileManager.brewByRatioActive());

        // Ratio deriving exactly the profile target (2.0 x 18 = 36): still
        // anchored, and a dose change still re-derives the target.
        f.settings.dye()->setDyeBeanWeight(18.0);
        QVERIFY(f.profileManager.brewByRatioActive());
        QCOMPARE(f.profileManager.targetWeight(), 36.0);
        f.settings.dye()->setDyeBeanWeight(17.5);
        QCOMPARE(f.profileManager.targetWeight(), 35.0);
    }

    void brewByRatioCalculation() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0);

        f.settings.dye()->setDyeBeanWeight(18.0);
        // Absolute anchor: the ratio derives (36 / 18 = 2).
        f.settings.brew()->setBrewYieldOverride(36.0);
        QCOMPARE(f.profileManager.brewByRatio(), 2.0);

        // Ratio anchor: the stored ratio verbatim, not a re-derivation.
        f.settings.brew()->setBrewRatioAnchor(2.5);
        QCOMPARE(f.profileManager.brewByRatio(), 2.5);
        QCOMPARE(f.profileManager.targetWeight(), 45.0);
    }

    // resolve() ladder contract (add-yield-ratio-anchor task 2.9): each mode,
    // a ratio with no dose, and "none" falling through to the profile.
    void targetWeightResolvesEachMode() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0);

        // Mode none: the profile answers.
        f.settings.brew()->clearAllBrewOverrides();
        QCOMPARE(f.profileManager.targetWeight(), 36.0);

        // Absolute: the stored grams, dose-independent.
        f.settings.brew()->setBrewYieldOverride(40.0);
        f.settings.dye()->setDyeBeanWeight(18.0);
        QCOMPARE(f.profileManager.targetWeight(), 40.0);
        f.settings.dye()->setDyeBeanWeight(20.0);
        QCOMPARE(f.profileManager.targetWeight(), 40.0);

        // Ratio: value x dose.
        f.settings.brew()->setBrewRatioAnchor(2.0);
        QCOMPARE(f.profileManager.targetWeight(), 40.0);

        // Ratio with no usable dose: falls back to the profile target —
        // a 0 g stop target must never reach the machine.
        f.settings.dye()->setDyeBeanWeight(0.0);
        QCOMPARE(f.profileManager.targetWeight(), 36.0);
    }

    // The shot latch (add-yield-ratio-anchor Decision 9): NOTHING moves the
    // resolved target while a shot runs; releasing re-resolves so the next
    // shot picks the new state up.
    //
    // Regression: latching only the DOSE was not enough. Every other input
    // stayed live, and each one re-resolves straight through main.cpp's
    // ungated forwarder into the running WeightProcessor. A bean switch
    // mid-pour (clearBrewOverrides) dropped a live 45 g target to the
    // profile's 36 g and cut the shot short — observed on a real pour, not
    // hypothetical. Every arm below is one of those paths.
    // add-shot-flow-calibration: the shot record stores the flow calibration
    // multiplier the shot POURED under, and it must survive a write that lands
    // after the pour started. That is not hypothetical ordering paranoia:
    // MainController::onShotEnded() calls computeAutoFlowCalibration() BEFORE
    // it builds the shot metadata and saves, so on any shot that completes a
    // 5-shot batch the stored multiplier changes between pour and save. A
    // save-time read would record the value the shot PRODUCED, on exactly the
    // shots where it differs — silently, and only there.
    //
    // The end-to-end ordering can't be driven from a test (no harness
    // constructs MainController), so this asserts the property the fix rests
    // on: once latched, the value is immune to later writes.
    void shotLatchFreezesFlowCalibrationAgainstLateWrites() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0);

        f.settings.calibration()->setFlowCalibrationMultiplier(1.35);
        f.profileManager.latchForShot();
        QCOMPARE(f.profileManager.latchedFlowCalibration(), 1.35);

        // What auto calibration does at shot end, before the save.
        f.settings.calibration()->setFlowCalibrationMultiplier(1.22);
        QCOMPARE(f.profileManager.latchedFlowCalibration(), 1.35);

        // The NEXT shot re-resolves — the latch freezes one shot, not forever.
        // Compared against effectiveFlowCalibration() rather than a literal so
        // the assertion holds whichever key wins for this fixture's profile
        // (per-profile when one is stored and auto-cal is on, else global).
        f.profileManager.latchForShot();
        QCOMPARE(f.profileManager.latchedFlowCalibration(),
                 f.settings.calibration()->effectiveFlowCalibration(
                     f.profileManager.baseProfileName()));
        QCOMPARE(f.profileManager.latchedFlowCalibration(), 1.22);
    }

    void shotLatchFreezesTargetAgainstEveryLateWrite() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0);

        f.settings.dye()->setDyeBeanWeight(18.0);
        f.settings.brew()->setBrewRatioAnchor(2.5);
        QCOMPARE(f.profileManager.targetWeight(), 45.0);

        f.profileManager.latchForShot();

        // 1. A dose write (scale capture, MCP, settings import).
        f.settings.dye()->setDyeBeanWeight(20.0);
        QCOMPARE(f.profileManager.targetWeight(), 45.0);

        // 2. An anchor CLEAR — what a bean switch does (the real-world bug).
        f.settings.brew()->clearAllBrewOverrides();
        QCOMPARE(f.profileManager.targetWeight(), 45.0);

        // 3. An anchor WRITE (recipe activation, MCP/web edit).
        f.settings.brew()->setBrewYieldOverride(80.0);
        QCOMPARE(f.profileManager.targetWeight(), 45.0);

        // 4. A ratio anchor write.
        f.settings.brew()->setBrewRatioAnchor(1.0);
        QCOMPARE(f.profileManager.targetWeight(), 45.0);

        // Released: the next shot resolves against the live state (1.0 x 20).
        f.profileManager.releaseShotLatch();
        QCOMPARE(f.profileManager.targetWeight(), 20.0);
    }

    // A cycle that arms the latch and never releases must not poison the
    // session. The latch is armed at espressoCycleStarted (which fires during
    // preheat, before any flow) and released at espressoCycleEnded; shotEnded
    // — the old release point — is gated on flow having STARTED, so a cycle
    // aborted during preheat armed a latch that nothing ever released. Because
    // latchForShot resolved through its own flag, the next shot then
    // self-assigned the stale target and re-latched it: one abort silently
    // pinned the machine's target for the rest of the session while every
    // surface kept showing the live value. Both halves are asserted here.
    void abortedCycleDoesNotPinTargetForTheSession() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0);

        f.settings.dye()->setDyeBeanWeight(18.0);
        f.settings.brew()->setBrewRatioAnchor(2.5);
        f.profileManager.latchForShot();
        QCOMPARE(f.profileManager.targetWeight(), 45.0);

        // The cycle is abandoned during preheat: no flow, so no shotEnded.
        // The user then re-dials for the shot they actually intend to pull.
        f.settings.dye()->setDyeBeanWeight(20.0);
        f.settings.brew()->setBrewRatioAnchor(2.0);

        // Re-arming must re-resolve against the live state (2.0 x 20), not
        // launder the stale 45 through the still-armed flag.
        f.profileManager.latchForShot();
        QCOMPARE(f.profileManager.targetWeight(), 40.0);

        // And the release must still let the session move afterwards.
        f.profileManager.releaseShotLatch();
        f.settings.brew()->setBrewRatioAnchor(3.0);
        QCOMPARE(f.profileManager.targetWeight(), 60.0);
    }

    // The latch's release hangs off espressoCycleEnded (main.cpp), so that
    // signal must fire on a cycle that never flowed — the case the old
    // shotEnded release could not cover. The main.cpp wiring itself is out of
    // reach here; this pins the signal contract it depends on.
    void espressoCycleEndedFiresOnACycleThatNeverFlowed() {
        McpTestFixture f;
        f.device.m_simulationMode = true;  // isConnected() -> true

        QSignalSpy cycleStarted(&f.machineState, &MachineState::espressoCycleStarted);
        QSignalSpy cycleEnded(&f.machineState, &MachineState::espressoCycleEnded);
        QSignalSpy shotEnded(&f.machineState, &MachineState::shotEnded);

        // Enter the espresso cycle (preheat) — this is where the latch arms.
        f.device.m_state = DE1::State::Espresso;
        f.device.m_subState = DE1::SubState::Heating;
        f.machineState.updatePhase();
        QCOMPARE(f.machineState.phase(), MachineState::Phase::EspressoPreheating);
        QCOMPARE(cycleStarted.count(), 1);

        // Abort before any flow: straight back to Idle.
        f.device.m_state = DE1::State::Idle;
        f.device.m_subState = DE1::SubState::Ready;
        f.machineState.updatePhase();

        QCOMPARE(cycleEnded.count(), 1);
        // The old release point never fires here — this is the whole bug.
        QCOMPARE(shotEnded.count(), 0);
    }

    // A BLE drop mid-pour also LEAVES the espresso cycle, but through
    // updatePhase's disconnect branch, which returns before the normal
    // cycle-exit detection runs. It must still fire espressoCycleEnded or the
    // latch leaks exactly as it did off shotEnded — same bug, different road.
    void bleDropMidPourStillEndsTheEspressoCycle() {
        McpTestFixture f;
        // Connectivity comes from the transport here, NOT simulationMode
        // (which would short-circuit isConnected() to true and make the drop
        // below unrepresentable).
        f.transport.setConnectedSim(true);

        QSignalSpy cycleEnded(&f.machineState, &MachineState::espressoCycleEnded);

        // The phase AS IT STANDS INSIDE the emission, which a spy cannot see. The
        // pre-shot-zero clear in main.cpp hangs off this signal and is guarded on
        // Phase::Disconnected precisely to tell this exit from the normal one, where
        // clearing early would step the drip-settle samples. Assigning m_phase after
        // the emit instead of before would leave that guard reading Pouring and
        // silently stop it firing, with nothing else here failing.
        MachineState::Phase phaseAtEmit = MachineState::Phase::Idle;
        QObject::connect(&f.machineState, &MachineState::espressoCycleEnded,
                         [&f, &phaseAtEmit]() { phaseAtEmit = f.machineState.phase(); });

        f.device.m_state = DE1::State::Espresso;
        f.device.m_subState = DE1::SubState::Pouring;
        f.machineState.updatePhase();
        QCOMPARE(f.machineState.phase(), MachineState::Phase::Pouring);

        // The radio drops mid-pour: isConnected() goes false.
        //
        // ignoreMessage, not a filter: this ASSERTS the warning. A drop while the
        // machine is busy is the case that used to produce nothing above INFO —
        // de1LinkFault never fires on plain absence, no write is in flight
        // mid-pour, and on Apple platforms Qt raises no controller error at all.
        // DE1Device tiers this by state precisely so this scenario warns, and
        // ignoreMessage fails if it stops doing so.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("\\[DE1\\]\\[Device\\] DE1 DISCONNECTED while Espresso"));
        f.transport.setConnectedSim(false);
        f.machineState.updatePhase();

        QCOMPARE(f.machineState.phase(), MachineState::Phase::Disconnected);
        QCOMPARE(cycleEnded.count(), 1);
        QCOMPARE(phaseAtEmit, MachineState::Phase::Disconnected);

        // Idempotent: further disconnected updates must not re-fire it.
        f.machineState.updatePhase();
        QCOMPARE(cycleEnded.count(), 1);
    }

    // The shot-save snapshot (add-yield-ratio-anchor): what RAN, not what the
    // session drifted to. The save path runs after SAW settling — i.e. after
    // releaseShotLatch() — so the snapshot must survive the release, or a
    // realistic mid-shot write (weighing the next dose while the cup fills)
    // would record a target the machine never used.
    void shotSnapshotSurvivesLatchReleaseAndMidShotWrites() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0);

        f.settings.dye()->setDyeBeanWeight(18.0);
        f.settings.brew()->setBrewRatioAnchor(2.5);

        f.profileManager.latchForShot();
        QVERIFY(f.profileManager.hasShotSnapshot());
        QCOMPARE(f.profileManager.latchedTargetG(), 45.0);
        QCOMPARE(f.profileManager.latchedYieldMode(), QStringLiteral("ratio"));
        QCOMPARE(f.profileManager.latchedYieldAnchorValue(), 2.5);

        // Mid-shot: weigh the next dose, then a bean switch wipes the anchor.
        f.settings.dye()->setDyeBeanWeight(20.0);
        f.settings.brew()->clearAllBrewOverrides();

        // Shot end releases the freeze — but the snapshot still reports what ran.
        f.profileManager.releaseShotLatch();
        QVERIFY(f.profileManager.hasShotSnapshot());
        QCOMPARE(f.profileManager.latchedTargetG(), 45.0);
        QCOMPARE(f.profileManager.latchedYieldMode(), QStringLiteral("ratio"));
        QCOMPARE(f.profileManager.latchedYieldAnchorValue(), 2.5);
        // ...while live resolution has resumed for the NEXT shot.
        QCOMPARE(f.profileManager.targetWeight(), 36.0);
    }

    // The latch must NOT swallow the deliberate mid-shot +10 g bump, which
    // writes MachineState directly and never routes through targetWeight().
    void shotLatchDoesNotBlockDirectMachineStateWrites() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0);
        f.settings.dye()->setDyeBeanWeight(18.0);
        f.settings.brew()->setBrewRatioAnchor(2.0);

        f.profileManager.latchForShot();
        QCOMPARE(f.machineState.targetWeight(), 36.0);

        // The +10 g bump's path: straight at MachineState.
        f.machineState.setTargetWeight(f.machineState.targetWeight() + 10.0);
        QCOMPARE(f.machineState.targetWeight(), 46.0);

        f.profileManager.releaseShotLatch();
    }

    // Profile-load mode asymmetry (Decision 8): a runtime profile switch
    // clears an ABSOLUTE session anchor but keeps a RATIO one.
    void profileSwitchKeepsRatioClearsAbsolute() {
        McpTestFixture f;
        loadDFlowProfile(f, "TestA", 36.0);

        f.settings.brew()->setBrewYieldOverride(40.0);
        loadDFlowProfile(f, "TestB", 42.0);
        QVERIFY(!f.settings.brew()->hasBrewYieldOverride());
        QCOMPARE(f.profileManager.targetWeight(), 42.0);

        f.settings.dye()->setDyeBeanWeight(18.0);
        f.settings.brew()->setBrewRatioAnchor(2.0);
        loadDFlowProfile(f, "TestC", 48.0);
        QVERIFY(f.settings.brew()->hasBrewYieldOverride());
        QCOMPARE(f.settings.brew()->brewYieldMode(), QStringLiteral("ratio"));
        QCOMPARE(f.profileManager.targetWeight(), 36.0);  // still 2 x 18
    }

    void clearBrewOverridesResetsToProfileDefaults() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0, 93.0);

        // Activate with different values
        f.profileManager.activateBrewWithOverrides(20.0, 50.0, 96.0, "15");
        QVERIFY(f.settings.brew()->hasBrewYieldOverride());
        QVERIFY(f.settings.brew()->hasTemperatureOverride());

        // Clear genuinely clears (fix-recipe-grind-integrity Bug A: the flags
        // go false, not merely the values resyncing) — the EFFECTIVE values
        // then follow the profile defaults.
        f.profileManager.clearBrewOverrides();

        QVERIFY(!f.settings.brew()->hasBrewYieldOverride());
        QVERIFY(!f.settings.brew()->hasTemperatureOverride());
        QCOMPARE(f.profileManager.targetWeight(), 36.0);
        QCOMPARE(f.profileManager.getGroupTemperature(), 93.0);
    }

    // A value matching the profile's own default is not an override — the
    // flags mean "deliberately different from the profile" (Bug A fix), so
    // committing the defaults leaves the plan un-highlighted.
    void activateBrewAtProfileDefaultsSetsNoOverride() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0, 93.0);

        f.profileManager.activateBrewWithOverrides(18.0, 36.0, 93.0, "15");

        QVERIFY(!f.settings.brew()->hasBrewYieldOverride());
        QVERIFY(!f.settings.brew()->hasTemperatureOverride());
        QCOMPARE(f.profileManager.targetWeight(), 36.0);
        QCOMPARE(f.profileManager.getGroupTemperature(), 93.0);
    }

    // THE headline Bug A scenario (#1468, brew-overrides spec "Overrides
    // cleared on profile switch"): switching profiles genuinely clears the
    // override flags — a resync-instead-of-clear regression would leave the
    // Shot Plan latched orange forever.
    void profileSwitchClearsOverrides() {
        McpTestFixture f;
        loadDFlowProfile(f, "First", 36.0, 93.0);
        f.profileManager.activateBrewWithOverrides(20.0, 50.0, 96.0, "15");
        QVERIFY(f.settings.brew()->hasTemperatureOverride());
        QVERIFY(f.settings.brew()->hasBrewYieldOverride());

        loadDFlowProfile(f, "Second", 38.0, 90.0);

        QVERIFY(!f.settings.brew()->hasTemperatureOverride());
        QVERIFY(!f.settings.brew()->hasBrewYieldOverride());
        QCOMPARE(f.profileManager.targetWeight(), 38.0);
        QCOMPARE(f.profileManager.getGroupTemperature(), 90.0);
    }

    // The startup branch of resetBrewOverridesForLoadedProfile: a persisted
    // override survives a restart only when it genuinely differs from the
    // incoming profile's default; a same-as-default persisted value (the
    // noise every pre-fix session latched) is dropped.
    void startupRestorePreservesGenuineOverridesOnly() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0, 93.0);

        // Arm one genuine override (temp, differs) and one noise override
        // (yield ≈ the profile default).
        f.settings.brew()->setTemperatureOverride(96.0);
        f.settings.brew()->setBrewYieldOverride(36.05);
        QVERIFY(f.settings.brew()->hasTemperatureOverride());
        QVERIFY(f.settings.brew()->hasBrewYieldOverride());

        // Simulate the startup load of the same profile.
        f.profileManager.m_startupLoadDone = false;
        loadDFlowProfile(f, "Test", 36.0, 93.0);
        f.profileManager.m_startupLoadDone = true;

        QVERIFY(f.settings.brew()->hasTemperatureOverride());   // genuine: survives
        QCOMPARE(f.settings.brew()->temperatureOverride(), 96.0);
        QVERIFY(!f.settings.brew()->hasBrewYieldOverride());    // noise: dropped
        QCOMPARE(f.profileManager.targetWeight(), 36.0);
    }

    // Editing the profile's own temperature/target (uploadProfile) makes the
    // edited value the new default — any live override is now stale and must
    // clear, or uploadCurrentProfile would re-apply it as a second delta.
    void uploadProfileClearsStaleOverrides() {
        McpTestFixture f;
        loadDFlowProfile(f, "Test", 36.0, 93.0);
        f.profileManager.activateBrewWithOverrides(18.0, 40.0, 95.0, "15");
        QVERIFY(f.settings.brew()->hasTemperatureOverride());
        QVERIFY(f.settings.brew()->hasBrewYieldOverride());

        f.profileManager.uploadProfile(QVariantMap{
            {"espresso_temperature", 94.0}, {"target_weight", 38.0}});

        QVERIFY(!f.settings.brew()->hasTemperatureOverride());
        QVERIFY(!f.settings.brew()->hasBrewYieldOverride());
        QCOMPARE(f.profileManager.currentProfile().espressoTemperature(), 94.0);
        QCOMPARE(f.profileManager.targetWeight(), 38.0);
    }

    // === The dose ladder on profile load (dose-source-precedence) ===

    // Writes a D-Flow profile carrying an ENABLED recommended dose, then loads
    // it through loadProfile — the path that applies the dose, and the one
    // loadProfileFromJson (used by loadDFlowProfile above) never takes.
    static void loadDFlowWithRecommendedDose(McpTestFixture& f, const QString& fileName,
                                             double doseG) {
        QJsonObject p = makeDFlowJson(QStringLiteral("D-Flow / ") + fileName);
        p["recommended_dose"] = doseG;
        p["has_recommended_dose"] = true;
        const QString path = f.profileManager.userProfilesPath() + "/" + fileName + ".json";
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(QJsonDocument(p).toJson());
        out.close();
        f.profileManager.refreshProfiles();
        f.profileManager.loadProfile(fileName);
        // The write is deferred to the next event-loop turn.
        QCoreApplication::processEvents();
    }

    // The profile is the LAST rung. With nothing else active it supplies the
    // dose, which is the behaviour that must survive the gate.
    void profileDoseAppliesWhenNothingElseIsActive() {
        clearTestProfileStore();
        McpTestFixture f;
        f.settings.dye()->setActiveRecipeId(-1);
        f.settings.dye()->setActiveBagId(-1);
        f.settings.dye()->setDyeBeanWeight(15.0);

        loadDFlowWithRecommendedDose(f, "dose_profile_only", 21.0);

        QCOMPARE(f.settings.dye()->doseOwner(), SettingsDye::DoseOwner::Profile);
        QCOMPARE(f.settings.dye()->dyeBeanWeight(), 21.0);
    }

    // The inversion this change fixes. A recipe supplying a dose outranks the
    // profile, so switching profiles while it is active must not touch the
    // dose — and because dyeBeanWeightChanged stamps the recipe and writes
    // through to the bag, a write here would not merely show the wrong number,
    // it would overwrite the recipe's stored doseG.
    void loadingAProfileDoesNotOverwriteAnActiveRecipesDose() {
        clearTestProfileStore();
        McpTestFixture f;
        f.settings.dye()->setActiveBagId(-1);
        f.settings.dye()->setDyeBeanWeight(19.0);
        f.settings.dye()->setActiveRecipe(7, 19.0);

        loadDFlowWithRecommendedDose(f, "dose_recipe_wins", 21.0);

        QCOMPARE(f.settings.dye()->doseOwner(), SettingsDye::DoseOwner::Recipe);
        QCOMPARE(f.settings.dye()->dyeBeanWeight(), 19.0);
    }

    // Same rule one rung down, against a REAL bag row — because the damage a
    // profile load does to an active bag is persistent, not just a wrong live
    // number: setDyeBeanWeight writes through to the bag's stored doseWeightG,
    // so an ungated load replaces what the bean remembered. Asserting only the
    // session value would pass just as happily with the write-through intact,
    // which is the failure mode worth catching.
    void loadingAProfileDoesNotOverwriteAnActiveBagsStoredDose() {
        clearTestProfileStore();
        McpTestFixture f;

        QTemporaryDir dbDir;
        const QString dbPath = dbDir.filePath(QStringLiteral("bags.db"));
        {   // Migrate through the real chain, as tst_coffeebags does.
            ShotHistoryStorage migrate;
            QVERIFY(migrate.initialize(dbPath));
            migrate.close();
            for (int i = 0; i < 20; i++) { QCoreApplication::processEvents(); QThread::msleep(25); }
        }
        CoffeeBagStorage bags;
        bags.initialize(dbPath);

        qint64 bagId = -1;
        CoffeeBag bag;
        bag.roasterName = "R";
        bag.coffeeName = "Remembered";
        bag.doseWeightG = 20.0;
        QVERIFY(withTempDb(dbPath, QStringLiteral("dose_bag_seed"), [&](QSqlDatabase& db) {
            bagId = CoffeeBagStorage::insertBagStatic(db, bag);
        }));
        QVERIFY(bagId > 0);

        f.settings.dye()->setActiveRecipeId(-1);
        f.settings.dye()->setBagStorage(&bags);
        f.settings.dye()->setActiveBagId(static_cast<int>(bagId));
        QTRY_COMPARE(f.settings.dye()->dyeBeanWeight(), 20.0);
        QCOMPARE(f.settings.dye()->doseOwner(), SettingsDye::DoseOwner::Bag);

        loadDFlowWithRecommendedDose(f, "dose_bag_wins", 21.0);

        QCOMPARE(f.settings.dye()->dyeBeanWeight(), 20.0);
        // And the row itself is untouched.
        double storedDose = -1;
        QVERIFY(withTempDb(dbPath, QStringLiteral("dose_bag_read"), [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT dose_weight_g FROM coffee_bags WHERE id = ?"));
            q.addBindValue(bagId);
            if (q.exec() && q.next())
                storedDose = q.value(0).toDouble();
        }));
        QCOMPARE(storedDose, 20.0);

        f.settings.dye()->setActiveBagId(-1);
    }

    // A profile whose recommendation is not enabled changes nothing, whoever
    // owns the dose. makeDFlowJson writes no dose keys, so the loaded profile
    // holds the 18 g read-default with the flag off — exactly the shape every
    // built-in ships in, and the reason the flag is checked before the value.
    void aProfileWithNoRecommendationLeavesTheDoseAlone() {
        clearTestProfileStore();
        McpTestFixture f;
        f.settings.dye()->setActiveRecipeId(-1);
        f.settings.dye()->setActiveBagId(-1);
        f.settings.dye()->setDyeBeanWeight(15.0);

        loadThreeFrameDFlow(f, "dose_no_recommendation", "D-Flow / No Dose");
        QCoreApplication::processEvents();

        QVERIFY(!f.profileManager.currentProfile().hasRecommendedDose());
        QCOMPARE(f.settings.dye()->dyeBeanWeight(), 15.0);
    }

    // Startup is not a resolution point: the bag and recipe rows are still
    // loading, so the ladder cannot be answered, and the live dose is already
    // persisted from whichever source won last session. Writing the profile's
    // dose here would overwrite it on every launch.
    void theStartupLoadDoesNotApplyTheProfilesDose() {
        clearTestProfileStore();
        McpTestFixture f;
        f.settings.dye()->setActiveRecipeId(-1);
        f.settings.dye()->setActiveBagId(-1);
        f.settings.dye()->setDyeBeanWeight(19.5);   // persisted from last session

        f.profileManager.m_startupLoadDone = false;
        loadDFlowWithRecommendedDose(f, "dose_startup", 21.0);
        f.profileManager.m_startupLoadDone = true;

        QCOMPARE(f.settings.dye()->dyeBeanWeight(), 19.5);
    }

    // === activateBrewWithOverrides ===

    void activateBrewWithOverridesSetsSettings() {
        McpTestFixture f;
        loadDFlowProfile(f);
        const double doseBefore = f.profileManager.profileRecommendedDose();

        f.profileManager.activateBrewWithOverrides(18.0, 40.0, 95.0, "14");

        QCOMPARE(f.settings.dye()->dyeBeanWeight(), 18.0);
        QCOMPARE(f.settings.brew()->brewYieldOverride(), 40.0);
        QCOMPARE(f.settings.brew()->temperatureOverride(), 95.0);
        QCOMPARE(f.settings.dye()->dyeGrinderSetting(), "14");

        // The profile is NOT a write target for the dial (dose-source-precedence).
        // The only call that would write it, setCurrentProfileRecommendedDose,
        // marks the profile modified — and this dialog commits on every OK, so
        // "completing the ladder" with a third write target would make a 0.2 g
        // nudge dirty the loaded profile and ask to be saved. The recommendation
        // is stored design, edited in the profile editors.
        QCOMPARE(f.profileManager.profileRecommendedDose(), doseBefore);
        QVERIFY(!f.profileManager.isProfileModified());
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

    void convertToAdvancedCaseInsensitiveTitle() {
        // isDFlowTitle matches case-insensitively — stripping must too
        McpTestFixture f;
        loadDFlowProfile(f, "d-flow / lowercase test");
        QCOMPARE(f.profileManager.currentEditorType(), "dflow");

        f.profileManager.convertCurrentProfileToAdvanced();

        QCOMPARE(f.profileManager.currentEditorType(), "advanced");
        // Title should be "lowercase test", not still contain "d-flow"
        QVERIFY(!f.profileManager.currentProfileName().contains("flow", Qt::CaseInsensitive));
    }

    void convertToAdvancedBareDFlowTitle() {
        // Edge case: title is exactly "D-Flow" with no suffix
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow");
        QCOMPARE(f.profileManager.currentEditorType(), "dflow");

        f.profileManager.convertCurrentProfileToAdvanced();

        QCOMPARE(f.profileManager.currentEditorType(), "advanced");
        // currentProfileName() prepends "*" when modified
        QCOMPARE(f.profileManager.currentProfileName(), "*Advanced Profile");
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

        ScopedWarningFilter filter("BLOCKED during active phase|^  #");
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
        ScopedWarningFilter filter("BLOCKED during active phase|^  #");

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
        ScopedWarningFilter filter("BLOCKED during active phase|^  #");

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
        // Three frames: uploadRecipeProfile now refuses to regenerate a profile
        // whose frames its editor cannot read positionally, and a two-frame
        // "D-Flow" profile is not one.
        loadDFlowProfile(f, "D-Flow / Test", 36.0, 93.0, /*withInfuse=*/true);

        QSignalSpy modSpy(&f.profileManager, &ProfileManager::profileModifiedChanged);
        QSignalSpy curSpy(&f.profileManager, &ProfileManager::currentProfileChanged);
        QSignalSpy wgtSpy(&f.profileManager, &ProfileManager::targetWeightChanged);

        QVariantMap recipe;
        recipe["editorType"] = "dflow";
        recipe["targetWeight"] = 40.0;
        recipe["fillTemperature"] = 95.0;
        recipe["pourTemperature"] = 95.0;
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
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Cannot save read-only"));
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
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Cannot overwrite built-in"));
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

    // === Stored-encoding upgrade on load ===
    //
    // Change 1 made everything the app EMITS canonical, but a file already on disk
    // keeps its old encoding until something re-saves it — and DatabaseBackupManager
    // copies the profile directory verbatim, so a legacy-encoded file travels into a
    // backup and onto another device, where a stricter reader rejects it. The upgrade
    // runs on load rather than as a one-time pass because the user can drop a profile
    // into the folder at any time.

    // Write a profile whose numbers are JSON numbers rather than the canonical
    // strings, and which omits the two keys Decaid hard-requires. This is the
    // shape of a file written before Change 1.
    static QString writeLegacyEncodedProfile(McpTestFixture& f, const QString& filename) {
        const QString path = f.profileManager.userProfilesPath() + "/" + filename + ".json";
        QDir().mkpath(f.profileManager.userProfilesPath());

        QJsonObject step{
            {"name", "preinfusion"}, {"pump", "flow"},   {"transition", "fast"},
            {"sensor", "coffee"},    {"temperature", 92.0}, {"seconds", 20.0},
            {"volume", 100.0},       {"flow", 4.0},      {"pressure", 1.0},
        };
        // Advanced, so the stored steps are authoritative. A simple type would have
        // its frames regenerated from scalars this fixture does not carry, which
        // makes the fixture — not the code under test — the thing being measured.
        QJsonObject obj{
            {"title", "Legacy Encoded Test"},
            {"author", "Decent"},
            {"type", "advanced"},
            {"legacy_profile_type", "settings_2c"},
            {"beverage_type", "espresso"},
            {"version", "2"},
            // Present and not the bare 93.0 default, so fromJson does not derive it
            // and espressoTemperatureHealed() stays false. That matters twice over:
            // the heal has its own on-disk write, and a healed profile is excluded
            // from the encoding upgrade entirely — either would make these tests
            // measure something other than what they claim.
            {"espresso_temperature", 92.0},
            {"target_weight", 36.0},        // number, not "36.0"
            {"target_volume", 0.0},
            {"steps", QJsonArray{step}},
        };
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        return path;
    }

    void loadUpgradesLegacyEncodedUserProfile() {
        McpTestFixture f;
        const QString path = writeLegacyEncodedProfile(f, "legacy_encoding_xyz");

        f.profileManager.loadProfile("legacy_encoding_xyz");

        QFile after(path);
        QVERIFY(after.open(QIODevice::ReadOnly));
        const QJsonObject obj = QJsonDocument::fromJson(after.readAll()).object();
        after.close();

        // Numbers are string-encoded, and the keys Decaid requires are present —
        // the whole point of the conversion.
        QVERIFY(obj["target_weight"].isString());
        QVERIFY(obj.contains("tank_temperature"));
        QVERIFY(obj.contains("target_volume_count_start"));
        // Content is untouched: this converts encoding, never values.
        QCOMPARE(obj["title"].toString(), QString("Legacy Encoded Test"));
        QCOMPARE(obj["target_weight"].toString().toDouble(), 36.0);
        QCOMPARE(obj["steps"].toArray().size(), 1);

        QFile::remove(path);
    }

    void loadDoesNotRewriteAnAlreadyCanonicalProfile() {
        // Without an "already canonical" check the file would be rewritten on every
        // single activation, churning its mtime forever.
        McpTestFixture f;
        const QString path = writeLegacyEncodedProfile(f, "already_canonical_xyz");

        f.profileManager.loadProfile("already_canonical_xyz");   // converts
        QFile first(path);
        QVERIFY(first.open(QIODevice::ReadOnly));
        const QByteArray afterFirst = first.readAll();
        first.close();

        f.profileManager.loadProfile("already_canonical_xyz");   // must be a no-op
        QFile second(path);
        QVERIFY(second.open(QIODevice::ReadOnly));
        const QByteArray afterSecond = second.readAll();
        second.close();

        QCOMPARE(afterSecond, afterFirst);
        QFile::remove(path);
    }

    void loadLeavesBuiltInProfilesUntouched() {
        // `:/profiles/` is a read-only Qt resource. Attempting a write there would
        // fail rather than corrupt anything, but it must not be attempted at all —
        // and it must not leave a stray copy in the user directory either.
        McpTestFixture f;
        f.profileManager.loadProfile("default");

        QVERIFY(!QFile::exists(f.profileManager.userProfilesPath() + "/default.json"));
        QVERIFY(!QFile::exists(f.profileManager.downloadedProfilesPath() + "/default.json"));
    }

    void loadLeavesAProfileAloneWhenConversionWouldLoseData() {
        // A profile whose values do not survive the round trip must be left exactly
        // as it is, and the skip must be audible. Silently altering a user's file to
        // tidy its formatting is the one outcome this pass must never produce.
        //
        // The lossy ingredient is precision: target_weight is written with
        // ProfileJson::TargetMass, which is ONE decimal, so 36.1234 comes back as
        // 36.1 — a drift of 0.0234, far outside jsonParityErrors' 0.0005 tolerance.
        // Realistic for a profile authored in another tool. (An unknown top-level key
        // would NOT work: the serializer preserves those, which is worth knowing.)
        McpTestFixture f;
        const QString path = writeLegacyEncodedProfile(f, "lossy_encoding_xyz");

        QJsonObject obj;
        {
            QFile in(path);
            QVERIFY(in.open(QIODevice::ReadOnly));
            obj = QJsonDocument::fromJson(in.readAll()).object();
        }
        obj["target_weight"] = 36.1234;
        {
            QFile out(path);
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        }
        QFile before(path);
        QVERIFY(before.open(QIODevice::ReadOnly));
        const QByteArray original = before.readAll();
        before.close();

        // qWarning() << quotes a QString, so the name appears as "lossy_encoding_xyz".
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("leaving .*lossy_encoding_xyz.* in its stored encoding"));
        f.profileManager.loadProfile("lossy_encoding_xyz");

        QFile after(path);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), original);   // byte-for-byte untouched
        after.close();

        QFile::remove(path);
    }

    void loadDoesNotPersistBackfilledNotesIntoTheUsersFile() {
        // The notes backfill injects the BUILT-IN's notes into the in-memory profile.
        // No write in loadProfile may persist them: the user's file would gain text
        // it never contained. This regressed once — the espresso_temperature repair
        // ran after the backfill and serialized the whole profile.
        //
        // Uses a filename that matches a shipped built-in so a backfill source
        // exists, and omits espresso_temperature so the repair fires.
        McpTestFixture f;
        const QString path = f.profileManager.userProfilesPath() + "/default.json";
        QDir().mkpath(f.profileManager.userProfilesPath());

        QJsonObject step{
            {"name", "preinfusion"}, {"pump", "flow"},    {"transition", "fast"},
            {"sensor", "coffee"},    {"temperature", 92.0}, {"seconds", 20.0},
            {"volume", 100.0},       {"flow", 4.0},       {"pressure", 1.0},
        };
        QJsonObject obj{
            {"title", "Default"},   {"author", "Decent"},
            {"type", "advanced"},   {"legacy_profile_type", "settings_2c"},
            {"beverage_type", "espresso"}, {"version", "2"},
            {"notes", ""},          // empty, so the backfill has something to do
            {"target_weight", 36.0},
            {"steps", QJsonArray{step}},
        };
        {
            QFile out(path);
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        }

        f.profileManager.loadProfile("default");

        QFile after(path);
        QVERIFY(after.open(QIODevice::ReadOnly));
        const QJsonObject onDisk = QJsonDocument::fromJson(after.readAll()).object();
        after.close();

        // Non-vacuous: prove a write actually happened, otherwise "notes are empty"
        // would pass simply because nothing touched the file. The repair persists the
        // derived espresso_temperature, which the fixture deliberately omitted.
        QVERIFY2(onDisk.contains("espresso_temperature"),
                 "the espresso_temperature repair did not write, so this test would "
                 "pass without proving anything about the notes");

        // And in memory the backfill did run, so the ordering is what kept it off disk.
        QVERIFY(!f.profileManager.currentProfile().profileNotes().isEmpty());

        // On disk the notes must still be empty.
        QVERIFY2(onDisk.value("notes").toString().isEmpty(),
                 qPrintable("built-in notes leaked into the user's file: "
                            + onDisk.value("notes").toString().left(60)));

        QFile::remove(path);
    }

    void aHealedProfileIsLeftAloneWhenRewritingWouldLoseData() {
        // A profile that needs the espresso_temperature repair AND carries a value
        // the writer cannot round-trip must not be rewritten: the repair would carry
        // the unrelated loss into the user's file. The temperature stays corrected in
        // memory and the repair is retried on a later load.
        McpTestFixture f;
        const QString path = writeLegacyEncodedProfile(f, "healed_and_lossy_xyz");

        QJsonObject obj;
        {
            QFile in(path);
            QVERIFY(in.open(QIODevice::ReadOnly));
            obj = QJsonDocument::fromJson(in.readAll()).object();
        }
        obj.remove("espresso_temperature");   // forces the heal
        obj["target_weight"] = 36.1234;       // does not survive TargetMass precision
        {
            QFile out(path);
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        }
        QFile before(path);
        QVERIFY(before.open(QIODevice::ReadOnly));
        const QByteArray original = before.readAll();
        before.close();

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("NOT persisting the espresso_temperature repair"));
        f.profileManager.loadProfile("healed_and_lossy_xyz");

        QFile after(path);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), original);   // byte-for-byte untouched
        after.close();

        QFile::remove(path);
    }

    void upgradedProfileSurvivesABackupRoundTrip() {
        // The reason the conversion exists: DatabaseBackupManager copies the profile
        // directory verbatim, so whatever encoding is on disk is what lands on the
        // next device.
        McpTestFixture f;
        const QString path = writeLegacyEncodedProfile(f, "backup_roundtrip_xyz");
        f.profileManager.loadProfile("backup_roundtrip_xyz");

        QTemporaryDir backupDir;
        QVERIFY(backupDir.isValid());
        const QString copied = backupDir.filePath("backup_roundtrip_xyz.json");
        QVERIFY(QFile::copy(path, copied));

        const Profile restored = Profile::loadFromFile(copied);
        QVERIFY(restored.isValid());
        QCOMPARE(restored.title(), QString("Legacy Encoded Test"));
        // And it satisfies the cross-app contract that a legacy-encoded copy would not.
        QVERIFY(Profile::decaidReadabilityErrors(restored.toJsonObject()).isEmpty());

        QFile::remove(path);
    }

    // === renameProfile (in-place title rename) ===

    void renameProfileChangesTitleKeepsFilename() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / BeforeRename", 37.0);
        QVERIFY(f.profileManager.saveProfile("rename_test_xyz"));

        bool ok = f.profileManager.renameProfile("rename_test_xyz", "D-Flow / AfterRename");

        QVERIFY(ok);
        // Filename is unchanged — the same file holds the new title.
        QString path = f.profileManager.userProfilesPath() + "/rename_test_xyz.json";
        QVERIFY(QFile::exists(path));
        // The on-disk title reflects the rename; other fields are preserved.
        QVariantMap p = f.profileManager.getProfileByFilename("rename_test_xyz");
        QCOMPARE(p["title"].toString(), QString("D-Flow / AfterRename"));
        QCOMPARE(p["target_weight"].toDouble(), 37.0);

        QFile::remove(path);
    }

    void renameProfileUpdatesActiveProfileAndEmitsSignal() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / ActiveBefore");
        QVERIFY(f.profileManager.saveProfile("rename_active_xyz"));
        QCOMPARE(f.profileManager.baseProfileName(), "rename_active_xyz");

        QSignalSpy spy(&f.profileManager, &ProfileManager::currentProfileChanged);
        bool ok = f.profileManager.renameProfile("rename_active_xyz", "D-Flow / ActiveAfter");

        QVERIFY(ok);
        // The live copy of the active profile reflects the new title immediately.
        QCOMPARE(f.profileManager.currentProfileName(), QString("D-Flow / ActiveAfter"));
        QVERIFY(spy.count() >= 1);

        QFile::remove(f.profileManager.userProfilesPath() + "/rename_active_xyz.json");
    }

    void renameProfileSyncsFavoriteTitle() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / FavBefore");
        QVERIFY(f.profileManager.saveProfile("rename_fav_xyz"));
        f.settings.app()->addFavoriteProfile("D-Flow / FavBefore", "rename_fav_xyz");
        QVERIFY(f.settings.app()->isFavoriteProfile("rename_fav_xyz"));

        bool ok = f.profileManager.renameProfile("rename_fav_xyz", "D-Flow / FavAfter");

        QVERIFY(ok);
        // Favorite stays keyed by the same filename, with its stored title updated.
        QVERIFY(f.settings.app()->isFavoriteProfile("rename_fav_xyz"));
        QString favTitle;
        const QVariantList favs = f.settings.app()->favoriteProfiles();
        for (const QVariant& v : favs) {
            const QVariantMap m = v.toMap();
            if (m["filename"].toString() == "rename_fav_xyz") {
                favTitle = m["name"].toString();
                break;
            }
        }
        QCOMPARE(favTitle, QString("D-Flow / FavAfter"));

        QFile::remove(f.profileManager.userProfilesPath() + "/rename_fav_xyz.json");
    }

    void renameProfileRejectsEmptyTitle() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Whitespace");
        QVERIFY(f.profileManager.saveProfile("rename_empty_xyz"));

        // Whitespace-only title trims to empty and must be rejected.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("renameProfile"));
        QVERIFY(!f.profileManager.renameProfile("rename_empty_xyz", "   "));

        QFile::remove(f.profileManager.userProfilesPath() + "/rename_empty_xyz.json");
    }

    void renameProfileRejectsBuiltIn() {
        McpTestFixture f;
        // "default" is a known built-in (read-only QRC resource).
        if (!f.profileManager.isBuiltInFilename("default")) {
            QSKIP("No built-in profiles in test binary QRC");
        }
        // renameProfile refuses built-in profiles via ProfileSource::BuiltIn —
        // built-ins are read-only, so they can only be copied, not renamed.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("renameProfile"));
        QVERIFY(!f.profileManager.renameProfile("default", "Hacked Title"));
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

    // =========================================================================
    // editorType derivation — behavioral coverage for refactored paths
    // =========================================================================

    // === convertCurrentProfileToAdvanced ===

    void convertToAdvancedDFlowBecomesAdvanced() {
        // convertCurrentProfileToAdvanced must actually change the profile
        // so that editorType() returns "advanced" — even for D-Flow profiles.
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test");
        QCOMPARE(f.profileManager.currentEditorType(), "dflow");

        f.profileManager.convertCurrentProfileToAdvanced();

        // After conversion, the profile must be "advanced"
        QCOMPARE(f.profileManager.currentEditorType(), "advanced");
        QVERIFY(!f.profileManager.isCurrentProfileRecipe());
        QVERIFY(f.profileManager.isProfileModified());
        // Frames should be preserved
        QCOMPARE(f.profileManager.frameCount(), 2);
    }

    void convertToAdvancedAdvancedProfileStaysAdvanced() {
        // An advanced profile should remain advanced after conversion
        McpTestFixture f;
        f.profileManager.createNewProfile("My Custom Profile");
        QCOMPARE(f.profileManager.currentEditorType(), "advanced");

        f.profileManager.convertCurrentProfileToAdvanced();

        QCOMPARE(f.profileManager.currentEditorType(), "advanced");
    }

    void convertToAdvancedPressureProfileBecomesAdvanced() {
        // Pressure profiles must also become "advanced" after conversion
        McpTestFixture f;
        f.profileManager.createNewPressureProfile("My Pressure");
        QCOMPARE(f.profileManager.currentEditorType(), "pressure");

        f.profileManager.convertCurrentProfileToAdvanced();

        // After conversion, profileType must be changed to settings_2c
        QCOMPARE(f.profileManager.currentEditorType(), "advanced");
        QVERIFY(!f.profileManager.isCurrentProfileRecipe());
    }

    // === Frame editing preserves editorType ===

    void addFramePreservesEditorType() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test");
        QCOMPARE(f.profileManager.currentEditorType(), "dflow");

        f.profileManager.addFrame();

        // editorType is derived from title — adding frames doesn't change it
        QCOMPARE(f.profileManager.currentEditorType(), "dflow");
        QVERIFY(f.profileManager.isProfileModified());
    }

    void deleteFramePreservesEditorType() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test");
        QCOMPARE(f.profileManager.currentEditorType(), "dflow");
        QCOMPARE(f.profileManager.frameCount(), 2);

        f.profileManager.deleteFrame(1);

        QCOMPARE(f.profileManager.currentEditorType(), "dflow");
        QCOMPARE(f.profileManager.frameCount(), 1);
    }

    void moveFramePreservesEditorType() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test");

        f.profileManager.moveFrameDown(0);

        QCOMPARE(f.profileManager.currentEditorType(), "dflow");
    }

    void setFramePropertyPreservesEditorType() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test");

        f.profileManager.setFrameProperty(0, "temperature", 90.0);

        QCOMPARE(f.profileManager.currentEditorType(), "dflow");
    }

    void duplicateFramePreservesEditorType() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test");

        f.profileManager.duplicateFrame(0);

        QCOMPARE(f.profileManager.currentEditorType(), "dflow");
    }

    // === getOrConvertRecipeParams for different editor types ===

    void getOrConvertRecipeParamsDFlowDerivesFromFrames() {
        // Renamed from ...ReturnsStoredParams. It no longer does, deliberately:
        // a stored recipe block is a cache, and the frames win. The old
        // short-circuit left finding REC-1 half-fixed — every profile that
        // already carried a fabricated block, including the five shipped A-Flow
        // built-ins, kept showing the stale numbers because `prep` never ran.
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test", 36.0, 93.0, /*withInfuse=*/true);

        QVariantMap params = f.profileManager.getOrConvertRecipeParams();

        QCOMPARE(params["editorType"].toString(), "dflow");
        // Profile-level, so it comes through either way.
        QCOMPARE(params["targetWeight"].toDouble(), 36.0);
        // Frame-derived: the fixture's Infusing frame, not the stored block
        // (which carries none of these).
        QCOMPARE(params["infusePressure"].toDouble(), 3.0);
        QCOMPARE(params["infuseTime"].toDouble(), 20.0);
        QCOMPARE(params["infuseWeight"].toDouble(), 4.0);
    }

    void storedRecipeBlockNeverOverridesTheFrames() {
        // The spec requirement, asserted directly: "WHEN a profile carries a
        // recipe block whose values contradict its frames, THEN the parameters
        // used are those derived from the frames."
        //
        // This is the shape of the five shipped A-Flow built-ins, whose
        // byte-identical blocks claim 88 C / 20 s / 9 bar against frames saying
        // 93 / 60 / 10.
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Contradictory", 36.0, 93.0, /*withInfuse=*/true);

        Profile p = f.profileManager.currentProfile();
        RecipeParams stale;                 // deliberately disagrees with the frames
        stale.editorType = EditorType::DFlow;
        stale.infusePressure = 9.9;
        stale.infuseTime = 1.0;
        stale.fillTemperature = 60.0;
        p.setRecipeParams(stale);
        QVERIFY(f.profileManager.loadProfileFromJson(
            QString::fromUtf8(QJsonDocument(p.toJsonObject()).toJson(QJsonDocument::Compact))));

        const QVariantMap params = f.profileManager.getOrConvertRecipeParams();
        QCOMPARE(params["infusePressure"].toDouble(), 3.0);    // frame, not 9.9
        QCOMPARE(params["infuseTime"].toDouble(), 20.0);       // frame, not 1.0
        QCOMPARE(params["fillTemperature"].toDouble(), 93.0);  // frame, not 60.0
    }

    void getOrConvertRecipeParamsDFlowNoStoredExtractsFromFrames() {
        // D-Flow profile without stored recipe params (de1app import)
        // Should extract params from frames on-the-fly.
        //
        // The fixture is three frames — Filling / Infusing / Pouring — because
        // that is what a D-Flow profile IS. The plugin's `prep` reads indices
        // 0/1/2 with no pattern matching, so a two-frame profile has no pour
        // frame to read and the extraction has nothing to do. This used to be a
        // two-frame fixture asserting `pourFlow > 0`, which the struct default
        // of 2.0 satisfied on its own: it would have passed with extraction
        // deleted entirely. Assert the frames' own values instead.
        QJsonObject json;
        json["title"] = "D-Flow / Import";
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
        // No "recipe" block — simulates de1app import
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
        QJsonObject frameSoak;
        frameSoak["name"] = "infuse";
        frameSoak["temperature"] = 90.5;
        frameSoak["sensor"] = "coffee";
        frameSoak["pump"] = "pressure";
        frameSoak["transition"] = "fast";
        frameSoak["pressure"] = 3.5;
        frameSoak["flow"] = 8.0;
        frameSoak["seconds"] = 22.0;
        frameSoak["volume"] = 70.0;
        frameSoak["weight"] = 1.5;
        frameSoak["exit"] = QJsonObject{{"type", "pressure"}, {"condition", "over"}, {"value", 4.0}};
        frameSoak["limiter"] = QJsonObject{{"value", 0.0}, {"range", 0.6}};
        steps.append(frameSoak);
        QJsonObject frame2;
        frame2["name"] = "pour";
        frame2["temperature"] = 91.5;
        frame2["sensor"] = "coffee";
        frame2["pump"] = "flow";
        frame2["transition"] = "smooth";
        frame2["pressure"] = 6.0;
        frame2["flow"] = 2.4;
        frame2["seconds"] = 60.0;
        frame2["volume"] = 0.0;
        frame2["exit"] = QJsonObject{{"type", "pressure"}, {"condition", "over"}, {"value", 11.0}};
        frame2["limiter"] = QJsonObject{{"value", 8.5}, {"range", 0.6}};
        steps.append(frame2);
        json["steps"] = steps;

        McpTestFixture f;
        f.profileManager.loadProfileFromJson(QJsonDocument(json).toJson(QJsonDocument::Compact));

        QVariantMap params = f.profileManager.getOrConvertRecipeParams();
        QCOMPARE(params["editorType"].toString(), "dflow");

        // plugin.tcl:195-210 — filling(0), soaking(1), pouring(2), by index.
        QCOMPARE(params["fillTemperature"].toDouble(), 93.0);
        QCOMPARE(params["infusePressure"].toDouble(), 3.5);
        QCOMPARE(params["infuseTime"].toDouble(), 22.0);
        QCOMPARE(params["infuseVolume"].toDouble(), 70.0);
        QCOMPARE(params["infuseWeight"].toDouble(), 1.5);
        QCOMPARE(params["pourFlow"].toDouble(), 2.4);
        QCOMPARE(params["pourTemperature"].toDouble(), 91.5);
        // Pour pressure is the LIMITER, not the frame's pressure setpoint.
        QCOMPARE(params["pourPressure"].toDouble(), 8.5);
    }

    void getOrConvertRecipeParamsPressureReturnsScalarFields() {
        McpTestFixture f;
        f.profileManager.createNewPressureProfile("My Pressure");

        QVariantMap params = f.profileManager.getOrConvertRecipeParams();

        QCOMPARE(params["editorType"].toString(), "pressure");
        // Should come from scalar fields, not stored recipe params
        QVERIFY(params["targetWeight"].toDouble() > 0);
        QVERIFY(params["fillTemperature"].toDouble() > 0);
    }

    void getOrConvertRecipeParamsFlowReturnsScalarFields() {
        McpTestFixture f;
        f.profileManager.createNewFlowProfile("My Flow");

        QVariantMap params = f.profileManager.getOrConvertRecipeParams();

        QCOMPARE(params["editorType"].toString(), "flow");
        QVERIFY(params["targetWeight"].toDouble() > 0);
    }

    void getOrConvertRecipeParamsAdvancedReturnsDefaults() {
        McpTestFixture f;
        f.profileManager.createNewProfile("Advanced Profile");

        QVariantMap params = f.profileManager.getOrConvertRecipeParams();

        // Advanced profiles return default RecipeParams
        QVERIFY(!params.isEmpty());
    }

    // === uploadRecipeProfile frame regeneration ===

    void uploadRecipeProfileRegeneratesFramesOnParamChange() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Test", 36.0, 93.0, /*withInfuse=*/true);

        QVariantMap recipe;
        recipe["editorType"] = "dflow";
        recipe["targetWeight"] = 40.0;
        recipe["fillTemperature"] = 95.0;
        recipe["pourTemperature"] = 95.0;
        recipe["infusePressure"] = 8.0;  // Changed from 6.0
        recipe["pourFlow"] = 2.5;     // Changed from 2.0
        f.profileManager.uploadRecipeProfile(recipe);

        // Assert the frames were REGENERATED, by checking that the changed
        // params reached them — not merely that some frames exist.
        //
        // The earlier version of this test captured frameCount() before the
        // upload and never compared against it. The comparison was clearly
        // intended and never written, and -Werror on the unused variable is
        // the only reason anyone looked. A count comparison would have been
        // the weaker check anyway: D-Flow emits the same three frames for a
        // scalar change, so a correct regeneration and a no-op are identical
        // by count. The frame CONTENT is what distinguishes them.
        QCOMPARE(f.profileManager.frameCount(), 3);

        const QVariantMap filling = f.profileManager.getFrameAt(0);
        const QVariantMap pouring = f.profileManager.getFrameAt(2);
        QCOMPARE(filling["name"].toString(), QStringLiteral("Filling"));
        QCOMPARE(pouring["name"].toString(), QStringLiteral("Pouring"));

        // pourFlow 2.0 -> 2.5 is the change under test; it lands on the
        // flow-controlled Pouring frame.
        QCOMPARE(pouring["pump"].toString(), QStringLiteral("flow"));
        QCOMPARE(pouring["flow"].toDouble(), 2.5);
        // The fill frame's flow is a field update_D-Flow never writes, so it must
        // survive the regeneration carrying the SOURCE profile's value.
        //
        // This asserted 8.0 until the review caught it. 8.0 is
        // RecipeGenerator::createFillFrame's own hardcoded literal, and the
        // fixture was two frames — for which roleIndex returns -1 for every role
        // (n < 3), so the restore loop is skipped entirely. The assertion passed
        // whether restoreFieldsThePluginNeverWrites worked, was broken, or was
        // deleted. Three frames, and 4.0 from the fixture, makes it discriminate.
        QCOMPARE(filling["flow"].toDouble(), 4.0);
        QCOMPARE(pouring["temperature"].toDouble(), 95.0);

        QCOMPARE(f.profileManager.profileTargetWeight(), 40.0);
    }

    void uploadRecipeProfileSimpleProfileUsesScalarPath() {
        // Pressure profiles (settings_2a) should use the simple path
        McpTestFixture f;
        f.profileManager.createNewPressureProfile("My Pressure");
        QCOMPARE(f.profileManager.currentEditorType(), "pressure");

        QVariantMap recipe;
        recipe["editorType"] = "pressure";
        recipe["targetWeight"] = 40.0;
        recipe["fillTemperature"] = 95.0;
        recipe["pourTemperature"] = 95.0;
        recipe["espressoPressure"] = 9.0;
        recipe["pressureEnd"] = 6.0;
        recipe["preinfusionTime"] = 5.0;
        recipe["preinfusionFlowRate"] = 4.0;
        recipe["preinfusionStopPressure"] = 4.0;
        recipe["holdTime"] = 10.0;
        recipe["simpleDeclineTime"] = 15.0;
        f.profileManager.uploadRecipeProfile(recipe);

        QCOMPARE(f.profileManager.profileTargetWeight(), 40.0);
        // Should still be pressure type (simple path doesn't change profileType)
        QCOMPARE(f.profileManager.currentEditorType(), "pressure");
    }

    // === isCurrentProfileRecipe for all editor types ===

    void isCurrentProfileRecipeForAllTypes() {
        McpTestFixture f;

        // D-Flow → recipe
        loadDFlowProfile(f, "D-Flow / Test");
        QVERIFY(f.profileManager.isCurrentProfileRecipe());

        // A-Flow → recipe
        f.profileManager.createNewAFlowRecipe("A-Flow / Test");
        QVERIFY(f.profileManager.isCurrentProfileRecipe());

        // Pressure → recipe
        f.profileManager.createNewPressureProfile("My Pressure");
        QVERIFY(f.profileManager.isCurrentProfileRecipe());

        // Flow → recipe
        f.profileManager.createNewFlowProfile("My Flow");
        QVERIFY(f.profileManager.isCurrentProfileRecipe());

        // Advanced → NOT recipe
        f.profileManager.createNewProfile("Advanced");
        QVERIFY(!f.profileManager.isCurrentProfileRecipe());
    }

    // =========================================================================
    // Auto-retry on failed profile uploads
    // =========================================================================
    //
    // Covers the retry state machine added to ProfileManager: a failed
    // DE1Device::profileUploaded(false, reason) signal arms
    // m_profileUploadRetryTimer with exponential backoff (1s, 2s, 4s, 8s),
    // gives up after 5 consecutive failures, and sets the
    // de1CommunicationFailure flag so QML can surface the
    // power-cycle-the-DE1 dialog. See profilemanager.cpp kMax*Retry constants.
    //
    // Tests drive the state machine by calling uploadCurrentProfile() (which
    // emits the BLE writes through MockTransport) and then synthesising the
    // failure outcome via `emit f.device.profileUploaded(false, reason)` —
    // we don't need to plumb through the real DE1Device::finishProfileUpload
    // path because it's exercised in tst_profileupload.
    //
    // The retry timer is inspected via friend access rather than waiting for
    // real elapsed time (which would be 15s of dead air to exercise all 4
    // retries).

    void failedUploadWithRetryableReasonArmsTimer() {
        McpTestFixture f;
        loadDFlowProfile(f);
        QVERIFY(!f.profileManager.m_profileUploadRetryTimer.isActive());
        QCOMPARE(f.profileManager.m_profileUploadRetryAttempts, 0);

        f.profileManager.uploadCurrentProfile();

        // First failure with a retryable reason.
        emit f.device.profileUploaded(false,
            QStringLiteral("frame sequence mismatch (expected [0x00], got [0x01])"));

        QVERIFY(f.profileManager.m_profileUploadRetryTimer.isActive());
        QCOMPARE(f.profileManager.m_profileUploadRetryAttempts, 1);
        QCOMPARE(f.profileManager.m_profileUploadRetryTimer.interval(), 1000);
        QVERIFY(!f.profileManager.de1CommunicationFailure());
    }

    void retryBacksOffExponentiallyCappedAt8s() {
        McpTestFixture f;
        loadDFlowProfile(f);
        f.profileManager.uploadCurrentProfile();

        const QString reason =
            QStringLiteral("timeout waiting for write ACKs");

        // Attempts 1..4 arm the timer with delays 1s, 2s, 4s, 8s.
        const int expectedDelays[4] = {1000, 2000, 4000, 8000};
        for (int i = 0; i < 4; ++i) {
            emit f.device.profileUploaded(false, reason);
            QVERIFY2(f.profileManager.m_profileUploadRetryTimer.isActive(),
                qPrintable(QString("timer must be armed after failure %1").arg(i + 1)));
            QCOMPARE(f.profileManager.m_profileUploadRetryAttempts, i + 1);
            QCOMPARE(f.profileManager.m_profileUploadRetryTimer.interval(), expectedDelays[i]);
        }
    }

    void retryResetsOnSuccess() {
        McpTestFixture f;
        loadDFlowProfile(f);
        f.profileManager.uploadCurrentProfile();

        emit f.device.profileUploaded(false,
            QStringLiteral("frame sequence mismatch (expected [0x00], got [0x01])"));
        QCOMPARE(f.profileManager.m_profileUploadRetryAttempts, 1);
        QVERIFY(f.profileManager.m_profileUploadRetryTimer.isActive());

        emit f.device.profileUploaded(true, QString());
        QCOMPARE(f.profileManager.m_profileUploadRetryAttempts, 0);
        QVERIFY(!f.profileManager.m_profileUploadRetryTimer.isActive());
        QVERIFY(f.profileManager.m_lastUploadFailureReason.isEmpty());
    }

    void fiveConsecutiveFailuresSetCommunicationFailureFlag() {
        McpTestFixture f;
        loadDFlowProfile(f);
        // The 5th failure logs a qWarning — expected in this test.
        ScopedWarningFilter filter(
            "profile upload failed .* consecutive times");
        f.profileManager.uploadCurrentProfile();

        QSignalSpy flagSpy(&f.profileManager,
            &ProfileManager::de1CommunicationFailureChanged);

        const QString reason = QStringLiteral("timeout waiting for write ACKs");
        for (int i = 0; i < 5; ++i) {
            emit f.device.profileUploaded(false, reason);
        }

        QVERIFY2(f.profileManager.de1CommunicationFailure(),
            "de1CommunicationFailure must flip true after 5 retryable failures");
        QCOMPARE(flagSpy.count(), 1);
        // Timer must NOT still be running — there's no retry #6.
        QVERIFY(!f.profileManager.m_profileUploadRetryTimer.isActive());
    }

    void acknowledgeClearsCommunicationFailureAndResetsRetry() {
        McpTestFixture f;
        loadDFlowProfile(f);
        ScopedWarningFilter filter(
            "profile upload failed .* consecutive times");
        f.profileManager.uploadCurrentProfile();

        const QString reason = QStringLiteral("timeout waiting for write ACKs");
        for (int i = 0; i < 5; ++i) {
            emit f.device.profileUploaded(false, reason);
        }
        QVERIFY(f.profileManager.de1CommunicationFailure());

        QSignalSpy flagSpy(&f.profileManager,
            &ProfileManager::de1CommunicationFailureChanged);
        f.profileManager.acknowledgeDe1CommunicationFailure();

        QVERIFY(!f.profileManager.de1CommunicationFailure());
        QCOMPARE(flagSpy.count(), 1);
        QCOMPARE(f.profileManager.m_profileUploadRetryAttempts, 0);
        QVERIFY(f.profileManager.m_lastUploadFailureReason.isEmpty());
    }

    void supersededFailureDoesNotArmRetry() {
        McpTestFixture f;
        loadDFlowProfile(f);
        f.profileManager.uploadCurrentProfile();

        emit f.device.profileUploaded(false,
            QStringLiteral("superseded by a new upload"));

        QVERIFY(!f.profileManager.m_profileUploadRetryTimer.isActive());
        QCOMPARE(f.profileManager.m_profileUploadRetryAttempts, 0);
    }

    void bleDisconnectFailureDoesNotArmRetry() {
        // The reconnect path (initialSettingsComplete -> applyAllSettings ->
        // uploadCurrentProfile) handles this; the retry timer must not race
        // with it.
        McpTestFixture f;
        loadDFlowProfile(f);
        f.profileManager.uploadCurrentProfile();

        emit f.device.profileUploaded(false,
            QStringLiteral("BLE disconnect during upload"));

        QVERIFY(!f.profileManager.m_profileUploadRetryTimer.isActive());
        QCOMPARE(f.profileManager.m_profileUploadRetryAttempts, 0);
    }

    void queueClearFailureDoesNotArmRetry() {
        McpTestFixture f;
        loadDFlowProfile(f);
        f.profileManager.uploadCurrentProfile();

        emit f.device.profileUploaded(false,
            QStringLiteral("command queue cleared during upload"));

        QVERIFY(!f.profileManager.m_profileUploadRetryTimer.isActive());
        QCOMPARE(f.profileManager.m_profileUploadRetryAttempts, 0);
    }

    void transportDisconnectResetsRetryState() {
        McpTestFixture f;
        loadDFlowProfile(f);
        f.profileManager.uploadCurrentProfile();

        // Arm the retry via a retryable failure.
        emit f.device.profileUploaded(false,
            QStringLiteral("timeout waiting for write ACKs"));
        QVERIFY(f.profileManager.m_profileUploadRetryTimer.isActive());
        QCOMPARE(f.profileManager.m_profileUploadRetryAttempts, 1);

        // Simulate the transport dropping — DE1Device::onTransportDisconnected
        // fires, which emits connectedChanged. ProfileManager's handler must
        // clear the retry state so the reconnect path starts from attempt 0.
        f.transport.setConnectedSim(false);

        QVERIFY(!f.profileManager.m_profileUploadRetryTimer.isActive());
        QCOMPARE(f.profileManager.m_profileUploadRetryAttempts, 0);
    }

    void loadProfileResetsRetryState() {
        McpTestFixture f;
        loadDFlowProfile(f, "First");

        f.profileManager.uploadCurrentProfile();
        emit f.device.profileUploaded(false,
            QStringLiteral("timeout waiting for write ACKs"));
        QVERIFY(f.profileManager.m_profileUploadRetryTimer.isActive());
        QCOMPARE(f.profileManager.m_profileUploadRetryAttempts, 1);

        // User switches profiles — attempt counter must reset so the new
        // profile gets its own fresh 5-attempt budget.
        loadDFlowProfile(f, "Second");

        QVERIFY(!f.profileManager.m_profileUploadRetryTimer.isActive());
        QCOMPARE(f.profileManager.m_profileUploadRetryAttempts, 0);
    }

    void retryTimerFiringDuringActivePhaseDefersToPendingFlag() {
        // If the retry timer fires while the machine is in an active phase
        // (shot in progress), uploadCurrentProfile() hits the active-phase
        // guard, sets m_profileUploadPending = true, and returns without
        // attempting a BLE write. The phaseChanged handler must resume the
        // upload once the phase becomes Idle/Ready — and the retry counter
        // must stay intact so the 5-attempt budget carries across the
        // active-phase gap.
        McpTestFixture f;
        loadDFlowProfile(f);
        ScopedWarningFilter filter("BLOCKED during active phase|^  #");

        // Prime: one retryable failure arms the retry timer and sets
        // attempts=1.
        f.profileManager.uploadCurrentProfile();
        emit f.device.profileUploaded(false,
            QStringLiteral("timeout waiting for write ACKs"));
        QVERIFY(f.profileManager.m_profileUploadRetryTimer.isActive());
        QCOMPARE(f.profileManager.m_profileUploadRetryAttempts, 1);

        // Simulate the machine entering an active phase, then directly
        // invoke the retry timer's uploadCurrentProfile() call (rather than
        // waiting 1000 ms of real time).
        f.machineState.m_phase = MachineState::Phase::Pouring;
        f.transport.clearWrites();
        f.profileManager.uploadCurrentProfile();

        // The attempt was blocked: no BLE writes, pending flag set,
        // retry counter unchanged (blocked attempts don't consume budget).
        QVERIFY2(f.writesTo(HEADER_WRITE).isEmpty(),
            "Blocked attempt must not write profile header to BLE");
        QVERIFY(f.profileManager.m_profileUploadPending);
        QCOMPARE(f.profileManager.m_profileUploadRetryAttempts, 1);

        // Phase returns to Idle — the pending handler resumes the upload.
        f.machineState.m_phase = MachineState::Phase::Idle;
        emit f.machineState.phaseChanged();

        QVERIFY2(!f.writesTo(HEADER_WRITE).isEmpty(),
            "phaseChanged must resume the pending upload");
        QVERIFY(!f.profileManager.m_profileUploadPending);

        // If the resumed upload now succeeds, the retry state resets cleanly.
        emit f.device.profileUploaded(true, QString());
        QCOMPARE(f.profileManager.m_profileUploadRetryAttempts, 0);
        QVERIFY(!f.profileManager.m_profileUploadRetryTimer.isActive());
    }

    // =========================================================================
    // profileUploadRetrying Q_PROPERTY lifecycle (issue #750)
    // =========================================================================
    //
    // QML binds a "Reconnecting…" toast to this property, so it must flip true
    // within the same tick that the retry timer arms, and flip false cleanly
    // on every exit path (success, exhaustion, disconnect, profile switch,
    // acknowledge). The NOTIFY signal must fire exactly once per transition
    // — no spurious emissions, no missed edges.

    void profileUploadRetryingFlipsTrueOnFirstFailure() {
        McpTestFixture f;
        loadDFlowProfile(f);
        QVERIFY(!f.profileManager.profileUploadRetrying());

        QSignalSpy spy(&f.profileManager,
            &ProfileManager::profileUploadRetryingChanged);

        f.profileManager.uploadCurrentProfile();
        emit f.device.profileUploaded(false,
            QStringLiteral("timeout waiting for write ACKs"));

        QVERIFY(f.profileManager.profileUploadRetrying());
        QCOMPARE(spy.count(), 1);
    }

    void profileUploadRetryingClearsOnSuccess() {
        McpTestFixture f;
        loadDFlowProfile(f);
        f.profileManager.uploadCurrentProfile();
        emit f.device.profileUploaded(false,
            QStringLiteral("timeout waiting for write ACKs"));
        QVERIFY(f.profileManager.profileUploadRetrying());

        QSignalSpy spy(&f.profileManager,
            &ProfileManager::profileUploadRetryingChanged);
        emit f.device.profileUploaded(true, QString());

        QVERIFY(!f.profileManager.profileUploadRetrying());
        QCOMPARE(spy.count(), 1);
    }

    void profileUploadRetryingClearsOnExhaustion() {
        McpTestFixture f;
        loadDFlowProfile(f);
        ScopedWarningFilter filter(
            "profile upload failed .* consecutive times");
        f.profileManager.uploadCurrentProfile();

        const QString reason = QStringLiteral("timeout waiting for write ACKs");
        // Attempts 1..4 leave the flag true; the 5th exhausts the budget
        // and the flag must flip back to false so the toast yields to the
        // exhaustion dialog.
        for (int i = 0; i < 4; ++i) {
            emit f.device.profileUploaded(false, reason);
            QVERIFY(f.profileManager.profileUploadRetrying());
        }
        emit f.device.profileUploaded(false, reason);

        QVERIFY(f.profileManager.de1CommunicationFailure());
        QVERIFY2(!f.profileManager.profileUploadRetrying(),
            "exhaustion must clear profileUploadRetrying — the dialog takes over");
    }

    void profileUploadRetryingClearsOnDisconnect() {
        McpTestFixture f;
        loadDFlowProfile(f);
        f.profileManager.uploadCurrentProfile();
        emit f.device.profileUploaded(false,
            QStringLiteral("timeout waiting for write ACKs"));
        QVERIFY(f.profileManager.profileUploadRetrying());

        f.transport.setConnectedSim(false);

        QVERIFY(!f.profileManager.profileUploadRetrying());
    }

    void profileUploadRetryingClearsOnAcknowledge() {
        McpTestFixture f;
        loadDFlowProfile(f);
        ScopedWarningFilter filter(
            "profile upload failed .* consecutive times");
        f.profileManager.uploadCurrentProfile();
        const QString reason = QStringLiteral("timeout waiting for write ACKs");
        for (int i = 0; i < 5; ++i) {
            emit f.device.profileUploaded(false, reason);
        }
        QVERIFY(!f.profileManager.profileUploadRetrying());

        // Acknowledge is a clean no-op for the retrying flag (already false
        // at exhaustion) — but it must not regress to true.
        QSignalSpy spy(&f.profileManager,
            &ProfileManager::profileUploadRetryingChanged);
        f.profileManager.acknowledgeDe1CommunicationFailure();

        QVERIFY(!f.profileManager.profileUploadRetrying());
        QCOMPARE(spy.count(), 0);
    }

    void profileUploadRetryingClearsOnProfileSwitch() {
        McpTestFixture f;
        loadDFlowProfile(f, "First");
        f.profileManager.uploadCurrentProfile();
        emit f.device.profileUploaded(false,
            QStringLiteral("timeout waiting for write ACKs"));
        QVERIFY(f.profileManager.profileUploadRetrying());

        loadDFlowProfile(f, "Second");

        QVERIFY(!f.profileManager.profileUploadRetrying());
    }

    void retryableFailureDuringShotStopsShot() {
        // If the DE1 is mid-shot (user pressed the group-head button) while a
        // profile upload fails with a retryable reason, the machine is
        // running on stale frames. ProfileManager must stop the shot
        // immediately (same behaviour as aborting when no scale is
        // connected) and emit shotAbortedProfileUploadRetrying so the UI
        // can surface the reason.
        McpTestFixture f;
        loadDFlowProfile(f);
        f.profileManager.uploadCurrentProfile();

        f.machineState.m_phase = MachineState::Phase::Pouring;
        QSignalSpy abortSpy(&f.profileManager,
            &ProfileManager::shotAbortedProfileUploadRetrying);
        f.transport.clearWrites();

        ScopedWarningFilter filter(
            "aborting in-progress shot because profile upload is retrying");
        emit f.device.profileUploaded(false,
            QStringLiteral("timeout waiting for write ACKs"));

        QCOMPARE(abortSpy.count(), 1);
        // requestState(Idle) goes through the transport as a state-request
        // write — verifying the signal is sufficient to confirm the abort
        // path ran; the transport-level assertion is covered by other tests.
    }

    void retryableFailureOutsideShotDoesNotEmitAbort() {
        McpTestFixture f;
        loadDFlowProfile(f);
        f.profileManager.uploadCurrentProfile();

        QSignalSpy abortSpy(&f.profileManager,
            &ProfileManager::shotAbortedProfileUploadRetrying);
        // Idle phase: no shot to stop.
        emit f.device.profileUploaded(false,
            QStringLiteral("timeout waiting for write ACKs"));

        QCOMPARE(abortSpy.count(), 0);
    }

    void profileUploadRetryingDoesNotFireForNonRetryableFailure() {
        McpTestFixture f;
        loadDFlowProfile(f);
        f.profileManager.uploadCurrentProfile();

        QSignalSpy spy(&f.profileManager,
            &ProfileManager::profileUploadRetryingChanged);
        emit f.device.profileUploaded(false,
            QStringLiteral("superseded by a new upload"));

        QVERIFY(!f.profileManager.profileUploadRetrying());
        QCOMPARE(spy.count(), 0);
    }

    // ===== Auto-load entry point =====

    void autoLoadEmptyFilenameIsNoOp() {
        McpTestFixture f;
        f.settings.app()->setAutoLoadProfileFilename("");
        QSignalSpy staleSpy(&f.profileManager, &ProfileManager::autoLoadStaleCleared);
        QSignalSpy loadSpy(&f.profileManager, &ProfileManager::currentProfileChanged);

        f.profileManager.loadAutoLoadProfileIfNeeded();

        QCOMPARE(staleSpy.count(), 0);
        QCOMPARE(loadSpy.count(), 0);
    }

    void autoLoadStaleFilenameClears() {
        McpTestFixture f;
        f.settings.app()->setAutoLoadProfileFilename("nonexistent-profile-xyz");
        QSignalSpy staleSpy(&f.profileManager, &ProfileManager::autoLoadStaleCleared);

        f.profileManager.loadAutoLoadProfileIfNeeded();

        QCOMPARE(staleSpy.count(), 1);
        QCOMPARE(f.settings.app()->autoLoadProfileFilename(), QString(""));
    }

    void autoLoadAlreadyActiveDoesNotReload() {
        McpTestFixture f;
        loadDFlowProfile(f, "D-Flow / Active");
        // Manually drive base name to mimic a previously saved profile name.
        const QString baseName = f.profileManager.baseProfileName();
        // If baseName is empty (JSON load doesn't set it), the test would fail
        // for an unrelated reason — only continue if the precondition holds.
        if (baseName.isEmpty()) {
            QSKIP("baseProfileName not set after JSON load; not a valid precondition for this test");
        }
        f.settings.app()->setAutoLoadProfileFilename(baseName);

        QSignalSpy loadSpy(&f.profileManager, &ProfileManager::currentProfileChanged);
        f.profileManager.loadAutoLoadProfileIfNeeded();

        // No additional currentProfileChanged emissions — the auto-load was a no-op.
        QCOMPARE(loadSpy.count(), 0);
    }

    void eagerClearOnAddHiddenProfile() {
        // Hiding the pinned profile must clear the auto-load setting eagerly
        // so the UI strip disappears immediately.
        McpTestFixture f;
        const QString filename = "test-user-profile";
        // McpTestFixture uses real QSettings; ensure the precondition (profile
        // not yet hidden) so addHiddenProfile actually mutates state. Otherwise
        // a stale entry from a previous run short-circuits the eager-clear.
        f.settings.app()->removeHiddenProfile(filename);
        f.settings.app()->setAutoLoadProfileFilename(filename);
        QCOMPARE(f.settings.app()->autoLoadProfileFilename(), filename);

        f.settings.app()->addHiddenProfile(filename);

        QCOMPARE(f.settings.app()->autoLoadProfileFilename(), QString(""));

        // Cleanup so subsequent test runs start from a known state.
        f.settings.app()->removeHiddenProfile(filename);
    }

    void eagerClearOnRemoveSelectedBuiltIn() {
        McpTestFixture f;
        const QString filename = "test-builtin-profile";
        f.settings.app()->removeSelectedBuiltInProfile(filename);
        f.settings.app()->addSelectedBuiltInProfile(filename);
        f.settings.app()->setAutoLoadProfileFilename(filename);

        f.settings.app()->removeSelectedBuiltInProfile(filename);

        QCOMPARE(f.settings.app()->autoLoadProfileFilename(), QString(""));
    }

    // Deleting a profile is the one lifecycle event that changes what a TITLE
    // resolves to without changing what is loaded — currentProfileChanged does
    // not fire, so nothing downstream noticed and a recipe pinned to the
    // deleted profile stayed active. The signal carries the title, not the
    // filename, because that is what recipes and shots reference.
    void profileDeletedCarriesTheTitle() {
        McpTestFixture f;
        const QString filename = "zz-deleted-profile-signal";
        const QString title = "ZZ Deleted Profile Signal";
        const QString path = f.profileManager.userProfilesPath() + "/" + filename + ".json";
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(QJsonDocument(makeDFlowJson(title)).toJson());
        out.close();
        f.profileManager.refreshProfiles();

        QSignalSpy deletedSpy(&f.profileManager, &ProfileManager::profileDeleted);
        QVERIFY(f.profileManager.deleteProfile(filename));
        QCOMPARE(deletedSpy.count(), 1);
        // The TITLE, captured before the catalog was rebuilt — after
        // refreshProfiles() there is nothing left to resolve the filename against.
        QCOMPARE(deletedSpy.at(0).at(0).toString(), title);
    }

    // The signal's real contract: it fires only when the TITLE stops resolving.
    //
    // This is asserted through the title, NOT through deleteProfile's built-in
    // early return, because that return is not reachable the way it looks.
    // refreshProfiles() classifies everything ProfileStorage lists as
    // UserCreated and replaces the built-in catalog row with it, so a file
    // shadowing a built-in takes the ORDINARY delete path and would emit —
    // while the QRC built-in is restored under the same title and still
    // resolves. Gating the emit on findProfileByTitle covers both shapes; a
    // test of the early return alone covers neither.
    void deletingAShadowingProfileDoesNotAnnounceADeletion() {
        McpTestFixture f;
        // A title carried by a built-in, so that after the delete the QRC
        // version resolves it again.
        const QVariantList all = f.profileManager.allProfilesList();
        QString builtInTitle;
        for (const QVariant& v : all) {
            const QVariantMap m = v.toMap();
            if (f.profileManager.isBuiltInFilename(m.value("filename").toString())) {
                builtInTitle = m.value("title").toString();
                break;
            }
        }
        if (builtInTitle.isEmpty())
            QSKIP("no built-in profile in the catalog to shadow");

        // A DIFFERENTLY-named file carrying the built-in's title — the shape
        // the early return cannot catch, since its filename is not a built-in.
        const QString filename = "zz-shadowing-a-builtin-title";
        const QString path = f.profileManager.userProfilesPath() + "/" + filename + ".json";
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(QJsonDocument(makeDFlowJson(builtInTitle)).toJson());
        out.close();
        f.profileManager.refreshProfiles();

        QSignalSpy deletedSpy(&f.profileManager, &ProfileManager::profileDeleted);
        QVERIFY(f.profileManager.deleteProfile(filename));
        // Nothing announced: the title still resolves, so no recipe naming it
        // has broken and none should be deactivated.
        QVERIFY(!f.profileManager.findProfileByTitle(builtInTitle).isEmpty());
        QCOMPARE(deletedSpy.count(), 0);
    }

    // Cleaning up a local override of a BUILT-IN profile is not a deletion in
    // the sense that matters: the title still resolves afterwards, to the
    // built-in version, so nothing pointing at it has broken and no recipe
    // should be deactivated. deleteProfile returns false on that path, before
    // the emit — this asserts the emit really is behind that return.
    void builtInOverrideCleanupDoesNotAnnounceADeletion() {
        McpTestFixture f;
        // A filename that exists as a built-in QRC resource, so the source is
        // BuiltIn and the early return applies. Skip rather than assert if the
        // bundled set ever changes: the point is the branch, not this profile.
        const QVariantList all = f.profileManager.allProfilesList();
        QString builtInFilename;
        for (const QVariant& v : all) {
            const QVariantMap m = v.toMap();
            if (f.profileManager.isBuiltInFilename(m.value("filename").toString())) {
                builtInFilename = m.value("filename").toString();
                break;
            }
        }
        if (builtInFilename.isEmpty())
            QSKIP("no built-in profile in the catalog to exercise the override branch");

        // Write a local override so there is something for deleteProfile to
        // remove — without one it returns false having done nothing, which
        // would pass this test for the wrong reason.
        const QString path = f.profileManager.userProfilesPath() + "/" + builtInFilename + ".json";
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(QJsonDocument(makeDFlowJson("ZZ Override Of A Built-In")).toJson());
        out.close();
        f.profileManager.refreshProfiles();

        QSignalSpy deletedSpy(&f.profileManager, &ProfileManager::profileDeleted);
        // Returns false: a built-in can never be fully deleted.
        QVERIFY(!f.profileManager.deleteProfile(builtInFilename));
        QCOMPARE(deletedSpy.count(), 0);
    }

    // === Dial-in difference block (change: summarize-profile-changes-from-builtin) ===
    //
    // The QML-facing half. Base selection itself is covered in tst_shotsummarizer;
    // these assert the three outcomes a surface has to tell apart, and the rule
    // that a shot is compared against the profile IT was pulled with.

    // A retitled copy of a bundled profile, with `edit` applied to its JSON,
    // written into the user store and picked up by the catalog scan.
    static QJsonObject bundledJsonRetitled(const QString& file, const QString& title)
    {
        QFile f(QStringLiteral(":/profiles/") + file);
        if (!f.open(QIODevice::ReadOnly)) return {};
        QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        o[QStringLiteral("title")] = title;
        o[QStringLiteral("read_only")] = 0;
        return o;
    }

    static void setTempOnEveryStep(QJsonObject& o, const QString& temp)
    {
        QJsonArray steps = o[QStringLiteral("steps")].toArray();
        for (int i = 0; i < steps.size(); ++i) {
            QJsonObject st = steps[i].toObject();
            st[QStringLiteral("temperature")] = temp;
            steps[i] = st;
        }
        o[QStringLiteral("steps")] = steps;
    }

    static void writeUserProfile(McpTestFixture& f, const QString& fileName,
                                 const QJsonObject& json)
    {
        QFile out(f.profileManager.userProfilesPath() + "/" + fileName + ".json");
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(QJsonDocument(json).toJson());
        out.close();
        f.profileManager.refreshProfiles();
    }

    // Every dial-in `kind` and `unit` C++ can emit must be handled by the QML.
    //
    // The two lists are produced at different sites in different languages with
    // nothing connecting them, which is exactly the drift the centralization
    // rule targets, and the project already gates the same shape elsewhere
    // (tst_customwidgethtml::everyCatalogActionHasADispatchArm). Without this,
    // adding a dial-in field ships a raw identifier as a label, or a bare
    // unitless number, to a user — and both look finished, so nobody reports it.
    void profileDialInDiff_everyKindAndUnitIsHandledByTheQml()
    {
        QDir qmlDir(QCoreApplication::applicationDirPath() + "/../../../../qml");
        if (!qmlDir.exists())
            qmlDir.setPath(QString(SRCDIR) + "/../qml");
        if (!qmlDir.exists())
            QSKIP("QML directory not found — run from source tree");

        QFile block(qmlDir.absolutePath() + "/components/ProfileDialInDiffBlock.qml");
        QVERIFY2(block.open(QIODevice::ReadOnly | QIODevice::Text),
                 "ProfileDialInDiffBlock.qml missing");
        const QString qml = QString::fromUtf8(block.readAll());

        // Hardcoded rather than scraped from C++: the point is that adding a
        // field fails HERE until someone updates the QML too.
        const QStringList kinds{
            QStringLiteral("targetWeight"),    QStringLiteral("targetVolume"),
            QStringLiteral("maximumPressure"), QStringLiteral("maximumFlow"),
            QStringLiteral("minimumPressure"), QStringLiteral("tankTemperature"),
            QStringLiteral("espressoTemperature"), QStringLiteral("recommendedDose"),
            QStringLiteral("temperature"),     QStringLiteral("pressure"),
            QStringLiteral("flow"),            QStringLiteral("volume"),
            QStringLiteral("exitPressureOver"), QStringLiteral("exitPressureUnder"),
            QStringLiteral("exitFlowOver"),    QStringLiteral("exitFlowUnder"),
            QStringLiteral("exitWeight"),      QStringLiteral("maxFlowOrPressure"),
            QStringLiteral("name"),
        };
        QStringList missing;
        for (const QString& k : kinds)
            if (!qml.contains(QStringLiteral("\"%1\":").arg(k))) missing << k;
        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("dial-in kinds with no label in the QML: %1")
                                .arg(missing.join(QStringLiteral(", ")))));

        // "celsiusTank" is deliberately distinct from "celsius" in C++ so the
        // two can carry different tolerances; the QML must still format it as a
        // temperature rather than falling through to the raw-token suffix.
        // QStringLiteral, not bare "…": a const QString& bound to a temporary
        // built from a const char* is -Werror=range-loop-construct under GCC,
        // which clang does not diagnose — so this compiled clean on macOS and
        // broke the Linux release build. Every other range-for over strings in
        // this tree already does it this way.
        for (const QString& u : { QStringLiteral("celsius"), QStringLiteral("celsiusTank"),
                                  QStringLiteral("bar"), QStringLiteral("mlPerSec"),
                                  QStringLiteral("g"), QStringLiteral("ml") })
            QVERIFY2(qml.contains(QStringLiteral("\"%1\"").arg(u)),
                     qPrintable(QStringLiteral("unit token %1 is not formatted by the QML").arg(u)));

        // And the fallback stays honest: an unmapped token must reach the user
        // as a visible token, never as a bare number that looks finished.
        QVERIFY2(qml.contains(QStringLiteral("suffix = \" \" + row.unit")),
                 "the unmapped-unit fallback must append the raw token");

        // Display precision has to reach at least as fine as the tolerance the
        // row was EMITTED at, or the block renders a real change as two
        // identical numbers. celsius, bar and mlPerSec are compared at 0.005
        // (ProfileJson writes them at two decimals and the editor steps them at
        // 0.01), so the QML must be willing to spend a second decimal on them.
        const qsizetype fine = qml.indexOf(QStringLiteral("function maxDecimalsFor"));
        QVERIFY2(fine >= 0, "the block must pick its decimals from the row's unit");
        const QString fineBody = qml.mid(fine, 400);
        for (const QString& u : { QStringLiteral("celsius"), QStringLiteral("bar"),
                                  QStringLiteral("mlPerSec") })
            QVERIFY2(fineBody.contains(QStringLiteral("\"%1\"").arg(u)),
                     qPrintable(QStringLiteral("unit %1 is compared at 0.005 but is not granted "
                                               "two decimals of display").arg(u)));
    }

    // The lookup order in loadProfileByFilename is what makes an IN-PLACE edit of
    // a built-in work at all: the edit lands in the user folder under the
    // built-in's own filename, refreshProfiles() keeps the built-in catalog
    // entry, and only user-folder-before-:/profiles makes the diff read the
    // user's bytes. Reverse the list and every in-place edit reports "unchanged".
    void profileDialInDiff_readsTheUserCopyThatShadowsABuiltIn()
    {
        McpTestFixture f;
        QJsonObject json = bundledJsonRetitled(
            QStringLiteral("hybrid_pour_over_espresso.json"),
            QStringLiteral("Hybrid pour over espresso"));   // title UNCHANGED
        QVERIFY(!json.isEmpty());
        setTempOnEveryStep(json, QStringLiteral("95.00"));
        writeUserProfile(f, QStringLiteral("hybrid_pour_over_espresso"), json);

        const QVariantMap diff =
            f.profileManager.profileDialInDiff(QStringLiteral("Hybrid pour over espresso"));
        QVERIFY2(diff.value(QStringLiteral("hasBase")).toBool(),
                 "an in-place edit of a built-in must still find its base");
        QVERIFY2(!diff.value(QStringLiteral("unchanged")).toBool(),
                 "the shadowing user copy must be what gets compared");
        const QVariantList rows = diff.value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toMap().value(QStringLiteral("kind")).toString(),
                 QStringLiteral("temperature"));
    }

    void profileDialInDiff_namesTheBundledBaseAndListsTheEdit()
    {
        McpTestFixture f;
        QJsonObject json = bundledJsonRetitled(
            QStringLiteral("preinfuse_then_45ml_of_water.json"),
            QStringLiteral("Zzz Morning Variant"));
        QVERIFY(!json.isEmpty());
        setTempOnEveryStep(json, QStringLiteral("89.00"));
        writeUserProfile(f, QStringLiteral("zzz_morning_variant"), json);

        const QVariantMap diff =
            f.profileManager.profileDialInDiff(QStringLiteral("Zzz Morning Variant"));
        QVERIFY(diff.value(QStringLiteral("hasBase")).toBool());
        QCOMPARE(diff.value(QStringLiteral("baseTitle")).toString(),
                 QStringLiteral("Preinfuse then 45ml of water"));
        QVERIFY(!diff.value(QStringLiteral("unchanged")).toBool());

        const QVariantList rows = diff.value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.size(), 1);
        const QVariantMap row = rows.first().toMap();
        QCOMPARE(row.value(QStringLiteral("kind")).toString(), QStringLiteral("temperature"));
        QCOMPARE(row.value(QStringLiteral("oldValue")).toDouble(), 90.0);
        QCOMPARE(row.value(QStringLiteral("newValue")).toDouble(), 89.0);
    }

    // "Unchanged copy" must be distinguishable from "no base at all" — one says
    // the knowledge applies without qualification, the other says nothing can be
    // said. A single boolean conflating them would make the surface silent in
    // the case that most deserves a sentence.
    void profileDialInDiff_aRenamedCopyReportsUnchangedRatherThanNoBase()
    {
        McpTestFixture f;
        const QJsonObject json = bundledJsonRetitled(
            QStringLiteral("preinfuse_then_45ml_of_water.json"),
            QStringLiteral("Zzz Renamed Untouched"));
        QVERIFY(!json.isEmpty());
        writeUserProfile(f, QStringLiteral("zzz_renamed_untouched"), json);

        const QVariantMap diff =
            f.profileManager.profileDialInDiff(QStringLiteral("Zzz Renamed Untouched"));
        QVERIFY(diff.value(QStringLiteral("hasBase")).toBool());
        QVERIFY(diff.value(QStringLiteral("unchanged")).toBool());
        QVERIFY(diff.value(QStringLiteral("rows")).toList().isEmpty());
    }

    void profileDialInDiff_aTitleTheCatalogDoesNotHoldHasNoBase()
    {
        McpTestFixture f;
        const QVariantMap diff =
            f.profileManager.profileDialInDiff(QStringLiteral("Zzz Not Installed"));
        QVERIFY(!diff.value(QStringLiteral("hasBase")).toBool());
        QVERIFY(diff.value(QStringLiteral("rows")).toList().isEmpty());
    }

    // Unparseable JSON is "cannot be compared", not an error to announce: the
    // shot loader already warns where such a row is READ, and warning again on
    // every dialog open would double-report one defect. QTest::failOnWarning is
    // active, so a warning here fails this test.
    void profileDialInDiffForJson_unparseableJsonIsQuietlyNoBase()
    {
        McpTestFixture f;
        const QVariantMap diff =
            f.profileManager.profileDialInDiffForJson(QStringLiteral("{not json"));
        QVERIFY(!diff.value(QStringLiteral("hasBase")).toBool());
    }

    // A shot must report the profile it was PULLED with. Editing the catalog
    // copy afterwards must not rewrite what the shot's block says — this is the
    // whole reason the JSON entry point exists rather than a flag on the other.
    void profileDialInDiffForJson_readsTheShotsProfileNotTheCatalogs()
    {
        McpTestFixture f;
        QJsonObject catalogJson = bundledJsonRetitled(
            QStringLiteral("preinfuse_then_45ml_of_water.json"),
            QStringLiteral("Zzz Drifted"));
        QVERIFY(!catalogJson.isEmpty());
        setTempOnEveryStep(catalogJson, QStringLiteral("85.00"));   // edited since
        writeUserProfile(f, QStringLiteral("zzz_drifted"), catalogJson);

        QJsonObject shotJson = bundledJsonRetitled(
            QStringLiteral("preinfuse_then_45ml_of_water.json"),
            QStringLiteral("Zzz Drifted"));
        setTempOnEveryStep(shotJson, QStringLiteral("93.00"));      // as pulled

        const QVariantMap diff = f.profileManager.profileDialInDiffForJson(
            QString::fromUtf8(QJsonDocument(shotJson).toJson()));
        QVERIFY(diff.value(QStringLiteral("hasBase")).toBool());
        const QVariantList rows = diff.value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toMap().value(QStringLiteral("newValue")).toDouble(), 93.0);
    }
};

QTEST_GUILESS_MAIN(tst_ProfileManager)
#include "tst_profilemanager.moc"
