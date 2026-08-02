#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QDir>
#include <limits>

#include "mocks/McpTestFixture.h"
#include "ble/protocol/de1characteristics.h"
#include "core/dbutils.h"
#include "core/settings_visualizer.h"
#include "history/shothistorystorage.h"
#include "history/coffeebagstorage.h"
#include "history/equipmentstorage.h"
#include "history/recipestorage.h"
#include "core/settings_app.h"
#include "core/settings_dye.h"
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
class BeanBaseClient;
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
                        AIManager* aiManager,
                        BeanBaseClient* beanbase);

// Test MCP write tools (settings_set, profiles_set_active) against ProfileManager + MockTransport.
// Critical regression: settings_set temperature/weight must trigger BLE upload.

class tst_McpToolsWrite : public QObject {
    Q_OBJECT

private:
    // Wait for a storage's background DB work to actually finish, before letting it
    // go out of scope.
    //
    // This replaced five copies of `for (i < 20) { processEvents(); msleep(25); }`
    // — a fixed 500 ms guess at how long a background write takes. That is the
    // "timers as guards" anti-pattern CLAUDE.md forbids by name, and it behaved
    // exactly as that rule predicts: fine when run alone, fine on retry, and
    // intermittently NOT fine in the full parallel suite, where every binary is
    // ASan- and UBSan-instrumented and competing for cores. A duration cannot be
    // long enough, because the thing it is standing in for has no bound.
    //
    // Waiting matters rather than just being tidy: ~SerialDbWorker quit()s, which
    // DISCARDS queued-but-unstarted tasks, and close() calls
    // QSqlDatabase::removeDatabase(), which qWarns if a connection is still in use
    // — and init()'s failOnWarning turns that into a failure. So a task still in
    // flight at scope exit is either silently dropped or a warning, depending on
    // timing. QTRY_VERIFY spins the event loop (which is what delivers the result
    // callbacks) and fails loudly if the work never completes, instead of
    // continuing regardless the way the sleep did.
    // Drain, THEN close -- in that order, and only if the drain succeeded.
    //
    // The two were previously written as a `drainDbWork(storage); storage.close();` pair at
    // all 12 call sites. That is a drift opportunity (nothing enforces the order, or that
    // both are present), and it also mishandles failure: QTRY_VERIFY inside a non-slot
    // helper returns from the HELPER, so a timed-out drain fell straight through to the
    // close() it exists to protect, whose warning then became a second, louder failure that
    // buried the first. Returning early here instead leaves the close to
    // ~ShotHistoryStorage, which resets the worker (and WAITS) before closing -- so the
    // teardown is safe even on the failure path.
    // Seed one row so an update/delete has something real to affect. The real
    // schema, not the minimal one the storage tests build: uuid, timestamp,
    // profile_name and duration_seconds are NOT NULL. Returns the new id, or -1.
    static qint64 insertMinimalShot(ShotHistoryStorage& storage) {
        qint64 id = -1;
        withTempDb(storage.databasePath(), "tst_seed", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds) "
                      "VALUES ('outcome-seed-shot', 1000, 'P', 25.0)");
            if (q.exec())
                id = q.lastInsertId().toLongLong();
        });
        return id;
    }

    static void drainDbWorkAndClose(ShotHistoryStorage& storage) {
        QTRY_VERIFY(storage.isDbWorkIdle());
        // One more pass for anything the final callback itself posted.
        QCoreApplication::processEvents();
        storage.close();
    }

    // CoffeeBagStorage runs its OWN SerialDbWorker against the same database file,
    // so draining the ShotHistoryStorage says nothing about it. Missing this is a
    // real intermittent, not tidiness: bagCreateKindAndGating passed only when its
    // two queued bag writes happened to land before the stack object went out of
    // scope, and failed as
    //   "CoffeeBagStorageWorker" destroyed with 2 DB task(s) still queued
    //     — those writes are being discarded.        (dbutils.h:245)
    // Drain the bag storage BEFORE drainDbWorkAndClose(), because that close()
    // removes the shared connection this worker is still using.
    static void drainBagDbWork(CoffeeBagStorage& bagStorage) {
        QTRY_VERIFY(bagStorage.isDbWorkIdle());
        QCoreApplication::processEvents();
    }

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

        // A D-Flow profile is ALWAYS three frames — Filling / Infusing / Pouring —
        // because that is what the plugin's `prep` indexes (0/1/2, no pattern
        // matching). A two-frame fixture is not a D-Flow profile, and since
        // parameters are now derived from the frames it has no pour frame to
        // read from.
        QJsonObject frameSoak;
        frameSoak["name"] = "infuse";
        frameSoak["temperature"] = 93.0;
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
                          nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    }

private slots:
    // Redirect AppDataLocation, which ProfileManager::profilesPath() reads.
    // profilesSetActiveRefusedProfileReportsError writes a real file there, and
    // without this every test in this file that constructs a ProfileManager has
    // been reading the developer's own ~/Library/Application Support profiles.
    // Same call, for the same reason, as tst_profilemanager::initTestCase.
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void init() { QTest::failOnWarning(); }

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
                          nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

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

        // Drain BEFORE close(). close() calls QSqlDatabase::removeDatabase(), which
        // qWarns "connection is still in use" if background work still holds one —
        // and failOnWarning makes that a failure. Closing first was the original
        // order, with the sleep afterwards hoping the work had already finished.
        drainDbWorkAndClose(storage);
    }

    // equipment_update: a name-only rename must be REPORTED as a success, not
    // merely performed as one.
    //
    // The regression this pins: the success flag used to be set by
    // `ok = updatePackageFieldsStatic(...) || ok`. Removing that expression to
    // stop a successful identity edit masking a failed rename also removed the
    // only place `ok` was set for a rename — so a call carrying just a name
    // committed the rename and then answered "Package not found or update
    // failed". Nothing else in the suite inspects this tool's response shape.
    void equipmentUpdateNameOnlyReportsSuccess()
    {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("equpd.db")));
        registerWriteTools(&f.registry, &f.profileManager, &storage, &f.settings,
                          nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        qint64 packageId = -1;
        withTempDb(storage.databasePath(), "equpd_seed", [&](QSqlDatabase& db) {
            EquipmentPackage pkg;
            pkg.name = QStringLiteral("Bench Grinder");
            packageId = EquipmentStorage::createPackageWithGrinderStatic(
                db, pkg, QStringLiteral("Niche"), QStringLiteral("Zero"),
                QStringLiteral("63mm conical"), QString(), QString(), QString());
        });
        QVERIFY2(packageId > 0, "seed package should be created");

        // Name only: no grinder/basket/puckPrep keys at all.
        QJsonObject args;
        args["packageId"] = packageId;
        args["name"] = "Bench Grinder (renamed)";
        QJsonObject result = f.callAsyncTool("equipment_update", args);

        QVERIFY2(result["success"].toBool(), qPrintable(QJsonDocument(result).toJson()));
        QCOMPARE(result["package"].toObject()["name"].toString(),
                 QString("Bench Grinder (renamed)"));

        // And the rename really is on disk, so a green assertion above cannot
        // mean "reported success without writing".
        QString stored;
        withTempDb(storage.databasePath(), "equpd_check", [&](QSqlDatabase& db) {
            stored = EquipmentStorage::loadPackageStatic(db, packageId).name;
        });
        QCOMPARE(stored, QString("Bench Grinder (renamed)"));

        // Drain BEFORE close(). close() calls QSqlDatabase::removeDatabase(), which
        // qWarns "connection is still in use" if background work still holds one —
        // and failOnWarning makes that a failure. Closing first was the original
        // order, with the sleep afterwards hoping the work had already finished.
        drainDbWorkAndClose(storage);
    }

    // equipment_merge: the repair path for a grinder that got split in two
    // (#1713). Pinned at the tool layer because the destructive part — the source
    // package really is deleted and its shots really do move — is what an AI will
    // be asked to run on a user's live history, and the only signal it has that
    // the merge happened is this response.
    void equipmentMergeMovesHistoryAndDeletesSource()
    {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("eqmerge.db")));
        registerWriteTools(&f.registry, &f.profileManager, &storage, &f.settings,
                          nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        qint64 source = -1, target = -1, movedShot = -1;
        withTempDb(storage.databasePath(), "eqmerge_seed", [&](QSqlDatabase& db) {
            EquipmentPackage a;
            a.name = QStringLiteral("Eureka (with burrs)");
            source = EquipmentStorage::createPackageWithGrinderStatic(
                db, a, QStringLiteral("Eureka"), QStringLiteral("Mignon Single Dose"),
                QStringLiteral("Lebrew Sweet"), QString(), QString(), QString());
            EquipmentPackage b;
            b.name = QStringLiteral("Eureka");
            target = EquipmentStorage::createPackageWithGrinderStatic(
                db, b, QStringLiteral("Eureka"), QStringLiteral("Mignon Single Dose"),
                QString(), QString(), QString(), QString());
            // The real schema, not the minimal one the storage tests build: uuid,
            // timestamp, profile_name and duration_seconds are NOT NULL.
            QSqlQuery q(db);
            q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, equipment_id) "
                      "VALUES ('eqmerge-shot-1', 1000, 'P', 25.0, ?)");
            q.addBindValue(source);
            QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
            movedShot = q.lastInsertId().toLongLong();
        });
        QVERIFY2(source > 0 && target > 0, "seed packages should be created");

        QJsonObject args;
        args["sourcePackageId"] = source;
        args["targetPackageId"] = target;
        const QJsonObject result = f.callAsyncTool("equipment_merge", args);
        QVERIFY2(result["success"].toBool(), qPrintable(QJsonDocument(result).toJson()));
        QCOMPARE(result["shotsMoved"].toInteger(), (qint64)1);
        QCOMPARE(result["package"].toObject()["id"].toInteger(), target);
        // The survivor now HOLDS that shot, so the response must say so. Reporting
        // shotCount 0 here would tell an assistant the package it just moved history
        // onto is disposable — the same defect equipment_update was fixed for.
        QCOMPARE(result["package"].toObject()["shotCount"].toInteger(), (qint64)1);

        // On disk, not just in the response.
        qint64 shotEquipment = -1, sourceRows = -1;
        withTempDb(storage.databasePath(), "eqmerge_check", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            q.prepare("SELECT equipment_id FROM shots WHERE id = ?");
            q.addBindValue(movedShot);
            if (q.exec() && q.next()) shotEquipment = q.value(0).toLongLong();
            QSqlQuery c(db);
            c.prepare("SELECT COUNT(*) FROM equipment_packages WHERE id = ?");
            c.addBindValue(source);
            if (c.exec() && c.next()) sourceRows = c.value(0).toLongLong();
        });
        QCOMPARE(shotEquipment, target);
        QCOMPARE(sourceRows, (qint64)0);

        // Two ids that name one package is a refusal, not a self-merge that
        // deletes the package it was asked to keep.
        QJsonObject same;
        same["sourcePackageId"] = target;
        same["targetPackageId"] = target;
        const QJsonObject refused = f.callAsyncTool("equipment_merge", same);
        QVERIFY(!refused["success"].toBool());
        QVERIFY(refused.contains("error"));

        drainDbWorkAndClose(storage);
    }

    // bag_create (add-recipe-wizard-tea): kind stamped at creation, gated in
    // both directions. Needs a real CoffeeBagStorage (the tool creates via the
    // async instance — there is no static fallback like bag_update).
    void bagCreateKindAndGating()
    {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("bagcreate.db")));
        CoffeeBagStorage bagStorage;
        bagStorage.initialize(storage.databasePath());
        registerWriteTools(&f.registry, &f.profileManager, &storage, &f.settings,
                          nullptr, &bagStorage, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        // Tea bag with brewing data: kind + tea vocabulary round-trip.
        QJsonObject tea;
        tea["kind"] = "tea";
        tea["roasterName"] = "Harney & Sons";
        tea["coffeeName"] = "Decaf Ceylon";
        tea["teaType"] = "black";
        tea["brewTempC"] = 100;
        QJsonObject r = f.callAsyncTool("bag_create", tea);
        QVERIFY2(r["success"].toBool(), qPrintable(QJsonDocument(r).toJson()));
        QJsonObject bag = r["bag"].toObject();
        QCOMPARE(bag["kind"].toString(), QString("tea"));
        QCOMPARE(bag["teaType"].toString(), QString("black"));
        QCOMPARE(bag["brewTemperatureC"].toDouble(), 100.0);

        // kind=coffee + a tea field: rejected (synchronous, before storage).
        QJsonObject bad1;
        bad1["kind"] = "coffee"; bad1["roasterName"] = "X"; bad1["teaType"] = "black";
        QVERIFY(f.callAsyncTool("bag_create", bad1).contains("error"));

        // kind=tea + a coffee-only field: rejected.
        QJsonObject bad2;
        bad2["kind"] = "tea"; bad2["roasterName"] = "X"; bad2["roastLevel"] = "Light";
        QVERIFY(f.callAsyncTool("bag_create", bad2).contains("error"));

        // Omitted kind defaults to coffee.
        QJsonObject def;
        def["roasterName"] = "Onyx"; def["coffeeName"] = "Geometry";
        QJsonObject dr = f.callAsyncTool("bag_create", def);
        QVERIFY2(dr["success"].toBool(), qPrintable(QJsonDocument(dr).toJson()));
        QCOMPARE(dr["bag"].toObject()["kind"].toString(), QString("coffee"));

        // Neither roaster nor coffee name: rejected.
        QJsonObject empty; empty["kind"] = "tea";
        QVERIFY(f.callAsyncTool("bag_create", empty).contains("error"));

        // Drain BEFORE close(). close() calls QSqlDatabase::removeDatabase(), which
        // qWarns "connection is still in use" if background work still holds one —
        // and failOnWarning makes that a failure. Closing first was the original
        // order, with the sleep afterwards hoping the work had already finished.
        drainBagDbWork(bagStorage);
        drainDbWorkAndClose(storage);
    }

    // bag_update kind gate runs on the static path (nullptr bagStorage): tea
    // vocabulary is rejected on a coffee bag and coffee-only columns on a tea
    // bag; both accepted on the matching kind.
    void bagUpdateKindGating()
    {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("bagkind.db")));
        registerWriteTools(&f.registry, &f.profileManager, &storage, &f.settings,
                          nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        qint64 coffeeId = -1, teaId = -1;
        withTempDb(storage.databasePath(), "bagkind_seed", [&](QSqlDatabase& db) {
            CoffeeBag c; c.roasterName = "Onyx"; c.coffeeName = "Geometry";
            coffeeId = CoffeeBagStorage::insertBagStatic(db, c);
            CoffeeBag t; t.roasterName = "Harney"; t.coffeeName = "Ceylon"; t.kind = "tea";
            teaId = CoffeeBagStorage::insertBagStatic(db, t);
        });
        QVERIFY(coffeeId > 0 && teaId > 0);

        // teaType on a coffee bag: rejected.
        QJsonObject t1; t1["bagId"] = coffeeId; t1["teaType"] = "black";
        QVERIFY(f.callAsyncTool("bag_update", t1).contains("error"));

        // roastLevel/grinderSetting on a tea bag: rejected.
        QJsonObject t2; t2["bagId"] = teaId; t2["roastLevel"] = "Light";
        QVERIFY(f.callAsyncTool("bag_update", t2).contains("error"));
        QJsonObject t3; t3["bagId"] = teaId; t3["grinderSetting"] = "12";
        QVERIFY(f.callAsyncTool("bag_update", t3).contains("error"));

        // teaType on the tea bag: accepted.
        QJsonObject ok; ok["bagId"] = teaId; ok["teaType"] = "black";
        QJsonObject r = f.callAsyncTool("bag_update", ok);
        QVERIFY2(r["success"].toBool(), qPrintable(QJsonDocument(r).toJson()));
        QCOMPARE(r["bag"].toObject()["teaType"].toString(), QString("black"));

        // Drain BEFORE close(). close() calls QSqlDatabase::removeDatabase(), which
        // qWarns "connection is still in use" if background work still holds one —
        // and failOnWarning makes that a failure. Closing first was the original
        // order, with the sleep afterwards hoping the work had already finished.
        drainDbWorkAndClose(storage);
    }

    // The linked-bag case is where the MCP wiring does real work: the tool
    // description promises "Bean-detail edits keep a canonical Bean Base link
    // intact", the identity mirror rewrites the blob working keys, and the
    // first edit captures the pristine `canonical` snapshot.
    void bagUpdatePreservesCanonicalLinkAndCapturesSnapshot()
    {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("bagupd_linked.db")));
        registerWriteTools(&f.registry, &f.profileManager, &storage, &f.settings,
                          nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        qint64 bagId = -1;
        withTempDb(storage.databasePath(), "bagupd_seed3", [&](QSqlDatabase& db) {
            CoffeeBag bag;
            bag.roasterName = "Prodigal";
            bag.coffeeName = "First Batch";
            bag.beanBaseId = "uuid-1";
            bag.beanBaseData = QStringLiteral(
                "{\"id\":\"uuid-1\",\"visualizerCanonicalId\":\"uuid-1\","
                "\"canonicalRoasterId\":\"roaster-uuid\",\"roasterName\":\"Prodigal\","
                "\"roastName\":\"First Batch\",\"origin\":\"Colombia\"}");
            bagId = CoffeeBagStorage::insertBagStatic(db, bag);
        });
        QVERIFY(bagId > 0);

        QJsonObject args;
        args["bagId"] = bagId;
        args["tastingNotes"] = "plum";
        args["coffeeName"] = "First Batch 2026";
        QJsonObject result = f.callAsyncTool("bag_update", args);
        QVERIFY2(result["success"].toBool(), qPrintable(QJsonDocument(result).toJson()));
        const QJsonObject beanBase = result["bag"].toObject()["beanBase"].toObject();
        // Link intact, identity mirror applied, snapshot = pre-edit values.
        QCOMPARE(beanBase["id"].toString(), QString("uuid-1"));
        QCOMPARE(beanBase["canonicalRoasterId"].toString(), QString("roaster-uuid"));
        QCOMPARE(beanBase["roastName"].toString(), QString("First Batch 2026"));
        QCOMPARE(beanBase["tastingNotes"].toString(), QString("plum"));
        const QJsonObject canonical = beanBase["canonical"].toObject();
        QCOMPARE(canonical["roastName"].toString(), QString("First Batch"));
        QCOMPARE(canonical["origin"].toString(), QString("Colombia"));
        QVERIFY(!canonical.contains("tastingNotes"));  // canonical had none

        // Idempotent re-apply: same value again is a success, not an error.
        QJsonObject again;
        again["bagId"] = bagId;
        again["tastingNotes"] = "plum";
        QJsonObject result2 = f.callAsyncTool("bag_update", again);
        QVERIFY2(result2["success"].toBool(), qPrintable(QJsonDocument(result2).toJson()));
        // And the snapshot is untouched by the second edit.
        QCOMPARE(result2["bag"].toObject()["beanBase"].toObject()
                     ["canonical"].toObject()["origin"].toString(), QString("Colombia"));

        // Drain BEFORE close(). close() calls QSqlDatabase::removeDatabase(), which
        // qWarns "connection is still in use" if background work still holds one —
        // and failOnWarning makes that a failure. Closing first was the original
        // order, with the sleep afterwards hoping the work had already finished.
        drainDbWorkAndClose(storage);
    }

    // ===== recipe_get/set/clear_auto_load (recipe-auto-load) =====
    // These three tools were relocated here from mcptools_recipes.cpp
    // specifically so they could be tested without a real MainController —
    // see the note above their registration in mcptools_write.cpp.

    static qint64 seedRecipe(ShotHistoryStorage& storage, const QString& name, bool archived = false) {
        qint64 id = -1;
        withTempDb(storage.databasePath(), "recipe_autoload_seed", [&](QSqlDatabase& db) {
            Recipe r;
            r.name = name;
            r.profileTitle = "Test Profile";
            r.profileJson = "{\"title\":\"Test Profile\"}";
            r.archived = archived;
            id = RecipeStorage::insertRecipeStatic(db, r);
        });
        return id;
    }

    void recipeGetAutoLoadUnconfiguredReturnsNull()
    {
        McpTestFixture f;
        f.settings.dye()->setAutoLoadRecipeId(-1);
        f.settings.app()->setAutoLoadRevertMinutes(9);
        registerTools(f);

        QJsonObject result = f.callAsyncTool("recipe_get_auto_load", {});
        QVERIFY(result["recipeId"].isNull());
        QCOMPARE(result["revertMinutes"].toInt(), 9);
        QVERIFY(!result.contains("name"));
    }

    void recipeGetAutoLoadConfiguredReturnsNameAndId()
    {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("recipe_get.db")));
        registerWriteTools(&f.registry, &f.profileManager, &storage, &f.settings,
                          nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        const qint64 recipeId = seedRecipe(storage, "Morning Latte");
        QVERIFY(recipeId > 0);
        f.settings.dye()->setAutoLoadRecipeId(static_cast<int>(recipeId));
        f.settings.app()->setAutoLoadRevertMinutes(15);

        QJsonObject result = f.callAsyncTool("recipe_get_auto_load", {});
        QCOMPARE(result["recipeId"].toInteger(), recipeId);
        QCOMPARE(result["name"].toString(), QString("Morning Latte"));
        QCOMPARE(result["revertMinutes"].toInt(), 15);
        drainDbWorkAndClose(storage);
    }

    void recipeGetAutoLoadStaleIdReturnsNullNotError()
    {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("recipe_get_stale.db")));
        registerWriteTools(&f.registry, &f.profileManager, &storage, &f.settings,
                          nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        // A stale reference (the recipe row no longer exists) is a snapshot,
        // not an error — the next auto-load trigger discovers and clears it.
        f.settings.dye()->setAutoLoadRecipeId(99999);

        QJsonObject result = f.callAsyncTool("recipe_get_auto_load", {});
        QVERIFY(result["recipeId"].isNull());
        QVERIFY(!result.contains("error"));
        drainDbWorkAndClose(storage);
    }

    void recipeGetAutoLoadConfiguredButStorageUnavailableIsError()
    {
        // A recipe IS pinned, but the tool can't verify it right now (no
        // ShotHistoryStorage wired up here) — this must be reported as an
        // error, not collapsed into the same {"recipeId": null} shape as
        // "nothing is pinned" (which would misinform a caller).
        McpTestFixture f;
        const int before = f.settings.dye()->autoLoadRecipeId();  // shared PID-scoped store
        f.settings.dye()->setAutoLoadRecipeId(42);
        registerTools(f);  // shotHistory is nullptr here

        QJsonObject result = f.callAsyncTool("recipe_get_auto_load", {});
        QCOMPARE(result["error"].toString(), QString("Storage not available"));
        QVERIFY(!result.contains("recipeId"));

        f.settings.dye()->setAutoLoadRecipeId(before);
    }

    void recipeSetAutoLoadSuccessSetsSettingsAndClearsProfile()
    {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("recipe_set.db")));
        registerWriteTools(&f.registry, &f.profileManager, &storage, &f.settings,
                          nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        const qint64 recipeId = seedRecipe(storage, "Afternoon Cortado");
        QVERIFY(recipeId > 0);
        f.settings.app()->setAutoLoadProfileFilename("some-profile");  // must be cleared

        QJsonObject args;
        args["recipeId"] = recipeId;
        QJsonObject result = f.callAsyncTool("recipe_set_auto_load", args);

        QVERIFY2(result["success"].toBool(), qPrintable(QJsonDocument(result).toJson()));
        QCOMPARE(result["recipeId"].toInteger(), recipeId);
        QCOMPARE(result["name"].toString(), QString("Afternoon Cortado"));
        QCOMPARE(f.settings.dye()->autoLoadRecipeId(), static_cast<int>(recipeId));
        // Mutual exclusion: setting a recipe auto-load clears the profile side.
        QCOMPARE(f.settings.app()->autoLoadProfileFilename(), QString());
        drainDbWorkAndClose(storage);
    }

    void recipeSetAutoLoadMissingRecipeIdIsError()
    {
        McpTestFixture f;
        registerTools(f);

        QJsonObject result = f.callAsyncTool("recipe_set_auto_load", {});
        QCOMPARE(result["error"].toString(), QString("recipeId is required"));
    }

    void recipeSetAutoLoadInvalidRecipeIdIsDistinctFromMissing()
    {
        // recipeId WAS supplied here, just with an invalid value — the error
        // must say so rather than repeat the "is required" message used for
        // the genuinely-absent case above (that message would misleadingly
        // suggest the field was never sent).
        McpTestFixture f;
        registerTools(f);

        QJsonObject zero;
        zero["recipeId"] = 0;
        QCOMPARE(f.callAsyncTool("recipe_set_auto_load", zero)["error"].toString(),
                 QString("recipeId must be a positive integer"));

        QJsonObject negative;
        negative["recipeId"] = -5;
        QCOMPARE(f.callAsyncTool("recipe_set_auto_load", negative)["error"].toString(),
                 QString("recipeId must be a positive integer"));
    }

    void recipeSetAutoLoadOutOfRangeRecipeIdIsError()
    {
        // recipeId is stored as a plain `int` (SettingsDye::autoLoadRecipeId),
        // so a value past INT_MAX must be rejected before the later
        // static_cast<int>() truncates it into a bogus, silently-wrong id.
        McpTestFixture f;
        registerTools(f);

        QJsonObject args;
        args["recipeId"] = static_cast<qint64>(std::numeric_limits<int>::max()) + 1;
        QCOMPARE(f.callAsyncTool("recipe_set_auto_load", args)["error"].toString(),
                 QString("recipeId is out of range"));
    }

    void recipeSetAutoLoadNotFoundIsError()
    {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("recipe_set_notfound.db")));
        registerWriteTools(&f.registry, &f.profileManager, &storage, &f.settings,
                          nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        // Settings::testQSettingsPath() is a single PID-scoped store shared by
        // every McpTestFixture in this process, so a prior test's write can
        // still be sitting there — capture the baseline rather than assume -1.
        const int before = f.settings.dye()->autoLoadRecipeId();

        QJsonObject args;
        args["recipeId"] = 99999;
        QJsonObject result = f.callAsyncTool("recipe_set_auto_load", args);
        QCOMPARE(result["error"].toString(), QString("Recipe not found: 99999"));
        QCOMPARE(f.settings.dye()->autoLoadRecipeId(), before);
        drainDbWorkAndClose(storage);
    }

    void recipeSetAutoLoadArchivedIsError()
    {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("recipe_set_archived.db")));
        registerWriteTools(&f.registry, &f.profileManager, &storage, &f.settings,
                          nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        const int before = f.settings.dye()->autoLoadRecipeId();  // see note above

        const qint64 recipeId = seedRecipe(storage, "Retired Recipe", /*archived=*/true);
        QVERIFY(recipeId > 0);

        QJsonObject args;
        args["recipeId"] = recipeId;
        QJsonObject result = f.callAsyncTool("recipe_set_auto_load", args);
        QCOMPARE(result["error"].toString(), QString("Recipe is archived"));
        QCOMPARE(f.settings.dye()->autoLoadRecipeId(), before);
        drainDbWorkAndClose(storage);
    }

    void recipeSetAutoLoadOverwritesExistingPin()
    {
        // Pinning a second recipe while one is already pinned must silently
        // replace it — no error, no double-pin state, no need to clear the
        // first pin first.
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("recipe_set_overwrite.db")));
        registerWriteTools(&f.registry, &f.profileManager, &storage, &f.settings,
                          nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        const qint64 first = seedRecipe(storage, "First Pin");
        const qint64 second = seedRecipe(storage, "Second Pin");
        QVERIFY(first > 0 && second > 0 && first != second);

        QJsonObject argsFirst;
        argsFirst["recipeId"] = first;
        QJsonObject resultFirst = f.callAsyncTool("recipe_set_auto_load", argsFirst);
        QVERIFY2(resultFirst["success"].toBool(), qPrintable(QJsonDocument(resultFirst).toJson()));
        QCOMPARE(f.settings.dye()->autoLoadRecipeId(), static_cast<int>(first));

        QJsonObject argsSecond;
        argsSecond["recipeId"] = second;
        QJsonObject resultSecond = f.callAsyncTool("recipe_set_auto_load", argsSecond);
        QVERIFY2(resultSecond["success"].toBool(), qPrintable(QJsonDocument(resultSecond).toJson()));
        QCOMPARE(resultSecond["recipeId"].toInteger(), second);
        QCOMPARE(f.settings.dye()->autoLoadRecipeId(), static_cast<int>(second));
        drainDbWorkAndClose(storage);
    }

    void recipeSetAutoLoadOptionalRevertMinutesUpdatesSharedSetting()
    {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("recipe_set_revert.db")));
        registerWriteTools(&f.registry, &f.profileManager, &storage, &f.settings,
                          nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        const qint64 recipeId = seedRecipe(storage, "Evening Decaf");
        QVERIFY(recipeId > 0);

        QJsonObject args;
        args["recipeId"] = recipeId;
        args["revertMinutes"] = 33;
        QJsonObject result = f.callAsyncTool("recipe_set_auto_load", args);
        QVERIFY2(result["success"].toBool(), qPrintable(QJsonDocument(result).toJson()));
        QCOMPARE(result["revertMinutes"].toInt(), 33);
        // Shared with the profile side.
        QCOMPARE(f.settings.app()->autoLoadRevertMinutes(), 33);
        drainDbWorkAndClose(storage);
    }

    void recipeClearAutoLoadSuccessPreservesRevertMinutes()
    {
        McpTestFixture f;
        f.settings.dye()->setAutoLoadRecipeId(42);
        f.settings.app()->setAutoLoadRevertMinutes(27);
        registerTools(f);

        QJsonObject result = f.callAsyncTool("recipe_clear_auto_load", {});
        QVERIFY2(result["success"].toBool(), qPrintable(QJsonDocument(result).toJson()));
        QCOMPARE(f.settings.dye()->autoLoadRecipeId(), -1);
        // Timeout is untouched — preserved across enable/disable cycles.
        QCOMPARE(f.settings.app()->autoLoadRevertMinutes(), 27);
    }

    // ===== Outcome reporting: success must mean the operation happened =====
    //
    // Each of these covers a tool that used to report success for an operation
    // that did not take place. #1754 made a tool's `error` key reach the wire as
    // `isError`; it cannot reach these, because no `error` key was written at
    // all — the tool believed it had succeeded.

    // shots_delete waited on `shotDeleted`, which fires only on success, so a
    // delete that failed produced NO RESPONSE — the client hung with no error
    // and no timeout anywhere in the deferred path. The assertion that matters
    // is that a response arrives at all; `callAsyncTool` gives up after 5 s and
    // warns, so a regression here shows up as both a failed compare and that
    // warning.
    void shotsDeleteNonexistentShotAnswersInsteadOfHanging()
    {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("del.db")));
        registerWriteTools(&f.registry, &f.profileManager, &storage, &f.settings,
                           nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        // The storage layer logs the failed delete; that is the point, not a fault.
        ScopedWarningFilter deleteFilter("Failed to async delete shot");

        QJsonObject args;
        args["shotId"] = 99999;
        QJsonObject result = f.callAsyncTool("shots_delete", args);

        QVERIFY2(!result.isEmpty(), "shots_delete must respond even when the delete fails");
        QVERIFY2(result.contains("error"),
                 "deleting a shot that does not exist is a failure, not a success");
        QVERIFY2(!result.contains("success"), "a failed delete must not also report success");

        drainDbWorkAndClose(storage);
    }

    void shotsDeleteExistingShotStillReportsSuccess()
    {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("del2.db")));
        registerWriteTools(&f.registry, &f.profileManager, &storage, &f.settings,
                           nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        const qint64 shotId = insertMinimalShot(storage);
        QVERIFY(shotId > 0);

        QJsonObject args;
        args["shotId"] = shotId;
        QJsonObject result = f.callAsyncTool("shots_delete", args);

        QVERIFY2(result["success"].toBool(),
                 "a delete that removed a row must still report success");
        QVERIFY(!result.contains("error"));

        drainDbWorkAndClose(storage);
    }

    // `query.exec()` succeeding means the statement RAN. An UPDATE whose WHERE
    // matches nothing is a perfectly successful statement, and shots_update
    // reported it as an updated shot.
    void shotsUpdateNonexistentShotReportsError()
    {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("upd.db")));
        registerWriteTools(&f.registry, &f.profileManager, &storage, &f.settings,
                           nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        ScopedWarningFilter updateFilter("No shot with id");

        QJsonObject args;
        args["shotId"] = 99999;
        args["enjoyment"] = 80;
        QJsonObject result = f.callAsyncTool("shots_update", args);

        QVERIFY2(result.contains("error"),
                 "updating a shot that does not exist is a failure, not a success");
        QVERIFY(!result.contains("success"));

        drainDbWorkAndClose(storage);
    }

    void shotsUpdateExistingShotStillReportsSuccess()
    {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("upd2.db")));
        registerWriteTools(&f.registry, &f.profileManager, &storage, &f.settings,
                           nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        const qint64 shotId = insertMinimalShot(storage);
        QVERIFY(shotId > 0);

        QJsonObject args;
        args["shotId"] = static_cast<qint64>(shotId);
        args["enjoyment"] = 80;
        QJsonObject result = f.callAsyncTool("shots_update", args);

        QVERIFY2(result["success"].toBool(),
                 "an update that changed a row must still report success");

        drainDbWorkAndClose(storage);
    }

    // ProfileManager::loadProfile refuses a profile it cannot read and KEEPS the
    // previously active one. profiles_set_active's `profileExists` guard cannot
    // see this: the file is present, it just does not parse. The tool reported
    // "Profile activated" while the machine went on brewing the old profile.
    void profilesSetActiveRefusedProfileReportsError()
    {
        McpTestFixture f;
        registerTools(f);

        // A file that exists (so profileExists passes) and cannot be a profile.
        // It has to sit where loadProfile looks (userProfilesPath, tier 2) AND
        // where profileExists falls back to (profilesPath), which are different
        // directories — hence both writes. AppDataLocation is redirected by
        // initTestCase, so this touches the test store, not the real one.
        const QJsonObject broken{{"title", "Broken"}};
        const QByteArray brokenBytes = QJsonDocument(broken).toJson(QJsonDocument::Compact);
        for (const QString& dir : {f.profileManager.userProfilesPath(),
                                   f.profileManager.profilesPath()}) {
            QDir().mkpath(dir);
            QFile file(dir + "/broken.json");
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(brokenBytes);
            file.close();
        }
        QVERIFY(f.profileManager.profileExists("broken"));

        ScopedWarningFilter refusalFilter("loadProfile: refusing|Profile not found");

        QJsonObject args;
        args["filename"] = "broken";
        QJsonObject result = f.callAsyncTool("profiles_set_active", args);

        QVERIFY2(result.contains("error"),
                 "a profile the manager refused must not be reported as activated");
        QVERIFY(!result.contains("success"));
    }
};

QTEST_MAIN(tst_McpToolsWrite)
#include "tst_mcptools_write.moc"
