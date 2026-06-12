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

#include "history/shothistorystorage.h"
#include "history/coffeebagstorage.h"
#include "history/unifiedbeansearchmodel.h"
#include "core/settings_dye.h"
#include "core/settings_visualizer.h"

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
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("connection.*still in use"));
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

        // Expect: Inventory A (canonical absorbed), HistoryCanonical B (merged),
        // HistoryFreeText C. No CanonicalOnly row.
        QCOMPARE(merged.size(), 3);
        const QVariantMap first = merged[0].toMap();
        QCOMPARE(tierOf(merged[0]), Tier::Inventory);
        QCOMPARE(first.value("coffeeName").toString(), QString("A"));
        QCOMPARE(first.value("id").toInt(), 7);

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
            "inInventory", "visualizerBagId", "visualizerRoasterId"};
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
};

QTEST_MAIN(tst_CoffeeBags)
#include "tst_coffeebags.moc"
