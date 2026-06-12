#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>

#include "history/shothistorystorage.h"
#include "history/coffeebagstorage.h"
#include "history/unifiedbeansearchmodel.h"

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

    // Fresh fully-migrated DB via the real migration chain.
    QString freshDb() {
        const QString path = m_tempDir.filePath(QString("bags_%1.db").arg(m_dbCounter++));
        ShotHistoryStorage storage;
        if (!storage.initialize(path))
            return QString();
        storage.close();
        return path;
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
            QVERIFY(storage.initialize(path));
            storage.close();
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
            QVERIFY(storage.initialize(path));  // runs migration 19 again
            storage.close();
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

        // Failure path: nonexistent directory -> keys stay for retry.
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

            const QVector<CoffeeBag> inventory = CoffeeBagStorage::loadInventoryStatic(db);
            QCOMPARE(inventory.size(), 2);
            QHash<QString, qint64> countByRoaster;
            for (const CoffeeBag& bag : inventory)
                countByRoaster.insert(bag.roasterName, bag.shotCount);
            QCOMPARE(countByRoaster.value("Used"), qint64(2));
            QCOMPARE(countByRoaster.value("Fresh"), qint64(0));
        });
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
            usedBagId = CoffeeBagStorage::loadInventoryStatic(db).first().id;
            CoffeeBag fresh;
            fresh.roasterName = "Mistake";
            fresh.coffeeName = "Bag";
            freshBagId = CoffeeBagStorage::insertBagStatic(db, fresh);
        });

        QSignalSpy deletedSpy(&storage, &CoffeeBagStorage::bagDeleted);
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

        // Expect: tier0 A (canonical absorbed), tier1 B (merged), tier4 C. No tier2.
        QCOMPARE(merged.size(), 3);
        const QVariantMap first = merged[0].toMap();
        QCOMPARE(first.value("tier").toInt(), 0);
        QCOMPARE(first.value("coffeeName").toString(), QString("A"));
        QCOMPARE(first.value("id").toInt(), 7);

        const QVariantMap second = merged[1].toMap();
        QCOMPARE(second.value("tier").toInt(), 1);
        QCOMPARE(second.value("coffeeName").toString(), QString("B"));
        QCOMPARE(second.value("sources").toString(), QString("beanbase+history"));
        QCOMPARE(second.value("grinderSetting").toString(), QString("15"));  // history grinder carried

        const QVariantMap third = merged[2].toMap();
        QCOMPARE(third.value("tier").toInt(), 4);
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
                {"doseWeightG", 18.5}, {"yieldTargetG", 38.0}, {"lastUsedEpoch", 9999}}));
            const CoffeeBag stamped = CoffeeBagStorage::loadBagStatic(db, bagId);
            QCOMPARE(stamped.doseWeightG, 18.5);
            QCOMPARE(stamped.yieldTargetG, 38.0);
            QCOMPARE(stamped.lastUsedEpoch, qint64(9999));
            QCOMPARE(stamped.roasterName, QString("Stamp"));
        });
    }
};

QTEST_MAIN(tst_CoffeeBags)
#include "tst_coffeebags.moc"
