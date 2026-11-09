#include <QtTest>
#include <QLoggingCategory>
#include "core/settings.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSettings>

#include "history/shothistorystorage.h"
#include "network/visualizersync.h"
#include "history/shotwaterresult.h"
#include "history/coffeebagstorage.h"
#include "history/equipmentstorage.h"
#include "core/appsettings.h"

#include "shotrowfixtures.h"

// ShotHistoryStorage schema creation, the upgrade floor, and the migrations
// above it. A migration test rewinds schema_version on a current database and
// re-runs initialize().
//
// No de1app equivalent -- this is Decenza-internal storage.

// withRawDb / ShotRow / insertShot are shared fixtures — this file used to carry
// its own near-identical withRawDb, differing only in not asserting the open.
using ShotRowFixtures::withRawDb;
using ShotRowFixtures::ShotRow;
using ShotRowFixtures::insertShot;

using ShotRowFixtures::hasColumn;
using ShotRowFixtures::hasTable;
using ShotRowFixtures::hasIndex;

static int getSchemaVersion(QSqlDatabase& db) {
    QSqlQuery q(db);
    q.exec("SELECT MAX(version) FROM schema_version");
    return q.next() ? q.value(0).toInt() : -1;
}

// Every table's columns (name, type, not-null, default, primary key), every
// index's columns and every trigger's SQL, one line each, so two databases can
// be compared regardless of column order or the whitespace a statement was
// written with.
static QStringList schemaSignature(QSqlDatabase& db) {
    QStringList out;
    QSqlQuery objects(db);
    if (!objects.exec("SELECT type, name, tbl_name, sql FROM sqlite_master WHERE name NOT LIKE 'sqlite_autoindex%'"))
        return {QStringLiteral("unreadable: ") + objects.lastError().text()};
    while (objects.next()) {
        const QString type = objects.value(0).toString();
        const QString name = objects.value(1).toString();
        if (type == QLatin1String("table")) {
            QSqlQuery cols(db);
            cols.exec(QString("PRAGMA table_info(%1)").arg(name));
            while (cols.next())
                out << QString("column %1.%2 %3 notnull=%4 default=%5 pk=%6")
                           .arg(name, cols.value(1).toString(), cols.value(2).toString().toUpper(),
                                cols.value(3).toString(), cols.value(4).toString(), cols.value(5).toString());
        } else if (type == QLatin1String("index")) {
            QStringList columns;
            QSqlQuery cols(db);
            cols.exec(QString("PRAGMA index_info(%1)").arg(name));
            while (cols.next())
                columns << cols.value(2).toString();
            out << QString("index %1 on %2(%3)").arg(name, objects.value(2).toString(), columns.join(','));
        } else if (type == QLatin1String("trigger")) {
            out << QString("trigger %1 %2").arg(name, objects.value(3).toString().simplified()
                                                          .remove(QStringLiteral("IF NOT EXISTS ")));
        }
    }
    out.sort();
    return out;
}

class tst_DbMigration : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;

    QString freshDbPath() {
        static int counter = 0;
        return m_tempDir.path() + QString("/test_%1.db").arg(++counter);
    }

    // Run initialize, close, and let background DB work drain.
    void initAndClose(const QString& path, ShotHistoryStorage& storage) {
        // A test that rewound a current DB replays the chain against the pre-52 shape.
        withRawDb(path, "restore_legacy_bag_columns", [](QSqlDatabase& db) {
            if (!hasTable(db, "schema_version") || getSchemaVersion(db) >= 52
                || !hasTable(db, "coffee_bags") || hasColumn(db, "coffee_bags", "grinder_setting"))
                return;
            QVERIFY(ShotRowFixtures::restoreLegacyBagColumns(db));
        });
        ShotRowFixtures::initAndCloseStorage(path, storage);
    }

    // Recipe-identity fixture (history-recipe-identity). Lives under `private:`
    // with the other helpers. Qt Test collects a private slot as a test only when
    // it takes ZERO parameters, returns void, is not suffixed `_data`, and is
    // not named exactly `init`, `cleanup`, `initTestCase` or `cleanupTestCase`
    // (`isValidSlot`, qtbase/src/testlib/qtestcase.cpp:151 — only `_data` is a
    // wildcard there; `initFixture()` WOULD run as a test) — so this
    // two-argument helper would not have run as a test even under `private
    // slots:`. Stated because an earlier version of this comment claimed
    // otherwise without checking the Qt source.
    //
    // Deliberately: no shot's bean, profile or notes contains "monday",
    // "tuesday" or "vacation", so a search hit on any of those can only have
    // arrived through the `recipes` table.
    struct RecipeSeed { qint64 mondayId = 0; qint64 tuesdayId = 0; qint64 archivedId = 0; };
    void seedRecipeShots(const QString& path, RecipeSeed& seed) {
        withRawDb(path, "recipe_seed", [&](QSqlDatabase& db) {
            auto addRecipe = [&](const QString& name, const QString& drinkType, int archived) -> qint64 {
                QSqlQuery r(db);
                r.prepare("INSERT INTO recipes (name, profile_title, drink_type, archived) VALUES (?, 'P', ?, ?)");
                r.addBindValue(name); r.addBindValue(drinkType); r.addBindValue(archived);
                if (!r.exec()) return 0;
                return r.lastInsertId().toLongLong();
            };
            seed.mondayId   = addRecipe("Dad Monday", "latte", 0);
            seed.tuesdayId  = addRecipe("Dad Tuesday", "espresso", 0);
            seed.archivedId = addRecipe("Vacation Blend", "filter", 1);
            QVERIFY(seed.mondayId > 0 && seed.tuesdayId > 0 && seed.archivedId > 0);

            auto addShot = [&](const QString& uuid, qint64 ts, qint64 recipeId) {
                QSqlQuery sh(db);
                sh.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, "
                           "bean_brand, bean_type, recipe_id) "
                           "VALUES (?, ?, 'P', 30, 'Roaster', 'Beans', ?)");
                sh.addBindValue(uuid); sh.addBindValue(ts);
                if (recipeId > 0) sh.addBindValue(recipeId); else sh.addBindValue(QVariant());
                QVERIFY(sh.exec());
            };
            addShot("s-monday",   1000, seed.mondayId);
            addShot("s-tuesday",  2000, seed.tuesdayId);
            addShot("s-archived", 3000, seed.archivedId);
            addShot("s-none",     4000, 0);
        });
    }

    // Two bags of the SAME coffee, plus a pre-bag shot. Every shot here carries
    // identical bean_brand/bean_type, so a bean filter cannot separate them —
    // which is the whole reason the bag filter exists (history-bag-filter).
    struct BagSeed { qint64 julyId = 0; qint64 augustId = 0; };
    void seedBagShots(const QString& path, BagSeed& seed) {
        withRawDb(path, "bag_seed", [&](QSqlDatabase& db) {
            auto addBag = [&](const QString& roaster, const QString& coffee,
                              const QString& roastDate) -> qint64 {
                QSqlQuery b(db);
                b.prepare("INSERT INTO coffee_bags (roaster_name, coffee_name, roast_date) "
                          "VALUES (?, ?, ?)");
                b.addBindValue(roaster); b.addBindValue(coffee); b.addBindValue(roastDate);
                if (!b.exec()) return 0;
                return b.lastInsertId().toLongLong();
            };
            seed.julyId   = addBag("Roaster", "Ethiopia Guji", "2026-07-04");
            seed.augustId = addBag("Roaster", "Ethiopia Guji", "2026-08-11");
            QVERIFY(seed.julyId > 0 && seed.augustId > 0);

            auto addShot = [&](const QString& uuid, qint64 ts, qint64 bagId) {
                QSqlQuery sh(db);
                // A distinctive profile name, not the 'P' the recipe seed uses:
                // the scoping assertion needs a token that appears in a SHOT
                // column and in no bag column, and single letters do not
                // qualify — "P" is inside "EthioPia", which is how the first
                // version of that assertion failed.
                // shots.roast_date is set DELIBERATELY, and to a value no bag
                // has. `roast_date` exists on both tables, so if the bag
                // subquery's columns are ever left unqualified and resolve to
                // the outer scope, this seed turns it into a correlated query
                // that returns the wrong rows — with no SQL error to report.
                // With every shot carrying the same date as its bag, that
                // regression would pass green.
                sh.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, "
                           "bean_brand, bean_type, roast_date, bag_id) "
                           "VALUES (?, ?, 'Londinium Ristretto', 30, 'Roaster', 'Ethiopia Guji', "
                           "'1999-01-01', ?)");
                sh.addBindValue(uuid); sh.addBindValue(ts);
                if (bagId > 0) sh.addBindValue(bagId); else sh.addBindValue(QVariant());
                QVERIFY(sh.exec());
            };
            addShot("b-july-1",   1000, seed.julyId);
            addShot("b-july-2",   2000, seed.julyId);
            addShot("b-august-1", 3000, seed.augustId);
            addShot("b-prebag",   4000, 0);   // NULL bag_id
        });
    }

    // Like initAndClose(), but for an initialize() that is expected to emit a
    // single migration-failure qWarning (a gated migration that did NOT bump).
    void initExpectingMigrationWarning(const QString& path, const QString& warnRegex) {
        ShotHistoryStorage storage;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(warnRegex));
        QVERIFY(storage.initialize(path));
        storage.close();
        QTRY_VERIFY(storage.isDbWorkIdle());
    }

private slots:
    void waterResultAsyncWriterIsNarrowAndReportsFailure_data() {
        QTest::addColumn<bool>("ratio");
        QTest::addColumn<bool>("waterBeforeSave");
        QTest::newRow("fixed") << false << false;
        QTest::newRow("ratio-water-before-save") << true << true;
        QTest::newRow("ratio-water-after-save") << true << false;
    }
    void waterResultAsyncWriterIsNarrowAndReportsFailure() {
        QFETCH(bool, ratio);
        QFETCH(bool, waterBeforeSave);
        const QString path = freshDbPath();
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(path));
        ShotSaveData data;
        data.uuid = "saved-water";
        data.timestamp = 1000;
        data.profileName = "P";
        data.duration = 30;
        data.espressoNotes = "keep";
        data.espressoEnjoyment = 80;
        data.compressedSamples = qCompress(QByteArray("{}"));
        data.hotWaterJson = ratio
            ? R"({"hasWater":true,"amountMode":"ratio","waterToEspressoRatio":2.5,"resolvedTargetG":100})"
            : R"({"hasWater":true,"volume":100})";
        const QVariantMap measured{{"status", "dispensed"}, {"amountSource", "scale"}, {"weightG", 100.0}};
        QVariantMap initial = ratio && !waterBeforeSave ? QVariantMap() : measured;
        if (waterBeforeSave) initial.insert("targetWeightG", 105.0);
        data.hotWaterResultJson = ShotWaterResult::fromMap(initial).toJson();
        const qint64 id = ShotHistoryStorage::saveShotStatic(path, data);
        QVERIFY(id > 0);
        withRawDb(path, "water_initial", [&](QSqlDatabase& db) {
            const auto shot = ShotHistoryStorage::loadShotRecordStatic(db, id);
            QCOMPARE(ShotHistoryStorage::convertShotRecord(shot).hotWaterResult, initial);
            if (ratio) {
                const auto spec = QJsonDocument::fromJson(shot.hotWaterJson.toUtf8()).object();
                QCOMPARE(spec.value("resolvedTargetG").toInt(), waterBeforeSave ? 105 : 100);
            }
        });
        QSignalSpy changed(&storage, &ShotHistoryStorage::shotWaterResultUpdated);
        QSignalSpy finished(&storage, &ShotHistoryStorage::shotWaterResultUpdateFinished);
        QSignalSpy metadata(&storage, &ShotHistoryStorage::shotMetadataUpdated);
        QSignalSpy errors(&storage, &ShotHistoryStorage::errorOccurred);
        auto result = measured;
        result.insert("weightG", 103.2);
        if (ratio) result.insert("targetWeightG", 113.0);
        storage.requestUpdateShotWaterResult(id, data.uuid, result);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(finished.first().at(1).toBool());
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().at(1).toMap(), result);
        QCOMPARE(metadata.count(), 0);
        withRawDb(path, "water_async", [&](QSqlDatabase& db) {
            const auto shot = ShotHistoryStorage::loadShotRecordStatic(db, id);
            QCOMPARE(ShotHistoryStorage::convertShotRecord(shot).hotWaterResult, result);
            QCOMPARE(shot.espressoNotes, data.espressoNotes);
            QCOMPARE(shot.summary.enjoyment, 80);
            if (ratio) {
                const auto spec = QJsonDocument::fromJson(shot.hotWaterJson.toUtf8()).object();
                QCOMPARE(spec.value("resolvedTargetG").toInt(), 113);
                QCOMPARE(spec.value("waterToEspressoRatio").toDouble(), 2.5);
            } else {
                QCOMPARE(shot.hotWaterJson, data.hotWaterJson);
            }
            QSqlQuery q(db);
            QVERIFY(q.exec(QString("DELETE FROM shots WHERE id = %1").arg(id)));
        });
        storage.requestUpdateShotWaterResult(id, data.uuid, result);
        QTRY_COMPARE(finished.count(), 2);
        QVERIFY(!finished.last().at(1).toBool());
        QCOMPARE(changed.count(), 1);
        QCOMPARE(metadata.count(), 0);
        QCOMPARE(errors.count(), 1);
        QTRY_VERIFY(storage.isDbWorkIdle());
    }

    void waterResultPersistenceAndImport_data() {
        QTest::addColumn<bool>("legacy");
        QTest::addColumn<bool>("merge");
        for (bool legacy : {false, true})
            for (bool merge : {false, true})
                QTest::newRow(qPrintable(QString("legacy%1-merge%2").arg(legacy).arg(merge))) << legacy << merge;
    }
    void waterResultPersistenceAndImport() {
        QFETCH(bool, legacy);
        QFETCH(bool, merge);
        const QString source = freshDbPath(), dest = freshDbPath();
        { ShotHistoryStorage storage; initAndClose(source, storage); }
        { ShotHistoryStorage storage; initAndClose(dest, storage); }
        const QVariantMap result{{"status", "dispensed"}, {"amountSource", "scale"},
                                 {"weightG", 103.2}, {"temperatureC", 85.0}};
        withRawDb(source, "water_seed", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(hasColumn(db, "shots", "hot_water_result_json"));
            QVERIFY(q.exec("INSERT INTO shots (id,uuid,timestamp,profile_name,duration_seconds,espresso_notes,enjoyment,hot_water_json) "
                           R"(VALUES (1,'water',1000,'P',30,'keep',80,'{"hasWater":true,"volume":100}'))"));
            const auto before = ShotHistoryStorage::loadShotRecordStatic(db, 1);
            QVERIFY(before.hotWaterResultJson.isEmpty());
            QVERIFY(!ShotHistoryStorage::updateShotWaterResultStatic(db, 1, "wrong", result));
            QVERIFY(ShotHistoryStorage::updateShotWaterResultStatic(db, 1, "water", result));
            QVERIFY(ShotHistoryStorage::updateShotMetadataStatic(db, 1, {{"espressoNotes", "new note"}}));
            const auto shot = ShotHistoryStorage::loadShotRecordStatic(db, 1);
            QCOMPARE(ShotHistoryStorage::convertShotRecord(shot).hotWaterResult, result);
            QCOMPARE(shot.espressoNotes, QString("new note"));
            QCOMPARE(shot.summary.enjoyment, 80);
            QCOMPARE(shot.hotWaterJson, before.hotWaterJson);
            if (legacy) QVERIFY(q.exec("ALTER TABLE shots DROP COLUMN hot_water_result_json"));
        });
        QVERIFY(ShotHistoryStorage::importDatabaseStatic(dest, source, merge));
        withRawDb(dest, "water_import", [&](QSqlDatabase& db) {
            const auto record = ShotHistoryStorage::loadShotRecordStatic(db, 1);
            QCOMPARE(ShotHistoryStorage::convertShotRecord(record).hotWaterResult,
                     legacy ? QVariantMap() : result);
            QVERIFY(!ShotHistoryStorage::updateShotWaterResultStatic(db, 999, "water", result));
        });
    }

    void importRecipesWithoutShots_data() {
        QTest::addColumn<bool>("merge");
        QTest::newRow("merge") << true;
        QTest::newRow("replace") << false;
    }

    void importRecipesWithoutShots() {
        QFETCH(bool, merge);
        const QString source = freshDbPath();
        const QString destination = freshDbPath();
        ShotHistoryStorage sourceStorage, destinationStorage;
        initAndClose(source, sourceStorage);
        initAndClose(destination, destinationStorage);
        withRawDb(source, "source_recipe_only", [](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO recipes (name, profile_title, drink_type, archived) "
                           "VALUES ('Source', 'P', 'espresso', 0)"));
        });
        withRawDb(destination, "destination_recipe_only", [](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO recipes (name, profile_title, drink_type, archived) "
                           "VALUES ('Destination', 'P', 'espresso', 0)"));
        });
        QVERIFY(ShotHistoryStorage::importDatabaseStatic(destination, source, merge));
        withRawDb(destination, "check_recipe_only", [merge](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT name FROM recipes ORDER BY name"));
            QStringList names;
            while (q.next()) names.append(q.value(0).toString());
            QCOMPARE(names, merge ? (QStringList{QStringLiteral("Destination"), QStringLiteral("Source")})
                                  : QStringList{QStringLiteral("Source")});
        });
    }

    void initTestCase() {
        QVERIFY(m_tempDir.isValid());
        // Past Qt Test's 2000-message cap the rest of the run's output is dropped;
        // this suite's debug chatter alone crossed it.
        QLoggingCategory::setFilterRules(QStringLiteral("default.debug=false"));
    }

    // Reset the migration fault-injection seam before every test so a one-shot
    // fault set by one test can never leak into the next.
    void init() { QTest::failOnWarning(); }

    // ==========================================
    // Fresh DB: full schema at v12
    // ==========================================

    void portalMigrationIsSchemaOnlyFromMain_data() {
        QTest::addColumn<int>("previousVersion");
        QTest::newRow("main-47") << 47;
        QTest::newRow("preview-48") << 48;
        QTest::newRow("preview-49") << 49;
        QTest::newRow("preview-50") << 50;
    }

    void portalMigrationIsSchemaOnlyFromMain() {
        QFETCH(int, previousVersion);
        QTemporaryDir dir;
        const QString path = dir.filePath("portal-migration.db");
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(path));
        ShotRecord legacy;
        legacy.summary.uuid = QStringLiteral("legacy-portal");
        legacy.summary.timestamp = 1700000000;
        legacy.summary.profileName = QStringLiteral("Espresso");
        legacy.summary.duration = 25.0;
        legacy.summary.beanBrand = QStringLiteral("Roaster");
        legacy.summary.beanType = QStringLiteral("Coffee");
        legacy.drinkTds = 10.0;
        legacy.portalSamples = {{1.0, 8.0, 60.0, true}, {2.0, 7.0, 60.0, false}};
        const qint64 legacyId = storage.importShotRecord(legacy, true);
        QVERIFY(legacyId > 0);
        auto recorded = legacy;
        recorded.summary.uuid = QStringLiteral("recorded-portal");
        recorded.summary.timestamp += 60;
        recorded.summary.doseWeight = 16.0;
        recorded.summary.finalWeight = 10.0;
        recorded.weightSource = QStringLiteral("physical_scale");
        recorded.portalMountingMode = QStringLiteral("onScale");
        recorded.portalSamples.clear();
        for (int i = 0; i <= 50; ++i) {
            recorded.portalSamples.append({1.0 + i * 0.1, 8.0, 60.0, i == 0});
            recorded.weight.append({1.0 + i * 0.1, i * 0.2});
        }
        const qint64 recordedId = storage.importShotRecord(recorded, true);
        QVERIFY(recordedId > 0);
        auto ordinary = legacy;
        ordinary.summary.uuid = QStringLiteral("ordinary-refractometer");
        ordinary.summary.timestamp += 120;
        ordinary.portalSamples.clear();
        const qint64 ordinaryId = storage.importShotRecord(ordinary, true);
        QVERIFY(ordinaryId > 0);
        storage.close();

        // Main's version 47 has added water and no PORTAL columns; the previews
        // had the PORTAL summary columns without a presence flag or added water.
        const bool fromMain = previousVersion == 47;
        withRawDb(path, "portal_rewind", [&](QSqlDatabase& db) {
            QVERIFY(ShotRowFixtures::restoreLegacyBagColumns(db));
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("UPDATE schema_version SET version=%1").arg(previousVersion)));
            if (fromMain) {
                for (const char* column : {"portal_mean_ec", "portal_integral_ec_g", "portal_mean_temp_c",
                                           "portal_estimate_reliable", "portal_mounting_mode", "weight_source",
                                           "portal_present"})
                    QVERIFY(q.exec(QStringLiteral("ALTER TABLE shots DROP COLUMN %1").arg(QLatin1String(column))));
                return;
            }
            QVERIFY(q.exec("ALTER TABLE shots DROP COLUMN hot_water_result_json"));
            QVERIFY(q.exec("ALTER TABLE shots DROP COLUMN portal_present"));
            QVERIFY(q.exec(QString("UPDATE shots SET portal_mean_ec=8, portal_estimate_reliable=1 WHERE id=%1").arg(legacyId)));
        });

        QVERIFY(storage.initialize(path));
        QVERIFY(storage.isDbWorkIdle());
        withRawDb(path, "portal_migrated", [&](QSqlDatabase& db) {
            QCOMPARE(getSchemaVersion(db), ShotHistoryStorage::kCurrentSchemaVersion);
            QVERIFY(hasColumn(db, "shots", "hot_water_result_json"));
            QSqlQuery q(db);
            for (qint64 id : {legacyId, recordedId, ordinaryId}) {
                QVERIFY(q.exec(QString("SELECT portal_present FROM shots WHERE id=%1").arg(id)) && q.next());
                QCOMPARE(q.value(0).toBool(), !fromMain && id != ordinaryId);
                q.finish();
            }
            // A preview shot without weight provenance is present but never a reference.
            const auto points = ShotHistoryStorage::loadPortalCalibrationPointsStatic(db, -1, "Roaster", "Coffee");
            QVERIFY(points);
            QCOMPARE(points->size(), fromMain ? 0 : 1);
            if (!fromMain) {
                QCOMPARE(points->first().shotId, recordedId);
                QVERIFY(points->first().measurementReliable);
            }
        });
    }

    void probeUpgradeFloorReadsWithoutWriting_data() {
        // setup: "none" no file, "garbage" a text file, "empty" an SQLite file
        // with no tables, "noversion" shots but no schema_version table,
        // "emptyversion" both tables and no row, "vN" schema version N.
        QTest::addColumn<QString>("setup");
        QTest::addColumn<int>("expected");
        using P = ShotHistoryStorage::UpgradeFloorProbe;
        QTest::newRow("no file") << "none" << int(P::NoDatabase);
        QTest::newRow("no tables") << "empty" << int(P::NoDatabase);
        QTest::newRow("2.0.7") << "v41" << int(P::Supported);
        QTest::newRow("2.1.0") << "v47" << int(P::Supported);
        QTest::newRow("2.0.5") << "v40" << int(P::BelowFloor);
        QTest::newRow("1.8.1") << "v23" << int(P::BelowFloor);
        QTest::newRow("no version table") << "noversion" << int(P::BelowFloor);
        QTest::newRow("no version row") << "emptyversion" << int(P::BelowFloor);
        QTest::newRow("not a database") << "garbage" << int(P::Unknown);
    }

    void probeUpgradeFloorReadsWithoutWriting() {
        QFETCH(QString, setup);
        QFETCH(int, expected);
        const QString path = freshDbPath();
        if (setup == QLatin1String("garbage")) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(4096, 'x'));
        } else if (setup != QLatin1String("none")) {
            withRawDb(path, "probe_setup", [&](QSqlDatabase& db) {
                QSqlQuery q(db);
                if (setup == QLatin1String("empty"))
                    return;
                QVERIFY(q.exec("CREATE TABLE shots (id INTEGER PRIMARY KEY, uuid TEXT)"));
                QVERIFY(q.exec("INSERT INTO shots (uuid) VALUES ('kept')"));
                if (setup == QLatin1String("noversion"))
                    return;
                QVERIFY(q.exec("CREATE TABLE schema_version (version INTEGER PRIMARY KEY)"));
                if (setup.startsWith('v'))
                    QVERIFY(q.exec(QString("INSERT INTO schema_version (version) VALUES (%1)").arg(setup.mid(1))));
            });
        }
        auto bytes = [&]() -> QByteArray {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };
        const bool existed = QFileInfo::exists(path);
        const QByteArray before = bytes();

        QCOMPARE(int(ShotHistoryStorage::probeUpgradeFloor(path)), expected);

        QCOMPARE(QFileInfo::exists(path), existed);
        QCOMPARE(bytes(), before);
    }

    // A fresh install and a database 2.0.7 left behind must end on the same
    // schema: createTables()' baseline is the only thing standing in for the
    // migration steps that used to build it.
    void freshInstallMatchesAnUpgraded207Database() {
        const QString upgraded = freshDbPath();
        QVERIFY(QFile::copy(QStringLiteral(DECENZA_SOURCE_DIR "/tests/data/db/schema41.db"), upgraded));
        QVERIFY(QFile::setPermissions(upgraded, QFile::ReadOwner | QFile::WriteOwner));
        const QString fresh = freshDbPath();
        { ShotHistoryStorage s; initAndClose(upgraded, s); }
        { ShotHistoryStorage s; initAndClose(fresh, s); }

        QStringList upgradedSchema, freshSchema;
        withRawDb(upgraded, "eq_upgraded", [&](QSqlDatabase& db) {
            QCOMPARE(getSchemaVersion(db), ShotHistoryStorage::kCurrentSchemaVersion);
            upgradedSchema = schemaSignature(db);
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT COUNT(*) FROM shots WHERE espresso_notes LIKE 'fixture note %'") && q.next());
            QCOMPARE(q.value(0).toInt(), 2);
            QVERIFY(q.exec("SELECT COUNT(*) FROM shots_fts WHERE shots_fts MATCH 'Fixture'") && q.next());
            QCOMPARE(q.value(0).toInt(), 2);
            for (const char* table : {"coffee_bags", "equipment_packages", "recipes"}) {
                QVERIFY(q.exec(QString("SELECT COUNT(*) FROM %1").arg(QLatin1String(table))) && q.next());
                QCOMPARE(q.value(0).toInt(), 1);
            }
            QVERIFY(q.exec("SELECT COUNT(*) FROM shots s JOIN recipes r ON r.id = s.recipe_id "
                           "JOIN coffee_bags b ON b.id = s.bag_id") && q.next());
            QCOMPARE(q.value(0).toInt(), 2);
        });
        withRawDb(fresh, "eq_fresh", [&](QSqlDatabase& db) {
            QCOMPARE(getSchemaVersion(db), ShotHistoryStorage::kCurrentSchemaVersion);
            freshSchema = schemaSignature(db);
        });
        for (const QString& line : upgradedSchema)
            QVERIFY2(freshSchema.contains(line), qPrintable("fresh install lacks: " + line));
        for (const QString& line : freshSchema)
            QVERIFY2(upgradedSchema.contains(line), qPrintable("upgraded 2.0.7 database lacks: " + line));
    }

    void importRefusesASourceBelowTheFloor_data() {
        QTest::addColumn<bool>("merge");
        QTest::newRow("merge") << true;
        QTest::newRow("replace") << false;
    }

    void importRefusesASourceBelowTheFloor() {
        QFETCH(bool, merge);
        const QString dest = freshDbPath();
        { ShotHistoryStorage s; initAndClose(dest, s); }
        withRawDb(dest, "floor_dest_seed", [](QSqlDatabase& db) {
            QVERIFY(QSqlQuery(db).exec("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds) "
                                       "VALUES ('dest-kept', 1000, 'P', 30)"));
        });

        const QString oldSource = freshDbPath();
        QVERIFY(!ShotRowFixtures::writeBelowFloorDatabase(oldSource).isEmpty());

        ShotHistoryStorage::ImportResult result;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("source is below the upgrade floor"));
        QVERIFY(!ShotHistoryStorage::importDatabaseStatic(dest, oldSource, merge, &result));
        QVERIFY(result.sourceTooOld);
        withRawDb(dest, "floor_dest_check", [](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT uuid FROM shots") && q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("dest-kept"));
            QVERIFY(!q.next());
        });

        // The same database at the floor imports.
        withRawDb(oldSource, "floor_restore", [](QSqlDatabase& db) {
            QVERIFY(QSqlQuery(db).exec("UPDATE schema_version SET version = 41"));
        });
        ShotHistoryStorage::ImportResult accepted;
        QVERIFY(ShotHistoryStorage::importDatabaseStatic(dest, oldSource, merge, &accepted));
        QVERIFY(!accepted.sourceTooOld);
        QCOMPARE(accepted.imported, 2);
    }

    // runMigrations() reads the version before it writes anything, so a
    // database the launch probe could not classify is still refused intact.
    void belowFloorDatabaseIsRefusedUntouched() {
        const QString path = freshDbPath();
        withRawDb(path, "below_floor_seed", [](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE shots (id INTEGER PRIMARY KEY, uuid TEXT)"));
            QVERIFY(q.exec("INSERT INTO shots (uuid) VALUES ('kept')"));
            QVERIFY(q.exec("CREATE TABLE schema_version (version INTEGER PRIMARY KEY)"));
            QVERIFY(q.exec("INSERT INTO schema_version (version) VALUES (40)"));
        });
        auto dump = [&]() {
            QStringList out;
            withRawDb(path, "below_floor_dump", [&](QSqlDatabase& db) {
                QSqlQuery q(db);
                QVERIFY(q.exec("SELECT type, name, sql FROM sqlite_master ORDER BY name"));
                while (q.next())
                    out << q.value(0).toString() + '|' + q.value(1).toString() + '|' + q.value(2).toString();
                QVERIFY(q.exec("SELECT id, uuid FROM shots UNION ALL SELECT version, '' FROM schema_version"));
                while (q.next())
                    out << q.value(0).toString() + '|' + q.value(1).toString();
            });
            return out;
        };
        const QStringList before = dump();

        ShotHistoryStorage storage;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("below the upgrade floor"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Failed to run migrations"));
        QVERIFY(!storage.initialize(path));
        storage.close();

        QCOMPARE(dump(), before);
    }

    void freshDbCreatesSchema() {
        QString path = freshDbPath();
        ShotHistoryStorage storage;
        initAndClose(path, storage);

        withRawDb(path, "fresh_schema", [](QSqlDatabase& db) {
            QVERIFY(hasTable(db, "shots"));
            QVERIFY(hasTable(db, "shot_samples"));
            QVERIFY(hasTable(db, "shot_phases"));
            QVERIFY(hasTable(db, "schema_version"));
            QVERIFY(hasTable(db, "recipes"));  // migration 25 (add-recipes)
            QCOMPARE(getSchemaVersion(db), ShotHistoryStorage::kCurrentSchemaVersion);
        });
    }

    // ==========================================
    // FTS search works on fresh DB
    // ==========================================

    void ftsSearchWorks() {
        QString path = freshDbPath();
        ShotHistoryStorage storage;
        initAndClose(path, storage);

        withRawDb(path, "fts_test", [](QSqlDatabase& db) {
            QSqlQuery q(db);
            // Insert with the FTS-indexed fields. Grinder columns were dropped in
            // migration 23 and grinder is no longer an FTS column — grinder search
            // resolves through equipment_id instead (add-equipment-packages 4b.6).
            q.prepare(R"(INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds,
                espresso_notes, bean_brand, bean_type)
                VALUES (?, 1000, ?, 30.0, ?, ?, ?))");
            q.addBindValue(QUuid::createUuid().toString());
            q.addBindValue("Blooming Espresso");
            q.addBindValue("Excellent fruity notes");
            q.addBindValue("Onyx");
            q.addBindValue("Eclipse");
            QVERIFY(q.exec());

            // The FTS triggers should auto-populate
            QSqlQuery fts(db);
            QVERIFY(fts.exec("SELECT rowid FROM shots_fts WHERE shots_fts MATCH 'Blooming'"));
            QVERIFY2(fts.next(), "FTS should find by profile_name");

            QVERIFY(fts.exec("SELECT rowid FROM shots_fts WHERE shots_fts MATCH 'Onyx'"));
            QVERIFY2(fts.next(), "FTS should find by bean_brand");
        });
    }

    // ==========================================
    // Idempotency: run twice, no crash
    // ==========================================

    void idempotentMigration() {
        QString path = freshDbPath();
        { ShotHistoryStorage s; initAndClose(path, s); }
        { ShotHistoryStorage s; initAndClose(path, s); }

        withRawDb(path, "idempotent", [](QSqlDatabase& db) {
            QCOMPARE(getSchemaVersion(db), ShotHistoryStorage::kCurrentSchemaVersion);
        });
    }

    // add-ai-taste-intake: updateShotMetadataStatic accepts taste_balance /
    // taste_body, and DROPS an out-of-set value (with a warning) without failing
    // an update that also carries a legitimate field.
    void updateShotMetadata_validatesTasteValues() {
        QString path = freshDbPath();
        { ShotHistoryStorage s; initAndClose(path, s); }

        qint64 shotId = 0;
        withRawDb(path, "taste_seed", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds) "
                           "VALUES ('taste-shot', 1000, 'P', 25.0)"));
            shotId = q.lastInsertId().toLongLong();
        });
        QVERIFY(shotId > 0);

        // Valid values persist.
        withRawDb(path, "taste_valid", [&](QSqlDatabase& db) {
            QVariantMap meta;
            meta["tasteBalance"] = QStringLiteral("sour");
            meta["tasteBody"] = QStringLiteral("heavy");
            QVERIFY(ShotHistoryStorage::updateShotMetadataStatic(db, shotId, meta));
            QSqlQuery q(db);
            QVERIFY(q.exec(QString("SELECT taste_balance, taste_body FROM shots WHERE id = %1").arg(shotId)));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("sour"));
            QCOMPARE(q.value(1).toString(), QStringLiteral("heavy"));
        });

        // An out-of-set taste value is dropped (warned) but a co-present valid
        // field (enjoyment) still writes; the bad taste column is left unchanged.
        withRawDb(path, "taste_invalid", [&](QSqlDatabase& db) {
            // qWarning() quotes QString args, so the emitted text is:
            //   ShotHistoryStorage: dropping invalid "tasteBalance" value "garbage"
            QTest::ignoreMessage(QtWarningMsg,
                QRegularExpression("dropping invalid tasteBalance value garbage"));
            QVariantMap meta;
            meta["tasteBalance"] = QStringLiteral("garbage");
            meta["enjoyment"] = 80;
            QVERIFY(ShotHistoryStorage::updateShotMetadataStatic(db, shotId, meta));
            QSqlQuery q(db);
            QVERIFY(q.exec(QString("SELECT taste_balance, enjoyment FROM shots WHERE id = %1").arg(shotId)));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("sour"));  // unchanged
            QCOMPARE(q.value(1).toInt(), 80);                          // written
        });
    }

    // visualizer-two-way-sync: an edit marks only the Visualizer fields whose
    // value it changes (a form re-saving everything must not), a send clears
    // only what it carried and only if no edit landed since, and a pull writes
    // around a field edited here and not yet sent without marking anything.
    void visualizerDirty_marksClearsAndGuardsPulls() {
        QString path = freshDbPath();
        { ShotHistoryStorage s; initAndClose(path, s); }
        using namespace VisualizerSync;

        qint64 shotId = 0;
        withRawDb(path, "vizdirty_seed", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, enjoyment, "
                           "grinder_setting) VALUES ('viz-dirty', 1000, 'P', 25.0, 70, '2.4')"));
            shotId = q.lastInsertId().toLongLong();
        });
        QVERIFY(shotId > 0);

        withRawDb(path, "vizdirty_run", [&](QSqlDatabase& db) {
            quint32 dirty = 0;
            qint64 seq = 0;
            // Same rating (as a double and as a string), an empty barista over
            // NULL, a zero TDS over NULL: nothing moved, so neither the fields
            // nor the seq do.
            QVERIFY(ShotHistoryStorage::updateShotMetadataStatic(
                db, shotId, {{"enjoyment", 70.0}, {"barista", QString()}, {"grinderSetting", "2.4"},
                             {"drinkTds", 0.0}}));
            QVERIFY(ShotHistoryStorage::updateShotMetadataStatic(db, shotId, {{"enjoyment", "70"}}));
            QVERIFY(ShotHistoryStorage::readVisualizerDirtyStatic(db, shotId, &dirty, &seq));
            QCOMPARE(dirty, 0u);
            QCOMPARE(seq, qint64(0));

            QVERIFY(ShotHistoryStorage::updateShotMetadataStatic(db, shotId, {{"enjoyment", 85}, {"barista", "Jeff"}}));
            QVERIFY(ShotHistoryStorage::readVisualizerDirtyStatic(db, shotId, &dirty, &seq));
            QCOMPARE(dirty, quint32(Enjoyment | Barista));

            // An edit after the send read its seq keeps every bit.
            const qint64 sentSeq = seq;
            QVERIFY(ShotHistoryStorage::updateShotMetadataStatic(db, shotId, {{"enjoyment", 90}}));
            QVERIFY(ShotHistoryStorage::clearVisualizerDirtyStatic(db, shotId, Enjoyment | Barista, sentSeq));
            QVERIFY(ShotHistoryStorage::readVisualizerDirtyStatic(db, shotId, &dirty, &seq));
            QCOMPARE(dirty, quint32(Enjoyment | Barista));
            QVERIFY(ShotHistoryStorage::clearVisualizerDirtyStatic(db, shotId, Barista, seq));
            QVERIFY(ShotHistoryStorage::readVisualizerDirtyStatic(db, shotId, &dirty, &seq));
            QCOMPARE(dirty, quint32(Enjoyment));

            // The pull takes the grind changed there and keeps the unsent rating.
            QVariantMap written;
            QVariantMap previous;
            QVERIFY(ShotHistoryStorage::applyVisualizerPullStatic(
                db, shotId, {{"enjoyment", 60}, {"grinderSetting", "2.6"}}, &written, &previous));
            QCOMPARE(written.keys(), QStringList{QStringLiteral("grinderSetting")});
            QCOMPARE(previous.value("grinderSetting").toString(), QStringLiteral("2.4"));
            QSqlQuery q(db);
            QVERIFY(q.exec(QString("SELECT enjoyment, grinder_setting, visualizer_dirty FROM shots WHERE id = %1")
                               .arg(shotId)));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 90);
            QCOMPARE(q.value(1).toString(), QStringLiteral("2.6"));
            QCOMPARE(q.value(2).toUInt(), quint32(Enjoyment));
        });
    }

    // Migration 43 adds the sync columns to an existing database, with existing
    // shots starting clean (nothing unsent) and bags never seen.
    void v42ToV43AddsVisualizerSyncColumns() {
        QString path = freshDbPath();
        { ShotHistoryStorage s; initAndClose(path, s); }

        qint64 shotId = 0;
        withRawDb(path, "v43_seed", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("DELETE FROM schema_version"));
            QVERIFY(q.exec("INSERT INTO schema_version (version) VALUES (42)"));
            QVERIFY(q.exec("ALTER TABLE shots DROP COLUMN visualizer_dirty"));
            QVERIFY(q.exec("ALTER TABLE shots DROP COLUMN visualizer_dirty_seq"));
            QVERIFY(q.exec("ALTER TABLE coffee_bags DROP COLUMN visualizer_seen"));
            QVERIFY(q.exec("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds) "
                           "VALUES ('m43-a', 2000, 'P', 25.0)"));
            shotId = q.lastInsertId().toLongLong();
        });

        { ShotHistoryStorage s; initAndClose(path, s); }

        withRawDb(path, "v43_verify", [&](QSqlDatabase& db) {
            QCOMPARE(getSchemaVersion(db), ShotHistoryStorage::kCurrentSchemaVersion);
            quint32 dirty = 1;
            qint64 seq = 1;
            QVERIFY(ShotHistoryStorage::readVisualizerDirtyStatic(db, shotId, &dirty, &seq));
            QCOMPARE(dirty, 0u);
            QCOMPARE(seq, qint64(0));
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT visualizer_seen FROM coffee_bags LIMIT 0"));
        });
    }

    // Migration 45 offers every shot already on the Decent account for
    // re-upload (its times went as UTC), and no other shot.
    void v44ToV45MarksDecentShotsForResend() {
        QString path = freshDbPath();
        { ShotHistoryStorage s; initAndClose(path, s); }

        withRawDb(path, "v45_seed", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("DELETE FROM schema_version"));
            QVERIFY(q.exec("INSERT INTO schema_version (version) VALUES (44)"));
            QVERIFY(q.exec("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, decent_uploaded_at) "
                           "VALUES ('m45-uploaded', 2000, 'P', 25.0, 1000)"));
            QVERIFY(q.exec("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds) "
                           "VALUES ('m45-not-uploaded', 3000, 'P', 25.0)"));
        });

        { ShotHistoryStorage s; initAndClose(path, s); }

        withRawDb(path, "v45_verify", [&](QSqlDatabase& db) {
            QCOMPARE(getSchemaVersion(db), ShotHistoryStorage::kCurrentSchemaVersion);
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT uuid, decent_replace_pending FROM shots WHERE uuid LIKE 'm45-%' ORDER BY uuid"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("m45-not-uploaded"));
            QCOMPARE(q.value(1).toInt(), 0);
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("m45-uploaded"));
            QCOMPARE(q.value(1).toInt(), 1);
        });
    }

    // ==========================================
    // Schema version has exactly one row
    // ==========================================

    void schemaVersionSingleRow() {
        QString path = freshDbPath();
        ShotHistoryStorage storage;
        initAndClose(path, storage);

        withRawDb(path, "single_row", [](QSqlDatabase& db) {
            QSqlQuery q(db);
            q.exec("SELECT COUNT(*) FROM schema_version");
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 1);
        });
    }

    // ==========================================
    // lastSavedShotId seed at initialize() (review-page sticky-sync gate
    // input — the gate compares synchronously with no fallback, so this
    // seed is the only thing keeping "most recent shot" correct across
    // app restarts)
    // ==========================================

    void lastSavedShotIdSeededFromMaxId() {
        QString path = freshDbPath();

        // Empty DB: MAX(id) is NULL -> toLongLong() -> 0 (the load-bearing
        // conversion the code comment promises).
        {
            ShotHistoryStorage s;
            QVERIFY(s.initialize(path));
            QCOMPARE(s.lastSavedShotId(), qint64(0));
            s.close();
        }

        // Insert three shots, then construct a SECOND instance — modelling an
        // app restart (distinct from the save-time assignment, which only
        // covers the session the shot was pulled in).
        qint64 newestId = -1;
        withRawDb(path, "seed_insert", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            for (int i = 0; i < 3; i++) {
                q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds) "
                          "VALUES (:u, :t, 'P', 25.0)");
                q.bindValue(":u", QString("seed-uuid-%1").arg(i));
                q.bindValue(":t", 1000 + i);
                QVERIFY(q.exec());
                newestId = q.lastInsertId().toLongLong();
            }
        });
        {
            ShotHistoryStorage s;
            QVERIFY(s.initialize(path));
            QCOMPARE(s.lastSavedShotId(), newestId);
            s.close();
        }

        // Delete the newest row; a fresh instance must seed the surviving
        // MAX(id), not the deleted id and not 0 (catches a future rewrite
        // that caches the value instead of querying live).
        withRawDb(path, "seed_delete", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec(QString("DELETE FROM shots WHERE id = %1").arg(newestId)));
        });
        {
            ShotHistoryStorage s;
            QVERIFY(s.initialize(path));
            QCOMPARE(s.lastSavedShotId(), newestId - 1);
            s.close();
        }
    }

    // loadShotRecordStatic reads equipment_id/rpm by positional index (39/40);
    // guard the round-trip and the NULL→0 mapping so a future SELECT-list edit
    // that shifts those columns fails loudly.
    void v22_shotEquipmentRpmRoundTrip() {
        const QString path = freshDbPath();
        { ShotHistoryStorage s; initAndClose(path, s); }
        qint64 withEq = -1, without = -1;
        withRawDb(path, "v22_rt_seed", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, "
                      "equipment_id, rpm, grinder_setting) VALUES ('eq-1', 1000, 'P', 30, ?, ?, '2.4')");
            q.addBindValue((qint64)7); q.addBindValue(1400);
            QVERIFY(q.exec()); withEq = q.lastInsertId().toLongLong();
            QSqlQuery q2(db);
            QVERIFY(q2.exec("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds) "
                            "VALUES ('eq-2', 2000, 'P', 30)"));
            without = q2.lastInsertId().toLongLong();
        });
        withRawDb(path, "v22_rt_load", [&](QSqlDatabase& db) {
            const ShotRecord r = ShotHistoryStorage::loadShotRecordStatic(db, withEq);
            QCOMPARE(r.equipmentId, (qint64)7);
            QCOMPARE(r.rpm, (qint64)1400);
            const ShotRecord r2 = ShotHistoryStorage::loadShotRecordStatic(db, without);
            QCOMPARE(r2.equipmentId, (qint64)0);   // NULL equipment_id -> 0
            QCOMPARE(r2.rpm, (qint64)0);           // NULL rpm -> 0
        });
    }

    // add-ai-taste-intake: loadShotRecordStatic reads taste_balance/taste_body by
    // positional index (52/53, appended to the SELECT), and convertShotRecord
    // copies them into the projection. Guard the full write→read→project round-trip
    // so a SELECT-list edit that shifts those columns fails loudly.
    // add-shot-flow-calibration: the same guard v22 and v33 established for
    // their positionally-read columns. flow_calibration is read as
    // query.value(56) in loadShotRecordStatic(), so a future SELECT-list edit
    // that shifts it would silently load the wrong field. This walks the
    // production path: write, load into a ShotRecord, project. It also pins the
    // 0-means-unrecorded contract, which is the reason the column is nullable.
    void v39_flowCalibrationReadRoundTrip() {
        const QString path = freshDbPath();
        { ShotHistoryStorage s; initAndClose(path, s); }
        qint64 recorded = -1, unrecorded = -1;
        withRawDb(path, "v39_rt_seed", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, "
                           "flow_calibration) VALUES ('flowcal-rt', 1000, 'P', 30, 1.35)"));
            recorded = q.lastInsertId().toLongLong();
            QVERIFY(q.exec("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds) "
                           "VALUES ('flowcal-rt-null', 1001, 'P', 30)"));
            unrecorded = q.lastInsertId().toLongLong();
        });
        QVERIFY(recorded > 0 && unrecorded > 0);
        withRawDb(path, "v39_rt_load", [&](QSqlDatabase& db) {
            const ShotRecord r = ShotHistoryStorage::loadShotRecordStatic(db, recorded);
            QCOMPARE(r.flowCalibration, 1.35);
            const ShotProjection p = ShotHistoryStorage::convertShotRecord(r);
            QCOMPARE(p.flowCalibration, 1.35);
            QCOMPARE(p.toVariantMap().value(QStringLiteral("flowCalibration")).toDouble(), 1.35);

            // A NULL column must arrive as 0 and then vanish from the payload —
            // never as 1.0, which is a real multiplier.
            const ShotRecord n = ShotHistoryStorage::loadShotRecordStatic(db, unrecorded);
            QCOMPARE(n.flowCalibration, 0.0);
            QVERIFY(!ShotHistoryStorage::convertShotRecord(n).toVariantMap()
                         .contains(QStringLiteral("flowCalibration")));
        });
    }

    void v33_tasteAxesReadRoundTrip() {
        const QString path = freshDbPath();
        { ShotHistoryStorage s; initAndClose(path, s); }
        qint64 shotId = -1;
        withRawDb(path, "v33_rt_seed", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds) "
                           "VALUES ('taste-rt', 1000, 'P', 30)"));
            shotId = q.lastInsertId().toLongLong();
            QVariantMap meta;
            meta["tasteBalance"] = QStringLiteral("sour");
            meta["tasteBody"] = QStringLiteral("heavy");
            QVERIFY(ShotHistoryStorage::updateShotMetadataStatic(db, shotId, meta));
        });
        QVERIFY(shotId > 0);
        withRawDb(path, "v33_rt_load", [&](QSqlDatabase& db) {
            const ShotRecord r = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
            QCOMPARE(r.tasteBalance, QStringLiteral("sour"));
            QCOMPARE(r.tasteBody, QStringLiteral("heavy"));
            const ShotProjection p = ShotHistoryStorage::convertShotRecord(r);
            QCOMPARE(p.tasteBalance, QStringLiteral("sour"));
            QCOMPARE(p.tasteBody, QStringLiteral("heavy"));
        });
    }

    // loadShotRecordStatic resolves grinder brand/model/burrs through the
    // equipment_id JOIN (the per-shot columns are gone — migration 23) and
    // derives equipmentState from the package's in_inventory + superseded_by
    // lineage (add-equipment-packages 4.1 / 4b.7). Also pins that grinder
    // identity is NOT in shots_fts anymore (search resolves via the pointer).
    void v23_grinderResolvesViaJoinAndLineage() {
        const QString path = freshDbPath();
        { ShotHistoryStorage s; initAndClose(path, s); }

        qint64 curShot = -1, olderShot = -1, retiredShot = -1, noEqShot = -1;
        withRawDb(path, "v23_join_seed", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            // A newer package, an in-inventory current one, an "older" (superseded
            // by the newer) and a "retired" (out of inventory, no successor).
            QVERIFY(q.exec("INSERT INTO equipment_packages (name, in_inventory) VALUES ('New', 1)"));
            const qint64 newer = q.lastInsertId().toLongLong();
            QVERIFY(q.exec("INSERT INTO equipment_packages (name, in_inventory) VALUES ('Cur', 1)"));
            const qint64 cur = q.lastInsertId().toLongLong();
            q.prepare("INSERT INTO equipment_packages (name, in_inventory, superseded_by) VALUES ('Old', 0, ?)");
            q.addBindValue(newer); QVERIFY(q.exec());
            const qint64 older = q.lastInsertId().toLongLong();
            QVERIFY(q.exec("INSERT INTO equipment_packages (name, in_inventory) VALUES ('Ret', 0)"));
            const qint64 retired = q.lastInsertId().toLongLong();

            auto addGrinder = [&](qint64 pkg, const QString& brand, const QString& model, const QString& burrs) {
                QSqlQuery gi(db);
                gi.prepare("INSERT INTO equipment_items (package_id, kind, brand, model, attrs) "
                           "VALUES (?, 'grinder', ?, ?, ?)");
                gi.addBindValue(pkg); gi.addBindValue(brand); gi.addBindValue(model);
                gi.addBindValue(QString(R"({"burrs":"%1","rpmCapable":true})").arg(burrs));
                QVERIFY(gi.exec());
            };
            addGrinder(cur, "Niche", "Zero", "63mm conical");
            addGrinder(older, "Turin", "DF83V", "83mm flat steel");
            addGrinder(retired, "Mazzer", "Major", "83mm");

            // Basket on the current package only — pins the loadShotRecordStatic
            // basket JOIN, which reads eb.brand/eb.model by POSITIONAL index
            // (cols 44/45). A future SELECT-list edit that shifts those columns
            // would silently mis-resolve the basket; this assertion catches it.
            QSqlQuery bi(db);
            bi.prepare("INSERT INTO equipment_items (package_id, kind, brand, model, attrs) "
                       "VALUES (?, 'basket', 'Decent', '18g Ridgeless', '{}')");
            bi.addBindValue(cur);
            QVERIFY(bi.exec());

            // Puck prep on the current package — pins the loadShotRecordStatic
            // puckprep JOIN, which reads epp.model by POSITIONAL index (col 46).
            // The canonical flag string lives in the item's `model` column.
            QSqlQuery pi(db);
            pi.prepare("INSERT INTO equipment_items (package_id, kind, brand, model, attrs) "
                       "VALUES (?, 'puckprep', NULL, 'shaker,wdt', '{}')");
            pi.addBindValue(cur);
            QVERIFY(pi.exec());

            auto addShot = [&](const QString& uuid, qint64 pkg) -> qint64 {
                QSqlQuery s(db);
                s.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, "
                          "equipment_id, grinder_setting) VALUES (?, 1000, 'P', 30, ?, '2.4')");
                s.addBindValue(uuid);
                s.addBindValue(pkg > 0 ? QVariant(pkg) : QVariant());
                return s.exec() ? s.lastInsertId().toLongLong() : -1;
            };
            curShot = addShot("cur-1", cur);
            olderShot = addShot("old-1", older);
            retiredShot = addShot("ret-1", retired);
            noEqShot = addShot("noeq-1", 0);
            QVERIFY(curShot > 0 && olderShot > 0 && retiredShot > 0 && noEqShot > 0);
        });

        withRawDb(path, "v23_join_load", [&](QSqlDatabase& db) {
            const ShotRecord cur = ShotHistoryStorage::loadShotRecordStatic(db, curShot);
            QCOMPARE(cur.grinderBrand, QString("Niche"));
            QCOMPARE(cur.grinderModel, QString("Zero"));
            QCOMPARE(cur.grinderBurrs, QString("63mm conical"));  // json_extract path
            QCOMPARE(cur.basketBrand, QString("Decent"));         // cols 44/45 JOIN
            QCOMPARE(cur.basketModel, QString("18g Ridgeless"));
            QCOMPARE(cur.puckPrep, QString("shaker,wdt"));        // col 46 JOIN
            QCOMPARE(cur.equipmentState, QString(""));            // in inventory -> current

            const ShotRecord older = ShotHistoryStorage::loadShotRecordStatic(db, olderShot);
            QCOMPARE(older.grinderModel, QString("DF83V"));
            QCOMPARE(older.basketBrand, QString(""));             // no basket item -> empty
            QCOMPARE(older.equipmentState, QString("older"));     // superseded

            const ShotRecord ret = ShotHistoryStorage::loadShotRecordStatic(db, retiredShot);
            QCOMPARE(ret.grinderModel, QString("Major"));
            QCOMPARE(ret.equipmentState, QString("retired"));     // gone, no successor

            const ShotRecord noeq = ShotHistoryStorage::loadShotRecordStatic(db, noEqShot);
            QCOMPARE(noeq.grinderBrand, QString(""));             // NULL JOIN -> empty
            QCOMPARE(noeq.basketBrand, QString(""));              // NULL JOIN -> empty basket
            QCOMPARE(noeq.puckPrep, QString(""));                 // NULL JOIN -> empty puck prep
            QCOMPARE(noeq.equipmentState, QString(""));           // no package

            // Grinder identity is NOT FTS-indexed anymore: a search for a grinder
            // brand finds nothing through shots_fts (it resolves via equipment_id).
            QSqlQuery fts(db);
            QVERIFY(fts.exec("SELECT rowid FROM shots_fts WHERE shots_fts MATCH 'Niche'"));
            QVERIFY2(!fts.next(), "grinder brand must not be findable via FTS after migration 23");
        });
    }

    // .shot file import (importShotRecord) must preserve the parsed grinder
    // identity by find-or-creating an equipment package and linking equipment_id
    // — the per-shot grinder columns are gone (migration 23), so without the link
    // imported shots would resolve to a blank grinder.
    void v23_importShotRecordLinksEquipment() {
        const QString path = freshDbPath();
        qint64 shotId = -1;
        {
            ShotHistoryStorage s;
            QVERIFY(s.initialize(path));
            ShotRecord rec;
            rec.summary.uuid = QStringLiteral("import-grinder-1");
            rec.summary.timestamp = 1000;
            rec.summary.profileName = QStringLiteral("P");
            rec.summary.duration = 30.0;
            rec.grinderBrand = QStringLiteral("Niche");
            rec.grinderModel = QStringLiteral("Zero");
            rec.grinderBurrs = QStringLiteral("63mm conical");
            rec.grinderSetting = QStringLiteral("12");
            shotId = s.importShotRecord(rec);
            QVERIFY(shotId > 0);
            s.close();
        }
        withRawDb(path, "v23_import_verify", [&](QSqlDatabase& db) {
            const ShotRecord r = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
            QVERIFY2(r.equipmentId > 0, "imported shot must be linked to an equipment package");
            QCOMPARE(r.grinderBrand, QString("Niche"));
            QCOMPARE(r.grinderModel, QString("Zero"));
            QCOMPARE(r.grinderBurrs, QString("63mm conical"));   // resolved via the package's grinder item
            QCOMPARE(r.grinderSetting, QString("12"));           // per-shot dial-in stays on the row
        });
    }

    // Free-text shot-history search must still find a grinder name even though
    // grinder identity is no longer in shots_fts (migration 23) — it resolves the
    // term against equipment_items and matches via the equipment_id pointer
    // (add-equipment-packages 4b.6). Regression guard for "search 'niche' finds
    // nothing".
    void v23_grinderFreeTextSearchResolvesViaPointer() {
        const QString path = freshDbPath();
        ShotHistoryStorage s;
        QVERIFY(s.initialize(path));

        // One shot whose ONLY association with "niche" is its grinder package
        // (bean/profile/notes deliberately don't contain the term), plus a
        // control shot on a different grinder.
        withRawDb(path, "v23_search_seed", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO equipment_packages (name, in_inventory) VALUES ('Niche Zero', 1)"));
            const qint64 niche = q.lastInsertId().toLongLong();
            QVERIFY(q.exec("INSERT INTO equipment_packages (name, in_inventory) VALUES ('Other', 1)"));
            const qint64 other = q.lastInsertId().toLongLong();
            auto addGrinder = [&](qint64 pkg, const QString& brand, const QString& model) {
                QSqlQuery gi(db);
                gi.prepare("INSERT INTO equipment_items (package_id, kind, brand, model, attrs) "
                           "VALUES (?, 'grinder', ?, ?, '{\"burrs\":\"63mm\"}')");
                gi.addBindValue(pkg); gi.addBindValue(brand); gi.addBindValue(model);
                QVERIFY(gi.exec());
            };
            addGrinder(niche, "Niche", "Zero");
            addGrinder(other, "Mazzer", "Major");
            QSqlQuery sh(db);
            sh.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, equipment_id, bean_brand) "
                       "VALUES ('s-niche', 1000, 'P', 30, ?, 'SomeBean')");
            sh.addBindValue(niche); QVERIFY(sh.exec());
            sh.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, equipment_id, bean_brand) "
                       "VALUES ('s-other', 2000, 'P', 30, ?, 'SomeBean')");
            sh.addBindValue(other); QVERIFY(sh.exec());
        });

        QSignalSpy spy(&s, &ShotHistoryStorage::shotsFilteredReady);

        // "niche" matches only the Niche-package shot, via the pointer.
        s.requestShotsFiltered({{"searchText", "niche"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 1);   // total == 1

        // A term in no grinder/bean/profile matches nothing.
        s.requestShotsFiltered({{"searchText", "zzz-no-match"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 0);

        s.close();
    }

    // ---- Recipe identity on the shot list (history-recipe-identity) ----------
    //
    // Recipe name/type/archived are resolved LIVE by shots.recipe_id, never
    // snapshotted onto the shot, and a recipe's name is not in shots_fts (which
    // is external-content on `shots`, so it can only index shots columns). These
    // guard the resulting query: the display join, the free-text clause that
    // reaches the other table, the `recipe:` keyword's narrower scope, the exact
    // id filter, and the LIKE escaping.

    // The list row carries the recipe's identity, and a recipe-less shot carries
    // none of it — the two cases the row layout branches on.
    void recipeIdentityOnShotListRow() {
        const QString path = freshDbPath();
        ShotHistoryStorage s;
        QVERIFY(s.initialize(path));
        RecipeSeed seed;
        seedRecipeShots(path, seed);

        QSignalSpy spy(&s, &ShotHistoryStorage::shotsFilteredReady);
        s.requestShotsFiltered({}, 0, 50);
        QVERIFY(spy.wait(3000));

        QVariantMap byUuid;
        const QVariantList rows = spy.last().at(0).toList();
        for (const QVariant& v : rows) {
            const QVariantMap m = v.toMap();
            byUuid.insert(m.value("uuid").toString(), m);
        }
        QCOMPARE(rows.size(), 4);

        const QVariantMap monday = byUuid.value("s-monday").toMap();
        QCOMPARE(monday.value("recipeId").toLongLong(), seed.mondayId);
        QCOMPARE(monday.value("recipeName").toString(), QString("Dad Monday"));
        QCOMPARE(monday.value("recipeDrinkType").toString(), QString("latte"));
        QCOMPARE(monday.value("recipeArchived").toBool(), false);

        const QVariantMap archived = byUuid.value("s-archived").toMap();
        QCOMPARE(archived.value("recipeArchived").toBool(), true);

        // No recipe: the LEFT JOIN yields nulls, which must read as empty rather
        // than as some other shot's recipe.
        const QVariantMap none = byUuid.value("s-none").toMap();
        QCOMPARE(none.value("recipeId").toLongLong(), 0LL);
        QVERIFY(none.value("recipeName").toString().isEmpty());
        QVERIFY(none.value("recipeDrinkType").toString().isEmpty());
        QCOMPARE(none.value("recipeArchived").toBool(), false);

        s.close();
    }

    // A bare free-text term must reach the recipe name, which lives in another
    // table and cannot be in shots_fts. Without the OR'd subquery this returns 0
    // — the "typing my recipe's name finds nothing" gap.
    void recipeNameMatchesBareFreeText() {
        const QString path = freshDbPath();
        ShotHistoryStorage s;
        QVERIFY(s.initialize(path));
        RecipeSeed seed;
        seedRecipeShots(path, seed);
        Q_UNUSED(seed)

        QSignalSpy spy(&s, &ShotHistoryStorage::shotsFilteredReady);

        // "monday" appears in no bean, profile or note — only in a recipe name.
        s.requestShotsFiltered({{"searchText", "monday"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(0).toList().size(), 1);
        // The count query runs against a DIFFERENT `FROM` (no `recipes` join) with
        // the same disjunction string, so this checks the qualified names stay
        // valid without the join. (It is no longer the "two independently built
        // strings" risk an earlier version of this comment described — this change
        // hoisted both to one shared `freeTextMatch`.)
        QCOMPARE(spy.last().at(2).toInt(), 1);

        // Word order must not matter — FTS treats space-separated terms as AND in
        // any order, and this clause has to agree or the same query returns
        // different answers depending on how the user typed it.
        s.requestShotsFiltered({{"searchText", "monday dad"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 1);
        QCOMPARE(spy.last().at(0).toList().size(), 1);

        // Both terms must be present: "dad" matches two recipes, "dad vacation"
        // must match neither.
        s.requestShotsFiltered({{"searchText", "dad vacation"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 0);

        // Archived recipes still match: the shot happened.
        s.requestShotsFiltered({{"searchText", "vacation"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 1);

        s.close();
    }

    // The `recipe:` keyword is narrower than bare text: recipe names only.
    void recipeKeywordScopesToRecipeName() {
        const QString path = freshDbPath();
        ShotHistoryStorage s;
        QVERIFY(s.initialize(path));
        RecipeSeed seed;
        seedRecipeShots(path, seed);
        Q_UNUSED(seed)

        QSignalSpy spy(&s, &ShotHistoryStorage::shotsFilteredReady);

        // Single token: both "Dad Monday" and "Dad Tuesday".
        // Assert ROWS as well as the count on every case: the count query does not
        // join `recipes`, so a defect confined to the data query leaves the count
        // correct and the rows empty — the "shots: [] beside a non-zero total"
        // shape that made the MCP tool's failure invisible.
        s.requestShotsFiltered({{"recipeName", "dad"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 2);
        QCOMPARE(spy.last().at(0).toList().size(), 2);

        // Quoted form (the QML hands the inner text through): disambiguates.
        s.requestShotsFiltered({{"recipeName", "dad tuesday"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 1);
        QCOMPARE(spy.last().at(0).toList().size(), 1);

        // Keyword form is word-order independent too.
        s.requestShotsFiltered({{"recipeName", "tuesday dad"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 1);

        // Case-insensitive.
        s.requestShotsFiltered({{"recipeName", "DAD TUESDAY"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 1);

        // An explicitly empty QUOTED term must filter to NOTHING, never to
        // everything — the search box sends a whitespace sentinel for it. (A bare
        // `recipe:` with a space after it is deliberately NOT this case: the
        // keyword does not match at all and the following word searches normally,
        // because "recipe: dad" returning zero shots was worse than the widening
        // it was meant to prevent.)
        s.requestShotsFiltered({{"recipeName", QStringLiteral(" ")}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 0);

        // Scoped: every seeded shot has bean_brand 'Roaster', but no recipe is
        // named that, so the keyword must return nothing where bare text would
        // have returned everything.
        s.requestShotsFiltered({{"recipeName", "Roaster"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 0);

        s.close();
    }

    // The tap-through filters by ID, so it is unmoved by a rename and is not
    // confused by a second recipe with the same name.
    void recipeIdFilterIsExact() {
        const QString path = freshDbPath();
        ShotHistoryStorage s;
        QVERIFY(s.initialize(path));
        RecipeSeed seed;
        seedRecipeShots(path, seed);

        // A SECOND recipe named exactly "Dad Monday", with its own shot. A
        // name-based filter would conflate the two; an id-based one must not.
        withRawDb(path, "recipe_twin", [&](QSqlDatabase& db) {
            QSqlQuery r(db);
            QVERIFY(r.exec("INSERT INTO recipes (name, profile_title, drink_type, archived) "
                           "VALUES ('Dad Monday', 'P', 'espresso', 0)"));
            const qint64 twin = r.lastInsertId().toLongLong();
            QSqlQuery sh(db);
            sh.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, recipe_id) "
                       "VALUES ('s-twin', 5000, 'P', 30, ?)");
            sh.addBindValue(twin); QVERIFY(sh.exec());
        });

        QSignalSpy spy(&s, &ShotHistoryStorage::shotsFilteredReady);

        s.requestShotsFiltered({{"recipeId", seed.mondayId}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 1);
        QCOMPARE(spy.last().at(0).toList().size(), 1);
        QCOMPARE(spy.last().at(0).toList().first().toMap().value("uuid").toString(),
                 QString("s-monday"));

        // The name both share matches two shots, proving the id filter above was
        // doing something a name filter could not.
        s.requestShotsFiltered({{"recipeName", "dad monday"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 2);

        s.close();
    }

    // ---- Bag identity on the shot list (history-bag-filter) -----------------
    //
    // Same two scopes as recipe above: an exact id (the Custom widget's "this
    // bag" action) and a `bag:` keyword substring. What is worth guarding here
    // and is NOT a repeat of the recipe cases: a bag filter must be strictly
    // narrower than the bean filter beside it (two bags of one coffee), the
    // keyword spans three columns rather than one, and an unset filter must not
    // be confusable with "match the NULL bag_id rows".

    // The exact filter isolates ONE bag, where the bean filter over the same
    // rows returns all of them. If these two ever return the same set, the bag
    // filter has silently become a bean filter.
    void bagIdFilterIsNarrowerThanBean() {
        const QString path = freshDbPath();
        ShotHistoryStorage s;
        QVERIFY(s.initialize(path));
        BagSeed seed;
        seedBagShots(path, seed);

        QSignalSpy spy(&s, &ShotHistoryStorage::shotsFilteredReady);

        // Assert rows as well as the count throughout: the count query does not
        // carry the display join, so a defect confined to the data query leaves
        // a correct total beside empty rows.
        s.requestShotsFiltered({{"bagId", seed.julyId}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 2);
        QCOMPARE(spy.last().at(0).toList().size(), 2);

        s.requestShotsFiltered({{"bagId", seed.augustId}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 1);
        QCOMPARE(spy.last().at(0).toList().first().toMap().value("uuid").toString(),
                 QString("b-august-1"));

        // Same coffee, no bag scope: all four, spanning both bags AND the
        // pre-bag shot. This is the widening the bean action deliberately keeps.
        s.requestShotsFiltered({{"beanBrand", "Roaster"}, {"beanType", "Ethiopia Guji"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 4);

        s.close();
    }

    // An unset bagId must be a no-op, and must never be spelled the same way as
    // "the rows with no bag". The guard is bagIdIsSet(); with a hand-rolled `>= 0` a 0
    // would emit `bag_id = 0`, matching nothing while looking like a filter.
    void bagIdUnsetIsNotAFilter() {
        const QString path = freshDbPath();
        ShotHistoryStorage s;
        QVERIFY(s.initialize(path));
        BagSeed seed;
        seedBagShots(path, seed);
        Q_UNUSED(seed)

        QSignalSpy spy(&s, &ShotHistoryStorage::shotsFilteredReady);

        // -1 is parseFilterMap's default for an absent key; 0 is what a
        // present-but-malformed value parses to. Both mean unset — every shot,
        // including the NULL-bag one. (QML sends neither: the widget helper
        // returns no filter at all when there is no active bag.)
        s.requestShotsFiltered({{"bagId", -1}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 4);

        s.requestShotsFiltered({{"bagId", 0}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 4);

        s.close();
    }

    // A pre-bag shot (NULL bag_id) belongs to no bag, so no bag filter may
    // return it.
    //
    // The EXACT form gets this free from SQL's three-valued logic — `bag_id = ?`
    // never matches NULL whatever the id — so there is nothing here for a bag
    // test to earn. (An earlier version of this test claimed a rewrite to
    // `IFNULL(bag_id,0)` would break it silently; checked against sqlite3, that
    // rewrite returns identical rows. The claim was wrong and the assertion it
    // justified could not fail.) What IS worth pinning is the KEYWORD form: it
    // resolves through a subquery over coffee_bags, where a NULL bag_id landing
    // in an `IN (...)` is a real shape to get wrong.
    void bagKeywordExcludesPreBagShots() {
        const QString path = freshDbPath();
        ShotHistoryStorage s;
        QVERIFY(s.initialize(path));
        BagSeed seed;
        seedBagShots(path, seed);
        Q_UNUSED(seed)

        QSignalSpy spy(&s, &ShotHistoryStorage::shotsFilteredReady);

        // "roaster" is in BOTH bags' roaster_name and in the pre-bag shot's
        // bean_brand, so a clause that leaked to the shot row would return 4.
        s.requestShotsFiltered({{"bagTerm", "roaster"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 3);
        const QVariantList rows = spy.last().at(0).toList();
        QCOMPARE(rows.size(), 3);
        for (const QVariant& v : rows)
            QVERIFY(v.toMap().value("uuid").toString() != QLatin1String("b-prebag"));

        s.close();
    }

    // The `bag:` keyword spans coffee name, roaster AND roast date as ONE
    // identity string, so terms from different fields combine. Per-column ORs
    // would pass every single-term case here and fail the mixed one.
    void bagKeywordSpansIdentityFields() {
        const QString path = freshDbPath();
        ShotHistoryStorage s;
        QVERIFY(s.initialize(path));
        BagSeed seed;
        seedBagShots(path, seed);
        Q_UNUSED(seed)

        QSignalSpy spy(&s, &ShotHistoryStorage::shotsFilteredReady);

        // Coffee name alone: both bags.
        s.requestShotsFiltered({{"bagTerm", "guji"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 3);
        QCOMPARE(spy.last().at(0).toList().size(), 3);

        // Roast date alone narrows to one bag.
        s.requestShotsFiltered({{"bagTerm", "2026-08"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 1);

        // Terms drawn from DIFFERENT columns, ANDed: neither column contains
        // both words, so this is the case a per-column OR gets wrong.
        s.requestShotsFiltered({{"bagTerm", "guji 2026-07"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 2);
        QCOMPARE(spy.last().at(0).toList().size(), 2);

        // Word order must not matter, matching `recipe:` and FTS.
        s.requestShotsFiltered({{"bagTerm", "2026-07 guji"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 2);

        // Case-insensitive.
        s.requestShotsFiltered({{"bagTerm", "GUJI"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 3);

        // The subquery reads the BAG's roast_date, not the shot's. Both tables
        // have that column, so an unqualified reference would resolve outward
        // and match every shot on its own '1999-01-01' — silently, with no SQL
        // error, since the query stays valid either way.
        s.requestShotsFiltered({{"bagTerm", "1999"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 0);

        // An explicitly empty quoted term (`bag:""`) is the whitespace sentinel
        // the search box sends: filter to NOTHING, never to everything.
        s.requestShotsFiltered({{"bagTerm", QStringLiteral(" ")}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 0);

        // Scoped to bags: "ristretto" is in every seeded shot's profile_name and
        // in no bag field, so the keyword must return nothing where the same
        // word as free text returns everything. Both halves asserted — the
        // zero alone would also be produced by a keyword that matches nothing
        // at all, which is the shape a broken clause takes.
        s.requestShotsFiltered({{"bagTerm", "ristretto"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 0);

        s.requestShotsFiltered({{"searchText", "ristretto"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 4);

        s.close();
    }

    // LIKE metacharacters in a BAG's identity are matched literally.
    //
    // The decoy has to SHARE the literal prefix, or the test cannot fail: with
    // "Plain Kenya" as the second bag — the first version of this — the term
    // "100%" unescaped becomes '%100%%', which still needs the substring "100"
    // and so still matches only the first bag. Deleting escapeLikeWildcards left
    // it green. "100 Kenya" is the decoy that distinguishes them, the same shape
    // recipeNameLikeMetacharactersAreLiteral uses ("50% off" vs "50 off").
    //
    // The escaping itself is shared with the recipe keyword (both go through
    // likeContainsLiteral), so this is insurance rather than new coverage. It
    // earns a slot in an existing file — milliseconds of build — because the bag
    // clause matches a CONCATENATED expression over a different table, and the
    // one thing this pins is that the term, not the expression, is what gets
    // escaped.
    void bagTermLikeMetacharactersAreLiteral() {
        const QString path = freshDbPath();
        ShotHistoryStorage s;
        QVERIFY(s.initialize(path));
        withRawDb(path, "bag_meta_seed", [&](QSqlDatabase& db) {
            QSqlQuery b(db);
            QVERIFY(b.exec("INSERT INTO coffee_bags (roaster_name, coffee_name, roast_date) "
                           "VALUES ('R', '100% Kenya', '2026-07-04')"));
            const qint64 pct = b.lastInsertId().toLongLong();
            QVERIFY(b.exec("INSERT INTO coffee_bags (roaster_name, coffee_name, roast_date) "
                           "VALUES ('R', '100 Kenya', '2026-07-05')"));
            const qint64 plain = b.lastInsertId().toLongLong();
            QSqlQuery sh(db);
            sh.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, bag_id) "
                       "VALUES ('b-pct', 1000, 'P', 30, ?)");
            sh.addBindValue(pct); QVERIFY(sh.exec());
            sh.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, bag_id) "
                       "VALUES ('b-plain', 2000, 'P', 30, ?)");
            sh.addBindValue(plain); QVERIFY(sh.exec());
        });

        QSignalSpy spy(&s, &ShotHistoryStorage::shotsFilteredReady);

        // '%' is a literal here. Unescaped it is a wildcard, so "100%" would
        // match "100 Kenya" too and the count would read 2.
        s.requestShotsFiltered({{"bagTerm", "100%"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 1);
        QCOMPARE(spy.last().at(0).toList().first().toMap().value("uuid").toString(),
                 QString("b-pct"));

        // Positive control: without the metacharacter both bags match, so the 1
        // above is the escaping working and not the term simply missing.
        s.requestShotsFiltered({{"bagTerm", "100"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 2);

        s.close();
    }

    // LIKE metacharacters in a recipe name are matched literally. A missed escape
    // does not crash — it silently turns a user's '%' into a wildcard, so this is
    // the only thing that would catch it.
    void recipeNameLikeMetacharactersAreLiteral() {
        const QString path = freshDbPath();
        ShotHistoryStorage s;
        QVERIFY(s.initialize(path));

        withRawDb(path, "recipe_meta", [&](QSqlDatabase& db) {
            auto add = [&](const QString& name, const QString& uuid, qint64 ts) {
                QSqlQuery ri(db);
                ri.prepare("INSERT INTO recipes (name, profile_title, drink_type, archived) VALUES (?, 'P', 'espresso', 0)");
                ri.addBindValue(name);
                QVERIFY(ri.exec());
                const qint64 id = ri.lastInsertId().toLongLong();
                QSqlQuery sh(db);
                sh.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, recipe_id) "
                           "VALUES (?, ?, 'P', 30, ?)");
                sh.addBindValue(uuid); sh.addBindValue(ts); sh.addBindValue(id);
                QVERIFY(sh.exec());
            };
            add("50% off",    "s-pct",   1000);
            add("50 off",     "s-plain", 2000);   // '%' as a wildcard would also match this
            add("a_b",        "s-under", 3000);
            add("axb",        "s-any",   4000);   // '_' as a wildcard would also match this
            add("Bob's Brew", "s-quote", 5000);   // an unescaped quote would break the SQL
        });

        QSignalSpy spy(&s, &ShotHistoryStorage::shotsFilteredReady);

        s.requestShotsFiltered({{"recipeName", "50% off"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 1);
        QCOMPARE(spy.last().at(0).toList().size(), 1);

        s.requestShotsFiltered({{"recipeName", "a_b"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 1);

        s.requestShotsFiltered({{"recipeName", "Bob's"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 1);

        // Same escaping, but through the free-text path, which builds an inline
        // SQL literal instead of a bind — a different function with the same
        // failure mode.
        s.requestShotsFiltered({{"searchText", "Bob's"}}, 0, 50);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.last().at(2).toInt(), 1);

        s.close();
    }

    // Migration 40: the grind picker's reads must be served by a COVERING index,
    // not merely by an index that exists. An index SQLite declines to use, or
    // uses non-covering, buys nothing — and that is the silent failure a future
    // edit introduces: add a column to the SELECT, reorder the WHERE, change
    // grinderModelMatchSql, and the plan falls back to `SCAN shots` with every
    // test still green. That scan cost 435 ms and 483 ms on a 5,788-shot field
    // database, so the PLAN is what is under test.
    //
    // The WHERE half comes from grinderModelMatchSql() so a change to the
    // matching rule reaches this test. The SELECT half is spelled out — it is
    // what makes the index covering, and tracking production there would defeat
    // the assertion.
    void migration40IndexesCoverTheGrindStepQueries() {
        QString path = freshDbPath();
        ShotHistoryStorage storage;
        initAndClose(path, storage);

        withRawDb(path, "m40_plan", [](QSqlDatabase& db) {
            QVERIFY(hasIndex(db, "idx_shots_equip_grind"));
            QVERIFY(hasIndex(db, "idx_shots_equip_rpm"));

            // Rows so the planner is choosing against real content rather than
            // an empty table. Deliberately NO ANALYZE: nothing in src/history
            // ever runs it, so a shipped database has no sqlite_stat1 and SQLite
            // plans these from default row-count estimates. Creating stats here
            // would assert a configuration no install is in — and if a future
            // change made the indexed plan win only WITH stats, this would stay
            // green while every real database fell back to a full scan.
            ShotRow r;
            r.grinderBrand = QStringLiteral("DF");
            r.grinderModel = QStringLiteral("DF83V");
            for (int i = 0; i < 12; ++i) {
                r.uuid = QStringLiteral("m40-%1").arg(i);
                r.timestamp = 1700000000 + i;
                r.profileName = QStringLiteral("p");
                r.grinderSetting = QString::number(1.0 + i * 0.5);
                r.rpm = 600 + i * 100;
                QVERIFY(ShotRowFixtures::insertShot(db, r) > 0);
            }
            const QString match = ShotHistoryStorage::grinderModelMatchSql(QStringLiteral(":model"));
            const struct { const char* label; QString sql; QString index; } cases[] = {
                { "grind_setting",
                  QStringLiteral("SELECT DISTINCT grinder_setting FROM shots "
                                 "WHERE grinder_setting IS NOT NULL AND grinder_setting != '' "
                                 "AND ") + match,
                  QStringLiteral("idx_shots_equip_grind") },
                { "rpm",
                  QStringLiteral("SELECT DISTINCT rpm FROM shots "
                                 "WHERE rpm IS NOT NULL AND rpm > 0 AND ") + match,
                  QStringLiteral("idx_shots_equip_rpm") },
            };

            for (const auto& c : cases) {
                QSqlQuery q(db);
                QVERIFY2(q.prepare(QStringLiteral("EXPLAIN QUERY PLAN ") + c.sql),
                         qPrintable(QStringLiteral("%1: prepare failed: %2")
                                        .arg(QLatin1String(c.label), q.lastError().text())));
                q.bindValue(QStringLiteral(":model"), QStringLiteral("DF83V"));
                QVERIFY2(q.exec(), qPrintable(q.lastError().text()));

                QString plan;
                while (q.next())
                    plan += q.value(q.record().count() - 1).toString() + QLatin1Char(';');

                QVERIFY2(plan.contains(QStringLiteral("COVERING INDEX ") + c.index),
                         qPrintable(QStringLiteral("%1: expected a covering scan of %2, got: %3")
                                        .arg(QLatin1String(c.label), c.index, plan)));
                QVERIFY2(!plan.contains(QStringLiteral("SCAN shots")),
                         qPrintable(QStringLiteral("%1: fell back to a full table scan: %2")
                                        .arg(QLatin1String(c.label), plan)));
            }
        });
    }

};

QTEST_MAIN(tst_DbMigration)
#include "tst_dbmigration.moc"
