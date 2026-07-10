#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QThread>
#include <QNetworkAccessManager>

#include "history/shothistorystorage.h"
#include "history/coffeebagstorage.h"
#include "history/equipmentstorage.h"
#include "history/recipestorage.h"
#include "history/unifiedbeansearchmodel.h"
#include "core/settings_dye.h"
#include "core/settings_visualizer.h"
#include "network/visualizeruploader.h"

using Tier = UnifiedBeanSearchModel::Tier;

// Read a merged row's tier role back as the typed enum.
static Tier tierOf(const QVariant& row) {
    return static_cast<Tier>(row.toMap().value("tier").toInt());
}

// Coffee bag storage, the preset -> bag migration, transfer survival, and the
// unified bean search merge logic (openspec change bean-bag-inventory).
//
// DB strategy mirrors tst_dbmigration: temp-dir SQLite files, raw scoped
// connections, ShotHistoryStorage::initialize() to run the real migration
// chain. The legacy-preset QSettings tests snapshot and restore the real
// bean/presets key (the import deliberately uses the app's settings scope).

template<typename Work>
static void withRawDb(const QString& path, const QString& connName, Work&& work) {
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
        db.setDatabaseName(path);
        db.open();
        QSqlQuery(db).exec("PRAGMA foreign_keys = ON");
        work(db);
    }
    QSqlDatabase::removeDatabase(connName);
}

static bool hasColumn(QSqlDatabase& db, const QString& table, const QString& column) {
    QSqlQuery q(db);
    q.exec(QString("PRAGMA table_info(%1)").arg(table));
    while (q.next()) {
        if (q.value(1).toString() == column)
            return true;
    }
    return false;
}

static bool hasTable(QSqlDatabase& db, const QString& table) {
    QSqlQuery q(db);
    q.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name=?");
    q.addBindValue(table);
    return q.exec() && q.next();
}

// Minimal shot row insert for link / history-lane tests.
static qint64 insertShot(QSqlDatabase& db, const QString& brand, const QString& type,
                         qint64 timestamp, const QString& beanBaseId = QString(),
                         qint64 bagId = -1, const QString& beanBaseJson = QString()) {
    QSqlQuery q(db);
    q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, "
              "bean_brand, bean_type, beanbase_id, beanbase_json, bag_id, grinder_setting, dose_weight) "
              "VALUES (:uuid, :ts, 'Test', 30, :brand, :type, :bbid, :bbjson, :bag, '12', 18.0)");
    q.bindValue(":uuid", QUuid::createUuid().toString(QUuid::WithoutBraces));
    q.bindValue(":ts", timestamp);
    q.bindValue(":brand", brand);
    q.bindValue(":type", type);
    q.bindValue(":bbid", beanBaseId.isEmpty() ? QVariant() : beanBaseId);
    q.bindValue(":bbjson", beanBaseJson.isEmpty() ? QVariant() : beanBaseJson);
    q.bindValue(":bag", bagId > 0 ? QVariant(bagId) : QVariant());
    if (!q.exec()) {
        qWarning() << "insertShot failed:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

static QJsonObject makePreset(const QString& name, const QString& brand, const QString& type,
                              const QString& roastDate = QString()) {
    QJsonObject p;
    p["name"] = name;
    p["brand"] = brand;
    p["type"] = type;
    p["roastDate"] = roastDate;
    p["roastLevel"] = "Medium";
    p["grinderBrand"] = "Niche";
    p["grinderModel"] = "Zero";
    p["grinderBurrs"] = "Stock";
    p["grinderSetting"] = "15";
    p["barista"] = "Jeff";
    p["showOnIdle"] = true;
    return p;
}

class tst_CoffeeBags : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;
    int m_dbCounter = 0;

    // initialize() + close(), draining the background thread. Mirrors
    // tst_dbmigration::initAndClose: ShotHistoryStorage::close() removes its
    // connection while the initialize()-spawned distinct-cache thread may
    // still hold a QSqlQuery — Qt warns (harmless, ignored) and the thread
    // MUST be drained before the storage destructs or it SIGSEGVs.
    bool initAndClose(const QString& path, ShotHistoryStorage& storage) {
        if (!storage.initialize(path))
            return false;
        storage.close();
        for (int i = 0; i < 20; i++) {
            QCoreApplication::processEvents();
            QThread::msleep(25);
        }
        return true;
    }

    // Fresh fully-migrated DB via the real migration chain.
    QString freshDb() {
        const QString path = m_tempDir.filePath(QString("bags_%1.db").arg(m_dbCounter++));
        ShotHistoryStorage storage;
        return initAndClose(path, storage) ? path : QString();
    }

private slots:

    void initTestCase() {
        QVERIFY(m_tempDir.isValid());
    }

    // ==========================================
    // Migration 19 schema
    // ==========================================

    void migration19CreatesSchema() {
        const QString path = freshDb();
        QVERIFY(!path.isEmpty());
        withRawDb(path, "schema_check", [&](QSqlDatabase& db) {
            QVERIFY(hasTable(db, "coffee_bags"));
            QVERIFY(hasColumn(db, "shots", "bag_id"));
            QVERIFY(hasColumn(db, "shots", "frozen_date"));
            QVERIFY(hasColumn(db, "shots", "defrost_date"));
            QVERIFY(hasColumn(db, "shots", "beanbase_id"));
        });
    }

    void migration19BackfillsBeanbaseId() {
        // Simulate a pre-19 database: build the v18 shape by hand, then let
        // the migration chain bring it to 19 and backfill.
        const QString path = m_tempDir.filePath("backfill.db");
        {
            ShotHistoryStorage storage;
            QVERIFY(initAndClose(path, storage));
        }
        withRawDb(path, "backfill_prep", [&](QSqlDatabase& db) {
            // Strip the migration-19 artifacts to fake a v18 database.
            // (The index must go first — SQLite refuses DROP COLUMN on an
            // indexed column.)
            QSqlQuery q(db);
            QVERIFY(q.exec("DROP INDEX IF EXISTS idx_shots_beanbase_id"));
            QVERIFY(q.exec("ALTER TABLE shots DROP COLUMN beanbase_id"));
            QVERIFY(q.exec("ALTER TABLE shots DROP COLUMN bag_id"));
            QVERIFY(q.exec("ALTER TABLE shots DROP COLUMN frozen_date"));
            QVERIFY(q.exec("ALTER TABLE shots DROP COLUMN defrost_date"));
            QVERIFY(q.exec("DROP TABLE coffee_bags"));
            // Restore the grinder identity columns a real v18 DB had (migration 8
            // added them; the first full init above dropped them at migration 23).
            // Without these, the re-run of migration 22 in the chain below can't
            // read shots.grinder_* and logs a spurious "incomplete" failure.
            QVERIFY(q.exec("ALTER TABLE shots ADD COLUMN grinder_brand TEXT"));
            QVERIFY(q.exec("ALTER TABLE shots ADD COLUMN grinder_model TEXT"));
            QVERIFY(q.exec("ALTER TABLE shots ADD COLUMN grinder_burrs TEXT"));
            QVERIFY(q.exec("DELETE FROM schema_version"));
            QVERIFY(q.exec("INSERT INTO schema_version (version) VALUES (18)"));
            // One linked, one unlinked shot.
            QSqlQuery ins(db);
            ins.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, "
                        "bean_brand, bean_type, beanbase_json) VALUES (:u, 1000, 'P', 30, 'R', 'C', :j)");
            ins.bindValue(":u", "uuid-linked");
            ins.bindValue(":j", R"({"id":"canon-123","origin":"Ethiopia"})");
            QVERIFY(ins.exec());
            ins.bindValue(":u", "uuid-unlinked");
            ins.bindValue(":j", QVariant());
            QVERIFY(ins.exec());
        });
        {
            ShotHistoryStorage storage;
            QVERIFY(initAndClose(path, storage));  // runs migration 19 again
        }
        withRawDb(path, "backfill_check", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT beanbase_id FROM shots WHERE uuid = 'uuid-linked'"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QString("canon-123"));
            QVERIFY(q.exec("SELECT beanbase_id FROM shots WHERE uuid = 'uuid-unlinked'"));
            QVERIFY(q.next());
            QVERIFY(q.value(0).isNull());
        });
    }

    // ==========================================
    // Bag CRUD statics
    // ==========================================

    void insertLoadUpdateRoundTrip() {
        const QString path = freshDb();
        withRawDb(path, "crud", [&](QSqlDatabase& db) {
            CoffeeBag bag;
            bag.roasterName = "Prodigal";
            bag.coffeeName = "First Batch";
            bag.roastDate = "2026-06-01";
            bag.beanBaseId = "canon-1";
            bag.frozenDate = "2026-06-05";
            bag.startWeightG = 150;
            const qint64 id = CoffeeBagStorage::insertBagStatic(db, bag);
            QVERIFY(id > 0);

            CoffeeBag loaded = CoffeeBagStorage::loadBagStatic(db, id);
            QVERIFY(loaded.isValid());
            QCOMPARE(loaded.roasterName, QString("Prodigal"));
            QCOMPARE(loaded.frozenDate, QString("2026-06-05"));
            QCOMPARE(loaded.startWeightG, 150.0);
            QVERIFY(loaded.inInventory);
            QVERIFY(loaded.defrostDate.isEmpty());

            // Partial update: only the named field changes; empty clears to NULL.
            QVERIFY(CoffeeBagStorage::updateBagFieldsStatic(db, id, {
                {"defrostDate", "2026-06-10"}, {"grinderSetting", "14"}}));
            loaded = CoffeeBagStorage::loadBagStatic(db, id);
            QCOMPARE(loaded.defrostDate, QString("2026-06-10"));
            QCOMPARE(loaded.grinderSetting, QString("14"));
            QCOMPARE(loaded.roasterName, QString("Prodigal"));  // untouched

            QVERIFY(CoffeeBagStorage::updateBagFieldsStatic(db, id, {{"inInventory", false}}));
            QVERIFY(CoffeeBagStorage::loadInventoryStatic(db).isEmpty());
        });
    }

    // loadBagStatic materializes the bag's grinder identity from its equipment
    // package (the bag no longer stores brand/model/burrs — migration 23), so
    // MCP bag_list etc. still surface the grinder. Burrs comes from the item's
    // attrs JSON; a bag with no equipment_id leaves the fields empty.
    void loadBagResolvesGrinderFromPackage() {
        const QString path = freshDb();
        withRawDb(path, "bag_grinder", [&](QSqlDatabase& db) {
            EquipmentPackage pkg;
            const qint64 pid = EquipmentStorage::createPackageWithGrinderStatic(
                db, pkg, "Niche", "Zero", "63mm conical");
            QVERIFY(pid > 0);

            CoffeeBag linked;
            linked.roasterName = "Onyx";
            linked.coffeeName = "Geometry";
            linked.equipmentId = pid;
            linked.grinderSetting = "12";
            const qint64 linkedId = CoffeeBagStorage::insertBagStatic(db, linked);
            QVERIFY(linkedId > 0);

            const CoffeeBag loaded = CoffeeBagStorage::loadBagStatic(db, linkedId);
            QCOMPARE(loaded.grinderBrand, QString("Niche"));
            QCOMPARE(loaded.grinderModel, QString("Zero"));
            QCOMPARE(loaded.grinderBurrs, QString("63mm conical"));  // from attrs JSON
            QCOMPARE(loaded.grinderSetting, QString("12"));          // real bag column

            // A bag with no equipment_id resolves to empty grinder identity.
            CoffeeBag unlinked;
            unlinked.roasterName = "SEY";
            const qint64 unlinkedId = CoffeeBagStorage::insertBagStatic(db, unlinked);
            const CoffeeBag u = CoffeeBagStorage::loadBagStatic(db, unlinkedId);
            QVERIFY(u.grinderBrand.isEmpty() && u.grinderModel.isEmpty() && u.grinderBurrs.isEmpty());
        });
    }

    // ==========================================
    // Legacy preset conversion
    // ==========================================

    void presetMappingDropsBaristaAndShowOnIdle() {
        // name lands in notes only when it differs from "{brand} {type}".
        CoffeeBag named = CoffeeBagStorage::bagFromLegacyPreset(
            makePreset("Morning blend", "Prodigal", "First Batch", "2026-05-01"));
        QCOMPARE(named.roasterName, QString("Prodigal"));
        QCOMPARE(named.coffeeName, QString("First Batch"));
        QCOMPARE(named.roastDate, QString("2026-05-01"));
        QCOMPARE(named.notes, QString("Morning blend"));
        QCOMPARE(named.grinderSetting, QString("15"));
        QVERIFY(named.inInventory);

        CoffeeBag redundant = CoffeeBagStorage::bagFromLegacyPreset(
            makePreset("Prodigal First Batch", "Prodigal", "First Batch"));
        QVERIFY(redundant.notes.isEmpty());
    }

    void importLegacyPresetsDedupesAndMapsSelection() {
        const QString path = freshDb();
        withRawDb(path, "presets", [&](QSqlDatabase& db) {
            // Pre-existing bag matching preset[1] by identity.
            CoffeeBag existing;
            existing.roasterName = "ROASTER B";  // case-insensitive match
            existing.coffeeName = "Coffee B";
            existing.roastDate = "2026-05-10";
            const qint64 existingId = CoffeeBagStorage::insertBagStatic(db, existing);
            QVERIFY(existingId > 0);

            QJsonArray presets;
            presets.append(makePreset("A", "Roaster A", "Coffee A", "2026-05-01"));
            presets.append(makePreset("B", "roaster b", "coffee b", "2026-05-10"));  // dup
            presets.append(makePreset("C", "Roaster C", "Coffee C"));

            qint64 selectedBagId = -1;
            const int imported = CoffeeBagStorage::importLegacyPresetsStatic(db, presets, 1, &selectedBagId);
            QCOMPARE(imported, 2);                 // A and C; B matched existing
            QCOMPARE(selectedBagId, existingId);   // selection maps to the matched bag

            QCOMPARE(CoffeeBagStorage::loadInventoryStatic(db).size(), 3);
        });
    }

    void convertLegacyPresetSettingsClearsKeysOnSuccessOnly() {
        // Snapshot + restore the REAL settings keys (app scope, deliberate).
        QSettings appSettings(QStringLiteral("DecentEspresso"), QStringLiteral("DE1Qt"));
        const QVariant origPresets = appSettings.value("bean/presets");
        const QVariant origSelected = appSettings.value("bean/selectedPreset");
        auto restore = qScopeGuard([&]() {
            if (origPresets.isValid()) appSettings.setValue("bean/presets", origPresets);
            else appSettings.remove("bean/presets");
            if (origSelected.isValid()) appSettings.setValue("bean/selectedPreset", origSelected);
            else appSettings.remove("bean/selectedPreset");
        });

        QJsonArray presets;
        presets.append(makePreset("X", "Roaster X", "Coffee X", "2026-06-01"));
        appSettings.setValue("bean/presets", QJsonDocument(presets).toJson());
        appSettings.setValue("bean/selectedPreset", 0);

        // Failure path: nonexistent directory -> keys stay for retry. The
        // open failure is expected and logged — assert+silence it.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("DB open failed"));
        const qint64 failed = CoffeeBagStorage::convertLegacyPresetSettings(
            m_tempDir.filePath("no/such/dir/x.db"));
        QCOMPARE(failed, -1);
        QVERIFY(appSettings.contains("bean/presets"));

        // Success path: keys cleared, selection mapped.
        const QString path = freshDb();
        const qint64 selectedBagId = CoffeeBagStorage::convertLegacyPresetSettings(path);
        QVERIFY(selectedBagId > 0);
        QVERIFY(!appSettings.contains("bean/presets"));
        QVERIFY(!appSettings.contains("bean/selectedPreset"));

        withRawDb(path, "convert_check", [&](QSqlDatabase& db) {
            const CoffeeBag bag = CoffeeBagStorage::loadBagStatic(db, selectedBagId);
            QCOMPARE(bag.roasterName, QString("Roaster X"));
        });
    }

    // ==========================================
    // findBagForShot
    // ==========================================

    void linkOrphanShotsAdoptsPreBagHistory() {
        // Migration-20 repair: shots saved before bags existed (bag_id NULL)
        // are linked by identity so migrated favorites carry their history.
        const QString path = freshDb();
        withRawDb(path, "orphan_link", [&](QSqlDatabase& db) {
            // Pre-bag shots: two for Prodigal (one with a roast date), one
            // for an unknown coffee.
            QSqlQuery shotInsert(db);
            shotInsert.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, "
                               "bean_brand, bean_type, roast_date) VALUES (:u, :ts, 'P', 30, :b, :t, :rd)");
            auto addShot = [&](const char* uuid, const char* brand, const char* type, const char* roastDate) {
                shotInsert.bindValue(":u", uuid);
                shotInsert.bindValue(":ts", 1000);
                shotInsert.bindValue(":b", brand);
                shotInsert.bindValue(":t", type);
                shotInsert.bindValue(":rd", QString(roastDate).isEmpty() ? QVariant() : QVariant(roastDate));
                QVERIFY(shotInsert.exec());
            };
            addShot("s-exact", "Prodigal", "Espresso Milk Blend", "2026-01-01");
            addShot("s-dateless", "prodigal", "espresso milk blend", "");   // case-insensitive, no date
            addShot("s-unknown", "Someone", "Else", "");

            // Two Prodigal bags: an old one and the current (MRU) one.
            CoffeeBag oldBag;
            oldBag.roasterName = "Prodigal";
            oldBag.coffeeName = "Espresso Milk Blend";
            oldBag.roastDate = "2026-01-01";
            oldBag.lastUsedEpoch = 100;
            const qint64 oldBagId = CoffeeBagStorage::insertBagStatic(db, oldBag);
            CoffeeBag newBag = oldBag;
            newBag.roastDate = "2026-06-01";
            newBag.lastUsedEpoch = 200;
            const qint64 newBagId = CoffeeBagStorage::insertBagStatic(db, newBag);

            QVERIFY(CoffeeBagStorage::linkOrphanShotsStatic(db) >= 0);

            auto bagOf = [&](const char* uuid) {
                QSqlQuery q(db);
                q.prepare("SELECT bag_id FROM shots WHERE uuid = :u");
                q.bindValue(":u", uuid);
                if (!q.exec() || !q.next()) return qint64(-2);
                return q.value(0).isNull() ? qint64(-1) : q.value(0).toLongLong();
            };
            QCOMPARE(bagOf("s-exact"), oldBagId);     // exact roast-date match wins
            QCOMPARE(bagOf("s-dateless"), newBagId);  // identity fallback -> MRU bag
            QCOMPARE(bagOf("s-unknown"), qint64(-1)); // no matching bag -> stays NULL

            // Idempotent: a second run changes nothing.
            QVERIFY(CoffeeBagStorage::linkOrphanShotsStatic(db) >= 0);
            QCOMPARE(bagOf("s-exact"), oldBagId);
        });
    }

    void propagateBeanBaseFixesLinkedShots() {
        // Linking a bag to Bean Base in the edit dialog rewrites the
        // canonical snapshot of every shot pulled with that bag; clearing
        // the link propagates the unlink the same way.
        const QString path = freshDb();
        withRawDb(path, "propagate", [&](QSqlDatabase& db) {
            CoffeeBag bag;
            bag.roasterName = "Prodigal";
            bag.coffeeName = "Espresso Milk Blend";
            const qint64 bagId = CoffeeBagStorage::insertBagStatic(db, bag);
            const qint64 linkedShot = insertShot(db, "Prodigal", "Espresso Milk Blend", 1000, QString(), bagId);
            const qint64 otherShot = insertShot(db, "Other", "Coffee", 1001);

            QVERIFY(CoffeeBagStorage::updateBagFieldsStatic(db, bagId, {
                {"beanBaseId", "canon-77"},
                {"beanBaseData", R"({"id":"canon-77","origin":"Colombia"})"}}));
            QCOMPARE(CoffeeBagStorage::propagateBeanBaseStatic(db, bagId), 1);

            QSqlQuery q(db);
            q.prepare("SELECT beanbase_id, beanbase_json FROM shots WHERE id = :id");
            q.bindValue(":id", linkedShot);
            QVERIFY(q.exec() && q.next());
            QCOMPARE(q.value(0).toString(), QString("canon-77"));
            QVERIFY(q.value(1).toString().contains("Colombia"));

            q.bindValue(":id", otherShot);
            QVERIFY(q.exec() && q.next());
            QVERIFY(q.value(0).isNull());  // unrelated shot untouched

            // Unlink propagates the clear.
            QVERIFY(CoffeeBagStorage::updateBagFieldsStatic(db, bagId, {
                {"beanBaseId", ""}, {"beanBaseData", ""}}));
            QCOMPARE(CoffeeBagStorage::propagateBeanBaseStatic(db, bagId), 1);
            q.bindValue(":id", linkedShot);
            QVERIFY(q.exec() && q.next());
            QVERIFY(q.value(0).isNull());
        });
    }

    void inventoryCarriesShotCount() {
        // shotCount drives the card's single removal action (trash while 0,
        // "Bag finished" once shots exist).
        const QString path = freshDb();
        withRawDb(path, "shotcount", [&](QSqlDatabase& db) {
            CoffeeBag used;
            used.roasterName = "Used";
            used.coffeeName = "Bag";
            const qint64 usedId = CoffeeBagStorage::insertBagStatic(db, used);
            insertShot(db, "Used", "Bag", 1000, QString(), usedId);
            insertShot(db, "Used", "Bag", 1001, QString(), usedId);
            CoffeeBag fresh;
            fresh.roasterName = "Fresh";
            fresh.coffeeName = "Bag";
            CoffeeBagStorage::insertBagStatic(db, fresh);

            const QVector<InventoryBag> inventory = CoffeeBagStorage::loadInventoryStatic(db);
            QCOMPARE(inventory.size(), 2);
            QHash<QString, qint64> countByRoaster;
            for (const InventoryBag& entry : inventory)
                countByRoaster.insert(entry.bag.roasterName, entry.shotCount);
            QCOMPARE(countByRoaster.value("Used"), qint64(2));
            QCOMPARE(countByRoaster.value("Fresh"), qint64(0));
        });
    }

    void requestInventoryInjectsShotCountIntoMap() {
        // shotCount is no longer a CoffeeBag field: requestInventory injects it
        // into the variant map the QML card reads. Assert that C++->QML contract
        // through the async signal, not just the loader struct (the field could
        // be present on InventoryBag yet dropped from the emitted map).
        const QString path = freshDb();
        withRawDb(path, "inv_map", [&](QSqlDatabase& db) {
            CoffeeBag used;
            used.roasterName = "Used";
            used.coffeeName = "Bag";
            const qint64 usedId = CoffeeBagStorage::insertBagStatic(db, used);
            insertShot(db, "Used", "Bag", 1000, QString(), usedId);
            insertShot(db, "Used", "Bag", 1001, QString(), usedId);
            CoffeeBag fresh;
            fresh.roasterName = "Fresh";
            fresh.coffeeName = "Bag";
            CoffeeBagStorage::insertBagStatic(db, fresh);
        });

        CoffeeBagStorage storage;
        storage.initialize(path);
        QSignalSpy readySpy(&storage, &CoffeeBagStorage::inventoryReady);
        storage.requestInventory();
        QTRY_COMPARE(readySpy.count(), 1);

        const QVariantList bags = readySpy.at(0).at(0).toList();
        QCOMPARE(bags.size(), 2);
        QHash<QString, qint64> countByRoaster;
        for (const QVariant& v : bags) {
            const QVariantMap map = v.toMap();
            QVERIFY2(map.contains(QStringLiteral("shotCount")),
                     "inventory map must carry the injected shotCount key");
            countByRoaster.insert(map.value(QStringLiteral("roasterName")).toString(),
                                  map.value(QStringLiteral("shotCount")).toLongLong());
        }
        QCOMPARE(countByRoaster.value("Used"), qint64(2));
        QCOMPARE(countByRoaster.value("Fresh"), qint64(0));
    }

    void findBagForShotPrefersLinkThenIdentity() {
        const QString path = freshDb();
        withRawDb(path, "findbag", [&](QSqlDatabase& db) {
            CoffeeBag bagA;
            bagA.roasterName = "Roaster";
            bagA.coffeeName = "Coffee";
            bagA.inInventory = false;  // older bag of the same coffee
            const qint64 bagAId = CoffeeBagStorage::insertBagStatic(db, bagA);
            CoffeeBag bagB = bagA;
            bagB.inInventory = true;
            const qint64 bagBId = CoffeeBagStorage::insertBagStatic(db, bagB);

            // Linked shot -> its own bag wins even if not in inventory.
            const qint64 linkedShot = insertShot(db, "Roaster", "Coffee", 1000, QString(), bagAId);
            QCOMPARE(CoffeeBagStorage::findBagForShotStatic(db, linkedShot, "Roaster", "Coffee"), bagAId);

            // Pre-bag shot -> identity match, in-inventory bag preferred.
            const qint64 legacyShot = insertShot(db, "Roaster", "Coffee", 1001);
            QCOMPARE(CoffeeBagStorage::findBagForShotStatic(db, legacyShot, "Roaster", "Coffee"), bagBId);

            // No match at all.
            QCOMPARE(CoffeeBagStorage::findBagForShotStatic(db, -1, "Unknown", "Bean"), qint64(-1));
        });
    }

    // ==========================================
    // Delete guard
    // ==========================================

    void deleteGuardOnlyRemovesUnusedBags() {
        // The guard lives in the async path; verify the underlying invariant
        // the same way: bag with a linked shot must survive, unused bag goes.
        const QString path = freshDb();
        withRawDb(path, "delete", [&](QSqlDatabase& db) {
            CoffeeBag bag;
            bag.roasterName = "R";
            bag.coffeeName = "C";
            const qint64 usedId = CoffeeBagStorage::insertBagStatic(db, bag);
            insertShot(db, "R", "C", 1000, QString(), usedId);

            QSqlQuery count(db);
            count.prepare("SELECT COUNT(*) FROM shots WHERE bag_id = :id");
            count.bindValue(":id", usedId);
            QVERIFY(count.exec() && count.next());
            QCOMPARE(count.value(0).toInt(), 1);  // guard precondition holds
        });

        CoffeeBagStorage storage;
        storage.initialize(path);
        qint64 usedBagId = -1, freshBagId = -1;
        withRawDb(path, "delete_ids", [&](QSqlDatabase& db) {
            usedBagId = CoffeeBagStorage::loadInventoryStatic(db).first().bag.id;
            CoffeeBag fresh;
            fresh.roasterName = "Mistake";
            fresh.coffeeName = "Bag";
            freshBagId = CoffeeBagStorage::insertBagStatic(db, fresh);
        });

        QSignalSpy deletedSpy(&storage, &CoffeeBagStorage::bagDeleted);
        // The guard logs its refusal — expected, assert+silence it.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("refusing to delete bag"));
        storage.requestDeleteBag(usedBagId);   // refused: has a linked shot
        storage.requestDeleteBag(freshBagId);  // allowed
        QTRY_COMPARE(deletedSpy.count(), 2);

        QHash<qint64, bool> outcome;
        for (const auto& args : deletedSpy)
            outcome.insert(args[0].toLongLong(), args[1].toBool());
        QCOMPARE(outcome.value(usedBagId), false);
        QCOMPARE(outcome.value(freshBagId), true);

        withRawDb(path, "delete_check", [&](QSqlDatabase& db) {
            QVERIFY(CoffeeBagStorage::loadBagStatic(db, usedBagId).isValid());
            QVERIFY(!CoffeeBagStorage::loadBagStatic(db, freshBagId).isValid());
        });
    }

    // ==========================================
    // Transfer / backup import
    // ==========================================

    void importDatabaseRemapsBagIdsAndKeepsSnapshotColumns() {
        const QString srcPath = freshDb();
        const QString destPath = freshDb();

        qint64 srcBagId = -1;
        withRawDb(srcPath, "imp_src", [&](QSqlDatabase& db) {
            CoffeeBag bag;
            bag.roasterName = "Transfer";
            bag.coffeeName = "Roast";
            bag.frozenDate = "2026-06-01";
            srcBagId = CoffeeBagStorage::insertBagStatic(db, bag);
            QVERIFY(srcBagId > 0);
            // Shot linked to the bag, carrying the once-dropped columns.
            QSqlQuery q(db);
            q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, "
                      "bean_brand, bean_type, bag_id, stopped_by, beanbase_json, frozen_date) "
                      "VALUES ('src-uuid-1', 2000, 'P', 30, 'Transfer', 'Roast', :bag, 'weight', "
                      "'{\"id\":\"canon-9\"}', '2026-06-01')");
            q.bindValue(":bag", srcBagId);
            QVERIFY(q.exec());
        });

        // Occupy a dest row id so the remap is observable (src bag id 1 must
        // NOT map to dest id 1).
        withRawDb(destPath, "imp_dest_prep", [&](QSqlDatabase& db) {
            CoffeeBag filler;
            filler.roasterName = "Existing";
            filler.coffeeName = "Bag";
            QVERIFY(CoffeeBagStorage::insertBagStatic(db, filler) > 0);
        });

        QVERIFY(ShotHistoryStorage::importDatabaseStatic(destPath, srcPath, /*merge=*/true));

        withRawDb(destPath, "imp_check", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT s.bag_id, s.stopped_by, s.beanbase_json, s.beanbase_id, s.frozen_date, "
                           "b.roaster_name FROM shots s JOIN coffee_bags b ON b.id = s.bag_id "
                           "WHERE s.uuid = 'src-uuid-1'"));
            QVERIFY(q.next());
            QVERIFY(q.value(0).toLongLong() != srcBagId);          // remapped
            QCOMPARE(q.value(1).toString(), QString("weight"));     // stopped_by restored
            QCOMPARE(q.value(2).toString(), QString("{\"id\":\"canon-9\"}"));
            QCOMPARE(q.value(3).toString(), QString("canon-9"));    // beanbase_id backfilled
            QCOMPARE(q.value(4).toString(), QString("2026-06-01")); // frozen_date carried
            QCOMPARE(q.value(5).toString(), QString("Transfer"));   // joined to the imported bag
        });
    }

    void importDatabaseMergeDedupesBags() {
        const QString srcPath = freshDb();
        const QString destPath = freshDb();
        withRawDb(srcPath, "dedup_src", [&](QSqlDatabase& db) {
            CoffeeBag bag;
            bag.roasterName = "Same";
            bag.coffeeName = "Coffee";
            bag.roastDate = "2026-06-01";
            QVERIFY(CoffeeBagStorage::insertBagStatic(db, bag) > 0);
            insertShot(db, "Same", "Coffee", 3000, QString(), 1);
        });
        qint64 destBagId = -1;
        withRawDb(destPath, "dedup_dest", [&](QSqlDatabase& db) {
            CoffeeBag bag;
            bag.roasterName = "same";  // case-insensitive identity
            bag.coffeeName = "coffee";
            bag.roastDate = "2026-06-01";
            destBagId = CoffeeBagStorage::insertBagStatic(db, bag);
        });

        QVERIFY(ShotHistoryStorage::importDatabaseStatic(destPath, srcPath, /*merge=*/true));

        withRawDb(destPath, "dedup_check", [&](QSqlDatabase& db) {
            QCOMPARE(CoffeeBagStorage::loadInventoryStatic(db).size(), 1);  // no duplicate
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT bag_id FROM shots LIMIT 1"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toLongLong(), destBagId);  // remapped to the existing bag
        });
    }

    // Recipe transfer (finish-recipes-first-class): recipes ride in shots.db and
    // must merge with equipment_id remapped and shots.recipe_id remapped — the
    // provenance that used to dangle after transfer.
    void importDatabaseRemapsRecipeIdsAndEquipment() {
        const QString srcPath = freshDb();
        const QString destPath = freshDb();

        qint64 srcPkgId = -1, srcRecipeId = -1;
        withRawDb(srcPath, "rec_imp_src", [&](QSqlDatabase& db) {
            EquipmentPackage pkg;
            srcPkgId = EquipmentStorage::createPackageWithGrinderStatic(db, pkg, "Niche", "Zero", "63mm");
            QVERIFY(srcPkgId > 0);

            Recipe r;
            r.name = "Americano";
            r.profileTitle = "Filter 2.0";
            r.equipmentId = srcPkgId;
            r.hotWaterJson = "{\"hasWater\":true,\"vesselName\":\"Cup\",\"volume\":120,"
                             "\"mode\":\"volume\",\"flowRate\":40,\"temperatureC\":90,\"order\":\"after\"}";
            srcRecipeId = RecipeStorage::insertRecipeStatic(db, r);
            QVERIFY(srcRecipeId > 0);

            QSqlQuery q(db);
            q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, recipe_id) "
                      "VALUES ('rec-uuid-1', 5000, 'Filter 2.0', 30, :rid)");
            q.bindValue(":rid", srcRecipeId);
            QVERIFY(q.exec());
        });

        // Occupy dest row ids so both remaps are observable (src ids must NOT
        // map to the same dest ids).
        withRawDb(destPath, "rec_imp_dest_prep", [&](QSqlDatabase& db) {
            EquipmentPackage filler;
            QVERIFY(EquipmentStorage::createPackageWithGrinderStatic(db, filler, "Filler", "Grinder", "40mm") > 0);
            Recipe fillerR;
            fillerR.name = "Filler";
            fillerR.profileTitle = "X";
            QVERIFY(RecipeStorage::insertRecipeStatic(db, fillerR) > 0);
        });

        QVERIFY(ShotHistoryStorage::importDatabaseStatic(destPath, srcPath, /*merge=*/true));

        withRawDb(destPath, "rec_imp_check", [&](QSqlDatabase& db) {
            QCOMPARE(RecipeStorage::loadInventoryStatic(db).size(), 2);  // filler + imported
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT s.recipe_id, r.name, r.equipment_id, r.hot_water_json "
                           "FROM shots s JOIN recipes r ON r.id = s.recipe_id "
                           "JOIN equipment_packages p ON p.id = r.equipment_id "
                           "WHERE s.uuid = 'rec-uuid-1'"));
            QVERIFY(q.next());                                     // JOINs matched → no dangling refs
            QVERIFY(q.value(0).toLongLong() != srcRecipeId);      // recipe_id remapped
            QCOMPARE(q.value(1).toString(), QString("Americano"));
            QVERIFY(q.value(2).toLongLong() != srcPkgId);         // equipment_id remapped to a real package
            QVERIFY(q.value(3).toString().contains("hasWater"));  // hot-water block carried verbatim
        });
    }

    void importDatabaseMergeDedupesRecipes() {
        const QString srcPath = freshDb();
        const QString destPath = freshDb();
        withRawDb(srcPath, "rec_dedup_src", [&](QSqlDatabase& db) {
            Recipe r;
            r.name = "Morning";
            r.profileTitle = "D-Flow";
            const qint64 rid = RecipeStorage::insertRecipeStatic(db, r);
            QVERIFY(rid > 0);
            // A shot must exist or importDatabaseStatic treats the source as an
            // empty backup and skips the recipe merge entirely (same guard the
            // bag dedup test satisfies). Link it so the remap is exercised too.
            QSqlQuery q(db);
            q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, recipe_id) "
                      "VALUES ('rec-dedup-uuid', 3000, 'D-Flow', 30, :rid)");
            q.bindValue(":rid", rid);
            QVERIFY(q.exec());
        });
        qint64 destRecipeId = -1;
        withRawDb(destPath, "rec_dedup_dest", [&](QSqlDatabase& db) {
            Recipe r;
            r.name = "morning";      // case-insensitive identity
            r.profileTitle = "d-flow";
            destRecipeId = RecipeStorage::insertRecipeStatic(db, r);
            QVERIFY(destRecipeId > 0);
        });

        QVERIFY(ShotHistoryStorage::importDatabaseStatic(destPath, srcPath, /*merge=*/true));

        withRawDb(destPath, "rec_dedup_check", [&](QSqlDatabase& db) {
            QCOMPARE(RecipeStorage::loadInventoryStatic(db).size(), 1);  // no duplicate
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT recipe_id FROM shots WHERE uuid = 'rec-dedup-uuid'"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toLongLong(), destRecipeId);  // remapped to the existing recipe
        });
    }

    // Inventory lifecycle signals (recipe-bag-lifecycle triggers): any
    // update carrying inInventory=false emits bagFinished (roll-on-finish
    // hook); inInventory=true emits bagRestocked (wake-on-restock hook);
    // updates not touching inventory — and failed updates — emit neither.
    void inventoryLifecycleSignals() {
        const QString path = freshDb();
        qint64 bagId = 0;
        withRawDb(path, "lifecycle_setup", [&](QSqlDatabase& db) {
            CoffeeBag bag; bag.roasterName = "R"; bag.coffeeName = "C";
            bagId = CoffeeBagStorage::insertBagStatic(db, bag);
        });
        QVERIFY(bagId > 0);

        CoffeeBagStorage storage;
        storage.initialize(path);
        QSignalSpy finished(&storage, &CoffeeBagStorage::bagFinished);
        QSignalSpy restocked(&storage, &CoffeeBagStorage::bagRestocked);
        QSignalSpy updated(&storage, &CoffeeBagStorage::bagUpdated);

        // Mark empty (the card's Bag Finished button) → bagFinished.
        storage.requestMarkEmpty(bagId);
        QTRY_COMPARE(updated.count(), 1);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(0).toLongLong(), bagId);
        QCOMPARE(restocked.count(), 0);

        // Return to inventory (MCP/web-style update) → bagRestocked.
        storage.requestUpdateBag(bagId, {{"inInventory", true}});
        QTRY_COMPARE(updated.count(), 2);
        QCOMPARE(restocked.count(), 1);
        QCOMPARE(restocked.at(0).at(0).toLongLong(), bagId);
        QCOMPARE(finished.count(), 1);

        // An update that doesn't touch inventory → neither.
        storage.requestUpdateBag(bagId, {{"notes", "tasty"}});
        QTRY_COMPARE(updated.count(), 3);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(restocked.count(), 1);

        // A FAILED update (missing row) → neither.
        storage.requestUpdateBag(999999, {{"inInventory", false}});
        QTRY_COMPARE(updated.count(), 4);
        QCOMPARE(updated.at(3).at(1).toBool(), false);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(restocked.count(), 1);
    }

    void migration24AddsSyncPendingColumn() {
        // Fresh DB: the column exists (CREATE TABLE path) with default 0.
        const QString path = freshDb();
        withRawDb(path, "m24_fresh", [&](QSqlDatabase& db) {
            QVERIFY(hasColumn(db, "coffee_bags", "visualizer_sync_pending"));
            CoffeeBag bag;
            bag.roasterName = "R";
            bag.coffeeName = "C";
            const qint64 id = CoffeeBagStorage::insertBagStatic(db, bag);
            const CoffeeBag loaded = CoffeeBagStorage::loadBagStatic(db, id);
            QVERIFY(!loaded.visualizerSyncPending);
            // Round-trips through the kCols update map.
            QVERIFY(CoffeeBagStorage::updateBagFieldsStatic(
                db, id, {{"visualizerSyncPending", true}}));
            QVERIFY(CoffeeBagStorage::loadBagStatic(db, id).visualizerSyncPending);
        });

        // Upgrade path: fake a v23 database (column stripped), re-run the chain.
        withRawDb(path, "m24_strip", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("ALTER TABLE coffee_bags DROP COLUMN visualizer_sync_pending"));
            QVERIFY(q.exec("DELETE FROM schema_version"));
            QVERIFY(q.exec("INSERT INTO schema_version (version) VALUES (23)"));
        });
        {
            ShotHistoryStorage storage;
            QVERIFY(initAndClose(path, storage));  // runs migration 24
        }
        withRawDb(path, "m24_check", [&](QSqlDatabase& db) {
            QVERIFY(hasColumn(db, "coffee_bags", "visualizer_sync_pending"));
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT COALESCE(visualizer_sync_pending, -1) FROM coffee_bags LIMIT 1"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 0);  // existing rows default to 0
            QVERIFY(q.exec("SELECT version FROM schema_version"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 29);  // chain runs on to the latest (recipes bag_id)
        });
    }

    void importToleratesSourceWithoutNewerColumns() {
        // A backup written by an older app version lacks newer coffee_bags
        // columns; the import SELECT substitutes NULL so the restore still
        // works (fields land on struct defaults).
        const QString srcPath = freshDb();
        const QString destPath = freshDb();
        withRawDb(srcPath, "old_src", [&](QSqlDatabase& db) {
            CoffeeBag bag;
            bag.roasterName = "Old Backup";
            bag.coffeeName = "Roast";
            QVERIFY(CoffeeBagStorage::insertBagStatic(db, bag) > 0);
            insertShot(db, "Old Backup", "Roast", 4000, QString(), 1);
            QSqlQuery q(db);
            // Strip columns newer than the pretend source version.
            QVERIFY(q.exec("ALTER TABLE coffee_bags DROP COLUMN visualizer_sync_pending"));
            QVERIFY(q.exec("ALTER TABLE coffee_bags DROP COLUMN equipment_id"));
            QVERIFY(q.exec("ALTER TABLE coffee_bags DROP COLUMN rpm"));
            // kind is NOT NULL DEFAULT 'coffee' in the dest: the missing
            // source column reads as "" and must normalize at bind time
            // (bindKind), not blow up the INSERT with an explicit NULL.
            QVERIFY(q.exec("ALTER TABLE coffee_bags DROP COLUMN kind"));
        });

        QVERIFY(ShotHistoryStorage::importDatabaseStatic(destPath, srcPath, /*merge=*/true));

        withRawDb(destPath, "old_src_check", [&](QSqlDatabase& db) {
            const QVector<InventoryBag> bags = CoffeeBagStorage::loadInventoryStatic(db);
            QCOMPARE(bags.size(), 1);
            QCOMPARE(bags.first().bag.roasterName, QString("Old Backup"));
            QVERIFY(!bags.first().bag.visualizerSyncPending);
            QCOMPARE(bags.first().bag.equipmentId, qint64(0));
            QCOMPARE(bags.first().bag.kind, QString("coffee"));
        });
    }

    void rankedProfilesForBean() {
        const QString path = freshDb();
        withRawDb(path, "ranked", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            const auto shot = [&](const QString& profile, qint64 ts, const QString& brand,
                                  const QString& type, const QString& roast, qint64 bagId = -1) {
                q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, "
                          "bean_brand, bean_type, roast_level, bag_id) VALUES (?,?,?,30,?,?,?,?)");
                q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
                q.addBindValue(ts);
                q.addBindValue(profile);
                q.addBindValue(brand);
                q.addBindValue(type);
                q.addBindValue(roast);
                q.addBindValue(bagId > 0 ? QVariant(bagId) : QVariant());
                QVERIFY(q.exec());
            };

            // Coffee: this bean (R/Ethiopia, Light) used P1 then P2; a similar
            // Light bean used P3 and P2 (P2 must dedupe out of tier 2); a Dark
            // bean used P4 (wrong roast, excluded).
            shot("P1", 100, "R", "Ethiopia", "Light");
            shot("P2", 200, "R", "Ethiopia", "Light");
            shot("P3", 300, "R", "Colombia", "Light");
            shot("P2", 250, "R", "Colombia", "Light");
            shot("P4", 400, "R", "Brazil", "Dark");

            QVariantMap r = ShotHistoryStorage::loadRankedProfilesForBeanStatic(
                db, "R", "Ethiopia", "Light");
            QVariantList withBean = r.value("withBean").toList();
            QVariantList similar = r.value("similar").toList();
            QCOMPARE(withBean.size(), 2);
            QCOMPARE(withBean.at(0).toMap().value("profileName").toString(), QString("P2"));
            QCOMPARE(withBean.at(0).toMap().value("lastUsed").toLongLong(), qint64(200));
            QCOMPARE(withBean.at(1).toMap().value("profileName").toString(), QString("P1"));
            QCOMPARE(similar.size(), 1);
            QCOMPARE(similar.at(0).toMap().value("profileName").toString(), QString("P3"));

            // Tea: similarity is the bag blob's teaType, not roast level.
            CoffeeBag t1; t1.roasterName = "F&M"; t1.coffeeName = "Royal Blend";
            t1.kind = "tea"; t1.beanBaseData = "{\"teaType\":\"black\"}";
            CoffeeBag t2; t2.roasterName = "Harney"; t2.coffeeName = "Ceylon";
            t2.kind = "tea"; t2.beanBaseData = "{\"teaType\":\"Black\"}";
            CoffeeBag t3; t3.roasterName = "YS"; t3.coffeeName = "Bi Luo Chun";
            t3.kind = "tea"; t3.beanBaseData = "{\"teaType\":\"green\"}";
            const qint64 b1 = CoffeeBagStorage::insertBagStatic(db, t1);
            const qint64 b2 = CoffeeBagStorage::insertBagStatic(db, t2);
            const qint64 b3 = CoffeeBagStorage::insertBagStatic(db, t3);
            QVERIFY(b1 > 0 && b2 > 0 && b3 > 0);
            shot("TeaBlack", 500, "F&M", "Royal Blend", "", b1);
            shot("TeaOther", 600, "Harney", "Ceylon", "", b2);
            shot("TeaGreen", 700, "YS", "Bi Luo Chun", "", b3);

            r = ShotHistoryStorage::loadRankedProfilesForBeanStatic(
                db, "F&M", "Royal Blend", QString(), "black");
            QCOMPARE(r.value("withBean").toList().size(), 1);
            QCOMPARE(r.value("withBean").toList().at(0).toMap().value("profileName").toString(),
                     QString("TeaBlack"));
            QCOMPARE(r.value("similar").toList().size(), 1);
            QCOMPARE(r.value("similar").toList().at(0).toMap().value("profileName").toString(),
                     QString("TeaOther"));

            // Bean-less request: no tiers, no noise.
            r = ShotHistoryStorage::loadRankedProfilesForBeanStatic(db, "", "", "");
            QVERIFY(r.value("withBean").toList().isEmpty());
            QVERIFY(r.value("similar").toList().isEmpty());
        });
    }

    void latestShotForBeanProfilePrefill() {
        const QString path = freshDb();
        withRawDb(path, "prefill", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            const auto shot = [&](qint64 ts, double dose, const QVariant& yieldOverride,
                                  double finalW, const QString& grind) {
                q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, "
                          "bean_brand, bean_type, dose_weight, yield_override, final_weight, "
                          "temperature_override, grinder_setting, rpm) "
                          "VALUES (?,?,'P',30,'R','Ethiopia',?,?,?,92.5,?,90)");
                q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
                q.addBindValue(ts);
                q.addBindValue(dose);
                q.addBindValue(yieldOverride);
                q.addBindValue(finalW);
                q.addBindValue(grind);
                QVERIFY(q.exec());
            };
            shot(100, 17.0, 34.0, 35.2, "14");
            shot(200, 18.0, 40.0, 41.1, "15");   // most recent wins
            shot(150, 18.5, QVariant(), 39.0, "16");

            QVariantMap s = ShotHistoryStorage::loadLatestShotForBeanProfileStatic(
                db, "R", "Ethiopia", "P");
            QCOMPARE(s.value("doseWeightG").toDouble(), 18.0);
            QCOMPARE(s.value("targetWeightG").toDouble(), 40.0);  // SAW target, not final
            QCOMPARE(s.value("temperatureOverrideC").toDouble(), 92.5);
            QCOMPARE(s.value("grinderSetting").toString(), QString("15"));
            QCOMPARE(s.value("rpm").toLongLong(), qint64(90));

            // No yield override on the newest shot -> final weight fallback.
            shot(300, 19.0, QVariant(), 42.3, "17");
            s = ShotHistoryStorage::loadLatestShotForBeanProfileStatic(db, "R", "Ethiopia", "P");
            QCOMPARE(s.value("targetWeightG").toDouble(), 42.3);

            // Unknown pair -> empty map.
            QVERIFY(ShotHistoryStorage::loadLatestShotForBeanProfileStatic(
                        db, "R", "Kenya", "P").isEmpty());
        });
    }

    void latestGrindForBean() {
        const QString path = freshDb();
        withRawDb(path, "beangrind", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            const auto shot = [&](qint64 ts, const QString& brand, const QString& type,
                                  const QString& roast, const QString& grind,
                                  const QString& profile) {
                q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, "
                          "bean_brand, bean_type, roast_level, grinder_setting, rpm) "
                          "VALUES (?,?,?,30,?,?,?,?,90)");
                q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
                q.addBindValue(ts);
                q.addBindValue(profile);
                q.addBindValue(brand);
                q.addBindValue(type);
                q.addBindValue(roast);
                q.addBindValue(grind.isEmpty() ? QVariant() : QVariant(grind));
                QVERIFY(q.exec());
            };
            shot(100, "R", "Ethiopia", "Light", "14", "D-Flow / default");
            shot(200, "R", "Ethiopia", "Light", "15", "Blooming Espresso");  // newest for the bean
            shot(300, "R", "Ethiopia", "Light", "",   "Default");            // no grind: skipped
            shot(400, "R", "Kenya",    "Light", "16", "Rao Allongé");        // similar roast, newer

            // Exact bean wins over the newer similar-roast shot, and the
            // grind-less newest row is skipped.
            QVariantMap g = ShotHistoryStorage::loadLatestGrindForBeanStatic(
                db, "R", "Ethiopia", "Light");
            QCOMPARE(g.value("grinderSetting").toString(), QString("15"));
            QCOMPARE(g.value("profileName").toString(), QString("Blooming Espresso"));
            QCOMPARE(g.value("matchLevel").toString(), QString("bean"));

            // Unknown bean falls back to the same-roast lane.
            g = ShotHistoryStorage::loadLatestGrindForBeanStatic(db, "R", "Colombia", "Light");
            QCOMPARE(g.value("grinderSetting").toString(), QString("16"));
            QCOMPARE(g.value("matchLevel").toString(), QString("similarRoast"));

            // Nothing matches: empty map.
            QVERIFY(ShotHistoryStorage::loadLatestGrindForBeanStatic(
                        db, "X", "Y", "Dark").isEmpty());
        });
    }

    void teaBrewingBlobParsing() {
        // Full tea blob: everything lands typed and normalized.
        const TeaBrewingData full = CoffeeBag::teaBrewingFromBlob(
            "{\"teaType\":\"Black\",\"origin\":\"Sri Lanka\",\"brewTempC\":100,"
            "\"leafGramsPer100Ml\":0.85,\"steepTime\":\"3-5 minutes\"}");
        QCOMPARE(full.teaType, QString("black"));
        QCOMPARE(full.brewTempC, 100.0);
        QCOMPARE(full.leafGramsPer100Ml, 0.85);
        QCOMPARE(full.steepTime, QString("3-5 minutes"));

        // String-encoded numerics (the extraction fill + blob merge write
        // strings — live-caught): parse identically to native numbers.
        const TeaBrewingData strings = CoffeeBag::teaBrewingFromBlob(
            "{\"teaType\":\"black\",\"brewTempC\":\"100\",\"leafGramsPer100Ml\":\"0.84\"}");
        QCOMPARE(strings.brewTempC, 100.0);
        QCOMPARE(strings.leafGramsPer100Ml, 0.84);

        // Vendor stated nothing (Yunnan Sourcing case): defaults, no guesses.
        const TeaBrewingData sparse = CoffeeBag::teaBrewingFromBlob(
            "{\"teaType\":\"black\",\"origin\":\"Yunnan\"}");
        QCOMPARE(sparse.teaType, QString("black"));
        QCOMPARE(sparse.brewTempC, 0.0);
        QCOMPARE(sparse.leafGramsPer100Ml, 0.0);
        QVERIFY(sparse.steepTime.isEmpty());

        // String-typed numbers (the extraction->form->blob path stores every
        // value as a string) parse the same as native numbers.
        const TeaBrewingData stringly = CoffeeBag::teaBrewingFromBlob(
            "{\"teaType\":\"green\",\"brewTempC\":\"80\",\"leafGramsPer100Ml\":\"0.85\"}");
        QCOMPARE(stringly.brewTempC, 80.0);
        QCOMPARE(stringly.leafGramsPer100Ml, 0.85);

        // Empty / invalid blobs are tolerated.
        QVERIFY(CoffeeBag::teaBrewingFromBlob(QString()).teaType.isEmpty());
        QVERIFY(CoffeeBag::teaBrewingFromBlob("not json").teaType.isEmpty());

        // isTea reads the kind, empty kind = coffee.
        CoffeeBag bag;
        QVERIFY(!bag.isTea());
        bag.kind = "tea";
        QVERIFY(bag.isTea());
        bag.kind.clear();
        QVERIFY(!bag.isTea());
    }

    void bagKindRoundTripsAndTeaSurvivesImport() {
        // Tea kind round-trips through insert/load, and rides a transfer
        // import verbatim (kCols-driven SELECT + INSERT).
        const QString srcPath = freshDb();
        const QString destPath = freshDb();
        withRawDb(srcPath, "tea_src", [&](QSqlDatabase& db) {
            CoffeeBag tea;
            tea.roasterName = "Fortnum & Mason";
            tea.coffeeName = "Royal Blend";
            tea.kind = "tea";
            const qint64 id = CoffeeBagStorage::insertBagStatic(db, tea);
            QVERIFY(id > 0);
            QCOMPARE(CoffeeBagStorage::loadBagStatic(db, id).kind, QString("tea"));
            insertShot(db, "Fortnum & Mason", "Royal Blend", 5000, QString(), id);
        });

        QVERIFY(ShotHistoryStorage::importDatabaseStatic(destPath, srcPath, /*merge=*/true));

        withRawDb(destPath, "tea_check", [&](QSqlDatabase& db) {
            const QVector<InventoryBag> bags = CoffeeBagStorage::loadInventoryStatic(db);
            QCOMPARE(bags.size(), 1);
            QCOMPARE(bags.first().bag.kind, QString("tea"));
        });
    }

    void migration24RetriesAfterMidStepCrash() {
        // The state the hasColumn guard exists for: crash between the ALTER
        // and the version write (column present, version still 23).
        const QString path = freshDb();
        withRawDb(path, "m24_crash", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(hasColumn(db, "coffee_bags", "visualizer_sync_pending"));  // ALTER "already ran"
            QVERIFY(q.exec("DELETE FROM schema_version"));
            QVERIFY(q.exec("INSERT INTO schema_version (version) VALUES (23)"));
        });
        {
            ShotHistoryStorage storage;
            QVERIFY(initAndClose(path, storage));  // re-runs migration 24
        }
        withRawDb(path, "m24_crash_check", [&](QSqlDatabase& db) {
            QVERIFY(hasColumn(db, "coffee_bags", "visualizer_sync_pending"));
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT version FROM schema_version"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 29);  // chain runs on to the latest (recipes bag_id)
        });
    }

    // Migration 26 (add-recipes) repairs branch-dev DBs that stamped version 25
    // with the OLD recipes table (no rpm_pinned). The fresh-DB path gets the
    // column from ensureTableStatic, so exercising the ALTER branch needs a
    // staged v25 DB with the column dropped — otherwise the "hasColumn" assert
    // passes on migration 25's work, not 26's.
    void migration26AddsRpmPinnedColumn() {
        const QString path = freshDb();
        // Stand the schema up fully, then rewind to a v25-without-rpm_pinned DB.
        {
            ShotHistoryStorage storage;
            QVERIFY(initAndClose(path, storage));
        }
        withRawDb(path, "m26_strip", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(hasColumn(db, "recipes", "rpm_pinned"));  // fresh path had it
            QVERIFY(q.exec("ALTER TABLE recipes DROP COLUMN rpm_pinned"));
            QVERIFY(!hasColumn(db, "recipes", "rpm_pinned"));
            QVERIFY(q.exec("DELETE FROM schema_version"));
            QVERIFY(q.exec("INSERT INTO schema_version (version) VALUES (25)"));
        });
        {
            ShotHistoryStorage storage;
            QVERIFY(initAndClose(path, storage));  // runs migration 26's ALTER
        }
        withRawDb(path, "m26_check", [&](QSqlDatabase& db) {
            QVERIFY(hasColumn(db, "recipes", "rpm_pinned"));
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT version FROM schema_version"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 29);  // chain runs on to the latest (recipes bag_id)
            // The repaired table is writable — insertRecipeStatic binds
            // rpm_pinned unconditionally, so it would fail wholesale if the
            // ALTER hadn't landed.
            Recipe r; r.name = "R"; r.rpmPinned = 1350;
            const qint64 id = RecipeStorage::insertRecipeStatic(db, r);
            QVERIFY(id > 0);
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, id).rpmPinned, (qint64)1350);
        });
    }

    // The entry point of the edit-push retry state machine: an edit made
    // before any upload has probed CM (state Unknown - e.g. offline start)
    // must be parked as sync-pending, NOT silently dropped. Parks before any
    // network I/O, so this runs harness-free.
    void unknownCmParksEditAsSyncPending() {
        const QString path = freshDb();
        qint64 bagId = -1;
        withRawDb(path, "park_seed", [&](QSqlDatabase& db) {
            CoffeeBag bag;
            bag.roasterName = "Park";
            bag.coffeeName = "Me";
            bagId = CoffeeBagStorage::insertBagStatic(db, bag);
        });
        QVERIFY(bagId > 0);

        QNetworkAccessManager nam;
        VisualizerUploader uploader(&nam, /*settings=*/nullptr);
        uploader.setLocalDbPath(path);
        QCOMPARE(uploader.cmState(), VisualizerUploader::CmState::Unknown);
        uploader.updateBagOnVisualizer(bagId);

        // The park is a background single-row write - poll for it.
        bool pending = false;
        QElapsedTimer timer; timer.start();
        while (!pending && timer.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            withRawDb(path, "park_check", [&](QSqlDatabase& db) {
                pending = CoffeeBagStorage::loadBagStatic(db, bagId).visualizerSyncPending;
            });
        }
        QVERIFY2(pending, "edit during CM-Unknown must be parked as sync-pending");
    }

    // ==========================================
    // History lane + merge logic
    // ==========================================

    void historyQueryGroupsAndMergesLinkedUnlinked() {
        const QString path = freshDb();
        withRawDb(path, "hist", [&](QSqlDatabase& db) {
            // Same coffee, unlinked early shots then linked later shots.
            insertShot(db, "Prodigal", "First Batch", 100);
            insertShot(db, "Prodigal", "First Batch", 200, "canon-1", -1, R"({"id":"canon-1"})");
            // A different coffee.
            insertShot(db, "Other", "Beans", 150);

            const QVariantList rows = UnifiedBeanSearchModel::queryHistoryStatic(db, QString());
            QCOMPARE(rows.size(), 2);  // linked+unlinked merged into one

            // The merged Prodigal row keeps the canonical link and the newest epoch.
            QVariantMap prodigal;
            for (const QVariant& v : rows)
                if (v.toMap().value("roasterName") == "Prodigal")
                    prodigal = v.toMap();
            QCOMPARE(prodigal.value("beanBaseId").toString(), QString("canon-1"));
            QCOMPARE(prodigal.value("lastUsedEpoch").toLongLong(), qint64(200));

            // Filter narrows.
            QCOMPARE(UnifiedBeanSearchModel::queryHistoryStatic(db, "Other").size(), 1);
        });
    }

    // Tea re-buy lane (add-recipe-wizard-tea): the kind filter keeps only
    // identities that belong to a known tea bag — matched by bag_id OR by
    // case-insensitive roaster+coffee identity — while kind="" stays unfiltered.
    void historyQueryTeaKindFilter() {
        const QString path = freshDb();
        withRawDb(path, "histkind", [&](QSqlDatabase& db) {
            // A tea bag linked to its shot by bag_id.
            CoffeeBag teaBag; teaBag.roasterName = "Harney"; teaBag.coffeeName = "Ceylon";
            teaBag.kind = "tea";
            const qint64 teaBagId = CoffeeBagStorage::insertBagStatic(db, teaBag);
            insertShot(db, "Harney", "Ceylon", 100, QString(), teaBagId);

            // A tea bag matched only by identity (different case, no bag_id link).
            CoffeeBag teaBag2; teaBag2.roasterName = "Fortnum"; teaBag2.coffeeName = "Royal Blend";
            teaBag2.kind = "tea";
            QVERIFY(CoffeeBagStorage::insertBagStatic(db, teaBag2) > 0);
            insertShot(db, "FORTNUM", "royal blend", 200);  // case differs

            // A coffee bag + shot (must be excluded from the tea lane).
            CoffeeBag coffeeBag; coffeeBag.roasterName = "Onyx"; coffeeBag.coffeeName = "Geometry";
            const qint64 cId = CoffeeBagStorage::insertBagStatic(db, coffeeBag);
            insertShot(db, "Onyx", "Geometry", 300, QString(), cId);

            // Tea filter: exactly the two tea identities, coffee excluded.
            const QVariantList tea = UnifiedBeanSearchModel::queryHistoryStatic(db, QString(), 50, "tea");
            QCOMPARE(tea.size(), 2);
            for (const QVariant& v : tea)
                QVERIFY(v.toMap().value("roasterName").toString() != "Onyx");

            // Unfiltered lane still returns all three (legacy behavior).
            QCOMPARE(UnifiedBeanSearchModel::queryHistoryStatic(db, QString(), 50, QString()).size(), 3);
        });
    }

    void mergeLanesTiersAndAbsorption() {
        // Inventory bag for coffee A; canonical results for A and B;
        // history rows for B (linked) and C (free text).
        QVariantList inventory;
        inventory.append(QVariantMap{
            {"id", 7}, {"roasterName", "Roaster"}, {"coffeeName", "A"},
            {"beanBaseId", "canon-A"}, {"inInventory", true}, {"lastUsedEpoch", 500}});

        QVariantList canonical;
        canonical.append(QVariantMap{{"id", "canon-A"}, {"roasterName", "Roaster"}, {"roastName", "A"}});
        canonical.append(QVariantMap{{"id", "canon-B"}, {"roasterName", "Roaster"}, {"roastName", "B"}});

        QVariantList history;
        history.append(QVariantMap{
            {"roasterName", "Roaster"}, {"coffeeName", "B"}, {"beanBaseId", "canon-B"},
            {"grinderSetting", "15"}, {"lastUsedEpoch", 300}});
        history.append(QVariantMap{
            {"roasterName", "Roaster"}, {"coffeeName", "C"}, {"beanBaseId", ""},
            {"lastUsedEpoch", 400}});

        const QVariantList merged = UnifiedBeanSearchModel::mergeLanes(inventory, canonical, history, QString());

        // Expect: Inventory A (canonical absorbed), HistoryCanonical B (merged),
        // HistoryFreeText C. No CanonicalOnly row.
        QCOMPARE(merged.size(), 3);
        const QVariantMap first = merged[0].toMap();
        QCOMPARE(tierOf(merged[0]), Tier::Inventory);
        QCOMPARE(first.value("coffeeName").toString(), QString("A"));
        QCOMPARE(first.value("id").toInt(), 7);
        QCOMPARE(first.value("bagId").toInt(), 7);  // inventory rows mirror id into bagId

        const QVariantMap second = merged[1].toMap();
        QCOMPARE(tierOf(merged[1]), Tier::HistoryCanonical);
        QCOMPARE(second.value("coffeeName").toString(), QString("B"));
        QCOMPARE(second.value("sources").toString(), QString("beanbase+history"));
        QCOMPARE(second.value("grinderSetting").toString(), QString("15"));  // history grinder carried

        const QVariantMap third = merged[2].toMap();
        QCOMPARE(tierOf(merged[2]), Tier::HistoryFreeText);
        QCOMPARE(third.value("coffeeName").toString(), QString("C"));
        QCOMPARE(third.value("sources").toString(), QString("history"));
    }

    void mergeLanesQueryFiltersInventoryAndHistory() {
        QVariantList inventory;
        inventory.append(QVariantMap{{"id", 1}, {"roasterName", "Alpha"}, {"coffeeName", "One"},
                                     {"beanBaseId", ""}, {"lastUsedEpoch", 10}});
        inventory.append(QVariantMap{{"id", 2}, {"roasterName", "Beta"}, {"coffeeName", "Two"},
                                     {"beanBaseId", ""}, {"lastUsedEpoch", 20}});

        const QVariantList merged = UnifiedBeanSearchModel::mergeLanes(inventory, {}, {}, "alpha");
        QCOMPARE(merged.size(), 1);
        QCOMPARE(merged[0].toMap().value("roasterName").toString(), QString("Alpha"));
    }

    // The previous test deliberately has "No tier2" and no standalone tier3.
    // Cover both: a canonical-only result (tier 2) and a linked-history result
    // that matches neither inventory nor a canonical result (tier 3), alongside
    // a free-text history row (tier 4). Order is tier0+tier1+tier2+tier34.
    void mergeLanesTier2AndTier3() {
        QVariantList canonical;  // canonical-only X → tier 2
        canonical.append(QVariantMap{{"id", "canon-X"}, {"roasterName", "R"}, {"roastName", "X"}});

        QVariantList history;
        history.append(QVariantMap{  // linked but unabsorbed → tier 3
            {"roasterName", "R"}, {"coffeeName", "Y"}, {"beanBaseId", "canon-Y"},
            {"lastUsedEpoch", 200}});
        history.append(QVariantMap{  // free text → tier 4
            {"roasterName", "R"}, {"coffeeName", "Z"}, {"beanBaseId", ""},
            {"lastUsedEpoch", 100}});

        const QVariantList merged = UnifiedBeanSearchModel::mergeLanes({}, canonical, history, QString());
        QCOMPARE(merged.size(), 3);

        QCOMPARE(tierOf(merged[0]), Tier::CanonicalOnly);
        QCOMPARE(merged[0].toMap().value("coffeeName").toString(), QString("X"));
        QCOMPARE(merged[0].toMap().value("sources").toString(), QString("beanbase"));

        QCOMPARE(tierOf(merged[1]), Tier::HistoryLinked);  // HistoryLinked before HistoryFreeText
        QCOMPARE(merged[1].toMap().value("coffeeName").toString(), QString("Y"));
        QCOMPARE(merged[1].toMap().value("sources").toString(), QString("history"));

        QCOMPARE(tierOf(merged[2]), Tier::HistoryFreeText);
        QCOMPARE(merged[2].toMap().value("coffeeName").toString(), QString("Z"));
    }

    // Bean Base's canonical DB holds near-duplicate submissions: same roaster+
    // name under distinct canonical ids, differing only in descriptive attrs.
    // Each must stay its own row (distinct id = distinct pick target) and carry
    // a "detail" line (degree · origin · tastingNotes) so the UI can tell them
    // apart — the exact bug of identical-looking same-name rows.
    void mergeLanesCanonicalDuplicatesCarryDetail() {
        QVariantList canonical;
        canonical.append(QVariantMap{
            {"id", "canon-1"}, {"roasterName", "Sweet Bloom"}, {"roastName", "Hometown"},
            {"degree", "Medium-light"}, {"origin", "Colombia, Ethiopia"},
            {"tastingNotes", "Blackberries, Praline"}});
        canonical.append(QVariantMap{
            {"id", "canon-2"}, {"roasterName", "Sweet Bloom"}, {"roastName", "Hometown"},
            {"degree", "Light To Medium"}, {"origin", "Colombia, Ethiopia"},
            {"tastingNotes", "Blackberries, Cocoa"}});
        canonical.append(QVariantMap{  // no descriptive attrs at all
            {"id", "canon-3"}, {"roasterName", "Sweet Bloom"}, {"roastName", "Hometown"}});

        const QVariantList merged = UnifiedBeanSearchModel::mergeLanes({}, canonical, {}, "hometown");
        QCOMPARE(merged.size(), 3);

        QCOMPARE(merged[0].toMap().value("detail").toString(),
                 QString("Medium-light · Colombia, Ethiopia · Blackberries, Praline"));
        QCOMPARE(merged[1].toMap().value("detail").toString(),
                 QString("Light To Medium · Colombia, Ethiopia · Blackberries, Cocoa"));
        QCOMPARE(merged[2].toMap().value("detail").toString(), QString());
        for (const QVariant& v : merged)
            QCOMPARE(tierOf(v), Tier::CanonicalOnly);
    }

    // A bag created from a canonical row must carry the full descriptive blob,
    // not just the canonical id: with an empty blob the bag renders an empty
    // details popup, and BagCard's link backfill then persists `{"link":…}` as
    // the whole blob (the "broken details after adding Hometown" bug).
    void mergeLanesCanonicalRowsCarryBlob() {
        QVariantList canonical;
        canonical.append(QVariantMap{
            {"id", "canon-X"}, {"roasterName", "Sweet Bloom"}, {"roastName", "Hometown"},
            {"degree", "Light"}, {"origin", "Colombia"},
            {"link", "https://sweetbloomcoffee.com/product/hometown/"}});
        canonical.append(QVariantMap{
            {"id", "canon-Y"}, {"roasterName", "Sweet Bloom"}, {"roastName", "Other"},
            {"degree", "Dark"}});

        QVariantList history;  // canon-Y also in history, with an empty blob
        history.append(QVariantMap{
            {"roasterName", "Sweet Bloom"}, {"coffeeName", "Other"}, {"beanBaseId", "canon-Y"},
            {"beanBaseData", ""}, {"lastUsedEpoch", 100}});

        const QVariantList merged = UnifiedBeanSearchModel::mergeLanes({}, canonical, history, QString());
        QCOMPARE(merged.size(), 2);

        // Tier 1 (canon-Y merged with history): empty history blob falls back
        // to the serialized entry. The detail line rides on tier 1 rows too.
        QCOMPARE(tierOf(merged[0]), Tier::HistoryCanonical);
        const QVariantMap tier1Blob = QJsonDocument::fromJson(
            merged[0].toMap().value("beanBaseData").toString().toUtf8()).object().toVariantMap();
        QCOMPARE(tier1Blob.value("degree").toString(), QString("Dark"));
        QCOMPARE(merged[0].toMap().value("detail").toString(), QString("Dark"));

        // Tier 2 (canon-X): blob is the entry itself — id, identity, attrs, link.
        QCOMPARE(tierOf(merged[1]), Tier::CanonicalOnly);
        const QVariantMap tier2Blob = QJsonDocument::fromJson(
            merged[1].toMap().value("beanBaseData").toString().toUtf8()).object().toVariantMap();
        QCOMPARE(tier2Blob.value("id").toString(), QString("canon-X"));
        QCOMPARE(tier2Blob.value("roastName").toString(), QString("Hometown"));
        QCOMPARE(tier2Blob.value("roasterName").toString(), QString("Sweet Bloom"));
        QCOMPARE(tier2Blob.value("degree").toString(), QString("Light"));
        QCOMPARE(tier2Blob.value("origin").toString(), QString("Colombia"));
        QCOMPARE(tier2Blob.value("link").toString(),
                 QString("https://sweetbloomcoffee.com/product/hometown/"));
    }

    // A history snapshot holding only BagCard's `{"link":…}` backfill artifact
    // (a bag created while canonical rows carried no blob) must NOT shadow the
    // fresh entry — otherwise the pre-fix corruption re-persists into every
    // new bag created from that Tier 1 row.
    void mergeLanesTier1ReplacesLinkOnlyBlob() {
        QVariantList canonical;
        canonical.append(QVariantMap{
            {"id", "canon-W"}, {"roasterName", "R"}, {"roastName", "W"},
            {"degree", "Light"}, {"link", "https://roaster.example/w"}});
        QVariantList history;
        history.append(QVariantMap{
            {"roasterName", "R"}, {"coffeeName", "W"}, {"beanBaseId", "canon-W"},
            {"beanBaseData", "{\"link\":\"https://roaster.example/w\"}"},
            {"lastUsedEpoch", 100}});

        const QVariantList merged = UnifiedBeanSearchModel::mergeLanes({}, canonical, history, QString());
        QCOMPARE(merged.size(), 1);
        QCOMPARE(tierOf(merged[0]), Tier::HistoryCanonical);
        const QVariantMap blob = QJsonDocument::fromJson(
            merged[0].toMap().value("beanBaseData").toString().toUtf8()).object().toVariantMap();
        QCOMPARE(blob.value("degree").toString(), QString("Light"));  // fresh entry won
        QCOMPARE(blob.value("link").toString(), QString("https://roaster.example/w"));
    }

    // Tier 1 keeps a non-empty history blob (it may carry legacy-only fields
    // like a CDN image URL) instead of replacing it with the fresh entry.
    void mergeLanesTier1PrefersHistoryBlob() {
        QVariantList canonical;
        canonical.append(QVariantMap{
            {"id", "canon-Z"}, {"roasterName", "R"}, {"roastName", "Z"}, {"degree", "Light"}});
        QVariantList history;
        history.append(QVariantMap{
            {"roasterName", "R"}, {"coffeeName", "Z"}, {"beanBaseId", "canon-Z"},
            {"beanBaseData", "{\"degree\":\"Light\",\"image\":\"https://cdn/legacy.jpg\"}"},
            {"lastUsedEpoch", 100}});

        const QVariantList merged = UnifiedBeanSearchModel::mergeLanes({}, canonical, history, QString());
        QCOMPARE(merged.size(), 1);
        QCOMPARE(tierOf(merged[0]), Tier::HistoryCanonical);
        QCOMPARE(merged[0].toMap().value("beanBaseData").toString(),
                 QString("{\"degree\":\"Light\",\"image\":\"https://cdn/legacy.jpg\"}"));
    }

    // Within-tier MRU ordering: two inventory bags out of epoch order must come
    // back most-recently-used first (guards the stable_sort comparator).
    void mergeLanesOrdersByMruWithinTier() {
        QVariantList inventory;
        inventory.append(QVariantMap{{"id", 1}, {"roasterName", "R"}, {"coffeeName", "Old"},
                                     {"beanBaseId", ""}, {"lastUsedEpoch", 100}});
        inventory.append(QVariantMap{{"id", 2}, {"roasterName", "R"}, {"coffeeName", "New"},
                                     {"beanBaseId", ""}, {"lastUsedEpoch", 900}});

        const QVariantList merged = UnifiedBeanSearchModel::mergeLanes(inventory, {}, {}, QString());
        QCOMPARE(merged.size(), 2);
        QCOMPARE(merged[0].toMap().value("coffeeName").toString(), QString("New"));  // MRU first
        QCOMPARE(merged[1].toMap().value("coffeeName").toString(), QString("Old"));
    }

    // The TierRole int values are a hard cross-language contract: the emitted
    // role is the raw enum value and ChangeBeansDialog.qml compares it against
    // these literals (notably `model.tier === 0` for Inventory). Pin the
    // numbers so a future enumerator reorder can't silently break QML — the
    // enum-vs-enum assertions elsewhere would not catch that.
    void tierEnumValuesMatchQmlContract() {
        QCOMPARE(static_cast<int>(Tier::Inventory), 0);
        QCOMPARE(static_cast<int>(Tier::HistoryCanonical), 1);
        QCOMPARE(static_cast<int>(Tier::CanonicalOnly), 2);
        QCOMPARE(static_cast<int>(Tier::HistoryLinked), 3);
        QCOMPARE(static_cast<int>(Tier::HistoryFreeText), 4);
    }

    // BagIdRole must expose the real bag id for an inventory row (mirrored from
    // "id"), not -1. ChangeBeansDialog.qml compares `model.bagId ===
    // Settings.dye.activeBagId` to mark the active bag, so a -1 here would
    // never highlight it. Drives the model through data() directly.
    void modelBagIdRoleExposesInventoryBagId() {
        UnifiedBeanSearchModel model;
        model.m_inventory = QVariantList{
            QVariantMap{{"id", 7}, {"roasterName", "Roaster"}, {"coffeeName", "A"},
                        {"beanBaseId", ""}, {"inInventory", true}, {"lastUsedEpoch", 100}}};
        model.rebuild();

        QCOMPARE(model.rowCount(), 1);
        const QModelIndex idx = model.index(0);
        QCOMPARE(model.data(idx, UnifiedBeanSearchModel::TierRole).toInt(),
                 static_cast<int>(Tier::Inventory));
        QCOMPARE(model.data(idx, UnifiedBeanSearchModel::BagIdRole).toLongLong(), qint64(7));
    }

    // ==========================================
    // Dose/yield stamp primitive
    // ==========================================

    void doseStampUpdatesBagWithoutTouchingIdentity() {
        const QString path = freshDb();
        qint64 bagId = -1;
        withRawDb(path, "stamp", [&](QSqlDatabase& db) {
            CoffeeBag bag;
            bag.roasterName = "Stamp";
            bag.coffeeName = "Test";
            bag.doseWeightG = 18.0;
            bagId = CoffeeBagStorage::insertBagStatic(db, bag);

            QVERIFY(CoffeeBagStorage::updateBagFieldsStatic(db, bagId, {
                {"doseWeightG", 18.5}, {"yieldOverrideG", 38.0}, {"lastUsedEpoch", 9999}}));
            const CoffeeBag stamped = CoffeeBagStorage::loadBagStatic(db, bagId);
            QCOMPARE(stamped.doseWeightG, 18.5);
            QCOMPARE(stamped.yieldOverrideG, 38.0);
            QCOMPARE(stamped.lastUsedEpoch, qint64(9999));
            QCOMPARE(stamped.roasterName, QString("Stamp"));
        });
    }

    // touchesVisualizerFields drives the bagVisualizerFieldsChanged signal, so
    // it must fire for fields Visualizer stores on the bean and stay silent for
    // local-only columns (grinder/dose/yield/lastUsed/inInventory/sync-ids) —
    // otherwise a grinder dial-in write-through or a dose/yield stamp would
    // trigger a needless Visualizer PATCH.
    void touchesVisualizerFieldsMembership() {
        // Each Visualizer-stored key fires on its own.
        const QStringList visualizerKeys = {
            "roasterName", "coffeeName", "roastDate", "roastLevel",
            "frozenDate", "defrostDate", "notes", "beanBaseId", "beanBaseData"};
        for (const QString& key : visualizerKeys)
            QVERIFY2(CoffeeBagStorage::touchesVisualizerFields({{key, "x"}}),
                     qPrintable("expected " + key + " to be a Visualizer field"));

        // Local-only keys never fire, alone or together.
        const QStringList localKeys = {
            "grinderBrand", "grinderModel", "grinderBurrs", "grinderSetting",
            "doseWeightG", "yieldOverrideG", "startWeightG", "lastUsedEpoch",
            "inInventory", "visualizerBagId", "visualizerRoasterId",
            "visualizerSyncPending"};
        for (const QString& key : localKeys)
            QVERIFY2(!CoffeeBagStorage::touchesVisualizerFields({{key, "x"}}),
                     qPrintable("expected " + key + " to be local-only"));
        QVERIFY(!CoffeeBagStorage::touchesVisualizerFields({
            {"grinderSetting", "5"}, {"doseWeightG", 18.0}, {"lastUsedEpoch", 1}}));
        QVERIFY(!CoffeeBagStorage::touchesVisualizerFields({}));

        // A mixed update (dose stamp alongside a roast-date edit) still fires.
        QVERIFY(CoffeeBagStorage::touchesVisualizerFields({
            {"doseWeightG", 18.0}, {"roastDate", "2026-06-01"}}));
    }

    // yieldOverrideForTarget is the shared "is this an override?" rule used by
    // the shot-save stamp and the brew-settings commit. The realistic bug it
    // guards: a flipped comparison or dropped epsilon would pin the bag to the
    // profile default after a plain pour, turning the idle widget yellow forever.
    void yieldOverrideForTargetRule() {
        // Plain profile-default pour → 0 (no override, bag follows profile).
        QCOMPARE(CoffeeBagStorage::yieldOverrideForTarget(36.0, 36.0), 0.0);
        // Within epsilon of the default, both sides → still 0 (guards a one-sided
        // qAbs bug). Note: an exact 0.1-boundary case isn't asserted — 36.1-36.0
        // in double is ~0.10000000000000142, already past the strict-> threshold.
        QCOMPARE(CoffeeBagStorage::yieldOverrideForTarget(36.05, 36.0), 0.0);
        QCOMPARE(CoffeeBagStorage::yieldOverrideForTarget(35.95, 36.0), 0.0);
        // A real override → the shot target is recorded (both directions).
        QCOMPARE(CoffeeBagStorage::yieldOverrideForTarget(50.0, 36.0), 50.0);
        QCOMPARE(CoffeeBagStorage::yieldOverrideForTarget(30.0, 36.0), 30.0);
        // No target weight (SAW with no reading / non-SAW fallback) → 0.
        QCOMPARE(CoffeeBagStorage::yieldOverrideForTarget(0.0, 36.0), 0.0);
        // Just past the epsilon → recorded.
        QCOMPARE(CoffeeBagStorage::yieldOverrideForTarget(36.2, 36.0), 36.2);
    }

    // SettingsDye is the bag-as-bean-state orchestrator and had no coverage of
    // its yield-override path (no test attached a CoffeeBagStorage before). This
    // drives the real async apply chain (setActiveBagId -> bagReady ->
    // applyActiveBag -> activeBagYieldOverrideApplied), the write-through, and the
    // persist clamp — using only the public API.
    void settingsDyeYieldOverridePath() {
        // Isolate from any developer/CI dye state in the test app's QSettings.
        { QSettings s; s.remove(QStringLiteral("dye")); s.sync(); }

        const QString path = freshDb();
        CoffeeBagStorage storage;
        storage.initialize(path);

        // A bag WITH an override and one WITHOUT, inserted directly.
        qint64 bagOverride = -1, bagPlain = -1;
        withRawDb(path, "dye_seed", [&](QSqlDatabase& db) {
            CoffeeBag a; a.roasterName = "R"; a.coffeeName = "Override"; a.yieldOverrideG = 42.0;
            bagOverride = CoffeeBagStorage::insertBagStatic(db, a);
            CoffeeBag b; b.roasterName = "R"; b.coffeeName = "Plain";  // yieldOverrideG defaults 0
            bagPlain = CoffeeBagStorage::insertBagStatic(db, b);
        });
        QVERIFY(bagOverride > 0 && bagPlain > 0);

        SettingsVisualizer viz;
        SettingsDye dye(&viz);
        dye.setBagStorage(&storage);

        // Selecting the override bag re-applies its override and exposes it.
        QSignalSpy overrideSpy(&dye, &SettingsDye::activeBagYieldOverrideApplied);
        dye.setActiveBagId(static_cast<int>(bagOverride));
        QTRY_VERIFY(overrideSpy.count() >= 1);
        QCOMPARE(overrideSpy.last().at(0).toDouble(), 42.0);
        QCOMPARE(dye.activeBagYieldOverrideG(), 42.0);

        // Switching to the plain bag emits 0 (no override → follow profile).
        overrideSpy.clear();
        dye.setActiveBagId(static_cast<int>(bagPlain));
        QTRY_VERIFY(overrideSpy.count() >= 1);
        QCOMPARE(overrideSpy.last().at(0).toDouble(), 0.0);
        QCOMPARE(dye.activeBagYieldOverrideG(), 0.0);

        // persistYieldOverrideToBag clamps <=0 to 0 (no override) and keeps >0.
        // The cache is set synchronously; asserting it (rather than racing a DB
        // read against the storage's background writers) keeps this deterministic.
        // The write-through to the DB column is covered by the dose/yield stamp
        // and updateBagFieldsStatic tests.
        dye.persistYieldOverrideToBag(50.0);
        QCOMPARE(dye.activeBagYieldOverrideG(), 50.0);
        dye.persistYieldOverrideToBag(-5.0);
        QCOMPARE(dye.activeBagYieldOverrideG(), 0.0);
        dye.persistYieldOverrideToBag(0.0);
        QCOMPARE(dye.activeBagYieldOverrideG(), 0.0);

        // Drain the storage's async work to completion before the stack objects
        // destruct, so no worker is still holding a connection at teardown (which
        // would qWarning on stderr — the suite requires silence).
        for (int i = 0; i < 40; i++) { QCoreApplication::processEvents(); QThread::msleep(25); }
        { QSettings s; s.remove(QStringLiteral("dye")); s.sync(); }
    }

    // SettingsDye is the equipment switch + dual-write-through orchestrator
    // (add-equipment-packages). Drives the real async chain via the public API:
    // switchToEquipment applies the package's identity + last dial and points the
    // active bag at it; editing the dial fans out to BOTH the bag and the active
    // package's last-dial memory.
    void settingsDyeEquipmentSwitchAndDualWrite() {
        { QSettings s; s.remove(QStringLiteral("dye")); s.sync(); }

        const QString path = freshDb();
        withRawDb(path, "eqdye_tables", [&](QSqlDatabase& db) {
            CoffeeBagStorage::ensureTableStatic(db);
            EquipmentStorage::ensureTablesStatic(db);
        });
        qint64 bagId = -1, pkgId = -1;
        withRawDb(path, "eqdye_seed", [&](QSqlDatabase& db) {
            CoffeeBag b; b.roasterName = "R"; b.coffeeName = "C";
            bagId = CoffeeBagStorage::insertBagStatic(db, b);
            EquipmentPackage p; p.lastGrindSetting = "3.0"; p.lastRpm = 1200;
            pkgId = EquipmentStorage::createPackageWithGrinderStatic(db, p, "Turin", "DF83V", "83mm flat steel");
        });
        QVERIFY(bagId > 0 && pkgId > 0);

        CoffeeBagStorage bagStorage; bagStorage.initialize(path);
        EquipmentStorage eqStorage; eqStorage.initialize(path);
        SettingsVisualizer viz; SettingsDye dye(&viz);
        dye.setBagStorage(&bagStorage);
        dye.setEquipmentStorage(&eqStorage);

        // Select the bag and let its async apply settle (applyActiveBag sets
        // activeEquipmentId from the bag's equipment_id, which would otherwise
        // race the switch below).
        dye.setActiveBagId(static_cast<int>(bagId));
        for (int i = 0; i < 40; i++) { QCoreApplication::processEvents(); QThread::msleep(10); }

        // Switch equipment: identity + last dial applied synchronously from the map.
        QVariantMap pkg;
        pkg["id"] = static_cast<qlonglong>(pkgId);
        pkg["grinderBrand"] = "Turin"; pkg["grinderModel"] = "DF83V"; pkg["grinderBurrs"] = "83mm flat steel";
        pkg["lastGrindSetting"] = "3.0"; pkg["lastRpm"] = static_cast<qlonglong>(1200);
        dye.switchToEquipment(pkg);
        QCOMPARE(dye.activeEquipmentId(), static_cast<int>(pkgId));
        QCOMPARE(dye.dyeGrinderModel(), QString("DF83V"));
        QCOMPARE(dye.dyeGrinderSetting(), QString("3.0"));
        QCOMPARE(dye.dyeGrinderRpm(), 1200);

        auto bagEq = [&]() { qint64 v = -1; withRawDb(path, "eqdye_bageq",
            [&](QSqlDatabase& db) { v = CoffeeBagStorage::loadBagStatic(db, bagId).equipmentId; }); return v; };
        // Generous timeout: these are cross-thread background DB write-throughs
        // (QThread::create + withTempDb) whose latency spikes under full-suite
        // load — the default 5s QTRY window occasionally lapses on a cold/
        // contended run. 15s is ample headroom; a real logic failure still fails.
        QTRY_COMPARE_WITH_TIMEOUT(bagEq(), pkgId, 15000);   // bag adopted the package

        // Dual write-through: editing the dial updates BOTH the bag and the
        // active package's last dial. Edit one field at a time and let each land
        // (as the user would) — firing both at once would race two concurrent
        // background writes to the same DB.
        auto bagGrind = [&]() { QString v; withRawDb(path, "eqdye_bg",
            [&](QSqlDatabase& db) { v = CoffeeBagStorage::loadBagStatic(db, bagId).grinderSetting; }); return v; };
        auto pkgGrind = [&]() { QString v; withRawDb(path, "eqdye_pg",
            [&](QSqlDatabase& db) { v = EquipmentStorage::loadPackageStatic(db, pkgId).lastGrindSetting; }); return v; };
        auto bagRpm = [&]() { qint64 v = -1; withRawDb(path, "eqdye_br",
            [&](QSqlDatabase& db) { v = CoffeeBagStorage::loadBagStatic(db, bagId).rpm; }); return v; };
        auto pkgRpm = [&]() { qint64 v = -1; withRawDb(path, "eqdye_pr",
            [&](QSqlDatabase& db) { v = EquipmentStorage::loadPackageStatic(db, pkgId).lastRpm; }); return v; };

        dye.setDyeGrinderSetting("2.5");
        QTRY_COMPARE_WITH_TIMEOUT(bagGrind(), QString("2.5"), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(pkgGrind(), QString("2.5"), 15000);

        dye.setDyeGrinderRpm(1350);
        QTRY_COMPARE_WITH_TIMEOUT(bagRpm(), static_cast<qint64>(1350), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(pkgRpm(), static_cast<qint64>(1350), 15000);

        for (int i = 0; i < 40; i++) { QCoreApplication::processEvents(); QThread::msleep(10); }
        { QSettings s; s.remove(QStringLiteral("dye")); s.sync(); }
    }

    // Pinned-grind routing (add-recipes): while the active recipe PINS its
    // grind, SettingsDye suspends the bag write-through — the pin is the
    // recipe's private dial, and sibling recipes (which inherit the bag's
    // value) must not follow it. MainController sets/clears the suspension;
    // here we drive the flag directly and assert the bag row stays put.
    void settingsDyeGrindPinSuspendsBagWriteThrough() {
        { QSettings s; s.remove(QStringLiteral("dye")); s.sync(); }

        const QString path = freshDb();
        withRawDb(path, "pin_tables", [&](QSqlDatabase& db) {
            CoffeeBagStorage::ensureTableStatic(db);
        });
        qint64 bagId = -1;
        withRawDb(path, "pin_seed", [&](QSqlDatabase& db) {
            CoffeeBag b; b.roasterName = "R"; b.coffeeName = "Pin"; b.grinderSetting = "1.0";
            bagId = CoffeeBagStorage::insertBagStatic(db, b);
        });
        QVERIFY(bagId > 0);

        CoffeeBagStorage bagStorage; bagStorage.initialize(path);
        SettingsVisualizer viz; SettingsDye dye(&viz);
        dye.setBagStorage(&bagStorage);

        dye.setActiveBagId(static_cast<int>(bagId));
        QTRY_COMPARE_WITH_TIMEOUT(dye.dyeGrinderSetting(), QString("1.0"), 15000);

        auto bagGrind = [&]() { QString v; withRawDb(path, "pin_bg",
            [&](QSqlDatabase& db) { v = CoffeeBagStorage::loadBagStatic(db, bagId).grinderSetting; }); return v; };

        // Unsuspended: the edit writes through to the bag row.
        dye.setDyeGrinderSetting("2.0");
        QTRY_COMPARE_WITH_TIMEOUT(bagGrind(), QString("2.0"), 15000);

        // Suspended (recipe pin active): the edit stays on the dye cache; the
        // bag keeps its value. Drain the worker before the negative read so a
        // wrongly-issued write would have landed by the time we assert.
        dye.setGrindBagWriteThroughSuspended(true);
        dye.setDyeGrinderSetting("3.0");
        QCOMPARE(dye.dyeGrinderSetting(), QString("3.0"));
        for (int i = 0; i < 20; i++) { QCoreApplication::processEvents(); QThread::msleep(10); }
        QCOMPARE(bagGrind(), QString("2.0"));  // unchanged — the pin never landed

        // Lifting the suspension resumes normal bean-dial write-through.
        dye.setGrindBagWriteThroughSuspended(false);
        dye.setDyeGrinderSetting("4.0");
        QTRY_COMPARE_WITH_TIMEOUT(bagGrind(), QString("4.0"), 15000);

        // RPM is pinned WITH grind: the same suspension gates its bag
        // write-through independently (an rpm-only leak would silently pollute
        // the bean's dial). Establish a baseline, then set rpm while suspended.
        auto bagRpm = [&]() { qint64 v = -1; withRawDb(path, "pin_br",
            [&](QSqlDatabase& db) { v = CoffeeBagStorage::loadBagStatic(db, bagId).rpm; }); return v; };
        dye.setDyeGrinderRpm(1200);
        QTRY_COMPARE_WITH_TIMEOUT(bagRpm(), static_cast<qint64>(1200), 15000);
        dye.setGrindBagWriteThroughSuspended(true);
        dye.setDyeGrinderRpm(1400);
        QCOMPARE(dye.dyeGrinderRpm(), 1400);
        for (int i = 0; i < 20; i++) { QCoreApplication::processEvents(); QThread::msleep(10); }
        QCOMPARE(bagRpm(), static_cast<qint64>(1200));  // pinned rpm never landed on the bag

        // Drain before stack teardown (see settingsDyeYieldOverridePath).
        dye.setGrindBagWriteThroughSuspended(false);
        for (int i = 0; i < 40; i++) { QCoreApplication::processEvents(); QThread::msleep(10); }
        { QSettings s; s.remove(QStringLiteral("dye")); s.sync(); }
    }

    // Regression for the same-row write reorder that SerialDbWorker fixes. Fire
    // many writes at ONE package row back-to-back with NO settle between them and
    // assert the LAST submitted value is the one that SETTLES. Under the old
    // fresh-thread-per-request dispatch the OS scheduler could run an earlier
    // write last, clobbering the newer value (SQLite serializes commits, but not
    // in submission order); the single FIFO worker makes submission order the
    // commit order. We assert the settled value (drain, then re-read) rather than
    // first-observation: QTRY passes the instant it sees "49", and a reverted
    // per-thread build could expose "49" transiently mid-race before an earlier
    // write commits last — only the post-drain re-read distinguishes "49 is final"
    // (FIFO) from "49 was briefly seen" (reordered). (The existing dual-write test
    // above has a settle loop that mostly closes the window, so it only caught the
    // bug flakily — this one is deterministic.)
    void packageWritesApplyInSubmissionOrder() {
        const QString path = freshDb();
        withRawDb(path, "fifo_tables", [&](QSqlDatabase& db) {
            EquipmentStorage::ensureTablesStatic(db);
        });
        qint64 pkgId = -1;
        withRawDb(path, "fifo_seed", [&](QSqlDatabase& db) {
            EquipmentPackage p; p.lastGrindSetting = "seed";
            pkgId = EquipmentStorage::createPackageWithGrinderStatic(db, p, "Turin", "DF83V", "83mm flat steel");
        });
        QVERIFY(pkgId > 0);

        EquipmentStorage eqStorage; eqStorage.initialize(path);

        const int kWrites = 50;
        for (int i = 0; i < kWrites; i++)
            eqStorage.requestUpdatePackage(pkgId,
                {{QStringLiteral("lastGrindSetting"), QString::number(i)}});

        auto pkgGrind = [&]() { QString v; withRawDb(path, "fifo_read",
            [&](QSqlDatabase& db) { v = EquipmentStorage::loadPackageStatic(db, pkgId).lastGrindSetting; }); return v; };
        QTRY_COMPARE_WITH_TIMEOUT(pkgGrind(), QString::number(kWrites - 1), 15000);

        // Drain every write, then re-assert the SETTLED value is still the last
        // submitted. This is the real revert-detector: a reordered build's final
        // commit is the random last-scheduled write, almost never "49".
        for (int i = 0; i < 40; i++) { QCoreApplication::processEvents(); QThread::msleep(5); }
        QCOMPARE(pkgGrind(), QString::number(kWrites - 1));
    }

    // A DB-open FAILURE must not be delivered to readers as an empty result.
    // SettingsDye reads an empty packageReady/bagReady as "row vanished" and
    // clears the active selection, so a transient open failure would silently
    // wipe valid state — hence runAsync gates the read emit on dbOpened. A
    // GENUINE not-found (db opens, row absent) must still emit empty, since that
    // real clear is how a deleted selection gets cleared.
    void readEmitSuppressedOnDbOpenFailure() {
        // (a) Unopenable path (parent dir absent) -> requestPackage stays silent.
        EquipmentStorage bad;
        bad.initialize(m_tempDir.filePath(QStringLiteral("no_such_dir/eq.db")));
        QSignalSpy badSpy(&bad, &EquipmentStorage::packageReady);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("withTempDb: DB open failed"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("SerialDbWorker: failed to open DB"));
        bad.requestPackage(1);
        for (int i = 0; i < 60; i++) { QCoreApplication::processEvents(); QThread::msleep(5); }
        QCOMPARE(badSpy.count(), 0);

        // (b) Real DB, missing row -> the empty result IS delivered (real clear).
        const QString path = freshDb();
        withRawDb(path, "nf_tables", [&](QSqlDatabase& db) { EquipmentStorage::ensureTablesStatic(db); });
        EquipmentStorage ok; ok.initialize(path);
        QSignalSpy okSpy(&ok, &EquipmentStorage::packageReady);
        ok.requestPackage(999999);
        QTRY_COMPARE_WITH_TIMEOUT(okSpy.count(), 1, 15000);
        QVERIFY(okSpy.at(0).at(1).toMap().isEmpty());

        for (int i = 0; i < 40; i++) { QCoreApplication::processEvents(); QThread::msleep(5); }
    }

    // Same contract for ShotHistoryStorage::requestShot, which uses a DIFFERENT
    // code path (post() + hand-rolled `if (!opened) return`, not run()'s gate) and
    // guards the most destructive consumer: MainController's migration16 reads an
    // invalid shotReady as "shot gone" and PERMANENTLY drops a pending Visualizer
    // sync. requestShot's own !m_ready guard shares m_dbPath with the worker, so
    // initialize() can't produce "ready but worker-open-fails" — we set that state
    // directly via the DECENZA_TESTING friend seam.
    void requestShotEmitSuppressedOnDbOpenFailure() {
        // (a) m_ready=true + unopenable path (parent dir absent) -> no shotReady.
        ShotHistoryStorage bad;
        bad.m_ready = true;
        bad.m_dbPath = m_tempDir.filePath(QStringLiteral("no_such_dir/shots.db"));
        QSignalSpy badSpy(&bad, &ShotHistoryStorage::shotReady);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("withTempDb: DB open failed"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("requestShot.*DB open failed"));
        bad.requestShot(1);
        for (int i = 0; i < 60; i++) { QCoreApplication::processEvents(); QThread::msleep(5); }
        QCOMPARE(badSpy.count(), 0);

        // (b) m_ready + real schema DB (0 shots), missing row -> shotReady STILL
        // fires (genuine not-found, db opened). Point m_dbPath at a freshDb()
        // schema file rather than re-initialize() — that would re-open the shared
        // ShotHistoryConnection freshDb() already used and warn "still in use".
        ShotHistoryStorage ok;
        ok.m_ready = true;
        ok.m_dbPath = freshDb();
        QSignalSpy okSpy(&ok, &ShotHistoryStorage::shotReady);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Shot not found: 999999"));
        ok.requestShot(999999);
        QTRY_COMPARE_WITH_TIMEOUT(okSpy.count(), 1, 15000);

        for (int i = 0; i < 40; i++) { QCoreApplication::processEvents(); QThread::msleep(5); }
    }

    // VisualizerUploader::buildBagEnrichBody — the pure fill-blanks diff that
    // decides which descriptive fields get PATCHed onto the server's coffee bag.
    // Locks the blob->API field mapping (a typo here silently drops a field) and
    // the never-clobber contract.
    void enrichBody_fillsOnlyServerBlanksFromLocalBlob() {
        QVariantMap bag;
        bag.insert("beanBaseData", QStringLiteral(
            R"({"origin":"Brazil","region":"Carmo De Minas","producer":"Smallholders",)"
            R"("process":"Natural","tastingNotes":"Cacao","elevation":"1100m"})"));
        bag.insert("beanBaseId", "canon-123");
        bag.insert("notes", "my notes");

        QJsonObject remote;                              // server bag, mostly bare
        remote.insert("country", "Peru");                // already set by user
        remote.insert("region", QJsonValue(QJsonValue::Null));
        remote.insert("farmer", "");                     // empty string == blank

        const QJsonObject body = VisualizerUploader::buildBagEnrichBody(remote, bag);

        QVERIFY(!body.contains("country"));              // server value wins, untouched
        QCOMPARE(body.value("region").toString(), QStringLiteral("Carmo De Minas"));
        QCOMPARE(body.value("farmer").toString(), QStringLiteral("Smallholders"));
        QCOMPARE(body.value("processing").toString(), QStringLiteral("Natural"));
        QCOMPARE(body.value("tasting_notes").toString(), QStringLiteral("Cacao"));
        QCOMPARE(body.value("elevation").toString(), QStringLiteral("1100m"));
        QCOMPARE(body.value("notes").toString(), QStringLiteral("my notes"));
        QCOMPARE(body.value("canonical_coffee_bag_id").toString(), QStringLiteral("canon-123"));
        QVERIFY(!body.contains("variety"));              // absent in blob -> nothing to send
    }

    void enrichBody_treatsWhitespaceRemoteAsBlank_andSkipsEmptyLocal() {
        QVariantMap bag;
        bag.insert("beanBaseData", QStringLiteral(R"({"origin":"Brazil"})"));  // country only
        QJsonObject remote;
        remote.insert("country", "   ");                 // whitespace -> blank

        const QJsonObject body = VisualizerUploader::buildBagEnrichBody(remote, bag);

        QCOMPARE(body.value("country").toString(), QStringLiteral("Brazil"));  // filled over whitespace
        QVERIFY(!body.contains("region"));               // no local region -> nothing
    }

    void enrichBody_returnsEmptyWhenServerAlreadyComplete() {
        QVariantMap bag;
        bag.insert("beanBaseData", QStringLiteral(R"({"origin":"Brazil","region":"Sul"})"));
        QJsonObject remote;
        remote.insert("country", "Brazil");
        remote.insert("region", "Sul");

        // Nothing to fill -> empty body -> the caller skips the PATCH entirely.
        QVERIFY(VisualizerUploader::buildBagEnrichBody(remote, bag).isEmpty());
    }

    void enrichBody_fillsEditorOnlyFields() {
        // The add-bag-detail-editing fields (farm/quality_score/
        // place_of_purchase/url) ride the same fill-blanks contract.
        QVariantMap bag;
        bag.insert("beanBaseData", QStringLiteral(
            "{\"farm\":\"Gora Kone\",\"qualityScore\":\"88\",\"placeOfPurchase\":\"Local cafe\","
            "\"link\":\"https://roaster.example/bag\"}"));
        const QJsonObject body = VisualizerUploader::buildBagEnrichBody(QJsonObject(), bag);
        QCOMPARE(body.value("farm").toString(), QStringLiteral("Gora Kone"));
        QCOMPARE(body.value("quality_score").toString(), QStringLiteral("88"));
        QCOMPARE(body.value("place_of_purchase").toString(), QStringLiteral("Local cafe"));
        QCOMPARE(body.value("url").toString(), QStringLiteral("https://roaster.example/bag"));
    }

    // VisualizerUploader::addBagDescriptiveFields — the full-value body the
    // bag-edit push PATCHes (last-writer-wins for fields we hold; empty locals
    // omitted, never sent as null). Locks the blob->API mapping incl. the
    // add-bag-detail-editing fields.
    void patchBody_mapsAllFieldsAtCurrentValues() {
        QVariantMap bag;
        bag.insert("coffeeName", "First Batch");
        bag.insert("roastDate", "2026-06-01");
        bag.insert("roastLevel", "Medium");
        bag.insert("frozenDate", "2026-06-10");
        bag.insert("defrostDate", "2026-07-01");
        bag.insert("notes", "my notes");
        bag.insert("beanBaseId", "canon-123");
        bag.insert("beanBaseData", QStringLiteral(
            "{\"origin\":\"Colombia\",\"region\":\"Huila\",\"farm\":\"El Paraiso\",\"producer\":\"Diego\","
            "\"variety\":\"Caturra\",\"process\":\"Washed\",\"harvest\":\"Late 2025\",\"qualityScore\":\"87\","
            "\"placeOfPurchase\":\"Roaster site\",\"tastingNotes\":\"cherry\",\"elevation\":\"1900 m\","
            "\"link\":\"https://roaster.example/bag\",\"canonical\":{\"origin\":\"Colombia\"}}"));

        QJsonObject body;
        VisualizerUploader::addBagDescriptiveFields(body, bag);

        QCOMPARE(body.value("name").toString(), QStringLiteral("First Batch"));
        QCOMPARE(body.value("roast_date").toString(), QStringLiteral("2026-06-01"));
        QCOMPARE(body.value("roast_level").toString(), QStringLiteral("Medium"));
        QCOMPARE(body.value("frozen_date").toString(), QStringLiteral("2026-06-10"));
        QCOMPARE(body.value("defrosted_date").toString(), QStringLiteral("2026-07-01"));
        QCOMPARE(body.value("notes").toString(), QStringLiteral("my notes"));
        QCOMPARE(body.value("canonical_coffee_bag_id").toString(), QStringLiteral("canon-123"));
        QCOMPARE(body.value("country").toString(), QStringLiteral("Colombia"));
        QCOMPARE(body.value("region").toString(), QStringLiteral("Huila"));
        QCOMPARE(body.value("farm").toString(), QStringLiteral("El Paraiso"));
        QCOMPARE(body.value("farmer").toString(), QStringLiteral("Diego"));
        QCOMPARE(body.value("variety").toString(), QStringLiteral("Caturra"));
        QCOMPARE(body.value("processing").toString(), QStringLiteral("Washed"));
        QCOMPARE(body.value("harvest_time").toString(), QStringLiteral("Late 2025"));
        QCOMPARE(body.value("quality_score").toString(), QStringLiteral("87"));
        QCOMPARE(body.value("place_of_purchase").toString(), QStringLiteral("Roaster site"));
        QCOMPARE(body.value("tasting_notes").toString(), QStringLiteral("cherry"));
        QCOMPARE(body.value("elevation").toString(), QStringLiteral("1900 m"));
        QCOMPARE(body.value("url").toString(), QStringLiteral("https://roaster.example/bag"));
        QVERIFY(!body.contains("roaster_id"));   // caller-owned
        QVERIFY(!body.contains("canonical"));    // snapshot never leaves the device

        // Empty local values are OMITTED — a local clear must not null a
        // server-side value the user set on visualizer.coffee.
        QVariantMap sparse;
        sparse.insert("coffeeName", "Bare");
        QJsonObject sparseBody;
        VisualizerUploader::addBagDescriptiveFields(sparseBody, sparse);
        QCOMPARE(sparseBody.value("name").toString(), QStringLiteral("Bare"));
        QCOMPARE(sparseBody.size(), 1);
    }
};

QTEST_MAIN(tst_CoffeeBags)
#include "tst_coffeebags.moc"
