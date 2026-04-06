#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

#include "history/shothistorystorage.h"

// Test the ShotHistoryStorage schema creation and migration chain (v1->v11).
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

private slots:

    void initTestCase() {
        QVERIFY(m_tempDir.isValid());
    }

    // ==========================================
    // Fresh DB: full schema at v11
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
            QCOMPARE(getSchemaVersion(db), 11);
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
            QVERIFY(hasColumn(db, "shots", "grinder_brand"));
            QVERIFY(hasColumn(db, "shots", "grinder_burrs"));
            QVERIFY(hasColumn(db, "shots", "profile_kb_id"));
            QVERIFY(hasColumn(db, "shots", "channeling_detected"));
            QVERIFY(hasColumn(db, "shots", "temperature_unstable"));
            QVERIFY(hasColumn(db, "shots", "grind_issue_detected"));
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
            QVERIFY(hasIndex(db, "idx_shots_grinder"));
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
            QCOMPARE(getSchemaVersion(db), 11);
            QVERIFY(hasColumn(db, "shots", "temperature_override"));
            QVERIFY(hasColumn(db, "shots", "yield_override"));
            QVERIFY(hasColumn(db, "shots", "beverage_type"));
            QVERIFY(hasColumn(db, "shots", "grinder_brand"));
            QVERIFY(hasColumn(db, "shots", "grinder_burrs"));
            QVERIFY(hasColumn(db, "shots", "profile_kb_id"));
            QVERIFY(hasColumn(db, "shots", "channeling_detected"));
            QVERIFY(hasColumn(db, "shots", "temperature_unstable"));
            QVERIFY(hasColumn(db, "shots", "grind_issue_detected"));
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
            // Insert with all FTS-indexed fields
            q.prepare(R"(INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds,
                espresso_notes, bean_brand, bean_type, grinder_brand, grinder_model, grinder_burrs)
                VALUES (?, 1000, ?, 30.0, ?, ?, ?, ?, ?, ?))");
            q.addBindValue(QUuid::createUuid().toString());
            q.addBindValue("Blooming Espresso");
            q.addBindValue("Excellent fruity notes");
            q.addBindValue("Onyx");
            q.addBindValue("Eclipse");
            q.addBindValue("Niche");
            q.addBindValue("Zero");
            q.addBindValue("63mm conical");
            QVERIFY(q.exec());

            // The FTS triggers should auto-populate
            QSqlQuery fts(db);
            QVERIFY(fts.exec("SELECT rowid FROM shots_fts WHERE shots_fts MATCH 'Blooming'"));
            QVERIFY2(fts.next(), "FTS should find by profile_name");

            QVERIFY(fts.exec("SELECT rowid FROM shots_fts WHERE shots_fts MATCH 'Onyx'"));
            QVERIFY2(fts.next(), "FTS should find by bean_brand");

            QVERIFY(fts.exec("SELECT rowid FROM shots_fts WHERE shots_fts MATCH 'Niche'"));
            QVERIFY2(fts.next(), "FTS should find by grinder_brand");
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
    // v8 grinder columns exist with correct structure
    // ==========================================

    void grinderColumnsExist() {
        QString path = freshDbPath();
        ShotHistoryStorage storage;
        initAndClose(path, storage);

        withRawDb(path, "grinder_verify", [](QSqlDatabase& db) {
            QVERIFY(hasColumn(db, "shots", "grinder_brand"));
            QVERIFY(hasColumn(db, "shots", "grinder_burrs"));
            QVERIFY(hasColumn(db, "shots", "grinder_model"));
            QVERIFY(hasColumn(db, "shots", "grinder_setting"));

            // Insert and verify all grinder fields are queryable
            QSqlQuery q(db);
            q.exec("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, grinder_brand, grinder_model, grinder_burrs) VALUES ('grinder-test', 1000, 'Test', 30.0, 'Niche', 'Zero', '63mm conical')");
            q.exec("SELECT grinder_brand, grinder_model, grinder_burrs FROM shots WHERE uuid = 'grinder-test'");
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QString("Niche"));
            QCOMPARE(q.value(1).toString(), QString("Zero"));
            QCOMPARE(q.value(2).toString(), QString("63mm conical"));
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
            QCOMPARE(getSchemaVersion(db), 10);
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
            QCOMPARE(getSchemaVersion(db), 10);
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
            QCOMPARE(getSchemaVersion(db), 10);
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
            QCOMPARE(getSchemaVersion(db), 10);
            QSqlQuery q(db);
            q.exec("SELECT grinder_brand FROM shots WHERE uuid = 'test-null'");
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
    // Full chain v1->v9 preserves existing data
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
            QVERIFY(hasColumn(db, "shots", "grinder_brand"));
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
};

QTEST_MAIN(tst_DbMigration)
#include "tst_dbmigration.moc"
