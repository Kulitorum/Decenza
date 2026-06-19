#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSettings>

#include "history/shothistorystorage.h"
#include "history/coffeebagstorage.h"

// Test the ShotHistoryStorage schema creation and migration chain (v1->v15).
//
// Strategy: create a temp DB with an old schema (missing columns),
// set schema_version to an old value, then call initialize() which runs
// createTables() + runMigrations(). Verify columns, indexes, FTS, and data.
//
// No de1app equivalent -- this is Decenza-internal storage.

// Helper: run work with a scoped raw SQLite connection (avoids "still in use" warnings)
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

static bool hasIndex(QSqlDatabase& db, const QString& indexName) {
    QSqlQuery q(db);
    q.prepare("SELECT name FROM sqlite_master WHERE type='index' AND name=?");
    q.addBindValue(indexName);
    return q.exec() && q.next();
}

static int getSchemaVersion(QSqlDatabase& db) {
    QSqlQuery q(db);
    q.exec("SELECT MAX(version) FROM schema_version");
    return q.next() ? q.value(0).toInt() : -1;
}

// After a full init, migration 23 has dropped grinder_brand/model/burrs from
// shots + coffee_bags. Tests that rewind schema_version below 22 to re-exercise
// an earlier migration then re-run the WHOLE chain — including migration 22,
// which reads those columns to seed equipment packages. Production never rewinds
// (the chain is monotonic, so the columns are always present when migration 22
// runs), so restore them here to make the simulated "old version" DB schema-
// faithful. ADD COLUMN is a no-op-safe failure when the column already exists.
static void restoreLegacyGrinderColumns(QSqlDatabase& db) {
    QSqlQuery q(db);
    for (const char* table : {"shots", "coffee_bags"})
        for (const char* col : {"grinder_brand", "grinder_model", "grinder_burrs"})
            q.exec(QString("ALTER TABLE %1 ADD COLUMN %2 TEXT").arg(QLatin1String(table), QLatin1String(col)));
}

static void createV1Schema(QSqlDatabase& db) {
    QSqlQuery q(db);
    q.exec(R"(
        CREATE TABLE shots (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            uuid TEXT UNIQUE,
            timestamp INTEGER NOT NULL,
            profile_name TEXT NOT NULL,
            profile_json TEXT,
            duration_seconds REAL NOT NULL,
            final_weight REAL,
            dose_weight REAL,
            bean_brand TEXT,
            bean_type TEXT,
            roast_date TEXT,
            roast_level TEXT,
            grinder_model TEXT,
            grinder_setting TEXT,
            drink_tds REAL,
            drink_ey REAL,
            enjoyment INTEGER,
            espresso_notes TEXT,
            barista TEXT,
            visualizer_id TEXT,
            visualizer_url TEXT,
            debug_log TEXT,
            created_at INTEGER DEFAULT (strftime('%s', 'now')),
            updated_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )");
    q.exec(R"(
        CREATE TABLE shot_samples (
            shot_id INTEGER PRIMARY KEY REFERENCES shots(id) ON DELETE CASCADE,
            sample_count INTEGER NOT NULL,
            data_blob BLOB NOT NULL
        )
    )");
    q.exec(R"(
        CREATE TABLE shot_phases (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            shot_id INTEGER NOT NULL REFERENCES shots(id) ON DELETE CASCADE,
            time_offset REAL NOT NULL,
            label TEXT NOT NULL,
            frame_number INTEGER,
            is_flow_mode INTEGER DEFAULT 0
        )
    )");
    // NOTE: Do NOT create shots_fts here. createTables() will create it
    // with the correct column set. Creating it with fewer columns causes
    // trigger/FTS column count mismatches when createTables() creates triggers.
    q.exec("CREATE TABLE schema_version (version INTEGER PRIMARY KEY)");
    q.exec("INSERT INTO schema_version (version) VALUES (1)");
}

class tst_DbMigration : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;

    QString freshDbPath() {
        static int counter = 0;
        return m_tempDir.path() + QString("/test_%1.db").arg(++counter);
    }

    // Run initialize, close, and wait for background threads to finish.
    // ShotHistoryStorage::initialize() launches requestDistinctCache() on a
    // background thread. We must let that thread complete its callback before
    // the ShotHistoryStorage is destroyed, otherwise SIGSEGV.
    void initAndClose(const QString& path, ShotHistoryStorage& storage) {
        // ShotHistoryStorage::close() removes its DB connection while a background
        // thread may still hold a QSqlQuery reference. Qt warns about this but
        // it's harmless — the connection is cleaned up when the thread finishes.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("connection.*still in use"));
        QVERIFY(storage.initialize(path));
        storage.close();
        // Give background thread time to finish SQL + deliver callback
        for (int i = 0; i < 20; i++) {
            QCoreApplication::processEvents();
            QThread::msleep(25);
        }
    }

    // Like initAndClose(), but for an initialize() that is expected to emit a
    // single migration-failure qWarning (a gated migration that did NOT bump).
    // The migration warning is registered before the close-time "connection
    // still in use" warning so the FIFO ignore queue matches emission order.
    void initExpectingMigrationWarning(const QString& path, const QString& warnRegex) {
        ShotHistoryStorage storage;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(warnRegex));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("connection.*still in use"));
        QVERIFY(storage.initialize(path));
        storage.close();
        for (int i = 0; i < 20; i++) {
            QCoreApplication::processEvents();
            QThread::msleep(25);
        }
    }

private slots:

    void initTestCase() {
        QVERIFY(m_tempDir.isValid());
    }

    // Reset the migration fault-injection seam before every test so a one-shot
    // fault set by one test can never leak into the next.
    void init() {
        ShotHistoryStorage::s_faultInjectMigration = 0;
    }

    // ==========================================
    // Fresh DB: full schema at v12
    // ==========================================

    void freshDbCreatesSchema() {
        QString path = freshDbPath();
        ShotHistoryStorage storage;
        initAndClose(path, storage);

        withRawDb(path, "fresh_schema", [](QSqlDatabase& db) {
            QVERIFY(hasTable(db, "shots"));
            QVERIFY(hasTable(db, "shot_samples"));
            QVERIFY(hasTable(db, "shot_phases"));
            QVERIFY(hasTable(db, "schema_version"));
            QCOMPARE(getSchemaVersion(db), 23);
        });
    }

    void freshDbHasAllColumns() {
        QString path = freshDbPath();
        ShotHistoryStorage storage;
        initAndClose(path, storage);

        withRawDb(path, "fresh_cols", [](QSqlDatabase& db) {
            QVERIFY(hasColumn(db, "shots", "temperature_override"));
            QVERIFY(hasColumn(db, "shots", "yield_override"));
            QVERIFY(hasColumn(db, "shots", "beverage_type"));
            QVERIFY(hasColumn(db, "shots", "bean_notes"));
            QVERIFY(hasColumn(db, "shots", "profile_notes"));
            // Grinder identity columns were added by migration 8 and DROPPED by
            // migration 23 (add-equipment-packages) — identity now resolves via
            // equipment_id. The grind setting + equipment_id/rpm survive.
            QVERIFY(!hasColumn(db, "shots", "grinder_brand"));
            QVERIFY(!hasColumn(db, "shots", "grinder_model"));
            QVERIFY(!hasColumn(db, "shots", "grinder_burrs"));
            QVERIFY(hasColumn(db, "shots", "grinder_setting"));
            QVERIFY(hasColumn(db, "shots", "equipment_id"));
            QVERIFY(hasColumn(db, "shots", "rpm"));
            QVERIFY(hasColumn(db, "shots", "profile_kb_id"));
            QVERIFY(hasColumn(db, "shots", "channeling_detected"));
            // temperature_unstable was added in migration 10 and dropped in
            // migration 15 — see remove-temperature-unstable-badge change.
            QVERIFY(!hasColumn(db, "shots", "temperature_unstable"));
            QVERIFY(hasColumn(db, "shots", "grind_issue_detected"));
            QVERIFY(hasColumn(db, "shots", "skip_first_frame_detected"));
            QVERIFY(hasColumn(db, "shots", "pour_truncated_detected"));
            QVERIFY(hasColumn(db, "shots", "stopped_by"));  // migration 17 (#1161)
            QVERIFY(hasColumn(db, "shots", "beanbase_json"));  // migration 18 (bean base)
            QVERIFY(hasColumn(db, "shot_phases", "transition_reason"));
        });
    }

    void freshDbHasFtsAndIndexes() {
        QString path = freshDbPath();
        ShotHistoryStorage storage;
        initAndClose(path, storage);

        withRawDb(path, "fresh_fts", [](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT * FROM shots_fts LIMIT 0"));
            QVERIFY(hasIndex(db, "idx_shots_timestamp"));
            QVERIFY(hasIndex(db, "idx_shots_profile"));
            QVERIFY(hasIndex(db, "idx_shots_bean"));
            // idx_shots_grinder was on grinder_brand/model, dropped with those
            // columns in migration 23 (add-equipment-packages).
            QVERIFY(!hasIndex(db, "idx_shots_grinder"));
            QVERIFY(hasIndex(db, "idx_shots_enjoyment"));
            QVERIFY(hasIndex(db, "idx_shot_phases_shot"));
            QVERIFY(hasIndex(db, "idx_shots_profile_kb_id"));
        });
    }

    // ==========================================
    // Migration from v1: adds all missing columns
    // ==========================================

    void v1MigrationAddsColumns() {
        QString path = freshDbPath();

        withRawDb(path, "v1_create", [](QSqlDatabase& db) {
            createV1Schema(db);
            QCOMPARE(getSchemaVersion(db), 1);
            QVERIFY(!hasColumn(db, "shots", "temperature_override"));
        });

        ShotHistoryStorage storage;
        initAndClose(path, storage);

        withRawDb(path, "v1_verify", [](QSqlDatabase& db) {
            QCOMPARE(getSchemaVersion(db), 23);
            QVERIFY(hasColumn(db, "shots", "temperature_override"));
            QVERIFY(hasColumn(db, "shots", "yield_override"));
            QVERIFY(hasColumn(db, "shots", "beverage_type"));
            // Grinder identity columns are dropped by migration 23; equipment_id
            // is the identity pointer now (add-equipment-packages).
            QVERIFY(!hasColumn(db, "shots", "grinder_brand"));
            QVERIFY(!hasColumn(db, "shots", "grinder_burrs"));
            QVERIFY(hasColumn(db, "shots", "equipment_id"));
            QVERIFY(hasColumn(db, "shots", "profile_kb_id"));
            QVERIFY(hasColumn(db, "shots", "channeling_detected"));
            // temperature_unstable was added in migration 10 and dropped in
            // migration 15 — see remove-temperature-unstable-badge change.
            QVERIFY(!hasColumn(db, "shots", "temperature_unstable"));
            QVERIFY(hasColumn(db, "shots", "grind_issue_detected"));
            QVERIFY(hasColumn(db, "shots", "skip_first_frame_detected"));
            QVERIFY(hasColumn(db, "shots", "pour_truncated_detected"));
            QVERIFY(hasColumn(db, "shots", "stopped_by"));  // migration 17 (#1161)
            QVERIFY(hasColumn(db, "shots", "beanbase_json"));  // migration 18 (bean base)
            QVERIFY(hasColumn(db, "shot_phases", "transition_reason"));
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
    // beverage_type column exists and has default
    // ==========================================

    void beverageTypeColumn() {
        QString path = freshDbPath();
        ShotHistoryStorage storage;
        initAndClose(path, storage);

        withRawDb(path, "bev_verify", [](QSqlDatabase& db) {
            QVERIFY(hasColumn(db, "shots", "beverage_type"));
            // Insert and verify default is 'espresso'
            QSqlQuery q(db);
            q.exec("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds) VALUES ('bev-test', 1000, 'Test', 30.0)");
            q.exec("SELECT beverage_type FROM shots WHERE uuid = 'bev-test'");
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QString("espresso"));
        });
    }

    // ==========================================
    // Migration 23: grinder identity columns are dropped from shots
    // ==========================================
    // Migration 8 added grinder_brand/model/burrs; migration 23 (add-equipment-
    // packages) drops them from BOTH shots and coffee_bags — grinder identity
    // resolves through equipment_id to the package's grinder item. Only the
    // per-shot/per-bag grind setting survives.
    void grinderIdentityColumnsDropped() {
        QString path = freshDbPath();
        ShotHistoryStorage storage;
        initAndClose(path, storage);

        withRawDb(path, "grinder_verify", [](QSqlDatabase& db) {
            for (const char* table : {"shots", "coffee_bags"}) {
                QVERIFY2(!hasColumn(db, table, "grinder_brand"), table);
                QVERIFY2(!hasColumn(db, table, "grinder_model"), table);
                QVERIFY2(!hasColumn(db, table, "grinder_burrs"), table);
                // The dial-in + equipment pointer survive on both tables.
                QVERIFY2(hasColumn(db, table, "grinder_setting"), table);
                QVERIFY2(hasColumn(db, table, "equipment_id"), table);
            }
            // The grinder index is gone (it was on the dropped shots columns).
            QVERIFY(!hasIndex(db, "idx_shots_grinder"));
        });
    }

    // ==========================================
    // Migration v9: profile_kb_id
    // ==========================================

    void v9AddsProfileKbId() {
        QString path = freshDbPath();
        ShotHistoryStorage storage;
        initAndClose(path, storage);

        withRawDb(path, "v9_verify", [](QSqlDatabase& db) {
            QVERIFY(hasColumn(db, "shots", "profile_kb_id"));
            QVERIFY(hasIndex(db, "idx_shots_profile_kb_id"));
            QCOMPARE(getSchemaVersion(db), 23);
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
            QCOMPARE(getSchemaVersion(db), 23);
        });
    }

    // ==========================================
    // Edge case: empty v1 DB migrates cleanly
    // ==========================================

    void emptyDbMigration() {
        QString path = freshDbPath();
        withRawDb(path, "empty_create", [](QSqlDatabase& db) { createV1Schema(db); });

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("connection.*still in use"));
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(path));
        QCOMPARE(storage.totalShots(), 0);
        storage.close();
        QCoreApplication::processEvents();

        withRawDb(path, "empty_verify", [](QSqlDatabase& db) {
            QCOMPARE(getSchemaVersion(db), 23);
        });
    }

    // ==========================================
    // Edge case: NULLs in optional columns
    // ==========================================

    void nullColumnsNocrash() {
        QString path = freshDbPath();
        withRawDb(path, "null_create", [](QSqlDatabase& db) {
            createV1Schema(db);
            QSqlQuery(db).exec("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds) VALUES ('test-null', 1000, 'NullTest', 30.0)");
        });

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("connection.*still in use"));
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(path));
        QCOMPARE(storage.totalShots(), 1);
        storage.close();
        QCoreApplication::processEvents();

        withRawDb(path, "null_verify", [](QSqlDatabase& db) {
            QCOMPARE(getSchemaVersion(db), 23);
            QSqlQuery q(db);
            // grinder_brand was dropped in migration 23; grinder_setting (the
            // surviving per-shot dial-in) exercises the same NULL-tolerance path.
            q.exec("SELECT grinder_setting FROM shots WHERE uuid = 'test-null'");
            QVERIFY(q.next());
            QVERIFY(q.value(0).isNull() || q.value(0).toString().isEmpty());
        });
    }

    // ==========================================
    // FTS indexes data from v1 chain
    // ==========================================

    void ftsIndexesExistingData() {
        // Insert data in v1 schema, run full chain, verify FTS has it
        QString path = freshDbPath();
        withRawDb(path, "fts_v1", [](QSqlDatabase& db) {
            createV1Schema(db);
            QSqlQuery q(db);
            q.exec("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, espresso_notes, bean_brand) VALUES ('fts-v1', 1000, 'D-Flow Default', 30.0, 'Sweet and balanced', 'Onyx')");
        });

        ShotHistoryStorage storage;
        initAndClose(path, storage);

        withRawDb(path, "fts_v1_verify", [](QSqlDatabase& db) {
            QSqlQuery q(db);
            // FTS should have indexed the existing shot after migration chain
            QVERIFY(q.exec("SELECT rowid FROM shots_fts WHERE shots_fts MATCH 'Sweet'"));
            QVERIFY2(q.next(), "FTS should find by espresso_notes after full chain");

            QVERIFY(q.exec("SELECT rowid FROM shots_fts WHERE shots_fts MATCH 'Onyx'"));
            QVERIFY2(q.next(), "FTS should find by bean_brand after full chain");
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
    // Full chain v1->v15 preserves existing data
    // ==========================================

    void fullChainPreservesData() {
        QString path = freshDbPath();
        withRawDb(path, "chain_create", [](QSqlDatabase& db) {
            createV1Schema(db);
            QSqlQuery q(db);
            q.exec("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, final_weight, bean_brand, grinder_model) VALUES ('p1', 1000, 'Blooming', 28.5, 36.2, 'Onyx', 'Niche Zero')");
            q.exec("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, final_weight, bean_brand, grinder_model) VALUES ('p2', 2000, 'D-Flow', 32.0, 40.0, 'SEY', 'DF64')");
        });

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("connection.*still in use"));
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(path));
        QCOMPARE(storage.totalShots(), 2);
        storage.close();
        QCoreApplication::processEvents();

        withRawDb(path, "chain_verify", [](QSqlDatabase& db) {
            QSqlQuery q(db);
            q.exec("SELECT profile_name, final_weight, bean_brand FROM shots WHERE uuid = 'p1'");
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QString("Blooming"));
            QCOMPARE(q.value(1).toDouble(), 36.2);
            QCOMPARE(q.value(2).toString(), QString("Onyx"));
            // grinder_brand dropped by migration 23; equipment_id is the pointer.
            QVERIFY(!hasColumn(db, "shots", "grinder_brand"));
            QVERIFY(hasColumn(db, "shots", "equipment_id"));
            QVERIFY(hasColumn(db, "shots", "profile_kb_id"));
        });
    }

    // ==========================================
    // Sample data survives full migration chain
    // ==========================================

    void sampleDataSurvivesChain() {
        // Insert compressed sample data in v1 schema, run full chain, verify intact
        QString path = freshDbPath();
        withRawDb(path, "sample_create", [](QSqlDatabase& db) {
            createV1Schema(db);
            QSqlQuery q(db);
            q.exec("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds) VALUES ('sample-test', 1000, 'Test', 30.0)");
            q.exec("SELECT id FROM shots WHERE uuid = 'sample-test'");
            QVERIFY(q.next());
            qint64 shotId = q.value(0).toLongLong();

            // Create sample data with weightFlowRate
            QJsonObject root;
            QJsonObject wfrObj;
            QJsonArray timeArr, valueArr;
            for (int i = 0; i < 20; i++) {
                timeArr.append(i * 0.2);
                valueArr.append((i % 2 == 0) ? 2.0 : 0.0);
            }
            wfrObj["t"] = timeArr;
            wfrObj["v"] = valueArr;
            root["weightFlowRate"] = wfrObj;

            QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
            QByteArray blob = qCompress(json, 9);

            q.prepare("INSERT INTO shot_samples (shot_id, sample_count, data_blob) VALUES (?, 20, ?)");
            q.addBindValue(shotId);
            q.addBindValue(blob);
            QVERIFY(q.exec());
        });

        ShotHistoryStorage storage;
        initAndClose(path, storage);

        withRawDb(path, "sample_verify", [](QSqlDatabase& db) {
            QSqlQuery q(db);
            q.exec("SELECT data_blob FROM shot_samples");
            QVERIFY(q.next());

            QByteArray blob = q.value(0).toByteArray();
            QByteArray json = qUncompress(blob);
            QVERIFY2(!json.isEmpty(), "Sample data should decompress after migration");

            QJsonDocument doc = QJsonDocument::fromJson(json);
            QJsonArray vals = doc.object()["weightFlowRate"].toObject()["v"].toArray();
            QCOMPARE(vals.size(), 20);

            // v7 migration applies smoothing: values should be closer to mean (1.0)
            for (int i = 3; i < 17; i++) {
                double val = vals[i].toDouble();
                QVERIFY2(val > 0.3 && val < 1.7,
                         qPrintable(QString("Smoothed[%1]=%2, expected near 1.0").arg(i).arg(val)));
            }
        });
    }

    // Migration 16: drop enjoyment_source column. Layer 3's inferred
    // auto-rating was rolled back as a failed experiment — migration 14
    // added the column, migration 16 drops it after resetting any
    // inferred rows to the user's configured default rating. Idempotency
    // check: running ShotHistoryStorage::initialize twice on the same DB
    // ends at the current schema_version and the column is GONE on both passes.
    void v16_columnIsDropped()
    {
        const QString path = freshDbPath();
        {
            ShotHistoryStorage s1;
            initAndClose(path, s1);
        }
        {
            ShotHistoryStorage s2;
            initAndClose(path, s2);
        }

        bool hasEnjoymentSource = false;
        int versionFound = 0;
        withRawDb(path, "v16_idem", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT version FROM schema_version"));
            if (q.next()) versionFound = q.value(0).toInt();
            QVERIFY(q.exec("PRAGMA table_info(shots)"));
            while (q.next()) {
                if (q.value(1).toString() == "enjoyment_source") {
                    hasEnjoymentSource = true;
                    break;
                }
            }
        });
        QCOMPARE(versionFound, 23);
        QVERIFY2(!hasEnjoymentSource,
                 "enjoyment_source column must be absent after migration 16");
    }

    // Migration 16 contract: rows with enjoyment_source = 'inferred'
    // have their enjoyment reset to the user's configured default
    // rating (QSettings shot/defaultRating). Rows uploaded to
    // Visualizer (visualizer_id non-empty) are stashed in the pending
    // sync list for MainController to re-PATCH after boot.
    void v16_resetsInferredAndDropsColumn()
    {
        const QString path = freshDbPath();

        // First init creates the current schema (column already absent).
        // Re-add the column with v14 semantics, insert representative
        // rows, then rewind schema_version to 15 so the next init runs
        // migration 16 in isolation.
        {
            ShotHistoryStorage s1;
            initAndClose(path, s1);
        }

        const qint64 now = QDateTime::currentSecsSinceEpoch();
        withRawDb(path, "v16_seed", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("ALTER TABLE shots ADD COLUMN enjoyment_source TEXT NOT NULL DEFAULT 'none'"));

            // (u1) inferred + visualizer-uploaded — must be reset AND queued
            q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, "
                      "enjoyment, enjoyment_source, visualizer_id) "
                      "VALUES (?, ?, 'P', 30, 75, 'inferred', 'V1')");
            q.addBindValue("u1"); q.addBindValue(now - 3600);
            QVERIFY(q.exec());
            // (u2) inferred + not uploaded — reset but not queued
            q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, "
                      "enjoyment, enjoyment_source, visualizer_id) "
                      "VALUES (?, ?, 'P', 30, 75, 'inferred', NULL)");
            q.addBindValue("u2"); q.addBindValue(now - 7200);
            QVERIFY(q.exec());
            // (u3) user-rated — untouched
            q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, "
                      "enjoyment, enjoyment_source, visualizer_id) "
                      "VALUES (?, ?, 'P', 30, 90, 'user', 'V2')");
            q.addBindValue("u3"); q.addBindValue(now - 10800);
            QVERIFY(q.exec());
            // (u4) unrated — untouched
            q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, "
                      "enjoyment, enjoyment_source, visualizer_id) "
                      "VALUES (?, ?, 'P', 30, 0, 'none', NULL)");
            q.addBindValue("u4"); q.addBindValue(now - 14400);
            QVERIFY(q.exec());

            restoreLegacyGrinderColumns(db);  // migration 22 re-runs in the chain
            QVERIFY(q.exec("UPDATE schema_version SET version = 15"));
        });

        // Set the configured default rating that migration 16 reads.
        // Must use the SAME scope production reads — the app's Settings
        // owns QSettings("DecentEspresso","DE1Qt"). A bare QSettings()
        // here is exactly the scope-mismatch bug this regression-tests:
        // it passed before the production fix only because production
        // also (wrongly) used a bare QSettings.
        QSettings appSettings(QStringLiteral("DecentEspresso"),
                              QStringLiteral("DE1Qt"));
        const QVariant prior = appSettings.value("shot/defaultRating");
        const QVariant priorPending = appSettings.value("migration16/pendingVisualizerSync");
        appSettings.setValue("shot/defaultRating", 50);
        appSettings.remove("migration16/pendingVisualizerSync");

        // Run migration 16.
        {
            ShotHistoryStorage s2;
            initAndClose(path, s2);
        }

        bool columnGone = true;
        int versionFound = 0;
        int enjoy1 = -1, enjoy2 = -1, enjoy3 = -1, enjoy4 = -1;
        withRawDb(path, "v16_verify", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT version FROM schema_version"));
            if (q.next()) versionFound = q.value(0).toInt();

            QVERIFY(q.exec("PRAGMA table_info(shots)"));
            while (q.next()) {
                if (q.value(1).toString() == "enjoyment_source") {
                    columnGone = false;
                    break;
                }
            }

            QVERIFY(q.exec("SELECT uuid, enjoyment FROM shots ORDER BY uuid"));
            while (q.next()) {
                const QString u = q.value(0).toString();
                const int e = q.value(1).toInt();
                if      (u == "u1") enjoy1 = e;
                else if (u == "u2") enjoy2 = e;
                else if (u == "u3") enjoy3 = e;
                else if (u == "u4") enjoy4 = e;
            }
        });

        QCOMPARE(versionFound, 23);
        QVERIFY2(columnGone, "enjoyment_source column must be dropped");
        QCOMPARE(enjoy1, 50);
        QCOMPARE(enjoy2, 50);
        QCOMPARE(enjoy3, 90);
        QCOMPARE(enjoy4, 0);

        // Visualizer back-sync queue: exactly one entry (V1 — V2 belongs to
        // a user-rated row that we never auto-stamped, so it's not queued).
        const QByteArray pendingJson = appSettings.value(
            "migration16/pendingVisualizerSync").toByteArray();
        const QJsonArray pending = QJsonDocument::fromJson(pendingJson).array();
        QCOMPARE(pending.size(), 1);
        QCOMPARE(pending.first().toObject().value("visualizerId").toString(),
                 QStringLiteral("V1"));

        // Restore QSettings so we don't leak test state.
        if (prior.isValid()) appSettings.setValue("shot/defaultRating", prior);
        else appSettings.remove("shot/defaultRating");
        if (priorPending.isValid())
            appSettings.setValue("migration16/pendingVisualizerSync", priorPending);
        else appSettings.remove("migration16/pendingVisualizerSync");
    }

    // OpenSpec persist-visualizer-id-in-controller: the reconciliation
    // matcher links empty-visualizer_id rows to cloud shots by start
    // time (±2s), strict 1:1, never reusing an id already on a row,
    // skipping ambiguous and out-of-window rows; and is idempotent.
    void reconcileVisualizerLinks_matchingContract()
    {
        const QString path = freshDbPath();
        {
            ShotHistoryStorage s;
            initAndClose(path, s);
        }

        auto insertShot = [](QSqlDatabase& db, const QString& uuid, qint64 ts,
                             const QString& vizId) -> qint64 {
            QSqlQuery q(db);
            q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, "
                      "duration_seconds, visualizer_id) "
                      "VALUES (?, ?, 'P', 30, ?)");
            q.addBindValue(uuid);
            q.addBindValue(ts);
            q.addBindValue(vizId);
            if (!q.exec()) {
                qWarning() << "insertShot failed:" << q.lastError().text();
                return -1;
            }
            return q.lastInsertId().toLongLong();
        };

        const qint64 windowStart = 900;
        qint64 idA = 0, idB = 0, idC = 0, idD = 0, idE = 0;
        withRawDb(path, "recon_seed", [&](QSqlDatabase& db) {
            idA = insertShot(db, "A", 1000, QString());         // in-window, empty
            idB = insertShot(db, "B", 2000, "V-EXIST");         // already linked
            idC = insertShot(db, "C", 500,  QString());         // before window
            idD = insertShot(db, "D", 3000, QString());         // ambiguous (two cloud @ ~3000)
            idE = insertShot(db, "E", 4000, QString());         // only candidate id already used
        });
        QVERIFY(idA > 0 && idB > 0 && idC > 0 && idD > 0 && idE > 0);

        // Cloud list. clockEpoch within 2s of A's 1000 → link.
        // V-EXIST is already on row B → must never be reused (E).
        // Two cloud shots within tol of D's 3000 → ambiguous → skip.
        auto cloud = [](const QString& id, qint64 clk) {
            QVariantMap m;
            m["visualizerId"] = id;
            m["url"] = "https://visualizer.coffee/shots/" + id;
            m["clockEpoch"] = clk;
            return QVariant(m);
        };
        QVariantList cloudShots{
            cloud("V-A", 1001),       // matches A (Δ1s)
            cloud("V-EXIST", 4000),   // would match E but id already on B
            cloud("V-D1", 3000),      // \_ both within tol of D → ambiguous
            cloud("V-D2", 3001),      // /
            cloud("V-C", 500),        // matches C by time but C is out of window
        };

        QVariantList linked;
        bool ok = false;
        withRawDb(path, "recon_run", [&](QSqlDatabase& db) {
            ok = ShotHistoryStorage::reconcileVisualizerLinksStatic(
                db, cloudShots, windowStart, linked);
        });

        QVERIFY2(ok, "healthy DB must report success");
        QCOMPARE(linked.size(), 1);
        QCOMPARE(linked.first().toMap().value("shotId").toLongLong(), idA);
        QCOMPARE(linked.first().toMap().value("visualizerId").toString(),
                 QStringLiteral("V-A"));

        // Verify persisted state + non-targets untouched.
        withRawDb(path, "recon_verify", [&](QSqlDatabase& db) {
            auto vizId = [&](qint64 id) {
                QSqlQuery q(db);
                q.prepare("SELECT COALESCE(visualizer_id,'') FROM shots WHERE id = ?");
                q.addBindValue(id);
                q.exec(); q.next();
                return q.value(0).toString();
            };
            QCOMPARE(vizId(idA), QStringLiteral("V-A"));
            QCOMPARE(vizId(idB), QStringLiteral("V-EXIST"));  // unchanged
            QCOMPARE(vizId(idC), QString());                  // out of window
            QCOMPARE(vizId(idD), QString());                  // ambiguous
            QCOMPARE(vizId(idE), QString());                  // id already used
        });

        // Idempotent: A now has an id, nothing left to link.
        QVariantList second;
        bool secondOk = false;
        withRawDb(path, "recon_again", [&](QSqlDatabase& db) {
            secondOk = ShotHistoryStorage::reconcileVisualizerLinksStatic(
                db, cloudShots, windowStart, second);
        });
        QVERIFY2(secondOk, "idempotent re-run still reports success");
        QVERIFY2(second.isEmpty(), "re-run must be a no-op");
    }

    // ±2 s tolerance is the load-bearing line (local save-time vs cloud
    // shot-epoch skew is absorbed by it). Pin the inclusive boundary on
    // both sides and that +3 s is excluded — an off-by-one here silently
    // orphans real shots.
    void reconcileVisualizerLinks_toleranceBoundary()
    {
        const QString path = freshDbPath();
        { ShotHistoryStorage s; initAndClose(path, s); }

        auto ins = [](QSqlDatabase& db, const QString& uuid, qint64 ts) -> qint64 {
            QSqlQuery q(db);
            q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, "
                      "duration_seconds) VALUES (?, ?, 'P', 30)");
            q.addBindValue(uuid); q.addBindValue(ts);
            return q.exec() ? q.lastInsertId().toLongLong() : -1;
        };
        qint64 idPlus2 = 0, idMinus2 = 0, idPlus3 = 0;
        withRawDb(path, "tol_seed", [&](QSqlDatabase& db) {
            idPlus2  = ins(db, "P2", 10000);   // cloud @ 10002 → Δ+2 (inclusive)
            idMinus2 = ins(db, "M2", 20000);   // cloud @ 19998 → Δ-2 (inclusive)
            idPlus3  = ins(db, "P3", 30000);   // cloud @ 30003 → Δ+3 (excluded)
        });
        QVERIFY(idPlus2 > 0 && idMinus2 > 0 && idPlus3 > 0);

        auto cloud = [](const QString& id, qint64 clk) {
            QVariantMap m; m["visualizerId"] = id;
            m["url"] = "u/" + id; m["clockEpoch"] = clk; return QVariant(m);
        };
        QVariantList cloudShots{
            cloud("V-P2", 10002), cloud("V-M2", 19998), cloud("V-P3", 30003),
        };

        QVariantList linked;
        bool ok = false;
        withRawDb(path, "tol_run", [&](QSqlDatabase& db) {
            ok = ShotHistoryStorage::reconcileVisualizerLinksStatic(
                db, cloudShots, /*windowStart*/0, linked);
        });
        QVERIFY(ok);

        QSet<qint64> linkedIds;
        for (const QVariant& v : linked)
            linkedIds.insert(v.toMap().value("shotId").toLongLong());
        QVERIFY2(linkedIds.contains(idPlus2),  "+2 s must link (inclusive)");
        QVERIFY2(linkedIds.contains(idMinus2), "-2 s must link (inclusive)");
        QVERIFY2(!linkedIds.contains(idPlus3), "+3 s must NOT link");
        QCOMPARE(linked.size(), 2);
    }

    // The real production scenario is MANY orphaned rows reconciled in
    // one pass (shots ~901-923). Pin: distinct rows each claim their
    // own distinct cloud id, cross-row consumption never reuses an id,
    // and a row with ≥2 in-tolerance candidates is skipped (not guessed)
    // even amid other successful links. Also covers empty inputs.
    void reconcileVisualizerLinks_multiRowAndEmpty()
    {
        const QString path = freshDbPath();
        { ShotHistoryStorage s; initAndClose(path, s); }

        auto ins = [](QSqlDatabase& db, const QString& uuid, qint64 ts) -> qint64 {
            QSqlQuery q(db);
            q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, "
                      "duration_seconds) VALUES (?, ?, 'P', 30)");
            q.addBindValue(uuid); q.addBindValue(ts);
            return q.exec() ? q.lastInsertId().toLongLong() : -1;
        };
        auto cloud = [](const QString& id, qint64 clk) {
            QVariantMap m; m["visualizerId"] = id;
            m["url"] = "u/" + id; m["clockEpoch"] = clk; return QVariant(m);
        };

        // Empty cloud list against eligible rows → ok, nothing linked,
        // rows untouched (must not throw / mislink / report failure).
        qint64 e1 = 0;
        withRawDb(path, "mr_seed", [&](QSqlDatabase& db) {
            e1 = ins(db, "E1", 5000);
        });
        QVERIFY(e1 > 0);
        {
            QVariantList linked; bool ok = false;
            withRawDb(path, "mr_empty", [&](QSqlDatabase& db) {
                ok = ShotHistoryStorage::reconcileVisualizerLinksStatic(
                    db, QVariantList(), 0, linked);
            });
            QVERIFY2(ok, "empty cloud list is a successful no-op");
            QVERIFY(linked.isEmpty());
        }

        // Two distinct in-window rows + one ambiguous row, all in one
        // pass. X→V-X, Y→V-Y (distinct), Z has two candidates → skip.
        qint64 idX = 0, idY = 0, idZ = 0;
        withRawDb(path, "mr_seed2", [&](QSqlDatabase& db) {
            idX = ins(db, "X", 5000);
            idY = ins(db, "Y", 8000);
            idZ = ins(db, "Z", 9000);
        });
        QVERIFY(idX > 0 && idY > 0 && idZ > 0);
        QVariantList cloudShots{
            cloud("V-X", 5001),
            cloud("V-Y", 7999),
            cloud("V-Z1", 9000), cloud("V-Z2", 9001),  // ambiguous for Z
        };
        QVariantList linked;
        bool ok = false;
        withRawDb(path, "mr_run", [&](QSqlDatabase& db) {
            ok = ShotHistoryStorage::reconcileVisualizerLinksStatic(
                db, cloudShots, 0, linked);
        });
        QVERIFY(ok);
        QCOMPARE(linked.size(), 2);
        QHash<qint64, QString> got;
        for (const QVariant& v : linked) {
            const QVariantMap m = v.toMap();
            got.insert(m.value("shotId").toLongLong(),
                       m.value("visualizerId").toString());
        }
        QCOMPARE(got.value(idX), QStringLiteral("V-X"));
        QCOMPARE(got.value(idY), QStringLiteral("V-Y"));
        QVERIFY2(!got.contains(idZ), "ambiguous row Z must be skipped, not guessed");
        QVERIFY2(got.value(idX) != got.value(idY), "distinct rows get distinct ids");

        // E1 (seeded earlier, no matching cloud shot) stays unlinked.
        withRawDb(path, "mr_verify", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            q.prepare("SELECT COALESCE(visualizer_id,'') FROM shots WHERE id = ?");
            q.addBindValue(e1); q.exec(); q.next();
            QCOMPARE(q.value(0).toString(), QString());
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
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("connection.*still in use"));
            QVERIFY(s.initialize(path));
            QCOMPARE(s.lastSavedShotId(), qint64(0));
            s.close();
            for (int i = 0; i < 20; i++) { QCoreApplication::processEvents(); QThread::msleep(25); }
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
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("connection.*still in use"));
            QVERIFY(s.initialize(path));
            QCOMPARE(s.lastSavedShotId(), newestId);
            s.close();
            for (int i = 0; i < 20; i++) { QCoreApplication::processEvents(); QThread::msleep(25); }
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
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("connection.*still in use"));
            QVERIFY(s.initialize(path));
            QCOMPARE(s.lastSavedShotId(), newestId - 1);
            s.close();
            for (int i = 0; i < 20; i++) { QCoreApplication::processEvents(); QThread::msleep(25); }
        }
    }

    // ==========================================
    // Migration 21: coffee_bags.yield_target_g -> yield_override_g
    // ==========================================
    // The bag yield column was renamed when the yield-override model landed
    // (bean-bag-inventory). Fresh DBs get yield_override_g from CREATE TABLE;
    // dev DBs already at v19/v20 carry the old name and are repaired by
    // migration 21. Simulate the old state by renaming the column back and
    // resetting the version, then re-initialize and verify the rename plus
    // that the stored value survived the ALTER.
    void v21_renamesYieldColumn() {
        const QString path = freshDbPath();
        { ShotHistoryStorage s; initAndClose(path, s); }

        // Force the pre-rename schema: new name -> old name, version 20.
        withRawDb(path, "v21_setup", [](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("ALTER TABLE coffee_bags RENAME COLUMN yield_override_g TO yield_target_g"));
            QVERIFY(q.exec("INSERT INTO coffee_bags (roaster_name, coffee_name, yield_target_g, in_inventory) "
                           "VALUES ('Onyx', 'Geometry', 42.0, 1)"));
            restoreLegacyGrinderColumns(db);  // migration 22 re-runs in the chain
            q.exec("DELETE FROM schema_version");
            q.exec("INSERT INTO schema_version (version) VALUES (20)");
        });

        { ShotHistoryStorage s; initAndClose(path, s); }

        withRawDb(path, "v21_verify", [](QSqlDatabase& db) {
            QCOMPARE(getSchemaVersion(db), 23);
            QVERIFY(hasColumn(db, "coffee_bags", "yield_override_g"));
            QVERIFY(!hasColumn(db, "coffee_bags", "yield_target_g"));
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT yield_override_g FROM coffee_bags WHERE coffee_name = 'Geometry'"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toDouble(), 42.0);
        });
    }

    // ==========================================
    // Migration failure / retry branches (bean-bag-inventory #1327 follow-up)
    // ==========================================

    // The producer behind migration 20's gate: linkOrphanShotsStatic returns
    // -1 (not 0) when its UPDATE can't run. createV1Schema gives a shots table
    // with no bag_id column and no coffee_bags table, so pass-1 fails to
    // prepare. This is the SQL-failure signal the migration relies on to know
    // it must NOT bump the schema version.
    void linkOrphanShotsStatic_returnsMinusOneOnSqlFailure() {
        const QString path = freshDbPath();
        int result = 0;
        withRawDb(path, "orphan_minus1", [&](QSqlDatabase& db) {
            createV1Schema(db);  // shots without bag_id; no coffee_bags table
            QTest::ignoreMessage(QtWarningMsg,
                QRegularExpression("orphan-shot link pass 1 failed"));
            result = CoffeeBagStorage::linkOrphanShotsStatic(db);
        });
        QCOMPARE(result, -1);
    }

    // Migration 20 gate + retry. A transient failure (modelled with the
    // fault-injection seam) must leave the schema version UNbumped so the
    // whole chain retries cleanly next launch — bumping past it would strand
    // the orphan shots forever. First init: orphan-link "fails", version stays
    // at 19, the orphan shot is untouched. Second init (fault auto-cleared):
    // the chain runs to completion, reaching v21 and linking the shot to its
    // bag — proving migration 20 actually did its work on the retry, not just
    // bumped the counter.
    void v20_orphanLinkFailureRetriesCleanly() {
        const QString path = freshDbPath();
        { ShotHistoryStorage s; initAndClose(path, s); }

        // Seed a bag and a pre-bag orphan shot (bag_id NULL) that identity-
        // matches it, then rewind to v19 so migrations 20+21 re-run next init.
        qint64 bagId = -1, shotId = -1;
        withRawDb(path, "v20_seed", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO coffee_bags (roaster_name, coffee_name, in_inventory) "
                           "VALUES ('Onyx', 'Geometry', 1)"));
            bagId = q.lastInsertId().toLongLong();
            QVERIFY(q.exec("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, "
                           "bean_brand, bean_type) "
                           "VALUES ('orphan-1', 1000, 'P', 30.0, 'Onyx', 'Geometry')"));
            shotId = q.lastInsertId().toLongLong();
            restoreLegacyGrinderColumns(db);  // migration 22 re-runs in the chain
            q.exec("DELETE FROM schema_version");
            q.exec("INSERT INTO schema_version (version) VALUES (19)");
        });
        QVERIFY(bagId > 0 && shotId > 0);

        // First launch: orphan-link fails -> version NOT bumped, shot stays orphan.
        ShotHistoryStorage::s_faultInjectMigration = 20;
        initExpectingMigrationWarning(path, "migration 20 orphan-link failed");

        withRawDb(path, "v20_after_fail", [&](QSqlDatabase& db) {
            QCOMPARE(getSchemaVersion(db), 19);  // gated: stayed put for a retry
            QSqlQuery q(db);
            q.exec("SELECT bag_id FROM shots WHERE uuid = 'orphan-1'");
            QVERIFY(q.next());
            QVERIFY2(q.value(0).isNull(), "orphan shot must remain unlinked after the failed pass");
        });

        // Second launch: fault auto-cleared -> full chain runs, shot is linked.
        { ShotHistoryStorage s; initAndClose(path, s); }

        withRawDb(path, "v20_after_retry", [&](QSqlDatabase& db) {
            QCOMPARE(getSchemaVersion(db), 23);
            // The retry ran the WHOLE deferred chain, not just migration 20:
            // migration 21's rename landed too (post-condition column present).
            QVERIFY(hasColumn(db, "coffee_bags", "yield_override_g"));
            QSqlQuery q(db);
            q.exec("SELECT bag_id FROM shots WHERE uuid = 'orphan-1'");
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toLongLong(), bagId);
        });
    }

    // Migration 21 gate + retry. The yield column rename is gated on its
    // post-condition (the new column exists). A transient failure of the
    // RENAME (modelled with the seam) must leave the version at 20 with the
    // OLD column name intact, so the next launch retries. Second init renames
    // for real, preserves the stored value, and reaches v21.
    void v21_renameFailureRetriesCleanly() {
        const QString path = freshDbPath();
        { ShotHistoryStorage s; initAndClose(path, s); }

        // Force the pre-rename schema: new name -> old name, a stored value,
        // version 20.
        withRawDb(path, "v21_fail_setup", [](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("ALTER TABLE coffee_bags RENAME COLUMN yield_override_g TO yield_target_g"));
            QVERIFY(q.exec("INSERT INTO coffee_bags (roaster_name, coffee_name, yield_target_g, in_inventory) "
                           "VALUES ('Onyx', 'Geometry', 42.0, 1)"));
            restoreLegacyGrinderColumns(db);  // migration 22 re-runs in the chain
            q.exec("DELETE FROM schema_version");
            q.exec("INSERT INTO schema_version (version) VALUES (20)");
        });

        // First launch: rename "fails" -> version NOT bumped, old column kept.
        ShotHistoryStorage::s_faultInjectMigration = 21;
        initExpectingMigrationWarning(path, "migration 21 column rename failed");

        withRawDb(path, "v21_after_fail", [](QSqlDatabase& db) {
            QCOMPARE(getSchemaVersion(db), 20);  // gated: stayed put for a retry
            QVERIFY(hasColumn(db, "coffee_bags", "yield_target_g"));
            QVERIFY(!hasColumn(db, "coffee_bags", "yield_override_g"));
        });

        // Second launch: fault auto-cleared -> rename succeeds, value survives.
        { ShotHistoryStorage s; initAndClose(path, s); }

        withRawDb(path, "v21_after_retry", [](QSqlDatabase& db) {
            QCOMPARE(getSchemaVersion(db), 23);
            QVERIFY(hasColumn(db, "coffee_bags", "yield_override_g"));
            QVERIFY(!hasColumn(db, "coffee_bags", "yield_target_g"));
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT yield_override_g FROM coffee_bags WHERE coffee_name = 'Geometry'"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toDouble(), 42.0);
        });
    }

    // Migration 21 guard edge: coffee_bags has NEITHER yield_target_g (nothing
    // to rename) NOR yield_override_g (post-condition unmet). The guard must
    // refuse to bump rather than recording v21 over a table that lacks the
    // column production code reads. Contrived (a fresh DB always has the
    // column), but it pins the both-absent branch of the post-condition check.
    void v21_neitherYieldColumnLeavesVersionUnbumped() {
        const QString path = freshDbPath();
        { ShotHistoryStorage s; initAndClose(path, s); }

        // Drop the new column without adding the old one, then rewind to v20.
        withRawDb(path, "v21_neither_setup", [](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("ALTER TABLE coffee_bags DROP COLUMN yield_override_g"));
            q.exec("DELETE FROM schema_version");
            q.exec("INSERT INTO schema_version (version) VALUES (20)");
        });

        // Neither column present -> rename skipped, post-condition unmet,
        // version held at 20 with the "column rename failed" warning.
        initExpectingMigrationWarning(path, "migration 21 column rename failed");

        withRawDb(path, "v21_neither_verify", [](QSqlDatabase& db) {
            QCOMPARE(getSchemaVersion(db), 20);
            QVERIFY(!hasColumn(db, "coffee_bags", "yield_override_g"));
            QVERIFY(!hasColumn(db, "coffee_bags", "yield_target_g"));
        });
    }

    // ==========================================
    // Migration 22: equipment packages (add-equipment-packages)
    // ==========================================

    // The migration-22 data step is NOT idempotent (it creates packages); only
    // the version gate stops it re-running. Prove that re-initializing does not
    // duplicate the default package created from the live grinder settings — the
    // single most dangerous regression in this migration.
    void v22_reinitDoesNotDuplicatePackages() {
        const QString path = freshDbPath();
        QSettings app(QStringLiteral("DecentEspresso"), QStringLiteral("DE1Qt"));
        const QVariant pB = app.value("dye/grinderBrand"), pM = app.value("dye/grinderModel"),
                       pBu = app.value("dye/grinderBurrs"), pS = app.value("dye/grinderSetting");
        app.setValue("dye/grinderBrand", "Niche");
        app.setValue("dye/grinderModel", "Zero");
        app.setValue("dye/grinderBurrs", "63mm conical");
        app.setValue("dye/grinderSetting", "12");

        auto packageCount = [&]() {
            int n = -1;
            withRawDb(path, "v22_count", [&](QSqlDatabase& db) {
                QSqlQuery q(db);
                if (q.exec("SELECT COUNT(*) FROM equipment_packages") && q.next())
                    n = q.value(0).toInt();
            });
            return n;
        };

        { ShotHistoryStorage s; initAndClose(path, s); }
        withRawDb(path, "v22_ver", [](QSqlDatabase& db) { QCOMPARE(getSchemaVersion(db), 23); });
        QCOMPARE(packageCount(), 1);             // default package created from current settings
        { ShotHistoryStorage s; initAndClose(path, s); }
        QCOMPARE(packageCount(), 1);             // gate prevented a duplicate on re-init

        auto restore = [&](const char* k, const QVariant& v) { if (v.isValid()) app.setValue(k, v); else app.remove(k); };
        restore("dye/grinderBrand", pB); restore("dye/grinderModel", pM);
        restore("dye/grinderBurrs", pBu); restore("dye/grinderSetting", pS);
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
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("connection.*still in use"));
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
            for (int i = 0; i < 20; i++) { QCoreApplication::processEvents(); QThread::msleep(25); }
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
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("connection.*still in use"));
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
        for (int i = 0; i < 20; i++) { QCoreApplication::processEvents(); QThread::msleep(25); }
    }
};

QTEST_MAIN(tst_DbMigration)
#include "tst_dbmigration.moc"
