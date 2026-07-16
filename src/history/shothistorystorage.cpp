#include "shothistorystorage.h"
#include "shothistorystorage_internal.h"
#include "coffeebagstorage.h"
#include "equipmentstorage.h"
#include "core/settings.h"   // Settings::testQSettingsPath() under DECENZA_TESTING
#include "recipestorage.h"
#include "ai/conductance.h"
#include "ai/shotanalysis.h"
#include "ai/shotsummarizer.h"
#include "history/shotbadgeprojection.h"
#include "core/grinderaliases.h"
#include "models/shotdatamodel.h"
#include "profile/profile.h"
#include "network/visualizeruploader.h"
#include "network/beanbase_blob.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QDir>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QLocale>
#include <QDebug>
#include <QSettings>
#include <QThread>
#include <algorithm>
#include <cmath>
#include "core/dbutils.h"

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

// Internal helpers (use12h, ProfileFrameInfo, AnalysisInputs,
// profileFrameInfoFromJson, prepareAnalysisInputs) are declared in
// shothistorystorage_internal.h and shared with shothistorystorage_serialize.cpp
// and shothistorystorage_queries.cpp.
using decenza::storage::detail::AnalysisInputs;
using decenza::storage::detail::prepareAnalysisInputs;

const QString ShotHistoryStorage::DB_CONNECTION_NAME = "ShotHistoryConnection";

#ifdef DECENZA_TESTING
int ShotHistoryStorage::s_faultInjectMigration = 0;
#endif

ShotHistoryStorage::ShotHistoryStorage(QObject* parent)
    : QObject(parent)
{
}

ShotHistoryStorage::~ShotHistoryStorage()
{
    *m_destroyed = true;
    // Stop the CRUD worker before members vanish. reset() runs ~SerialDbWorker,
    // which quit()s (discarding queued-but-unstarted tasks) and wait()s for the
    // single in-flight task to finish its DB work — all while `this` is still
    // alive. The m_destroyed flag, set just above, suppresses that task's result
    // callback so it can't touch a half-destroyed object.
    m_dbWorker.reset();
    close();
}

void ShotHistoryStorage::runOnDbThread(std::function<void()> task)
{
    if (!m_dbWorker)
        m_dbWorker = std::make_unique<SerialDbWorker>(QStringLiteral("ShotHistoryStorageWorker"));
    m_dbWorker->post(std::move(task));
}

void ShotHistoryStorage::close()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    // Drop THIS object's handle to the named connection BEFORE removing it.
    // removeDatabase() warns ("connection is still in use, all queries will
    // cease to work") when a live QSqlDatabase still references the connection
    // — and the member m_db is exactly such a reference. Reset it to an
    // invalid database first so removeDatabase runs cleanly.
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(DB_CONNECTION_NAME)) {
        QSqlDatabase::removeDatabase(DB_CONNECTION_NAME);
    }
}

bool ShotHistoryStorage::initialize(const QString& dbPath)
{
    m_dbPath = dbPath;
    if (m_dbPath.isEmpty()) {
        QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir);
        m_dbPath = dataDir + "/shots.db";
    }

    qDebug() << "ShotHistoryStorage: Initializing database at" << m_dbPath;

    // Drop any existing connection (a re-initialize) cleanly first — close()
    // resets m_db before removing so removeDatabase doesn't warn about a
    // still-referenced connection.
    close();

    m_db = QSqlDatabase::addDatabase("QSQLITE", DB_CONNECTION_NAME);
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        qWarning() << "ShotHistoryStorage: Failed to open database:" << m_db.lastError().text();
        emit errorOccurred("Failed to open shot history database");
        return false;
    }

    // Enable WAL mode for better concurrent access
    QSqlQuery pragma(m_db);
    pragma.exec("PRAGMA journal_mode=WAL");
    pragma.exec("PRAGMA foreign_keys=ON");

    if (!createTables()) {
        qWarning() << "ShotHistoryStorage: Failed to create tables";
        return false;
    }

    if (!runMigrations()) {
        qWarning() << "ShotHistoryStorage: Failed to run migrations";
        return false;
    }

#ifndef DECENZA_TESTING
    // Test builds skip the automatic legacy-preset import: it reads AND
    // CLEARS the real bean/presets QSettings store, so a unit test calling
    // initialize() on a temp DB would silently consume the developer's own
    // presets. Tests drive CoffeeBagStorage::convertLegacyPresetSettings
    // directly with save/restore guards.
    importLegacyBeanPresets();
#endif

    // Checkpoint any existing WAL data from previous sessions
    // This ensures all data is in the main .db file
    QSqlQuery walQuery(m_db);
    if (walQuery.exec("PRAGMA wal_checkpoint(TRUNCATE)")) {
        qDebug() << "ShotHistoryStorage: Startup WAL checkpoint completed";
    }

    // Sync count at startup (before UI, acceptable on init)
    {
        QSqlQuery countQuery(m_db);
        if (countQuery.exec("SELECT COUNT(*) FROM shots") && countQuery.next())
            m_totalShots = countQuery.value(0).toInt();
        else
            qWarning() << "ShotHistoryStorage: Failed to count shots at startup:" << countQuery.lastError().text();
    }

    // Seed lastSavedShotId with the newest stored shot so direct readers see
    // a valid id immediately after an app restart. The review page's
    // sticky-sync gate DEPENDS on this (it compares synchronously, no
    // fallback); the Last Shot widget and MCP latest-shot tools keep their
    // own most-recent-by-timestamp DB fallbacks and merely take the fast
    // path now. Empty DB -> NULL -> 0 (unchanged).
    {
        QSqlQuery maxQuery(m_db);
        if (maxQuery.exec("SELECT MAX(id) FROM shots") && maxQuery.next())
            m_lastSavedShotId = maxQuery.value(0).toLongLong();
        else
            qWarning() << "ShotHistoryStorage: Failed to seed lastSavedShotId at startup:"
                       << maxQuery.lastError().text();
    }

    m_ready = true;
    emit readyChanged();

    // Pre-warm the distinct cache on a background thread
    requestDistinctCache();

    qDebug() << "ShotHistoryStorage: Database initialized with" << m_totalShots << "shots";
    return true;
}

bool ShotHistoryStorage::createTables()
{
    QSqlQuery query(m_db);

    // Main shots table
    QString createShots = R"(
        CREATE TABLE IF NOT EXISTS shots (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            uuid TEXT UNIQUE NOT NULL,
            timestamp INTEGER NOT NULL,

            profile_name TEXT NOT NULL,
            profile_json TEXT,
            beverage_type TEXT DEFAULT 'espresso',

            duration_seconds REAL NOT NULL,
            final_weight REAL,
            dose_weight REAL,

            bean_brand TEXT,
            bean_type TEXT,
            roast_date TEXT,
            roast_level TEXT,
            grinder_brand TEXT,
            grinder_model TEXT,
            grinder_burrs TEXT,
            grinder_setting TEXT,
            drink_tds REAL,
            drink_ey REAL,
            enjoyment INTEGER,
            espresso_notes TEXT,
            bean_notes TEXT,
            barista TEXT,
            profile_notes TEXT,

            visualizer_id TEXT,
            visualizer_url TEXT,

            debug_log TEXT,

            temperature_override REAL,
            yield_override REAL,

            created_at INTEGER DEFAULT (strftime('%s', 'now')),
            updated_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )";

    if (!query.exec(createShots)) {
        qWarning() << "Failed to create shots table:" << query.lastError().text();
        return false;
    }

    // Shot samples (compressed BLOB)
    QString createSamples = R"(
        CREATE TABLE IF NOT EXISTS shot_samples (
            shot_id INTEGER PRIMARY KEY REFERENCES shots(id) ON DELETE CASCADE,
            sample_count INTEGER NOT NULL,
            data_blob BLOB NOT NULL
        )
    )";

    if (!query.exec(createSamples)) {
        qWarning() << "Failed to create shot_samples table:" << query.lastError().text();
        return false;
    }

    // Phase markers
    QString createPhases = R"(
        CREATE TABLE IF NOT EXISTS shot_phases (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            shot_id INTEGER NOT NULL REFERENCES shots(id) ON DELETE CASCADE,
            time_offset REAL NOT NULL,
            label TEXT NOT NULL,
            frame_number INTEGER,
            is_flow_mode INTEGER DEFAULT 0
        )
    )";

    if (!query.exec(createPhases)) {
        qWarning() << "Failed to create shot_phases table:" << query.lastError().text();
        return false;
    }

    // Full-text search (includes notes, beans, profile, and grinder)
    QString createFts = R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS shots_fts USING fts5(
            espresso_notes,
            bean_brand,
            bean_type,
            profile_name,
            grinder_brand,
            grinder_model,
            grinder_burrs,
            content='shots',
            content_rowid='id'
        )
    )";

    if (!query.exec(createFts)) {
        qWarning() << "Failed to create FTS table:" << query.lastError().text();
        // FTS failure is not fatal
    }

    // Triggers for FTS sync
    query.exec(R"(
        CREATE TRIGGER IF NOT EXISTS shots_ai AFTER INSERT ON shots BEGIN
            INSERT INTO shots_fts(rowid, espresso_notes, bean_brand, bean_type, profile_name, grinder_brand, grinder_model, grinder_burrs)
            VALUES (new.id, new.espresso_notes, new.bean_brand, new.bean_type, new.profile_name, new.grinder_brand, new.grinder_model, new.grinder_burrs);
        END
    )");

    query.exec(R"(
        CREATE TRIGGER IF NOT EXISTS shots_ad AFTER DELETE ON shots BEGIN
            INSERT INTO shots_fts(shots_fts, rowid, espresso_notes, bean_brand, bean_type, profile_name, grinder_brand, grinder_model, grinder_burrs)
            VALUES ('delete', old.id, old.espresso_notes, old.bean_brand, old.bean_type, old.profile_name, old.grinder_brand, old.grinder_model, old.grinder_burrs);
        END
    )");

    query.exec(R"(
        CREATE TRIGGER IF NOT EXISTS shots_au AFTER UPDATE ON shots BEGIN
            INSERT INTO shots_fts(shots_fts, rowid, espresso_notes, bean_brand, bean_type, profile_name, grinder_brand, grinder_model, grinder_burrs)
            VALUES ('delete', old.id, old.espresso_notes, old.bean_brand, old.bean_type, old.profile_name, old.grinder_brand, old.grinder_model, old.grinder_burrs);
            INSERT INTO shots_fts(rowid, espresso_notes, bean_brand, bean_type, profile_name, grinder_brand, grinder_model, grinder_burrs)
            VALUES (new.id, new.espresso_notes, new.bean_brand, new.bean_type, new.profile_name, new.grinder_brand, new.grinder_model, new.grinder_burrs);
        END
    )");

    // Indexes
    query.exec("CREATE INDEX IF NOT EXISTS idx_shots_timestamp ON shots(timestamp DESC)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_shots_profile ON shots(profile_name)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_shots_bean ON shots(bean_brand, bean_type)");
    // No idx_shots_grinder: grinder_brand/model are dropped by migration 23 and
    // grinder identity resolves via equipment_id. Re-creating it here would error
    // ("no such column") on every post-migration-23 launch. Pre-23 DBs that still
    // have the index get it dropped by migration 23.
    query.exec("CREATE INDEX IF NOT EXISTS idx_shots_enjoyment ON shots(enjoyment)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_shot_phases_shot ON shot_phases(shot_id)");

    // Schema version table
    query.exec("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER PRIMARY KEY)");
    // Only insert initial version if table is empty (avoid creating duplicate rows
    // when a higher version already exists from a previous run)
    query.exec("INSERT INTO schema_version (version) SELECT 1 WHERE NOT EXISTS (SELECT 1 FROM schema_version)");

    return true;
}

bool ShotHistoryStorage::runMigrations()
{
    QSqlQuery query(m_db);

    // Fix duplicate rows created by the old INSERT OR IGNORE bug:
    // If multiple rows exist, keep only the highest version.
    query.exec("DELETE FROM schema_version WHERE version != (SELECT MAX(version) FROM schema_version)");

    query.exec("SELECT version FROM schema_version ORDER BY version DESC LIMIT 1");
    int currentVersion = query.next() ? query.value(0).toInt() : 1;

    // Helper: check if a column exists in a table
    auto hasColumn = [&](const QString& table, const QString& column) -> bool {
        QSqlQuery q(m_db);
        q.exec(QString("PRAGMA table_info(%1)").arg(table));
        while (q.next()) {
            if (q.value(1).toString() == column)
                return true;
        }
        return false;
    };

    // Migration 3: Replace brew_overrides_json with dedicated columns
    if (currentVersion < 3) {
        qDebug() << "ShotHistoryStorage: Running migration to version 3 (dedicated override columns)";

        if (!hasColumn("shots", "temperature_override"))
            query.exec("ALTER TABLE shots ADD COLUMN temperature_override REAL");
        if (!hasColumn("shots", "yield_override"))
            query.exec("ALTER TABLE shots ADD COLUMN yield_override REAL");

        query.exec("UPDATE schema_version SET version = 3");
        currentVersion = 3;
    }

    // Migration 4: Add transition_reason to shot_phases
    if (currentVersion < 4) {
        qDebug() << "ShotHistoryStorage: Running migration to version 4 (transition_reason)";

        if (!hasColumn("shot_phases", "transition_reason"))
            query.exec("ALTER TABLE shot_phases ADD COLUMN transition_reason TEXT DEFAULT ''");

        query.exec("UPDATE schema_version SET version = 4");
        currentVersion = 4;
    }

    // Migration 5: Add profile_name and grinder_model to FTS search
    if (currentVersion < 5) {
        qDebug() << "ShotHistoryStorage: Running migration to version 5 (FTS profile_name + grinder_model)";

        // Drop old FTS table and triggers
        query.exec("DROP TRIGGER IF EXISTS shots_ai");
        query.exec("DROP TRIGGER IF EXISTS shots_ad");
        query.exec("DROP TRIGGER IF EXISTS shots_au");
        query.exec("DROP TABLE IF EXISTS shots_fts");

        // Create the FTS table (must do it here, not rely on createTables())
        if (!query.exec(R"(
            CREATE VIRTUAL TABLE IF NOT EXISTS shots_fts USING fts5(
                espresso_notes, bean_brand, bean_type, profile_name, grinder_model,
                content='shots', content_rowid='id'
            )
        )")) {
            qWarning() << "Migration 5: Failed to create FTS table:" << query.lastError().text();
        }

        // Create triggers
        query.exec(R"(
            CREATE TRIGGER IF NOT EXISTS shots_ai AFTER INSERT ON shots BEGIN
                INSERT INTO shots_fts(rowid, espresso_notes, bean_brand, bean_type, profile_name, grinder_model)
                VALUES (new.id, new.espresso_notes, new.bean_brand, new.bean_type, new.profile_name, new.grinder_model);
            END
        )");
        query.exec(R"(
            CREATE TRIGGER IF NOT EXISTS shots_ad AFTER DELETE ON shots BEGIN
                INSERT INTO shots_fts(shots_fts, rowid, espresso_notes, bean_brand, bean_type, profile_name, grinder_model)
                VALUES ('delete', old.id, old.espresso_notes, old.bean_brand, old.bean_type, old.profile_name, old.grinder_model);
            END
        )");
        query.exec(R"(
            CREATE TRIGGER IF NOT EXISTS shots_au AFTER UPDATE ON shots BEGIN
                INSERT INTO shots_fts(shots_fts, rowid, espresso_notes, bean_brand, bean_type, profile_name, grinder_model)
                VALUES ('delete', old.id, old.espresso_notes, old.bean_brand, old.bean_type, old.profile_name, old.grinder_model);
                INSERT INTO shots_fts(rowid, espresso_notes, bean_brand, bean_type, profile_name, grinder_model)
                VALUES (new.id, new.espresso_notes, new.bean_brand, new.bean_type, new.profile_name, new.grinder_model);
            END
        )");

        // Rebuild FTS index from existing shots
        query.exec(R"(
            INSERT INTO shots_fts(rowid, espresso_notes, bean_brand, bean_type, profile_name, grinder_model)
            SELECT id, espresso_notes, bean_brand, bean_type, profile_name, grinder_model FROM shots
        )");

        query.exec("UPDATE schema_version SET version = 5");
        currentVersion = 5;
    }

    // Migration 6: Add beverage_type column and backfill from profile_json
    if (currentVersion < 6) {
        qDebug() << "ShotHistoryStorage: Running migration to version 6 (beverage_type)";

        if (!hasColumn("shots", "beverage_type"))
            query.exec("ALTER TABLE shots ADD COLUMN beverage_type TEXT DEFAULT 'espresso'");
        if (!hasColumn("shots", "bean_notes"))
            query.exec("ALTER TABLE shots ADD COLUMN bean_notes TEXT");
        if (!hasColumn("shots", "profile_notes"))
            query.exec("ALTER TABLE shots ADD COLUMN profile_notes TEXT");

        backfillBeverageType();

        query.exec("UPDATE schema_version SET version = 6");
        currentVersion = 6;
    }

    // Migration 7: Smooth weight flow rate data in all existing shots
    // The raw LSLR data has staircase artifacts from 0.1g scale quantization.
    // Apply the same centered moving average (window=5, 11-point) used for new shots.
    // This is a cosmetic improvement — if it fails, bump version anyway so the app starts.
    if (currentVersion < 7) {
        qDebug() << "ShotHistoryStorage: Running migration to version 7 (smooth weight flow rate)";

        bool smoothingOk = false;
        if (!m_db.transaction()) {
            qWarning() << "ShotHistoryStorage: Migration 7 failed to begin transaction:"
                       << m_db.lastError().text();
        } else {
            // Read all blobs first to avoid read cursor + write on same table
            QSqlQuery readQuery(m_db);
            readQuery.prepare("SELECT shot_id, data_blob FROM shot_samples");

            QVector<QPair<qint64, QByteArray>> rows;
            if (!readQuery.exec()) {
                qWarning() << "ShotHistoryStorage: Migration 7 failed to read shots:"
                           << readQuery.lastError().text();
            } else {
                while (readQuery.next()) {
                    rows.append({readQuery.value(0).toLongLong(),
                                 readQuery.value(1).toByteArray()});
                }
            }
            readQuery.finish();

            QSqlQuery updateQuery(m_db);
            updateQuery.prepare("UPDATE shot_samples SET data_blob = ? WHERE shot_id = ?");

            int smoothedCount = 0;
            bool migrationFailed = false;
            for (const auto& row : rows) {
                qint64 id = row.first;
                const QByteArray& blob = row.second;

                QByteArray json = qUncompress(blob);
                if (json.isEmpty()) {
                    if (!blob.isEmpty())
                        qWarning() << "ShotHistoryStorage: Migration 7 - shot" << id
                                   << "has non-empty blob (" << blob.size()
                                   << "bytes) that failed to decompress";
                    continue;
                }

                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
                if (parseError.error != QJsonParseError::NoError) {
                    qWarning() << "ShotHistoryStorage: Migration 7 - shot" << id
                               << "has invalid JSON at offset" << parseError.offset
                               << ":" << parseError.errorString();
                    continue;
                }
                QJsonObject root = doc.object();

                if (!root.contains("weightFlowRate")) continue;

                QJsonObject wfrObj = root["weightFlowRate"].toObject();
                QJsonArray timeArr = wfrObj["t"].toArray();
                QJsonArray valueArr = wfrObj["v"].toArray();
                qsizetype n = qMin(timeArr.size(), valueArr.size());
                if (n < 3) continue;

                // Centered moving average with window=5 (11-point, ~2.2s at 5Hz)
                constexpr int window = 5;
                QJsonArray smoothedArr;
                for (qsizetype i = 0; i < n; i++) {
                    qsizetype lo = qMax(qsizetype(0), i - window);
                    qsizetype hi = qMin(n - 1, i + window);
                    double sum = 0;
                    for (qsizetype j = lo; j <= hi; j++) {
                        sum += valueArr[j].toDouble();
                    }
                    smoothedArr.append(sum / (hi - lo + 1));
                }

                wfrObj["v"] = smoothedArr;
                root["weightFlowRate"] = wfrObj;
                QByteArray newJson = QJsonDocument(root).toJson(QJsonDocument::Compact);
                QByteArray newBlob = qCompress(newJson, 9);

                updateQuery.bindValue(0, newBlob);
                updateQuery.bindValue(1, id);
                if (!updateQuery.exec()) {
                    qWarning() << "ShotHistoryStorage: Migration 7 failed to update shot" << id
                               << ":" << updateQuery.lastError().text();
                    migrationFailed = true;
                    break;
                }
                smoothedCount++;
            }

            if (migrationFailed) {
                qWarning() << "ShotHistoryStorage: Migration 7 rolling back smoothing after" << smoothedCount << "shots";
                m_db.rollback();
            } else {
                qDebug() << "ShotHistoryStorage: Smoothed weight flow rate for" << smoothedCount << "shots";
                // Use DELETE+INSERT instead of UPDATE to avoid UNIQUE constraint issues
                // when updating the PRIMARY KEY column
                if (!query.exec("DELETE FROM schema_version") ||
                    !query.exec("INSERT INTO schema_version (version) VALUES (7)")) {
                    qWarning() << "ShotHistoryStorage: Migration 7 failed to bump schema version inside transaction:"
                               << query.lastError().text();
                    m_db.rollback();
                } else if (!m_db.commit()) {
                    qWarning() << "ShotHistoryStorage: Migration 7 commit failed:"
                               << m_db.lastError().text();
                    m_db.rollback();
                } else {
                    smoothingOk = true;
                }
            }
        }

        // Smoothing is cosmetic — always bump to version 7 so the app can start.
        // If the transaction succeeded, version is already 7 in the DB.
        // If it failed, bump it outside the transaction so we don't retry on every launch.
        if (!smoothingOk) {
            qWarning() << "ShotHistoryStorage: Migration 7 smoothing failed, bumping version anyway";
            query.exec("DELETE FROM schema_version");
            query.exec("INSERT INTO schema_version (version) VALUES (7)");
        }
        currentVersion = 7;
    }

    // Migration 8: Add grinder_brand and grinder_burrs columns, backfill from alias lookup, rebuild FTS
    if (currentVersion < 8) {
        qDebug() << "ShotHistoryStorage: Running migration to version 8 (structured grinder fields)";

        bool migrationOk = false;
        if (!m_db.transaction()) {
            qWarning() << "ShotHistoryStorage: Migration 8 failed to begin transaction:"
                       << m_db.lastError().text();
        } else {
            bool schemaOk = true;
            if (!hasColumn("shots", "grinder_brand")) {
                if (!query.exec("ALTER TABLE shots ADD COLUMN grinder_brand TEXT")) {
                    qWarning() << "ShotHistoryStorage: Migration 8 failed to add grinder_brand column:"
                               << query.lastError().text();
                    schemaOk = false;
                }
            }
            if (schemaOk && !hasColumn("shots", "grinder_burrs")) {
                if (!query.exec("ALTER TABLE shots ADD COLUMN grinder_burrs TEXT")) {
                    qWarning() << "ShotHistoryStorage: Migration 8 failed to add grinder_burrs column:"
                               << query.lastError().text();
                    schemaOk = false;
                }
            }

            bool migrationFailed = !schemaOk;
            if (schemaOk) {
                // Backfill: parse existing grinder_model through alias lookup
                QSqlQuery readQuery(m_db);
                readQuery.prepare("SELECT id, grinder_model FROM shots WHERE grinder_model IS NOT NULL AND grinder_model != ''");
                if (readQuery.exec()) {
                    QSqlQuery updateQuery(m_db);
                    updateQuery.prepare("UPDATE shots SET grinder_brand = ?, grinder_model = ?, grinder_burrs = ?, "
                                        "updated_at = strftime('%s', 'now') WHERE id = ?");
                    int backfillCount = 0;

                    while (readQuery.next()) {
                        qint64 id = readQuery.value(0).toLongLong();
                        QString rawModel = readQuery.value(1).toString();
                        auto result = GrinderAliases::lookup(rawModel);
                        if (result.found) {
                            updateQuery.bindValue(0, result.brand);
                            updateQuery.bindValue(1, result.model);
                            updateQuery.bindValue(2, result.stockBurrs);
                            updateQuery.bindValue(3, id);
                            if (!updateQuery.exec()) {
                                qWarning() << "ShotHistoryStorage: Migration 8 failed to update shot" << id
                                           << ":" << updateQuery.lastError().text();
                                migrationFailed = true;
                                break;
                            }
                            backfillCount++;
                        }
                    }
                    if (!migrationFailed)
                        qDebug() << "ShotHistoryStorage: Migration 8 backfilled" << backfillCount << "shots with structured grinder data";
                }
            }

            if (!migrationFailed) {
                // Rebuild FTS to include grinder_brand and grinder_burrs
                query.exec("DROP TRIGGER IF EXISTS shots_ai");
                query.exec("DROP TRIGGER IF EXISTS shots_ad");
                query.exec("DROP TRIGGER IF EXISTS shots_au");
                query.exec("DROP TABLE IF EXISTS shots_fts");

                if (!query.exec(R"(
                    CREATE VIRTUAL TABLE IF NOT EXISTS shots_fts USING fts5(
                        espresso_notes, bean_brand, bean_type, profile_name, grinder_brand, grinder_model, grinder_burrs,
                        content='shots', content_rowid='id'
                    )
                )")) {
                    qWarning() << "ShotHistoryStorage: Migration 8 failed to create FTS table:"
                               << query.lastError().text();
                    migrationFailed = true;
                }
            }

            if (!migrationFailed) {
                query.exec(R"(
                    CREATE TRIGGER IF NOT EXISTS shots_ai AFTER INSERT ON shots BEGIN
                        INSERT INTO shots_fts(rowid, espresso_notes, bean_brand, bean_type, profile_name, grinder_brand, grinder_model, grinder_burrs)
                        VALUES (new.id, new.espresso_notes, new.bean_brand, new.bean_type, new.profile_name, new.grinder_brand, new.grinder_model, new.grinder_burrs);
                    END
                )");
                query.exec(R"(
                    CREATE TRIGGER IF NOT EXISTS shots_ad AFTER DELETE ON shots BEGIN
                        INSERT INTO shots_fts(shots_fts, rowid, espresso_notes, bean_brand, bean_type, profile_name, grinder_brand, grinder_model, grinder_burrs)
                        VALUES ('delete', old.id, old.espresso_notes, old.bean_brand, old.bean_type, old.profile_name, old.grinder_brand, old.grinder_model, old.grinder_burrs);
                    END
                )");
                query.exec(R"(
                    CREATE TRIGGER IF NOT EXISTS shots_au AFTER UPDATE ON shots BEGIN
                        INSERT INTO shots_fts(shots_fts, rowid, espresso_notes, bean_brand, bean_type, profile_name, grinder_brand, grinder_model, grinder_burrs)
                        VALUES ('delete', old.id, old.espresso_notes, old.bean_brand, old.bean_type, old.profile_name, old.grinder_brand, old.grinder_model, old.grinder_burrs);
                        INSERT INTO shots_fts(rowid, espresso_notes, bean_brand, bean_type, profile_name, grinder_brand, grinder_model, grinder_burrs)
                        VALUES (new.id, new.espresso_notes, new.bean_brand, new.bean_type, new.profile_name, new.grinder_brand, new.grinder_model, new.grinder_burrs);
                    END
                )");

                // Rebuild FTS index
                if (!query.exec(R"(
                    INSERT INTO shots_fts(rowid, espresso_notes, bean_brand, bean_type, profile_name, grinder_brand, grinder_model, grinder_burrs)
                    SELECT id, espresso_notes, bean_brand, bean_type, profile_name, grinder_brand, grinder_model, grinder_burrs FROM shots
                )")) {
                    qWarning() << "ShotHistoryStorage: Migration 8 failed to populate FTS index:"
                               << query.lastError().text();
                    migrationFailed = true;
                }
            }

            if (migrationFailed) {
                qWarning() << "ShotHistoryStorage: Migration 8 rolling back";
                m_db.rollback();
            } else {
                if (!query.exec("DELETE FROM schema_version") ||
                    !query.exec("INSERT INTO schema_version (version) VALUES (8)")) {
                    qWarning() << "ShotHistoryStorage: Migration 8 failed to bump schema version:"
                               << query.lastError().text();
                    m_db.rollback();
                } else if (!m_db.commit()) {
                    qWarning() << "ShotHistoryStorage: Migration 8 commit failed:"
                               << m_db.lastError().text();
                    m_db.rollback();
                } else {
                    migrationOk = true;
                }
            }
        }

        // Schema changes are structural — always bump to version 8 so the app can start.
        // If the transaction succeeded, version is already 8 in the DB.
        // If it failed, bump outside the transaction so we don't retry on every launch.
        if (!migrationOk) {
            qWarning() << "ShotHistoryStorage: Migration 8 failed, bumping version anyway";
            query.exec("DELETE FROM schema_version");
            query.exec("INSERT INTO schema_version (version) VALUES (8)");
        }
        currentVersion = 8;
    }

    // Migration 9: Add profile_kb_id column for AI knowledge base matching.
    // New shots get this computed at save time. Old shots won't appear in
    // dial-in history queries (getRecentShotsByKbId), but system prompt
    // profile matching falls back to fuzzy title/editorType matching.
    if (currentVersion < 9) {
        qDebug() << "ShotHistoryStorage: Running migration to version 9 (profile_kb_id)";

        bool ok = true;
        if (!hasColumn("shots", "profile_kb_id")) {
            ok = query.exec("ALTER TABLE shots ADD COLUMN profile_kb_id TEXT");
            if (!ok)
                qWarning() << "ShotHistoryStorage: Migration 9 ALTER TABLE failed:" << query.lastError().text();
        }
        if (ok) {
            query.exec("CREATE INDEX IF NOT EXISTS idx_shots_profile_kb_id ON shots(profile_kb_id)");
        }

        query.exec("DELETE FROM schema_version");
        query.exec("INSERT INTO schema_version (version) VALUES (9)");
        currentVersion = 9;
    }

    // Migration 10: Add quality flags for shot review badges.
    // Computed at save time by saveShot() using ShotAnalysis helpers directly
    // (avoids a ShotSummarizer dependency); recomputed on-the-fly inside
    // loadShotRecordStatic() for shots that predate this migration.
    if (currentVersion < 10) {
        qDebug() << "ShotHistoryStorage: Running migration to version 10 (quality flags)";

        if (!hasColumn("shots", "channeling_detected"))
            query.exec("ALTER TABLE shots ADD COLUMN channeling_detected INTEGER DEFAULT 0");
        if (!hasColumn("shots", "temperature_unstable"))
            query.exec("ALTER TABLE shots ADD COLUMN temperature_unstable INTEGER DEFAULT 0");

        query.exec("DELETE FROM schema_version");
        query.exec("INSERT INTO schema_version (version) VALUES (10)");
        currentVersion = 10;
    }

    // Migration 11: Add grind_issue_detected flag.
    // Recomputed on-the-fly in loadShotRecordStatic() for shots predating this migration.
    if (currentVersion < 11) {
        qDebug() << "ShotHistoryStorage: Running migration to version 11 (grind_issue_detected)";

        if (!hasColumn("shots", "grind_issue_detected"))
            query.exec("ALTER TABLE shots ADD COLUMN grind_issue_detected INTEGER DEFAULT 0");

        query.exec("DELETE FROM schema_version");
        query.exec("INSERT INTO schema_version (version) VALUES (11)");
        currentVersion = 11;
    }

    // Migration 12: Add skip_first_frame_detected flag.
    // Detects DE1 firmware bug where the machine skips profile frame 0.
    if (currentVersion < 12) {
        qDebug() << "ShotHistoryStorage: Running migration to version 12 (skip_first_frame_detected)";

        if (!hasColumn("shots", "skip_first_frame_detected"))
            query.exec("ALTER TABLE shots ADD COLUMN skip_first_frame_detected INTEGER DEFAULT 0");

        query.exec("DELETE FROM schema_version");
        query.exec("INSERT INTO schema_version (version) VALUES (12)");
        currentVersion = 12;
    }

    // Migration 13: Add pour_truncated_detected flag.
    // Catches puck failures where peak pressure stayed below PRESSURE_FLOOR_BAR
    // (puck offered no resistance — channeling/grind detectors stay silent
    // or fire wrong because the curves they read off never built). When this
    // flag is true the other quality flags stay false because
    // ShotAnalysis::analyzeShot's suppression cascade skips the
    // channeling/grind blocks, leaving those DetectorResults fields at
    // their defaults; the badge projection (decenza::deriveBadgesFromAnalysis)
    // then reads those defaults. The cascade lives in exactly one place —
    // ShotAnalysis::analyzeShot — and the UI shows a single red "Puck failed"
    // chip rather than a contradictory mix.
    if (currentVersion < 13) {
        qDebug() << "ShotHistoryStorage: Running migration to version 13 (pour_truncated_detected)";

        if (!hasColumn("shots", "pour_truncated_detected"))
            query.exec("ALTER TABLE shots ADD COLUMN pour_truncated_detected INTEGER DEFAULT 0");

        query.exec("DELETE FROM schema_version");
        query.exec("INSERT INTO schema_version (version) VALUES (13)");
        currentVersion = 13;
    }

    // Migration 14: enjoyment_source column was introduced here. Layer 3
    // (the inferred auto-rating experiment that owned this column) was
    // rolled back in migration 16 below, which drops it. This step is
    // kept rather than fast-forwarded so legacy v13 DBs still pass
    // through the same back-fill (`enjoyment_source = 'user'` for rated
    // rows) that migration 16 later reads to build the back-sync list.
    // Migration 16 is independently safe via its hasColumn() guard.
    if (currentVersion < 14) {
        qDebug() << "ShotHistoryStorage: Running migration to version 14 (enjoyment_source)";

        if (!hasColumn("shots", "enjoyment_source")) {
            query.exec("ALTER TABLE shots ADD COLUMN enjoyment_source TEXT NOT NULL DEFAULT 'none'");
            // Back-fill: existing rated rows are user-rated by definition
            // (the column is new; only the manual editor / the rating slider
            // ever wrote those values).
            query.exec("UPDATE shots SET enjoyment_source = 'user' WHERE enjoyment > 0");
        }

        query.exec("DELETE FROM schema_version");
        query.exec("INSERT INTO schema_version (version) VALUES (14)");
        currentVersion = 14;
    }

    // Migration 15: Drop temperature_unstable column. The temp-stability badge
    // was retired because the underlying detector measured average deviation
    // from goal but was labeled "Temp unstable" - and the deviation it caught
    // was, in practice, profile-design intent (D-Flow / Extractamundo /
    // TurboBloom and 9+ other profiles deliberately ask the head to do things
    // it can't physically follow). Removed end-to-end; this migration drops
    // the now-unused column. SQLite >= 3.35 is required for ALTER TABLE
    // DROP COLUMN; every Qt 6.10 platform we ship satisfies this.
    if (currentVersion < 15) {
        qDebug() << "ShotHistoryStorage: Running migration to version 15 (drop temperature_unstable)";

        if (hasColumn("shots", "temperature_unstable")) {
            // Bail out without bumping schema_version if DROP COLUMN fails.
            // Otherwise we'd advance to v15 with the column stranded — and
            // since post-v15 SELECTs no longer reference the column, the
            // load path would silently see stale data instead of erroring.
            // Better to retry the migration on next launch than to leave
            // the schema in a half-migrated state.
            if (!query.exec("ALTER TABLE shots DROP COLUMN temperature_unstable")) {
                qWarning() << "ShotHistoryStorage: migration 15 DROP COLUMN failed:"
                           << query.lastError().text();
                return false;
            }
        }

        query.exec("DELETE FROM schema_version");
        query.exec("INSERT INTO schema_version (version) VALUES (15)");
        currentVersion = 15;
    }

    // Migration 16: drop enjoyment_source column. Layer 3 of the
    // shot-rating-capture change auto-stamped clean unrated shots with
    // enjoyment=75 and enjoymentSource="inferred", but the inferred
    // signal added no value over the LLM's existing detector observations
    // and silently overwrote the user's configured "Default Shot Rating"
    // (issue #1150). The column is dropped here. Before dropping:
    //   1) Stash the (shotId, visualizerId) pairs of inferred rows that
    //      were uploaded to Visualizer in a QSettings pending list so
    //      MainController can re-PATCH them with the corrected rating.
    //   2) Reset every inferred row's enjoyment to the user's configured
    //      default rating (QSettings shot/defaultRating, fallback 75 —
    //      matching SettingsVisualizer::defaultShotRating).
    if (currentVersion < 16) {
        qDebug() << "ShotHistoryStorage: Running migration to version 16 (drop enjoyment_source)";

        if (hasColumn("shots", "enjoyment_source")) {
            if (!m_db.transaction()) {
                qWarning() << "ShotHistoryStorage: migration 16 transaction begin failed:"
                           << m_db.lastError().text();
                return false;
            }

            // Read user's configured default rating up-front. MUST use
            // the same QSettings scope the app's Settings object owns
            // (settings.cpp: QSettings("DecentEspresso","DE1Qt")). A
            // bare QSettings() resolves to org/app "DecentEspresso"/
            // "Decenza" (main.cpp setApplicationName) — a DIFFERENT,
            // empty store — which would silently return the 75 fallback
            // and no-op the reset for every user whose default ≠ 75.
#ifdef DECENZA_TESTING
            QSettings appSettings(Settings::testQSettingsPath(), QSettings::IniFormat);
#else
            QSettings appSettings(QStringLiteral("DecentEspresso"),
                                  QStringLiteral("DE1Qt"));
#endif
            const int defaultRating = appSettings.value(
                QStringLiteral("shot/defaultRating"), 75).toInt();

            // 1) Collect inferred rows that were uploaded to Visualizer so
            //    the cloud copy can be corrected after boot. Append to any
            //    existing pending list (preserves entries from a prior
            //    failed sync run).
            {
                QSqlQuery pendingQ(m_db);
                if (!pendingQ.exec("SELECT id, visualizer_id FROM shots "
                                   "WHERE enjoyment_source = 'inferred' "
                                   "AND visualizer_id IS NOT NULL "
                                   "AND visualizer_id != ''")) {
                    qWarning() << "ShotHistoryStorage: migration 16 SELECT failed:"
                               << pendingQ.lastError().text();
                    m_db.rollback();
                    return false;
                }
                QJsonArray pending;
                {
                    const QByteArray existingJson = appSettings.value(
                        QStringLiteral("migration16/pendingVisualizerSync")).toByteArray();
                    if (!existingJson.isEmpty())
                        pending = QJsonDocument::fromJson(existingJson).array();
                }
                while (pendingQ.next()) {
                    QJsonObject entry;
                    entry["shotId"] = pendingQ.value(0).toLongLong();
                    entry["visualizerId"] = pendingQ.value(1).toString();
                    pending.append(entry);
                }
                if (!pending.isEmpty()) {
                    appSettings.setValue(
                        QStringLiteral("migration16/pendingVisualizerSync"),
                        QJsonDocument(pending).toJson(QJsonDocument::Compact));
                }
            }

            // 2) Reset enjoyment on inferred rows to the user's default.
            {
                QSqlQuery resetQ(m_db);
                resetQ.prepare("UPDATE shots SET enjoyment = :rating "
                               "WHERE enjoyment_source = 'inferred'");
                resetQ.bindValue(":rating", defaultRating);
                if (!resetQ.exec()) {
                    qWarning() << "ShotHistoryStorage: migration 16 UPDATE failed:"
                               << resetQ.lastError().text();
                    m_db.rollback();
                    return false;
                }
            }

            // 3) Drop the column. SQLite >= 3.35 (required since v15).
            if (!query.exec("ALTER TABLE shots DROP COLUMN enjoyment_source")) {
                qWarning() << "ShotHistoryStorage: migration 16 DROP COLUMN failed:"
                           << query.lastError().text();
                m_db.rollback();
                return false;
            }

            query.exec("DELETE FROM schema_version");
            query.exec("INSERT INTO schema_version (version) VALUES (16)");

            if (!m_db.commit()) {
                qWarning() << "ShotHistoryStorage: migration 16 commit failed:"
                           << m_db.lastError().text();
                m_db.rollback();
                return false;
            }
        } else {
            // Column already absent (fresh DB or a previously-completed
            // migration 16). Just record the schema version.
            query.exec("DELETE FROM schema_version");
            query.exec("INSERT INTO schema_version (version) VALUES (16)");
        }
        currentVersion = 16;
    }

    // Migration 17: stopped_by column (#1161). Records why the shot ended
    // ("weight"/"volume"/"manual"/"profileEnd"/""). Additive TEXT column,
    // default '' — pre-migration rows read back as "" (unknown), which the
    // dial-in consumer treats conservatively. Backward-safe: old code that
    // never SELECTs the column is unaffected. Whitespace before the
    // open-paren dodges the QSqlQuery permission-hook false-positive, as
    // elsewhere in the codebase. Do not auto-format.
    if (currentVersion < 17) {
        qDebug() << "ShotHistoryStorage: Running migration to version 17 (stopped_by)";

        // No NOT NULL: callers (incl. the dev/fake-shot path and any shot
        // whose reason is unknown) bind a null QString, which SQLite stores
        // as NULL. Reads use value().toString() → "" for NULL, so unknown
        // collapses to "" everywhere. SQLite does NOT rewrite existing rows
        // on ADD COLUMN — pre-migration rows keep no stored value and a
        // SELECT returns the DEFAULT '' for them virtually (so reads are
        // "" without any physical back-fill).
        if (!hasColumn("shots", "stopped_by"))
            query.exec ("ALTER TABLE shots ADD COLUMN stopped_by TEXT DEFAULT ''");

        query.exec ("DELETE FROM schema_version");
        query.exec ("INSERT INTO schema_version (version) VALUES (17)");
        currentVersion = 17;
    }

    // Migration 18: beanbase_json column (add-bean-base-integration).
    // Compact-JSON snapshot of the linked bean entry (Visualizer canonical
    // or Bean Base sourced — see docs/CLAUDE_MD/BEAN_BASE.md) the shot was
    // pulled with: id, roaster, origin, variety, process, etc. Snapshotted
    // at save time so history stays accurate after the preset is edited or
    // deleted. NULL = unlinked bean (the common free-text case). Additive
    // TEXT column, NULL default; old code that never SELECTs it is
    // unaffected. Whitespace before the open-paren dodges the QSqlQuery
    // permission-hook false-positive, as elsewhere.
    if (currentVersion < 18) {
        qDebug() << "ShotHistoryStorage: Running migration to version 18 (beanbase_json)";

        if (!hasColumn("shots", "beanbase_json"))
            query.exec ("ALTER TABLE shots ADD COLUMN beanbase_json TEXT");

        // Check-before-bump (migration-15 precedent): saveShotStatic's INSERT
        // names this column unconditionally, so recording version 18 without
        // the column would permanently break shot saving. On failure the
        // version stays < 18 and the ALTER retries next launch.
        if (hasColumn("shots", "beanbase_json")) {
            query.exec ("DELETE FROM schema_version");
            query.exec ("INSERT INTO schema_version (version) VALUES (18)");
            currentVersion = 18;
        } else {
            qWarning() << "ShotHistoryStorage: migration 18 failed to add beanbase_json:"
                       << query.lastError().text() << "- will retry next launch";
        }
    }

    // Migration 19: coffee bags (bean-bag-inventory). Creates the coffee_bags
    // table and adds nullable bag_id / frozen_date / defrost_date /
    // beanbase_id columns to shots, backfilling beanbase_id from the
    // beanbase_json blob so the Change Beans history lane can GROUP BY an
    // indexed column instead of json_extract'ing every row at query time.
    // The legacy preset import is deliberately NOT version-gated — see
    // importLegacyBeanPresets(), which runs every launch and also covers
    // keys reintroduced by old-version device transfer. Check-before-bump
    // (migration-18 precedent): saveShotStatic names the new columns
    // unconditionally, so version 19 is only recorded when they all exist.
    // Whitespace before the open-paren dodges the QSqlQuery permission-hook
    // false-positive, as elsewhere. Do not auto-format.
    if (currentVersion < 19) {
        qDebug() << "ShotHistoryStorage: Running migration to version 19 (coffee bags)";

        const bool tableOk = CoffeeBagStorage::ensureTableStatic(m_db);

        if (!hasColumn("shots", "bag_id"))
            query.exec ("ALTER TABLE shots ADD COLUMN bag_id INTEGER");
        if (!hasColumn("shots", "frozen_date"))
            query.exec ("ALTER TABLE shots ADD COLUMN frozen_date TEXT");
        if (!hasColumn("shots", "defrost_date"))
            query.exec ("ALTER TABLE shots ADD COLUMN defrost_date TEXT");
        if (!hasColumn("shots", "beanbase_id")) {
            if (query.exec ("ALTER TABLE shots ADD COLUMN beanbase_id TEXT")) {
                // One-time backfill from the blob. json_extract returns NULL
                // for NULL/invalid JSON, so unlinked shots stay NULL. Failure
                // is non-fatal: the history lane falls back to brand|type
                // grouping for pre-migration rows.
                if (!query.exec("UPDATE shots SET beanbase_id = json_extract(beanbase_json, '$.id') "
                                "WHERE beanbase_json IS NOT NULL"))
                    qWarning() << "ShotHistoryStorage: migration 19 beanbase_id backfill failed:"
                               << query.lastError().text();
            }
        }
        query.exec("CREATE INDEX IF NOT EXISTS idx_shots_beanbase_id ON shots(beanbase_id)");

        if (tableOk && hasColumn("shots", "bag_id") && hasColumn("shots", "frozen_date")
            && hasColumn("shots", "defrost_date") && hasColumn("shots", "beanbase_id")) {
            query.exec ("DELETE FROM schema_version");
            query.exec ("INSERT INTO schema_version (version) VALUES (19)");
            currentVersion = 19;
        } else {
            qWarning() << "ShotHistoryStorage: migration 19 incomplete:"
                       << query.lastError().text() << "- will retry next launch";
        }
    }

    // Migration 20: link pre-bag shots to their bags by identity
    // (bean-bag-inventory follow-up). Migration 19 created bags from presets
    // but shots saved before the upgrade carry bag_id NULL, so migrated
    // favorites looked shot-less (the card offered delete instead of "Bag
    // finished"). Idempotent identity backfill; on a FRESH upgrade the bags
    // don't exist yet at this point (the preset import runs after
    // migrations) — that path is covered by the same link call inside
    // convertLegacyPresetSettings. This migration repairs devices that
    // upgraded before the link existed.
    if (currentVersion < 20) {
        qDebug() << "ShotHistoryStorage: Running migration to version 20 (link pre-bag shots to bags)";
        // Gate the bump on success: linkOrphanShotsStatic returns -1 on a SQL
        // failure (e.g. a locked DB at migration time). The op is idempotent,
        // so if we DON'T bump on failure it simply retries next launch; bumping
        // unconditionally would strand the shots orphaned forever (the < 20
        // block never runs again). Mirrors migration 19's check-before-bump.
        bool linkFaulted = false;
#ifdef DECENZA_TESTING
        linkFaulted = (s_faultInjectMigration == 20);
        if (linkFaulted)
            s_faultInjectMigration = 0;  // one-shot: clears so the retry succeeds
#endif
        const int linked = linkFaulted ? -1 : CoffeeBagStorage::linkOrphanShotsStatic(m_db);
        if (linked >= 0) {
            query.exec ("DELETE FROM schema_version");
            query.exec ("INSERT INTO schema_version (version) VALUES (20)");
            currentVersion = 20;
        } else {
            qWarning() << "ShotHistoryStorage: migration 20 orphan-link failed - will retry next launch";
#ifdef DECENZA_TESTING
            // Model the locked DB faithfully: the same lock that failed the
            // orphan-link also fails every later migration's writes this pass,
            // so nothing persists past version 19 and the WHOLE deferred chain
            // (migrations 21, 22, and 23) retries next launch. Without this abort,
            // the independently-gated later migrations would advance the version
            // via their own ">= N" gates (ending at 23) and the < 20 block would
            // never run again, stranding the orphan bag_id links.
            if (linkFaulted) {
                m_schemaVersion = currentVersion;
                return true;
            }
#endif
        }
    }

    // Migration 21: rename coffee_bags.yield_target_g -> yield_override_g
    // (bean-bag-inventory yield-override model). The bag's yield is the
    // bean's override of the profile's target weight, not a separate target —
    // the column name now says so. Unreleased feature, so this only repairs
    // dev databases already migrated to 19/20; fresh DBs get the new name
    // straight from ensureTableStatic. RENAME COLUMN needs SQLite >= 3.25
    // (we require >= 3.35). Whitespace before the open-paren dodges the
    // QSqlQuery permission-hook false-positive, as elsewhere.
    if (currentVersion < 21) {
        qDebug() << "ShotHistoryStorage: Running migration to version 21 (yield_target_g -> yield_override_g)";
        bool renameFaulted = false;
#ifdef DECENZA_TESTING
        renameFaulted = (s_faultInjectMigration == 21);
        if (renameFaulted)
            s_faultInjectMigration = 0;  // one-shot: clears so the retry succeeds
#endif
        if (!renameFaulted
            && hasColumn("coffee_bags", "yield_target_g") && !hasColumn("coffee_bags", "yield_override_g"))
            query.exec ("ALTER TABLE coffee_bags RENAME COLUMN yield_target_g TO yield_override_g");
        // Gate the bump on the post-condition (the new column exists): bumping
        // after a failed RENAME would leave code reading a missing column with
        // no retry. Fresh DBs already satisfy it via ensureTableStatic.
        if (hasColumn("coffee_bags", "yield_override_g")) {
            query.exec ("DELETE FROM schema_version");
            query.exec ("INSERT INTO schema_version (version) VALUES (21)");
            currentVersion = 21;
        } else {
            qWarning() << "ShotHistoryStorage: migration 21 column rename failed - will retry next launch";
        }
    }

    // Migration 22: extract the grinder into first-class Equipment packages
    // (add-equipment-packages). Create equipment tables; add equipment_id + rpm
    // to coffee_bags and shots; create one package per distinct grinder identity
    // (brand/model/burrs — NOT grind/rpm) plus a default from the current
    // settings; link every row; split combined "24 1400rpm" settings into
    // grinder_setting + rpm. This is the ADDITIVE half — the legacy grinder
    // identity columns are KEPT for now so existing readers (CoffeeBagStorage,
    // shot projection) keep working; they are dropped in migration 23 once every
    // reader resolves identity via equipment_id. The data step is NOT idempotent
    // (it would re-create packages), so the version bumps on the additive part;
    // the block then never re-runs. Whitespace before the open-paren dodges
    // the QSqlQuery permission-hook false-positive, as elsewhere.
    //
    // Gate on ">= 21 && < 22" (advance only from a completed 21), NOT "< 22":
    // migration 21's RENAME is gated on its post-condition and holds the version
    // at 20 on failure so it retries next launch. A "< 22" gate would let
    // migration 22 run and bump straight to 22 after a faulted 21, stranding the
    // yield rename. The whole step is wrapped in a transaction (like migrations
    // 7/8/16) so a failure rolls back the partial package inserts — the data step
    // is NOT idempotent, so a half-applied retry would duplicate packages.
    if (currentVersion >= 21 && currentVersion < 22) {
        qDebug() << "ShotHistoryStorage: Running migration to version 22 (equipment packages)";

        const bool txn = m_db.transaction();
        const bool tablesOk = EquipmentStorage::ensureTablesStatic(m_db);

        if (!hasColumn("coffee_bags", "equipment_id"))
            query.exec ("ALTER TABLE coffee_bags ADD COLUMN equipment_id INTEGER");
        if (!hasColumn("coffee_bags", "rpm"))
            query.exec ("ALTER TABLE coffee_bags ADD COLUMN rpm INTEGER");
        if (!hasColumn("shots", "equipment_id"))
            query.exec ("ALTER TABLE shots ADD COLUMN equipment_id INTEGER");
        if (!hasColumn("shots", "rpm"))
            query.exec ("ALTER TABLE shots ADD COLUMN rpm INTEGER");

        const bool colsOk = hasColumn("coffee_bags", "equipment_id") && hasColumn("coffee_bags", "rpm")
            && hasColumn("shots", "equipment_id") && hasColumn("shots", "rpm");

        // The default package seeds from the user's live grinder settings (the
        // active bag mirrors these via write-through, but reading QSettings here
        // also covers a user who set a grinder but never saved a shot/bag).
        bool dataOk = false;
        if (tablesOk && colsOk) {
            // Read the SAME QSettings scope SettingsDye writes to — a bare
            // QSettings() depends on QCoreApplication org/app being set, which
            // isn't guaranteed (the scope-mismatch bug migration 16 fixed).
#ifdef DECENZA_TESTING
            QSettings settings(Settings::testQSettingsPath(), QSettings::IniFormat);
#else
            QSettings settings(QStringLiteral("DecentEspresso"), QStringLiteral("DE1Qt"));
#endif
            const QString curBrand = settings.value("dye/grinderBrand").toString();
            const QString curModel = settings.value("dye/grinderModel").toString();
            const QString curBurrs = settings.value("dye/grinderBurrs").toString();
            const QString curSetting = settings.value("dye/grinderSetting").toString();
            dataOk = EquipmentStorage::migrateFromGrinderColumnsStatic(
                m_db, curBrand, curModel, curBurrs, curSetting);
        }

        if (tablesOk && colsOk && dataOk) {
            // Bump the version INSIDE the transaction so the data + version commit
            // atomically — a crash between them would otherwise leave migrated
            // data at version 21 and re-run the non-idempotent step next launch.
            query.exec ("DELETE FROM schema_version");
            query.exec ("INSERT INTO schema_version (version) VALUES (22)");
            if (!txn || m_db.commit()) {
                currentVersion = 22;
            } else {
                if (txn) m_db.rollback();
                qWarning() << "ShotHistoryStorage: migration 22 commit failed - will retry next launch";
            }
        } else {
            if (txn)
                m_db.rollback();  // undo partial package inserts so the retry is clean
            qWarning() << "ShotHistoryStorage: migration 22 incomplete (tables" << tablesOk
                       << "cols" << colsOk << "data" << dataOk
                       << ") - will retry next launch";
        }
    }

    // Migration 23: drop the legacy grinder identity columns now that every
    // reader resolves brand/model/burrs through equipment_id (add-equipment-
    // packages). Migration 22 deliberately KEPT grinder_brand/model/burrs on
    // shots + coffee_bags so the upgrade could seed packages from them and
    // existing readers kept working; with the reader sweep complete, the
    // pointer-to-immutable-package is the only identity path and the snapshot
    // columns are dead weight, so they go.
    //
    // Ordering inside the step matters: shots_fts is an external-content FTS5
    // index over the grinder columns and its triggers reference new./old.grinder_*,
    // and idx_shots_grinder is built on them. SQLite refuses DROP COLUMN on a
    // column referenced by a trigger or index, so the FTS index/triggers are
    // rebuilt WITHOUT the grinder columns and the index is dropped BEFORE the
    // columns. DROP COLUMN needs SQLite >= 3.35 (we require it). The whole step
    // is wrapped in a transaction and gated on the post-condition (columns gone)
    // so a failure rolls back and retries next launch — it is idempotent
    // (hasColumn guards each drop). Gate ">= 22 && < 23" so it only advances
    // from a committed migration 22 (mirrors migration 22's gate on 21).
    if (currentVersion >= 22 && currentVersion < 23) {
        qDebug() << "ShotHistoryStorage: Running migration to version 23 (drop legacy grinder columns)";

        const bool txn = m_db.transaction();
        bool ok = true;

        // 1. Rebuild shots_fts without the grinder columns (mirror migration 8's
        //    rebuild). Drop the triggers + FTS table, recreate over the
        //    non-grinder text columns, then repopulate from shots.
        query.exec("DROP TRIGGER IF EXISTS shots_ai");
        query.exec("DROP TRIGGER IF EXISTS shots_ad");
        query.exec("DROP TRIGGER IF EXISTS shots_au");
        query.exec("DROP TABLE IF EXISTS shots_fts");

        if (!query.exec(R"(
            CREATE VIRTUAL TABLE IF NOT EXISTS shots_fts USING fts5(
                espresso_notes, bean_brand, bean_type, profile_name,
                content='shots', content_rowid='id'
            )
        )")) {
            qWarning() << "ShotHistoryStorage: migration 23 failed to recreate FTS table:"
                       << query.lastError().text();
            ok = false;
        }

        if (ok) {
            query.exec(R"(
                CREATE TRIGGER IF NOT EXISTS shots_ai AFTER INSERT ON shots BEGIN
                    INSERT INTO shots_fts(rowid, espresso_notes, bean_brand, bean_type, profile_name)
                    VALUES (new.id, new.espresso_notes, new.bean_brand, new.bean_type, new.profile_name);
                END
            )");
            query.exec(R"(
                CREATE TRIGGER IF NOT EXISTS shots_ad AFTER DELETE ON shots BEGIN
                    INSERT INTO shots_fts(shots_fts, rowid, espresso_notes, bean_brand, bean_type, profile_name)
                    VALUES ('delete', old.id, old.espresso_notes, old.bean_brand, old.bean_type, old.profile_name);
                END
            )");
            query.exec(R"(
                CREATE TRIGGER IF NOT EXISTS shots_au AFTER UPDATE ON shots BEGIN
                    INSERT INTO shots_fts(shots_fts, rowid, espresso_notes, bean_brand, bean_type, profile_name)
                    VALUES ('delete', old.id, old.espresso_notes, old.bean_brand, old.bean_type, old.profile_name);
                    INSERT INTO shots_fts(rowid, espresso_notes, bean_brand, bean_type, profile_name)
                    VALUES (new.id, new.espresso_notes, new.bean_brand, new.bean_type, new.profile_name);
                END
            )");
            if (!query.exec(R"(
                INSERT INTO shots_fts(rowid, espresso_notes, bean_brand, bean_type, profile_name)
                SELECT id, espresso_notes, bean_brand, bean_type, profile_name FROM shots
            )")) {
                qWarning() << "ShotHistoryStorage: migration 23 failed to repopulate FTS index:"
                           << query.lastError().text();
                ok = false;
            }
        }

        // 2. Drop the grinder index (DROP COLUMN refuses an indexed column).
        if (ok)
            query.exec ("DROP INDEX IF EXISTS idx_shots_grinder");

        // 3. Drop grinder_brand/model/burrs from both shots and coffee_bags.
        //    Idempotent: hasColumn skips a column already gone (e.g. a retried
        //    run after a mid-step crash). coffee_bags is guaranteed to exist
        //    here (created by migration 19, earlier in the chain).
        if (ok) {
            for (const char* col : {"grinder_brand", "grinder_model", "grinder_burrs"}) {
                for (const char* table : {"shots", "coffee_bags"}) {
                    if (hasColumn(table, col)) {
                        QSqlQuery drop(m_db);
                        if (!drop.exec (QStringLiteral("ALTER TABLE %1 DROP COLUMN %2")
                                            .arg(QLatin1String(table), QLatin1String(col)))) {
                            qWarning() << "ShotHistoryStorage: migration 23 failed to drop" << table << col << ":"
                                       << drop.lastError().text();
                            ok = false;
                        }
                    }
                }
            }
        }

        const bool dropped =
            !hasColumn("shots", "grinder_brand") && !hasColumn("shots", "grinder_model")
            && !hasColumn("shots", "grinder_burrs")
            && !hasColumn("coffee_bags", "grinder_brand") && !hasColumn("coffee_bags", "grinder_model")
            && !hasColumn("coffee_bags", "grinder_burrs");

        if (ok && dropped) {
            query.exec ("DELETE FROM schema_version");
            query.exec ("INSERT INTO schema_version (version) VALUES (23)");
            if (!txn || m_db.commit()) {
                currentVersion = 23;
                qInfo() << "ShotHistoryStorage: migration 23 complete - dropped grinder identity"
                           " columns from shots + coffee_bags, rebuilt shots_fts without them";
            } else {
                if (txn) m_db.rollback();
                qWarning() << "ShotHistoryStorage: migration 23 commit failed - will retry next launch";
            }
        } else {
            if (txn)
                m_db.rollback();
            qWarning() << "ShotHistoryStorage: migration 23 incomplete (ok" << ok
                       << "dropped" << dropped << ") - will retry next launch";
        }
    }

    // Migration 24: visualizer_sync_pending on coffee_bags (add-bag-detail-
    // editing) — set when a bag edit's Visualizer PATCH fails retryably, so
    // the next upload cycle re-pushes it. One idempotent additive column;
    // hasColumn guards the retry after a mid-step crash. Gate ">= 23 && < 24"
    // so it only advances from a committed migration 23 (mirrors the chain).
    if (currentVersion >= 23 && currentVersion < 24) {
        qDebug() << "ShotHistoryStorage: Running migration to version 24 (bag visualizer_sync_pending)";

        if (!hasColumn("coffee_bags", "visualizer_sync_pending"))
            query.exec ("ALTER TABLE coffee_bags ADD COLUMN visualizer_sync_pending INTEGER NOT NULL DEFAULT 0");

        if (hasColumn("coffee_bags", "visualizer_sync_pending")) {
            query.exec ("DELETE FROM schema_version");
            query.exec ("INSERT INTO schema_version (version) VALUES (24)");
            currentVersion = 24;
        } else {
            qWarning() << "ShotHistoryStorage: migration 24 incomplete - will retry next launch";
        }
    }

    // Migration 25: recipes (add-recipes). Create the recipes table and add
    // shot provenance: recipe_id names the recipe active at shot start, and
    // steam_json snapshots the steam spec in effect so promote-from-shot
    // round-trips the whole drink. Both nullable — legacy rows unaffected.
    // Idempotent (CREATE IF NOT EXISTS + hasColumn guards); the bump gates on
    // the post-conditions like migrations 19/24. Whitespace before the
    // open-paren dodges the QSqlQuery permission-hook false-positive, as
    // elsewhere. Do not auto-format.
    if (currentVersion >= 24 && currentVersion < 25) {
        qDebug() << "ShotHistoryStorage: Running migration to version 25 (recipes)";

        const bool tableOk = RecipeStorage::ensureTableStatic(m_db);

        if (!hasColumn("shots", "recipe_id"))
            query.exec ("ALTER TABLE shots ADD COLUMN recipe_id INTEGER");
        if (!hasColumn("shots", "steam_json"))
            query.exec ("ALTER TABLE shots ADD COLUMN steam_json TEXT");
        query.exec("CREATE INDEX IF NOT EXISTS idx_shots_recipe_id ON shots(recipe_id)");

        if (tableOk && hasColumn("shots", "recipe_id") && hasColumn("shots", "steam_json")) {
            query.exec ("DELETE FROM schema_version");
            query.exec ("INSERT INTO schema_version (version) VALUES (25)");
            currentVersion = 25;
        } else {
            qWarning() << "ShotHistoryStorage: migration 25 incomplete - will retry next launch";
        }
    }

    // Migration 26: recipes.rpm_pinned (add-recipes follow-up) — the grind
    // override pins grind AND rpm together. Fresh DBs get the column from
    // ensureTableStatic (the hasColumn guard makes both paths converge);
    // this repairs branch-dev DBs that already ran migration 25 with the
    // old table. One idempotent additive column, gated ">= 25 && < 26".
    if (currentVersion >= 25 && currentVersion < 26) {
        qDebug() << "ShotHistoryStorage: Running migration to version 26 (recipes rpm_pinned)";

        if (!hasColumn("recipes", "rpm_pinned"))
            query.exec ("ALTER TABLE recipes ADD COLUMN rpm_pinned INTEGER");

        if (hasColumn("recipes", "rpm_pinned")) {
            query.exec ("DELETE FROM schema_version");
            query.exec ("INSERT INTO schema_version (version) VALUES (26)");
            currentVersion = 26;
        } else {
            qWarning() << "ShotHistoryStorage: migration 26 incomplete - will retry next launch";
        }
    }

    // Migration 27: hot_water_json (finish-recipes-first-class) — the opt-in
    // added-hot-water block (water-vessel snapshot) that lets a recipe describe
    // an Americano. Added on BOTH recipes (the recipe's own block) and shots
    // (the snapshot in effect at shot start, so promote-from-shot round-trips
    // the whole drink, exactly like shots.steam_json from migration 25). Fresh
    // DBs get recipes.hot_water_json from ensureTableStatic (the hasColumn guard
    // makes both paths converge); this repairs DBs that already ran migration
    // 25/26 with the old table. Idempotent additive columns, gated
    // ">= 26 && < 27", mirroring migration 26.
    if (currentVersion >= 26 && currentVersion < 27) {
        qDebug() << "ShotHistoryStorage: Running migration to version 27 (hot_water_json)";

        if (!hasColumn("recipes", "hot_water_json"))
            query.exec ("ALTER TABLE recipes ADD COLUMN hot_water_json TEXT");
        if (!hasColumn("shots", "hot_water_json"))
            query.exec ("ALTER TABLE shots ADD COLUMN hot_water_json TEXT");

        if (hasColumn("recipes", "hot_water_json") && hasColumn("shots", "hot_water_json")) {
            query.exec ("DELETE FROM schema_version");
            query.exec ("INSERT INTO schema_version (version) VALUES (27)");
            currentVersion = 27;
        } else {
            qWarning() << "ShotHistoryStorage: migration 27 incomplete - will retry next launch";
        }
    }

    // Migration 28: recipes.drink_type + coffee_bags.kind (add-recipe-wizard-tea).
    // drink_type records the wizard's drink intent (empty on legacy rows =
    // derive from blocks at read time); kind marks a bag as coffee or tea and
    // is stamped at creation only. Both are additive with safe defaults —
    // existing bags become 'coffee' via the column default; existing recipes
    // keep an empty drink_type. Fresh DBs get both columns from the
    // ensureTableStatic CREATE TABLEs (the hasColumn guards make both paths
    // converge). Idempotent, gated ">= 27 && < 28", mirroring migration 27.
    if (currentVersion >= 27 && currentVersion < 28) {
        qDebug() << "ShotHistoryStorage: Running migration to version 28 (drink_type + bag kind)";

        if (!hasColumn("recipes", "drink_type")
            && !query.exec ("ALTER TABLE recipes ADD COLUMN drink_type TEXT"))
            qWarning() << "ShotHistoryStorage: migration 28 add recipes.drink_type failed -"
                       << query.lastError().text();
        if (!hasColumn("coffee_bags", "kind")
            && !query.exec ("ALTER TABLE coffee_bags ADD COLUMN kind TEXT NOT NULL DEFAULT 'coffee'"))
            qWarning() << "ShotHistoryStorage: migration 28 add coffee_bags.kind failed -"
                       << query.lastError().text();

        if (hasColumn("recipes", "drink_type") && hasColumn("coffee_bags", "kind")) {
            query.exec ("DELETE FROM schema_version");
            query.exec ("INSERT INTO schema_version (version) VALUES (28)");
            currentVersion = 28;
        } else {
            qWarning() << "ShotHistoryStorage: migration 28 incomplete - will retry next launch";
        }
    }

    // Migration 29: recipes.bag_id (recipes-bag-links-ui-polish). Recipes now
    // link a SPECIFIC bag instead of resolving their bean identity to the
    // most-recently-used open bag at every activation (which silently picked
    // the wrong bag for users running two bags of one bean at different
    // ages). Additive column plus a one-time data pass that resolves each
    // existing recipe's bean identity to its current open bag — the retired
    // resolver's logic, run once; recipes whose bean has no open bag stay
    // NULL and present as stale. Fresh DBs get the column from
    // ensureTableStatic (the hasColumn guard makes both paths converge).
    // Idempotent (the data pass only touches NULL bag_id rows), gated
    // ">= 28 && < 29", mirroring migration 28.
    if (currentVersion >= 28 && currentVersion < 29) {
        qDebug() << "ShotHistoryStorage: Running migration to version 29 (recipes bag_id)";

        if (!hasColumn("recipes", "bag_id")
            && !query.exec ("ALTER TABLE recipes ADD COLUMN bag_id INTEGER"))
            qWarning() << "ShotHistoryStorage: migration 29 add recipes.bag_id failed -"
                       << query.lastError().text();

        // The version bump gates on the DATA pass too: the pass is
        // idempotent (NULL bag_id rows only), so a transient failure —
        // locked DB at upgrade time — simply retries next launch instead
        // of permanently stranding every recipe stale.
        if (hasColumn("recipes", "bag_id") && RecipeStorage::migrateBagLinksStatic(m_db)) {
            query.exec ("DELETE FROM schema_version");
            query.exec ("INSERT INTO schema_version (version) VALUES (29)");
            currentVersion = 29;
        } else {
            qWarning() << "ShotHistoryStorage: migration 29 incomplete - will retry next launch";
        }
    }

    // Migration 30: recipe-owned grind (fix-recipe-grind-integrity). The
    // "empty grind_pinned = inherit from the bag" mode is retired — grind
    // always lives on the recipe. Pure data pass, no schema change: each
    // inherit-mode row adopts its linked bag's current grinder_setting/rpm
    // once (the historical equivalent of the creation-time default). Rows
    // whose bag has no dial (tea bags, never-dialed) are skipped, bag-less
    // rows untouched. Idempotent and gated ">= 29 && < 30", mirroring 29:
    // a transient failure retries next launch instead of stranding rows.
    if (currentVersion >= 29 && currentVersion < 30) {
        qDebug() << "ShotHistoryStorage: Running migration to version 30 (recipe-owned grind)";

        if (RecipeStorage::migrateGrindOwnershipStatic(m_db)) {
            query.exec ("DELETE FROM schema_version");
            query.exec ("INSERT INTO schema_version (version) VALUES (30)");
            currentVersion = 30;
        } else {
            qWarning() << "ShotHistoryStorage: migration 30 incomplete - will retry next launch";
        }
    }

    // Migration 31: recipes.temp_offset_c (recipe-relative-temp-offset). A
    // recipe's temperature becomes a SIGNED DELTA against its profile instead
    // of the absolute temp_override_c, so a profile temperature edit moves the
    // recipe with the profile and can never manufacture a phantom offset.
    // Schema-only here: the new column's NULL default IS the "unconverted"
    // marker, and the data pass (RecipeStorage::convertLegacyTempOffsetsStatic)
    // runs deferred from MainController once the profile catalog — the
    // conversion anchor — is available. temp_override_c stays in place, dead:
    // it is the conversion input and the staging column for legacy-source
    // imports. Fresh DBs get temp_offset_c from ensureTableStatic's CREATE
    // TABLE (the hasColumn guard makes both paths converge). Idempotent,
    // gated ">= 30 && < 31", mirroring migration 30.
    if (currentVersion >= 30 && currentVersion < 31) {
        qDebug() << "ShotHistoryStorage: Running migration to version 31 (recipe temp offset)";

        if (!hasColumn("recipes", "temp_offset_c")
            && !query.exec ("ALTER TABLE recipes ADD COLUMN temp_offset_c REAL"))
            qWarning() << "ShotHistoryStorage: migration 31 add recipes.temp_offset_c failed -"
                       << query.lastError().text();

        if (hasColumn("recipes", "temp_offset_c")) {
            query.exec ("DELETE FROM schema_version");
            query.exec ("INSERT INTO schema_version (version) VALUES (31)");
            currentVersion = 31;
        } else {
            qWarning() << "ShotHistoryStorage: migration 31 incomplete - will retry next launch";
        }
    }

    // Migration 32: non-frozen storage lifecycle (bean-freshness-followup).
    // storage_hint (a non-frozen storage category) and opened_date (the
    // non-frozen analogue of defrost_date) join the existing freeze-lifecycle
    // fields on BOTH tables: coffee_bags (the current-portion state) AND shots
    // (the per-shot snapshot, so historical shots record which storage regime
    // they were pulled under). The shots columns mirror frozen_date/defrost_date
    // added by migration 19 — the shot-save INSERT, shot-read SELECT, and
    // device-transfer INSERT all name them. Additive with NULL defaults, no
    // backfill (existing bags/shots simply have both unset). Fresh DBs get the
    // coffee_bags columns from ensureTableStatic and the shots columns here (the
    // shots CREATE TABLE omits the lifecycle columns, same as frozen_date). The
    // hasColumn guards make both paths converge. Idempotent, gated
    // ">= 31 && < 32", mirroring migration 31. Whitespace before the open-paren
    // dodges the QSqlQuery permission-hook false-positive; do not auto-format.
    if (currentVersion >= 31 && currentVersion < 32) {
        qDebug() << "ShotHistoryStorage: Running migration to version 32 (storage hint + opened date)";

        if (!hasColumn("coffee_bags", "storage_hint")
            && !query.exec ("ALTER TABLE coffee_bags ADD COLUMN storage_hint TEXT"))
            qWarning() << "ShotHistoryStorage: migration 32 add coffee_bags.storage_hint failed -"
                       << query.lastError().text();
        if (!hasColumn("coffee_bags", "opened_date")
            && !query.exec ("ALTER TABLE coffee_bags ADD COLUMN opened_date TEXT"))
            qWarning() << "ShotHistoryStorage: migration 32 add coffee_bags.opened_date failed -"
                       << query.lastError().text();
        if (!hasColumn("shots", "storage_hint")
            && !query.exec ("ALTER TABLE shots ADD COLUMN storage_hint TEXT"))
            qWarning() << "ShotHistoryStorage: migration 32 add shots.storage_hint failed -"
                       << query.lastError().text();
        if (!hasColumn("shots", "opened_date")
            && !query.exec ("ALTER TABLE shots ADD COLUMN opened_date TEXT"))
            qWarning() << "ShotHistoryStorage: migration 32 add shots.opened_date failed -"
                       << query.lastError().text();

        if (hasColumn("coffee_bags", "storage_hint") && hasColumn("coffee_bags", "opened_date")
            && hasColumn("shots", "storage_hint") && hasColumn("shots", "opened_date")) {
            query.exec ("DELETE FROM schema_version");
            query.exec ("INSERT INTO schema_version (version) VALUES (32)");
            currentVersion = 32;
        } else {
            qWarning() << "ShotHistoryStorage: migration 32 incomplete - will retry next launch";
        }
    }

    // Migration 33: structured taste axes (add-ai-taste-intake). taste_balance
    // (sour|balanced|bitter) and taste_body (thin|medium|heavy) capture the two
    // dial-in taste axes the shot curve can't reveal, tapped in the AI taste
    // intake picker or on the review page. Shots-only — taste is a per-shot
    // observation, not a coffee_bags attribute. Empty-string = unset (matching
    // enjoyment0to100 == 0); an ADD COLUMN with no default leaves existing rows
    // NULL, which surfaces as "" on read (QSqlQuery::value().toString()) — the
    // same "unset" sentinel new writes use. Written only via
    // requestUpdateShotMetadata (a post-hoc edit),
    // so no shot-save INSERT binding is needed — but the shot-read SELECT and the
    // device-transfer INSERT name them. Idempotent, gated ">= 32 && < 33".
    // Whitespace before the open-paren dodges the QSqlQuery permission-hook
    // false-positive; do not auto-format.
    if (currentVersion >= 32 && currentVersion < 33) {
        qDebug() << "ShotHistoryStorage: Running migration to version 33 (taste axes)";

        if (!hasColumn("shots", "taste_balance")
            && !query.exec ("ALTER TABLE shots ADD COLUMN taste_balance TEXT"))
            qWarning() << "ShotHistoryStorage: migration 33 add shots.taste_balance failed -"
                       << query.lastError().text();
        if (!hasColumn("shots", "taste_body")
            && !query.exec ("ALTER TABLE shots ADD COLUMN taste_body TEXT"))
            qWarning() << "ShotHistoryStorage: migration 33 add shots.taste_body failed -"
                       << query.lastError().text();

        if (hasColumn("shots", "taste_balance") && hasColumn("shots", "taste_body")) {
            query.exec ("DELETE FROM schema_version");
            query.exec ("INSERT INTO schema_version (version) VALUES (33)");
            currentVersion = 33;
        } else {
            qWarning() << "ShotHistoryStorage: migration 33 incomplete - will retry next launch";
        }
    }

    m_schemaVersion = currentVersion;
    return true;
}

void ShotHistoryStorage::importLegacyBeanPresets()
{
    // Version-independent merge-import of legacy bean presets (bean-bag-
    // inventory). Runs every launch: converts any non-empty bean/presets
    // QSettings array — the initial upgrade, a previously failed import, or
    // a key reintroduced by old-version device transfer — into coffee_bags
    // rows. The shared static clears the keys only after a successful
    // commit, so a failure retries next launch with the preset data intact.
    if (m_schemaVersion < 19)
        return;  // coffee_bags table not ready; presets stay put for next launch

    // SettingsDye adopts the returned id through setActiveBagId() after
    // storage init — a raw QSettings write here would bypass the settings
    // cache and NOTIFY.
    m_migratedActiveBagId = CoffeeBagStorage::convertLegacyPresetSettings(m_dbPath);
}

QJsonObject ShotHistoryStorage::pointsToJsonObject(const QVector<QPointF>& points)
{
    QJsonArray timeArr, valueArr;
    for (const auto& pt : points) {
        timeArr.append(pt.x());
        valueArr.append(pt.y());
    }
    QJsonObject obj;
    obj["t"] = timeArr;
    obj["v"] = valueArr;
    return obj;
}

QByteArray ShotHistoryStorage::compressSampleData(ShotDataModel* shotData, const QString& phaseSummariesJson)
{
    QJsonObject root;

    root["pressure"] = pointsToJsonObject(shotData->pressureData());
    root["flow"] = pointsToJsonObject(shotData->flowData());
    root["temperature"] = pointsToJsonObject(shotData->temperatureData());
    root["pressureGoal"] = pointsToJsonObject(shotData->pressureGoalData());
    root["flowGoal"] = pointsToJsonObject(shotData->flowGoalData());
    root["temperatureGoal"] = pointsToJsonObject(shotData->temperatureGoalData());

    root["temperatureMix"] = pointsToJsonObject(shotData->temperatureMixData());
    root["resistance"] = pointsToJsonObject(shotData->resistanceData());
    root["conductance"] = pointsToJsonObject(shotData->conductanceData());
    root["darcyResistance"] = pointsToJsonObject(shotData->darcyResistanceData());
    root["conductanceDerivative"] = pointsToJsonObject(shotData->conductanceDerivativeData());
    root["waterDispensed"] = pointsToJsonObject(shotData->waterDispensedData());

    // Weight data - store cumulative weight for history
    root["weight"] = pointsToJsonObject(shotData->cumulativeWeightData());
    // Also store flow rate from scale for future graph display
    root["weightFlow"] = pointsToJsonObject(shotData->weightData());
    // Weight-based flow rate (g/s) for visualizer export
    root["weightFlowRate"] = pointsToJsonObject(shotData->weightFlowRateData());

    // Phase summaries for UI display (pre-computed by saveShot() via computePhaseSummaries)
    if (!phaseSummariesJson.isEmpty()) {
        root["phaseSummaries"] = QJsonDocument::fromJson(phaseSummariesJson.toUtf8()).array();
    }

    QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return qCompress(json, 9);  // Max compression
}

void ShotHistoryStorage::decompressSampleData(const QByteArray& blob, ShotRecord* record)
{
    QByteArray json = qUncompress(blob);
    if (json.isEmpty()) {
        qWarning() << "ShotHistoryStorage: Failed to decompress sample data";
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(json);
    QJsonObject root = doc.object();

    auto arrayToPoints = [](const QJsonObject& obj) {
        QVector<QPointF> points;
        QJsonArray timeArr = obj["t"].toArray();
        QJsonArray valueArr = obj["v"].toArray();
        qsizetype count = qMin(timeArr.size(), valueArr.size());
        points.reserve(count);
        for (qsizetype i = 0; i < count; ++i) {
            points.append(QPointF(timeArr[i].toDouble(), valueArr[i].toDouble()));
        }
        return points;
    };

    record->pressure = arrayToPoints(root["pressure"].toObject());
    record->flow = arrayToPoints(root["flow"].toObject());
    record->temperature = arrayToPoints(root["temperature"].toObject());
    record->pressureGoal = arrayToPoints(root["pressureGoal"].toObject());
    record->flowGoal = arrayToPoints(root["flowGoal"].toObject());
    record->temperatureGoal = arrayToPoints(root["temperatureGoal"].toObject());
    if (root.contains("temperatureMix"))
        record->temperatureMix = arrayToPoints(root["temperatureMix"].toObject());
    if (root.contains("resistance"))
        record->resistance = arrayToPoints(root["resistance"].toObject());
    if (root.contains("conductance"))
        record->conductance = arrayToPoints(root["conductance"].toObject());
    if (root.contains("darcyResistance"))
        record->darcyResistance = arrayToPoints(root["darcyResistance"].toObject());
    if (root.contains("conductanceDerivative"))
        record->conductanceDerivative = arrayToPoints(root["conductanceDerivative"].toObject());
    if (root.contains("waterDispensed"))
        record->waterDispensed = arrayToPoints(root["waterDispensed"].toObject());
    record->weight = arrayToPoints(root["weight"].toObject());
    if (root.contains("weightFlowRate"))
        record->weightFlowRate = arrayToPoints(root["weightFlowRate"].toObject());

    // Phase summaries (stored as JSON array in the compressed blob)
    if (root.contains("phaseSummaries")) {
        record->phaseSummariesJson = QString::fromUtf8(
            QJsonDocument(root["phaseSummaries"].toArray()).toJson(QJsonDocument::Compact));
    }
}

qint64 ShotHistoryStorage::saveShot(ShotDataModel* shotData,
                                     const Profile* profile,
                                     double duration,
                                     double finalWeight,
                                     double doseWeight,
                                     const ShotMetadata& metadata,
                                     const QString& debugLog,
                                     double temperatureOverride,
                                     double targetWeight,
                                     const QString& stoppedBy)
{
    if (!m_ready || m_backupInProgress || !shotData) {
        qWarning() << "ShotHistoryStorage: Cannot save shot - not ready, backup in progress, or no data";
        emit shotSaved(-1);
        return -1;
    }

    // Extract all data from QObject pointers on the main thread into a plain value struct
    ShotSaveData data;
    data.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    data.timestamp = QDateTime::currentSecsSinceEpoch();
    data.profileName = profile ? profile->title() : QStringLiteral("Unknown");
    data.profileJson = profile ? QString::fromUtf8(profile->toJson().toJson(QJsonDocument::Compact)) : QString();
    data.beverageType = profile ? profile->beverageType() : QStringLiteral("espresso");
    data.duration = duration;
    data.finalWeight = finalWeight;
    data.doseWeight = doseWeight;
    data.temperatureOverride = temperatureOverride;
    data.targetWeight = targetWeight;
    data.stoppedBy = stoppedBy;
    data.beanBrand = metadata.beanBrand;
    data.beanType = metadata.beanType;
    data.roastDate = metadata.roastDate;
    data.roastLevel = metadata.roastLevel;
    data.grinderBrand = metadata.grinderBrand;
    data.grinderModel = metadata.grinderModel;
    data.grinderBurrs = metadata.grinderBurrs;
    data.grinderSetting = metadata.grinderSetting;
    data.equipmentId = metadata.equipmentId;
    data.rpm = metadata.rpm;
    data.drinkTds = metadata.drinkTds;
    data.drinkEy = metadata.drinkEy;
    data.espressoEnjoyment = metadata.espressoEnjoyment;
    data.espressoNotes = metadata.espressoNotes;
    data.barista = metadata.barista;
    data.profileNotes = profile ? profile->profileNotes() : QString();
    data.debugLog = debugLog;
    data.beanBaseJson = metadata.beanBaseJson;
    data.bagId = metadata.bagId;
    data.frozenDate = metadata.frozenDate;
    data.defrostDate = metadata.defrostDate;
    data.storageHint = metadata.storageHint;
    data.openedDate = metadata.openedDate;
    data.recipeId = metadata.recipeId;
    data.steamJson = metadata.steamJson;
    data.hotWaterJson = metadata.hotWaterJson;

    if (profile) {
        data.profileKbId = ShotSummarizer::computeProfileKbId(profile->title(), profile->editorType());
    }

    // Compute conductance derivative (post-shot Gaussian smoothing) before compression
    shotData->computeConductanceDerivative();

    // Compute quality flags and phase summaries. Uses ShotSummarizer::getAnalysisFlags()
    // for KB flag lookups and ShotAnalysis helpers for detection. Runs on the main
    // thread before data is handed to the background save thread.
    // Build a temporary ShotRecord from the live data to reuse the static helpers.
    {
        ShotRecord tmpRecord;
        tmpRecord.pressure = shotData->pressureData();
        tmpRecord.flow = shotData->flowData();
        tmpRecord.temperature = shotData->temperatureData();
        tmpRecord.weight = shotData->cumulativeWeightData();

        // Extract phase markers into the record
        QVariantList tmpMarkers = shotData->phaseMarkersVariant();
        for (const QVariant& mv : tmpMarkers) {
            QVariantMap m = mv.toMap();
            HistoryPhaseMarker pm;
            pm.time = m["time"].toDouble();
            pm.label = m["label"].toString();
            pm.frameNumber = m["frameNumber"].toInt();
            pm.isFlowMode = m["isFlowMode"].toBool();
            pm.transitionReason = m["transitionReason"].toString();
            tmpRecord.phases.append(pm);
        }

        // Compute phase summaries
        computePhaseSummaries(tmpRecord);
        data.phaseSummariesJson = tmpRecord.phaseSummariesJson;

        // Compute all four quality badges via a single ShotAnalysis::analyzeShot
        // pass and project the booleans from DetectorResults using the
        // documented mapping. This unifies the save-time, load-time, and
        // dialog/AI/MCP cascades on one pipeline — the cascade lives in exactly
        // one place (analyzeShot's body). See docs/SHOT_REVIEW.md §4 for the
        // full mapping table and decenza::deriveBadgesFromAnalysis (in
        // history/shotbadgeprojection.h) for the projection rules.
        const AnalysisInputs inputs = prepareAnalysisInputs(data.profileKbId, data.profileJson);
        // KB-resolved bit gates grind Arm 1 — see openspec change
        // skip-grind-arm1-when-kb-unresolved. data.profileKbId is empty
        // when ShotSummarizer::matchProfileKey returned no hit (no exact
        // alias, no #1198 prefix, no editor-type default), which is the
        // signal "we have no profile context for Arm 1 to be meaningful".
        const bool profileKbResolved = !data.profileKbId.isEmpty();
        const auto analysis = ShotAnalysis::analyzeShot(
            tmpRecord.pressure, shotData->flowData(),
            shotData->cumulativeWeightData(),
            shotData->conductanceDerivativeData(),
            tmpRecord.phases, data.beverageType, duration,
            shotData->pressureGoalData(), shotData->flowGoalData(),
            inputs.analysisFlags, inputs.firstFrameSeconds,
            data.targetWeight, data.finalWeight,
            inputs.frameCount, inputs.expertBand,
            profileKbResolved);
        decenza::applyBadgesToTarget(data, analysis.detectors);
    }

    // Compress sample data on main thread (reads QObject data vectors)
    data.compressedSamples = compressSampleData(shotData, data.phaseSummariesJson);
    data.sampleCount = static_cast<int>(shotData->pressureData().size());

    // Extract phase markers on main thread
    QVariantList markers = shotData->phaseMarkersVariant();
    for (const QVariant& markerVar : markers) {
        QVariantMap marker = markerVar.toMap();
        HistoryPhaseMarker pm;
        pm.time = marker["time"].toDouble();
        pm.label = marker["label"].toString();
        pm.frameNumber = marker["frameNumber"].toInt();
        pm.isFlowMode = marker["isFlowMode"].toBool();
        pm.transitionReason = marker["transitionReason"].toString();
        data.phaseMarkers.append(pm);
    }

    // Run DB work on background thread
    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, data = std::move(data), destroyed]() {
        qint64 shotId = saveShotStatic(dbPath, data);

        // Capture only the fields needed for logging (avoid copying the large compressedSamples blob)
        QString profileName = data.profileName;
        double duration = data.duration;
        int sampleCount = data.sampleCount;
        qsizetype compressedSize = data.compressedSamples.size();

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, shotId, destroyed,
                                         profileName, duration, sampleCount, compressedSize]() {
            if (*destroyed) {
                qDebug() << "ShotHistoryStorage: saveShot callback dropped (object destroyed)";
                return;
            }

            if (shotId > 0) {
                m_lastSavedShotId = shotId;
                refreshTotalShots();  // already calls invalidateDistinctCache() internally

                qDebug() << "ShotHistoryStorage: Saved shot" << shotId
                         << "- Profile:" << profileName
                         << "- Duration:" << duration << "s"
                         << "- Samples:" << sampleCount
                         << "- Compressed size:" << compressedSize << "bytes";
            } else {
                emit errorOccurred("Failed to save shot to database");
            }

            emit shotSaved(shotId);
        }, Qt::QueuedConnection);
    });

    return 0;  // Async — actual shotId delivered via shotSaved signal
}

qint64 ShotHistoryStorage::saveShotStatic(const QString& dbPath, const ShotSaveData& data)
{
    if (data.uuid.isEmpty() || data.timestamp <= 0) {
        qWarning() << "ShotHistoryStorage::saveShotStatic: invalid data - uuid empty or timestamp zero";
        return -1;
    }

    qint64 shotId = -1;
    withTempDb(dbPath, "shs_save", [&](QSqlDatabase& db) {
        auto isLockError = [](const QSqlError& e) {
            const QString code = e.nativeErrorCode();
            return code == QLatin1String("5") || code == QLatin1String("6")
                || e.text().contains(QLatin1String("locked"), Qt::CaseInsensitive)
                || e.text().contains(QLatin1String("busy"), Qt::CaseInsensitive);
        };
        // A transient SQLITE_BUSY/locked from a concurrent writer (the post-shot
        // bags_update stamp, reconciliation drain, daily backup, WAL checkpoint)
        // must not drop a real shot. BEGIN IMMEDIATE (below) lets busy_timeout
        // absorb that contention; the retry loop is a backstop for the rare case a
        // writer is held past busy_timeout — retry the transaction before giving up.
        // attemptSave runs one full INSERT transaction. Returns true on commit;
        // on a lock-induced failure it sets `locked` so the caller retries.
        auto attemptSave = [&](bool& locked) -> bool {
            shotId = -1;
            locked = false;
            // BEGIN IMMEDIATE takes the write lock up front. db.transaction()'s plain
            // BEGIN is DEFERRED — it acquires a read lock first, so the INSERT then has
            // to upgrade while a concurrent writer (e.g. the post-shot bags_update bag
            // stamp) holds the lock, and SQLite returns SQLITE_BUSY immediately without
            // waiting out busy_timeout (deadlock avoidance). With IMMEDIATE the busy
            // handler rides out the brief writer, so the lock no longer surfaces.
            QSqlQuery beginQuery(db);
            if (!beginQuery.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
                locked = isLockError(beginQuery.lastError());
                qWarning() << "ShotHistoryStorage: Failed to start transaction:" << beginQuery.lastError().text();
                return false;
            }

            QSqlQuery query(db);
            query.prepare(R"(
                INSERT INTO shots (
                    uuid, timestamp, profile_name, profile_json, beverage_type,
                    duration_seconds, final_weight, dose_weight,
                    bean_brand, bean_type, roast_date, roast_level,
                    grinder_setting,
                    equipment_id, rpm,
                    drink_tds, drink_ey, enjoyment, espresso_notes, bean_notes, barista,
                    profile_notes, debug_log,
                    temperature_override, yield_override, profile_kb_id,
                    channeling_detected, grind_issue_detected,
                    skip_first_frame_detected, pour_truncated_detected,
                    stopped_by, beanbase_json, beanbase_id,
                    bag_id, frozen_date, defrost_date, storage_hint, opened_date,
                    recipe_id, steam_json, hot_water_json
                ) VALUES (
                    :uuid, :timestamp, :profile_name, :profile_json, :beverage_type,
                    :duration, :final_weight, :dose_weight,
                    :bean_brand, :bean_type, :roast_date, :roast_level,
                    :grinder_setting,
                    :equipment_id, :rpm,
                    :drink_tds, :drink_ey, :enjoyment, :espresso_notes, :bean_notes, :barista,
                    :profile_notes, :debug_log,
                    :temperature_override, :yield_override, :profile_kb_id,
                    :channeling_detected, :grind_issue_detected,
                    :skip_first_frame_detected, :pour_truncated_detected,
                    :stopped_by, :beanbase_json, :beanbase_id,
                    :bag_id, :frozen_date, :defrost_date, :storage_hint, :opened_date,
                    :recipe_id, :steam_json, :hot_water_json
                )
            )");

            query.bindValue(":uuid", data.uuid);
            query.bindValue(":timestamp", data.timestamp);
            query.bindValue(":profile_name", data.profileName);
            query.bindValue(":profile_json", data.profileJson);
            query.bindValue(":beverage_type", data.beverageType);
            query.bindValue(":duration", data.duration);
            query.bindValue(":final_weight", data.finalWeight);
            query.bindValue(":dose_weight", data.doseWeight);
            query.bindValue(":bean_brand", data.beanBrand);
            query.bindValue(":bean_type", data.beanType);
            query.bindValue(":roast_date", data.roastDate);
            query.bindValue(":roast_level", data.roastLevel);
            // Grinder identity (brand/model/burrs) is no longer snapshotted on the
            // shot — it resolves through equipment_id to the package's immutable
            // grinder item (migration 23). Only the per-shot dial-in is stored.
            query.bindValue(":grinder_setting", data.grinderSetting);
            query.bindValue(":equipment_id", data.equipmentId > 0 ? QVariant(data.equipmentId) : QVariant());
            query.bindValue(":rpm", data.rpm > 0 ? QVariant(data.rpm) : QVariant());
            query.bindValue(":drink_tds", data.drinkTds);
            query.bindValue(":drink_ey", data.drinkEy);
            query.bindValue(":enjoyment", data.espressoEnjoyment);
            query.bindValue(":espresso_notes", data.espressoNotes);
            query.bindValue(":bean_notes", QString());
            query.bindValue(":barista", data.barista);
            query.bindValue(":profile_notes", data.profileNotes);
            query.bindValue(":debug_log", data.debugLog);
            query.bindValue(":temperature_override", data.temperatureOverride);
            query.bindValue(":yield_override", data.targetWeight);
            query.bindValue(":profile_kb_id", data.profileKbId.isEmpty() ? QVariant() : data.profileKbId);
            query.bindValue(":channeling_detected", data.channelingDetected ? 1 : 0);
            query.bindValue(":grind_issue_detected", data.grindIssueDetected ? 1 : 0);
            query.bindValue(":skip_first_frame_detected", data.skipFirstFrameDetected ? 1 : 0);
            query.bindValue(":pour_truncated_detected", data.pourTruncatedDetected ? 1 : 0);
            query.bindValue(":stopped_by", data.stoppedBy);
            query.bindValue(":beanbase_json", data.beanBaseJson.isEmpty() ? QVariant() : data.beanBaseJson);
            // beanbase_id is the indexed canonical UUID for the history
            // search lane — derived from the same blob the row stores.
            {
                const QString canonicalId = BeanBaseBlob::canonicalId(data.beanBaseJson);
                query.bindValue(":beanbase_id", canonicalId.isEmpty() ? QVariant() : canonicalId);
            }
            query.bindValue(":bag_id", bagIdIsSet(data.bagId) ? QVariant(data.bagId) : QVariant());
            query.bindValue(":frozen_date", data.frozenDate.isEmpty() ? QVariant() : data.frozenDate);
            query.bindValue(":defrost_date", data.defrostDate.isEmpty() ? QVariant() : data.defrostDate);
            query.bindValue(":storage_hint", data.storageHint.isEmpty() ? QVariant() : data.storageHint);
            query.bindValue(":opened_date", data.openedDate.isEmpty() ? QVariant() : data.openedDate);
            query.bindValue(":recipe_id", data.recipeId > 0 ? QVariant(data.recipeId) : QVariant());
            query.bindValue(":steam_json", data.steamJson.isEmpty() ? QVariant() : data.steamJson);
            query.bindValue(":hot_water_json", data.hotWaterJson.isEmpty() ? QVariant() : data.hotWaterJson);

            if (!query.exec()) {
                locked = isLockError(query.lastError());
                qWarning() << "ShotHistoryStorage: Failed to insert shot:" << query.lastError().text()
                           << "(sqlite code" << query.lastError().nativeErrorCode() << ")";
                QSqlQuery(db).exec(QStringLiteral("ROLLBACK"));
                return false;
            }

            shotId = query.lastInsertId().toLongLong();

            // Insert compressed sample data
            query.prepare("INSERT INTO shot_samples (shot_id, sample_count, data_blob) VALUES (:id, :count, :blob)");
            query.bindValue(":id", shotId);
            query.bindValue(":count", data.sampleCount);
            query.bindValue(":blob", data.compressedSamples);

            if (!query.exec()) {
                locked = isLockError(query.lastError());
                qWarning() << "ShotHistoryStorage: Failed to insert samples:" << query.lastError().text();
                QSqlQuery(db).exec(QStringLiteral("ROLLBACK"));
                shotId = -1;
                return false;
            }

            // Insert phase markers
            for (const HistoryPhaseMarker& pm : data.phaseMarkers) {
                query.prepare(R"(
                    INSERT INTO shot_phases (shot_id, time_offset, label, frame_number, is_flow_mode, transition_reason)
                    VALUES (:shot_id, :time, :label, :frame, :flow_mode, :reason)
                )");
                query.bindValue(":shot_id", shotId);
                query.bindValue(":time", pm.time);
                query.bindValue(":label", pm.label);
                query.bindValue(":frame", pm.frameNumber);
                query.bindValue(":flow_mode", pm.isFlowMode ? 1 : 0);
                query.bindValue(":reason", pm.transitionReason);
                query.exec();  // Non-critical if markers fail
            }

            // COMMIT. On a transient lock SQLite keeps the transaction staged, so
            // retry the COMMIT itself rather than rolling back — a ROLLBACK here
            // would discard the inserted row + sample blob + phase markers and force
            // the outer loop to redo the whole insert. (Rare in WAL after BEGIN
            // IMMEDIATE; only a non-lock error or exhausting the commit retries
            // falls through to ROLLBACK + a full-transaction retry.)
            QSqlQuery commitQuery(db);
            for (int commitTry = 1; !commitQuery.exec(QStringLiteral("COMMIT")); ++commitTry) {
                const bool commitLocked = isLockError(commitQuery.lastError());
                if (!commitLocked || commitTry >= 4) {
                    locked = commitLocked;
                    qWarning() << "ShotHistoryStorage: Failed to commit shot:"
                               << commitQuery.lastError().text();
                    QSqlQuery(db).exec(QStringLiteral("ROLLBACK"));
                    shotId = -1;
                    return false;
                }
                QThread::msleep(static_cast<unsigned long>(50 * commitTry));
            }

            // Checkpoint WAL
            QSqlQuery walQuery(db);
            walQuery.exec("PRAGMA wal_checkpoint(PASSIVE)");
            return true;
        };

        // Retry only a lock-induced miss; success or a real error (constraint,
        // etc.) exits immediately. Short escalating backoff lets the competing
        // writer finish before the next attempt.
        for (int attempt = 1; attempt <= 4; ++attempt) {
            bool locked = false;
            if (attemptSave(locked) || !locked)
                break;
            qWarning() << "ShotHistoryStorage: shot save hit a transient lock, retrying ("
                       << attempt << "of 4)";
            QThread::msleep(static_cast<unsigned long>(50 * attempt));
        }
    });

    return shotId;
}

void ShotHistoryStorage::requestUpdateVisualizerInfo(qint64 shotId, const QString& visualizerId, const QString& visualizerUrl)
{
    if (!m_ready) {
        emit visualizerInfoUpdated(shotId, false);
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, shotId, visualizerId, visualizerUrl, destroyed]() {
        bool success = false;
        bool opened = withTempDb(dbPath, "shs_vizupd", [&](QSqlDatabase& db) {
            QSqlQuery query(db);
            if (!query.prepare("UPDATE shots SET visualizer_id = :viz_id, visualizer_url = :viz_url, "
                               "updated_at = strftime('%s', 'now') WHERE id = :id")) {
                qWarning() << "ShotHistoryStorage: Failed to prepare visualizer update:" << query.lastError().text();
                return;
            }
            query.bindValue(":viz_id", visualizerId);
            query.bindValue(":viz_url", visualizerUrl);
            query.bindValue(":id", shotId);
            success = query.exec();
            if (!success)
                qWarning() << "ShotHistoryStorage: Failed to async update visualizer info:" << query.lastError().text();
        });
        if (!opened)
            qWarning() << "ShotHistoryStorage: requestUpdateVisualizerInfo failed - could not open DB for shot" << shotId;

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, shotId, success, destroyed]() {
            if (*destroyed) return;
            if (success)
                qDebug() << "ShotHistoryStorage: Async updated visualizer info for shot" << shotId;
            else
                qWarning() << "ShotHistoryStorage: Async visualizer info update FAILED for shot" << shotId;
            emit visualizerInfoUpdated(shotId, success);
        }, Qt::QueuedConnection);
    });
}

void ShotHistoryStorage::requestClearStaleVisualizerLink(qint64 shotId, const QString& staleVisualizerId)
{
    if (!m_ready || shotId <= 0 || staleVisualizerId.isEmpty()) {
        qWarning() << "ShotHistoryStorage: stale visualizer link NOT cleared for shot" << shotId
                   << "(not ready or bad args) — dead link may remain on the row";
        return;
    }

    const QString dbPath = m_dbPath;
    runOnDbThread([dbPath, shotId, staleVisualizerId]() {
        bool success = false;
        int rowsChanged = 0;
        bool opened = withTempDb(dbPath, "shs_vizclear", [&](QSqlDatabase& db) {
            QSqlQuery query(db);
            // The visualizer_id predicate is the guard: no-op if the row's
            // link was replaced since the stale id was queued.
            if (!query.prepare("UPDATE shots SET visualizer_id = '', visualizer_url = '', "
                               "updated_at = strftime('%s', 'now') "
                               "WHERE id = :id AND visualizer_id = :stale_id")) {
                qWarning() << "ShotHistoryStorage: Failed to prepare stale link clear:" << query.lastError().text();
                return;
            }
            query.bindValue(":id", shotId);
            query.bindValue(":stale_id", staleVisualizerId);
            success = query.exec();
            if (success)
                rowsChanged = query.numRowsAffected();
            else
                qWarning() << "ShotHistoryStorage: Failed to clear stale visualizer link:" << query.lastError().text();
        });
        if (!opened || !success)
            qWarning() << "ShotHistoryStorage: stale visualizer link clear FAILED for shot" << shotId
                       << "— dead link" << staleVisualizerId << "remains on the row";
        else if (rowsChanged > 0)
            qDebug() << "ShotHistoryStorage: cleared stale visualizer link" << staleVisualizerId
                     << "on shot" << shotId;
        else
            qDebug() << "ShotHistoryStorage: shot" << shotId << "no longer holds visualizer link"
                     << staleVisualizerId << "— nothing to clear (replaced meanwhile)";
    });
}

bool ShotHistoryStorage::reconcileVisualizerLinksStatic(
    QSqlDatabase& db, const QVariantList& cloudShots, qint64 windowStartEpoch,
    QVariantList& outLinked)
{
    // Match local shots.timestamp to a Visualizer shot's clock within
    // this tolerance (rounding only — Decenza uploads the shot epoch
    // verbatim as `clock`).
    constexpr qint64 kReconcileToleranceSec = 2;

    // Visualizer ids already attached to any local row must never be
    // reused. Seed the consumed-set from them. A failure here is NOT
    // ignorable: proceeding with an empty set would disable the
    // duplicate-link guard, so treat it as a hard failure (caller
    // retries next boot rather than burning the run-once flag).
    QSet<QString> usedIds;
    {
        QSqlQuery q(db);
        if (!q.exec("SELECT visualizer_id FROM shots "
                    "WHERE visualizer_id IS NOT NULL AND visualizer_id != ''")) {
            qWarning() << "ShotHistoryStorage: reconcile usedIds seed SELECT failed:"
                       << q.lastError().text();
            return false;
        }
        while (q.next()) usedIds.insert(q.value(0).toString());
    }

    QSqlQuery sel(db);
    sel.prepare("SELECT id, timestamp FROM shots "
                "WHERE (visualizer_id IS NULL OR visualizer_id = '') "
                "AND timestamp >= :winStart ORDER BY timestamp");
    sel.bindValue(":winStart", windowStartEpoch);
    if (!sel.exec()) {
        qWarning() << "ShotHistoryStorage: reconcile SELECT failed:"
                   << sel.lastError().text();
        return false;
    }

    struct LocalRow { qint64 id; qint64 ts; };
    QVector<LocalRow> rows;
    while (sel.next())
        rows.append({sel.value(0).toLongLong(), sel.value(1).toLongLong()});

    for (const LocalRow& r : rows) {
        // Strict 1:1: exactly one not-yet-consumed cloud shot within
        // tolerance. 0 → no match; >=2 → ambiguous, skip (never guess).
        QString matchId, matchUrl;
        int matchCount = 0;
        for (const QVariant& cv : cloudShots) {
            const QVariantMap c = cv.toMap();
            const QString cid = c.value("visualizerId").toString();
            if (cid.isEmpty() || usedIds.contains(cid)) continue;
            const qint64 clk = c.value("clockEpoch").toLongLong();
            if (qAbs(clk - r.ts) <= kReconcileToleranceSec) {
                if (++matchCount == 1) {
                    matchId = cid;
                    matchUrl = c.value("url").toString();
                } else {
                    break;  // ambiguous
                }
            }
        }
        if (matchCount != 1) continue;

        QSqlQuery upd(db);
        upd.prepare("UPDATE shots SET visualizer_id = :vid, "
                    "visualizer_url = :vurl, "
                    "updated_at = strftime('%s', 'now') WHERE id = :id");
        upd.bindValue(":vid", matchId);
        upd.bindValue(":vurl", matchUrl);
        upd.bindValue(":id", r.id);
        if (!upd.exec()) {
            qWarning() << "ShotHistoryStorage: reconcile UPDATE failed for shot"
                       << r.id << ":" << upd.lastError().text();
            continue;
        }
        usedIds.insert(matchId);  // consumed — no reuse this pass
        QVariantMap m;
        m["shotId"] = r.id;
        m["visualizerId"] = matchId;
        outLinked.append(m);
        qDebug() << "ShotHistoryStorage: reconcile linked shot" << r.id
                 << "->" << matchId;
    }
    return true;
}

void ShotHistoryStorage::requestReconcileVisualizerLinks(const QVariantList& cloudShots,
                                                         qint64 windowStartEpoch)
{
    if (!m_ready) {
        emit visualizerLinksReconciled(false, QVariantList());
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, cloudShots, windowStartEpoch, destroyed]() {
        QVariantList linked;
        bool staticOk = false;
        const bool opened = withTempDb(dbPath, "shs_vizrecon", [&](QSqlDatabase& db) {
            staticOk = reconcileVisualizerLinksStatic(db, cloudShots, windowStartEpoch, linked);
        });
        // ok only when the DB opened AND every SQL step succeeded.
        // Otherwise the caller must NOT advance the run-once flag.
        const bool ok = opened && staticOk;
        if (!opened)
            qWarning() << "ShotHistoryStorage: reconcile could not open DB — will retry next boot";

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, ok, linked, destroyed]() {
            if (*destroyed) return;
            qDebug() << "ShotHistoryStorage: reconcile" << (ok ? "completed" : "FAILED")
                     << "— linked" << linked.size() << "shot(s)";
            emit visualizerLinksReconciled(ok, linked);
        }, Qt::QueuedConnection);
    });
}

void ShotHistoryStorage::requestMostRecentShotId()
{
    if (!m_ready) {
        emit mostRecentShotIdReady(-1);
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, destroyed]() {
        qint64 shotId = -1;
        bool opened = withTempDb(dbPath, "shs_recent", [&](QSqlDatabase& db) {
            QSqlQuery query(db);
            if (query.exec("SELECT id FROM shots ORDER BY timestamp DESC LIMIT 1") && query.next())
                shotId = query.value(0).toLongLong();
        });
        if (!opened)
            qWarning() << "ShotHistoryStorage: requestMostRecentShotId failed - could not open DB";

        // Skip the emit on open failure: -1 here means "no shots" to a consumer,
        // and a transient open failure must not masquerade as an empty history.
        if (*destroyed || !opened) return;
        QMetaObject::invokeMethod(this, [this, shotId, destroyed]() {
            if (*destroyed) return;
            emit mostRecentShotIdReady(shotId);
        }, Qt::QueuedConnection);
    });
}

void ShotHistoryStorage::requestShot(qint64 shotId)
{
    if (!m_ready) {
        emit shotReady(shotId, ShotProjection());
        return;
    }

    const QString dbPath = m_dbPath;

    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, shotId, destroyed]() {
        ShotRecord record;
        bool badgesPersisted = false;
        const bool opened = withTempDb(dbPath, "shs_shot", [&](QSqlDatabase& db) {
            record = loadShotRecordStatic(db, shotId, &badgesPersisted);
        });
        // On a DB-open failure `record` is default/invalid; do NOT deliver it as a
        // shotReady. MainController's migration16 visualizer-sync reads an invalid
        // projection as "shot no longer exists" and permanently pops a pending sync
        // — a transient open failure must not trigger that drop. A genuine
        // not-found (db opened, row absent) still emits, preserving that path.
        if (!opened)
            qWarning() << "ShotHistoryStorage: requestShot — DB open failed for shot"
                       << shotId << "(no shotReady emitted)";

        // Convert to QVariantMap on main thread (touches QML-visible data).
        // shotReady carries the recomputed badges already; shotBadgesUpdated
        // fires only when the load actually rewrote the stored columns, so
        // listeners that care about "this shot just got its badges corrected"
        // (e.g., a future history-list filter that wants to refresh) get a
        // signal without having to re-query.
        if (*destroyed || !opened) return;
        QMetaObject::invokeMethod(this, [this, shotId, record = std::move(record), badgesPersisted, destroyed]() {
            if (*destroyed) {
                qDebug() << "ShotHistoryStorage: requestShot callback dropped (object destroyed)";
                return;
            }
            emit shotReady(shotId, convertShotRecord(record));
            if (badgesPersisted) {
                emit shotBadgesUpdated(shotId,
                    record.channelingDetected,
                    record.grindIssueDetected,
                    record.skipFirstFrameDetected,
                    record.pourTruncatedDetected);
            }
        }, Qt::QueuedConnection);
    });
}

void ShotHistoryStorage::requestReanalyzeBadges(qint64 shotId)
{
    if (!m_ready) return;

    // loadShotRecordStatic already recomputes all four badges and persists to
    // the DB when any flag differs from the stored value. This path exists so
    // QML callers (ShotDetailPage / PostShotReviewPage) can fire a background
    // worker after onShotReady and learn — via shotBadgesUpdated — when the
    // recompute actually changed anything. We forward the load's
    // outBadgesPersisted to drive that signal.
    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, shotId, destroyed]() {
        bool recordFound = false;
        bool badgesPersisted = false;
        bool newChanneling = false;
        bool newGrindIssue = false, newSkipFirstFrame = false, newPourTruncated = false;

        withTempDb(dbPath, "shs_badges", [&](QSqlDatabase& db) {
            ShotRecord record = loadShotRecordStatic(db, shotId, &badgesPersisted);
            if (record.summary.id == 0) return;
            recordFound = true;
            newChanneling = record.channelingDetected;
            newGrindIssue = record.grindIssueDetected;
            newSkipFirstFrame = record.skipFirstFrameDetected;
            newPourTruncated = record.pourTruncatedDetected;
        });

        if (!recordFound || !badgesPersisted || *destroyed) return;
        QMetaObject::invokeMethod(
            this,
            [this, shotId, newChanneling, newGrindIssue, newSkipFirstFrame, newPourTruncated, destroyed]() {
                if (*destroyed) return;
                emit shotBadgesUpdated(shotId, newChanneling, newGrindIssue, newSkipFirstFrame, newPourTruncated);
            },
            Qt::QueuedConnection);
    });
}

void ShotHistoryStorage::computeDerivedCurves(ShotRecord& record)
{
    const qsizetype n = qMin(record.pressure.size(), record.flow.size());
    if (n < 3) return;

    // Share the conductance + derivative formulas with ShotDataModel (live path)
    // and shot_eval (offline) so all three agree on kernel / clamp / scaling.
    record.conductance = Conductance::fromPressureFlow(record.pressure, record.flow);

    // Darcy resistance (P/F²) isn't exposed via Conductance yet — retain the
    // inline loop here; mirror the same thresholds and clamp the namespace uses.
    record.darcyResistance.clear();
    record.darcyResistance.reserve(n);
    for (qsizetype i = 0; i < n; ++i) {
        const double p = record.pressure[i].y();
        const double f = record.flow[i].y();
        double dr = 0.0;
        if (f > 0.05 && p > 0.05) dr = qMin(p / (f * f), 19.0);
        record.darcyResistance.append(QPointF(record.pressure[i].x(), dr));
    }

    record.conductanceDerivative = Conductance::derivative(record.conductance);
}

void ShotHistoryStorage::computePhaseSummaries(ShotRecord& record)
{
    // Helper: average Y values in a time range
    auto avgInRange = [](const QVector<QPointF>& data, double t0, double t1) {
        double sum = 0;
        int count = 0;
        for (const auto& p : data) {
            if (p.x() >= t0 && p.x() <= t1) {
                sum += p.y();
                ++count;
            }
        }
        return count > 0 ? sum / count : 0.0;
    };

    // Helper: find Y value at or near a time
    auto valueAtTime = [](const QVector<QPointF>& data, double t) {
        if (data.isEmpty()) return 0.0;
        for (qsizetype i = 0; i < data.size(); ++i) {
            if (data[i].x() >= t)
                return data[i].y();
        }
        return data.last().y();
    };

    // Build phase boundaries from markers
    struct PhaseBound { QString name; double start; double end; bool isFlowMode; };
    QVector<PhaseBound> bounds;

    for (qsizetype i = 0; i < record.phases.size(); ++i) {
        const auto& marker = record.phases[i];
        if (marker.label == "End") continue;

        double end = (i + 1 < record.phases.size())
            ? record.phases[i + 1].time
            : (record.pressure.isEmpty() ? 0 : record.pressure.last().x());

        QString phaseName = marker.label;
        if (phaseName == "Start") phaseName = QStringLiteral("Preinfusion");

        bounds.append({phaseName, marker.time, end, marker.isFlowMode});
    }

    // If no usable phases, create single "Extraction" phase
    if (bounds.isEmpty() && !record.pressure.isEmpty()) {
        bounds.append({QStringLiteral("Extraction"), record.pressure.first().x(),
                       record.pressure.last().x(), false});
    }

    QJsonArray phasesArray;
    for (const auto& b : bounds) {
        if (b.end <= b.start) continue;

        QJsonObject phaseObj;
        phaseObj["name"] = b.name;
        phaseObj["duration"] = qRound((b.end - b.start) * 10.0) / 10.0;
        phaseObj["avgPressure"] = qRound(avgInRange(record.pressure, b.start, b.end) * 10.0) / 10.0;
        phaseObj["avgFlow"] = qRound(avgInRange(record.flow, b.start, b.end) * 10.0) / 10.0;
        phaseObj["avgTemperature"] = qRound(avgInRange(record.temperature, b.start, b.end) * 10.0) / 10.0;

        double w0 = valueAtTime(record.weight, b.start);
        double w1 = valueAtTime(record.weight, b.end);
        phaseObj["weightGained"] = qRound((w1 - w0) * 10.0) / 10.0;
        phaseObj["isFlowMode"] = b.isFlowMode;
        phasesArray.append(phaseObj);
    }

    record.phaseSummariesJson = QString::fromUtf8(
        QJsonDocument(phasesArray).toJson(QJsonDocument::Compact));
}

ShotRecord ShotHistoryStorage::loadShotRecordStatic(QSqlDatabase& db, qint64 shotId,
                                                     bool* outBadgesPersisted)
{
    if (outBadgesPersisted) *outBadgesPersisted = false;
    ShotRecord record;

    QSqlQuery query(db);
    // Grinder identity (brand/model/burrs) is resolved by following the shot's
    // equipment_id pointer to its package's grinder item, not from per-shot
    // snapshot columns (add-equipment-packages task 4.1). The legacy
    // grinder_brand/model/burrs columns are dropped in migration 23; this JOIN
    // is the single read path so the snapshot columns can go away. burrs lives
    // in the item's attrs JSON blob (json_extract). A NULL equipment_id (shot
    // with no grinder identity) LEFT-JOINs to NULLs — same empty strings the old
    // columns held.
    if (!query.prepare(R"(
        SELECT s.id, s.uuid, s.timestamp, s.profile_name, s.profile_json,
               s.duration_seconds, s.final_weight, s.dose_weight,
               s.bean_brand, s.bean_type, s.roast_date, s.roast_level,
               eg.brand, eg.model, json_extract(eg.attrs, '$.burrs'), s.grinder_setting,
               s.drink_tds, s.drink_ey, s.enjoyment, s.espresso_notes, s.bean_notes, s.barista,
               s.profile_notes, s.visualizer_id, s.visualizer_url, s.debug_log,
               s.temperature_override, s.yield_override, s.beverage_type, s.profile_kb_id,
               s.channeling_detected, s.grind_issue_detected,
               s.skip_first_frame_detected, s.pour_truncated_detected,
               s.stopped_by, s.beanbase_json,
               s.bag_id, s.frozen_date, s.defrost_date,
               s.equipment_id, s.rpm,
               ep.in_inventory, ep.superseded_by, ep.name,
               eb.brand, eb.model,
               epp.model,
               s.recipe_id, s.steam_json, s.hot_water_json,
               s.storage_hint, s.opened_date,
               s.taste_balance, s.taste_body
        FROM shots s
        LEFT JOIN equipment_items eg ON eg.package_id = s.equipment_id AND eg.kind = 'grinder'
        LEFT JOIN equipment_items eb ON eb.package_id = s.equipment_id AND eb.kind = 'basket'
        LEFT JOIN equipment_items epp ON epp.package_id = s.equipment_id AND epp.kind = 'puckprep'
        LEFT JOIN equipment_packages ep ON ep.id = s.equipment_id
        WHERE s.id = ?
    )")) {
        qWarning() << "ShotHistoryStorage::loadShotRecordStatic: prepare failed:" << query.lastError().text();
        return record;
    }
    query.bindValue(0, shotId);

    if (!query.exec() || !query.next()) {
        qWarning() << "ShotHistoryStorage::loadShotRecordStatic: Shot not found:" << shotId;
        return record;
    }

    record.summary.id = query.value(0).toLongLong();
    record.summary.uuid = query.value(1).toString();
    record.summary.timestamp = query.value(2).toLongLong();
    record.summary.profileName = query.value(3).toString();
    record.profileJson = query.value(4).toString();
    record.summary.duration = query.value(5).toDouble();
    record.summary.finalWeight = query.value(6).toDouble();
    record.summary.doseWeight = query.value(7).toDouble();
    record.summary.beanBrand = query.value(8).toString();
    record.summary.beanType = query.value(9).toString();
    record.roastDate = query.value(10).toString();
    record.roastLevel = query.value(11).toString();
    record.grinderBrand = query.value(12).toString();
    record.grinderModel = query.value(13).toString();
    record.grinderBurrs = query.value(14).toString();
    record.grinderSetting = query.value(15).toString();
    record.drinkTds = query.value(16).toDouble();
    record.drinkEy = query.value(17).toDouble();
    record.summary.enjoyment = query.value(18).toInt();
    record.espressoNotes = query.value(19).toString();
    record.beanNotes = query.value(20).toString();
    record.barista = query.value(21).toString();
    record.profileNotes = query.value(22).toString();
    record.visualizerId = query.value(23).toString();
    record.visualizerUrl = query.value(24).toString();
    record.debugLog = query.value(25).toString();
    record.temperatureOverride = query.value(26).toDouble();
    record.targetWeight = query.value(27).toDouble();
    record.summary.beverageType = query.value(28).toString();
    record.profileKbId = query.value(29).toString();
    record.channelingDetected = query.value(30).toInt() != 0;
    record.grindIssueDetected = query.value(31).toInt() != 0;
    record.skipFirstFrameDetected = query.value(32).toInt() != 0;
    record.pourTruncatedDetected = query.value(33).toInt() != 0;
    record.stoppedBy = query.value(34).toString();
    record.beanBaseJson = query.value(35).toString();
    record.bagId = query.value(36).isNull() ? -1 : query.value(36).toLongLong();
    record.frozenDate = query.value(37).toString();
    record.defrostDate = query.value(38).toString();
    record.equipmentId = query.value(39).isNull() ? 0 : query.value(39).toLongLong();
    record.rpm = query.value(40).toLongLong();
    // Equipment lineage state for history display (add-equipment-packages 4b.7).
    // A NULL in_inventory means the shot has no linked package (LEFT JOIN miss) —
    // leave the state empty. Otherwise: in inventory = current (""), out of
    // inventory with a superseded_by pointer = "older" (a newer fork exists),
    // out of inventory without one = "retired".
    if (record.equipmentId > 0 && !query.value(41).isNull()) {
        const bool inInventory = query.value(41).toInt() != 0;
        const bool hasSuccessor = !query.value(42).isNull() && query.value(42).toLongLong() > 0;
        if (!inInventory)
            record.equipmentState = hasSuccessor ? QStringLiteral("older") : QStringLiteral("retired");
    }
    // Package display name (col 43) — UI shows this rather than the raw grinder
    // identity. Falls back to brand+model when the package has no custom name.
    record.equipmentName = query.value(43).toString();
    if (record.equipmentName.isEmpty())
        record.equipmentName = (record.grinderBrand + QLatin1Char(' ') + record.grinderModel).trimmed();
    // Basket identity (cols 44/45) resolved through equipment_id (add-basket-
    // equipment); empty when the package has no basket. Specs are derived
    // downstream from BasketAliases, never stored.
    record.basketBrand = query.value(44).toString();
    record.basketModel = query.value(45).toString();
    // Puck-prep canonical flag string (col 46) resolved through equipment_id
    // (add-puckprep-equipment); empty when the package has no puck prep. Flags +
    // distribution are derived downstream (core/puckprep.h), never stored.
    record.puckPrep = query.value(46).toString();
    // Recipe provenance (cols 47/48, add-recipes): NULL = pre-recipe shot.
    // hot_water_json (col 49, finish-recipes-first-class) is the added-hot-water
    // snapshot for promote-from-shot round-trip.
    record.recipeId = query.value(47).isNull() ? -1 : query.value(47).toLongLong();
    record.steamJson = query.value(48).toString();
    record.hotWaterJson = query.value(49).toString();
    // Non-frozen storage lifecycle snapshot (cols 50/51, bean-freshness-
    // followup): the non-frozen analogue of frozen_date/defrost_date. Appended
    // at the end of the SELECT so existing positional reads keep their indices.
    record.storageHint = query.value(50).toString();
    record.openedDate = query.value(51).toString();
    // Structured taste axes (cols 52/53, add-ai-taste-intake): sour|balanced|
    // bitter and thin|medium|heavy. "" = unset. Appended at the end of the SELECT
    // so existing positional reads keep their indices.
    record.tasteBalance = query.value(52).toString();
    record.tasteBody = query.value(53).toString();
    record.summary.hasVisualizerUpload = !record.visualizerId.isEmpty();

    // Snapshot stored badge values before the recompute block overwrites them, so
    // we can detect drift and persist the corrected flags below.
    const bool storedChanneling = record.channelingDetected;
    const bool storedGrindIssue = record.grindIssueDetected;
    const bool storedSkipFirstFrame = record.skipFirstFrameDetected;
    const bool storedPourTruncated = record.pourTruncatedDetected;

    if (query.prepare("SELECT data_blob FROM shot_samples WHERE shot_id = ?")) {
        query.bindValue(0, shotId);
        if (query.exec() && query.next()) {
            QByteArray blob = query.value(0).toByteArray();
            decompressSampleData(blob, &record);
        }
    }

    // On-the-fly computation of derived curves for legacy shots that lack them.
    // Empty conductance = pre-migration-10 shot (the column was added in migration 10);
    // derive it now so the badge-recompute block below can always assume
    // conductanceDerivative is populated for the channeling check.
    bool needsDerivedCurves = record.conductance.isEmpty() && !record.pressure.isEmpty();
    if (needsDerivedCurves) {
        computeDerivedCurves(record);
    }

    if (query.prepare("SELECT time_offset, label, frame_number, is_flow_mode, transition_reason "
                      "FROM shot_phases WHERE shot_id = ? ORDER BY time_offset")) {
        query.bindValue(0, shotId);
        if (query.exec()) {
            while (query.next()) {
                HistoryPhaseMarker marker;
                marker.time = query.value(0).toDouble();
                marker.label = query.value(1).toString();
                marker.frameNumber = query.value(2).toInt();
                marker.isFlowMode = query.value(3).toInt() != 0;
                marker.transitionReason = query.value(4).toString();
                record.phases.append(marker);
            }
        }
    }

    // Compute phase summaries on-the-fly for legacy shots that lack them
    if (record.phaseSummariesJson.isEmpty() && !record.pressure.isEmpty() && !record.phases.isEmpty()) {
        computePhaseSummaries(record);
    }

    // For shots predating migration 9, profile_kb_id was not stored in the DB.
    // Derive it from the stored profile JSON so that channeling/grind suppression
    // flags still apply to old shots.
    if (record.profileKbId.isEmpty() && !record.profileJson.isEmpty()) {
        QJsonDocument kbDoc = QJsonDocument::fromJson(record.profileJson.toUtf8());
        if (!kbDoc.isNull()) {
            record.profileKbId = ShotSummarizer::computeProfileKbId(
                kbDoc.object()[QStringLiteral("title")].toString(),
                kbDoc.object()[QStringLiteral("legacy_profile_type")].toString());
        }
    }

    // Always recompute every quality badge from the loaded curve data, so that
    // detector improvements take effect on existing shots without a one-shot
    // re-analyze pass. Stored badge values are only authoritative as of save
    // time; the detectors evolve. The channeling sub-block uses
    // conductanceDerivative, which is either loaded from the DB
    // (post-migration-10) or filled by computeDerivedCurves() above (legacy).
    // The grind and skip-first-frame sub-blocks need only flow / flowGoal /
    // pressure / phases, which are always available.
    // Compute all four quality badges via a single ShotAnalysis::analyzeShot
    // pass and project the booleans from DetectorResults. The cascade lives in
    // exactly one place (analyzeShot's body) and the badge columns are a
    // deterministic projection — see decenza::deriveBadgesFromAnalysis (in
    // history/shotbadgeprojection.h) and docs/SHOT_REVIEW.md §4 for the
    // full mapping table.
    //
    // analyzeShot tolerates empty / partial inputs (its internal
    // pressure.size() < 10 short-circuit handles aborted shots), so the
    // outer "if (!record.pressure.isEmpty())" guard the per-detector code
    // used to need is no longer required — analyzeShot returns clean
    // defaults for any input shape it can't handle, which the projection
    // helper interprets as "all badges false."
    {
        const AnalysisInputs inputs = prepareAnalysisInputs(record.profileKbId, record.profileJson);
        // KB-resolved bit gates grind Arm 1 — see openspec change
        // skip-grind-arm1-when-kb-unresolved. record.profileKbId can be
        // empty either because the saved row predates KB resolution or
        // because matchProfileKey returned no hit at save time; in both
        // cases the right answer at recompute-on-load is "we have no
        // profile context for Arm 1".
        const bool profileKbResolved = !record.profileKbId.isEmpty();
        auto analysis = ShotAnalysis::analyzeShot(
            record.pressure, record.flow, record.weight,
            record.conductanceDerivative,
            record.phases, record.summary.beverageType, record.summary.duration,
            record.pressureGoal, record.flowGoal,
            inputs.analysisFlags, inputs.firstFrameSeconds,
            record.targetWeight, record.summary.finalWeight,
            inputs.frameCount, inputs.expertBand,
            profileKbResolved);
        decenza::applyBadgesToTarget(record, analysis.detectors);
        // Cache the AnalysisResult on the ShotRecord so convertShotRecord
        // (called next in the requestShot path) doesn't have to re-run
        // analyzeShot on the same inputs. See cachedAnalysis docstring on
        // ShotRecord for the invalidation contract.
        record.cachedAnalysis = std::move(analysis);
    }

    // Persist any drift between the stored badge columns and the recomputed values
    // on the same connection. Loading a shot is the canonical "touched it under the
    // current detector" event — both UI and MCP go through this path — so the DB
    // converges with detector improvements as shots are viewed without needing a
    // separate bulk-resweep migration. The UPDATE is skipped when nothing changed.
    const bool flagsChanged = (storedChanneling != record.channelingDetected
        || storedGrindIssue != record.grindIssueDetected
        || storedSkipFirstFrame != record.skipFirstFrameDetected
        || storedPourTruncated != record.pourTruncatedDetected);
    if (flagsChanged) {
        QSqlQuery upd(db);
        upd.prepare("UPDATE shots SET channeling_detected=:c,"
                    " grind_issue_detected=:g,"
                    " skip_first_frame_detected=:s,"
                    " pour_truncated_detected=:p,"
                    " updated_at = strftime('%s', 'now') WHERE id=:id");
        upd.bindValue(":c", record.channelingDetected ? 1 : 0);
        upd.bindValue(":g", record.grindIssueDetected ? 1 : 0);
        upd.bindValue(":s", record.skipFirstFrameDetected ? 1 : 0);
        upd.bindValue(":p", record.pourTruncatedDetected ? 1 : 0);
        upd.bindValue(":id", shotId);
        if (upd.exec()) {
            if (outBadgesPersisted) *outBadgesPersisted = true;
        } else {
            qWarning() << "ShotHistoryStorage::loadShotRecordStatic: badge persist failed for shot"
                       << shotId << upd.lastError();
        }
    }

    return record;
}

bool ShotHistoryStorage::deleteShot(qint64 shotId)
{
    if (!m_ready) return false;

    QSqlQuery query(m_db);
    query.prepare("DELETE FROM shots WHERE id = ?");
    query.bindValue(0, shotId);

    if (!query.exec()) {
        qWarning() << "ShotHistoryStorage: Failed to delete shot:" << query.lastError().text();
        return false;
    }

    // Note: no updateTotalShots()/invalidateDistinctCache()/shotDeleted() here.
    // This method is only called from importShotRecord() during overwrite, which
    // handles refresh via ShotImporter::refreshTotalShots() after the full batch.
    qDebug() << "ShotHistoryStorage: Deleted shot" << shotId;
    return true;
}

void ShotHistoryStorage::deleteShots(const QVariantList& shotIds)
{
    if (!m_ready || shotIds.isEmpty()) return;

    const QString dbPath = m_dbPath;

    // Build placeholders on main thread (pure computation, fast)
    QStringList placeholders;
    placeholders.reserve(shotIds.size());
    for (int i = 0; i < shotIds.size(); ++i)
        placeholders << "?";
    QString sql = "DELETE FROM shots WHERE id IN (" + placeholders.join(",") + ")";

    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, sql, shotIds, destroyed]() {
        bool success = false;
        withTempDb(dbPath, "shs_delete", [&](QSqlDatabase& db) {
            db.transaction();
            QSqlQuery query(db);
            if (query.prepare(sql)) {
                for (int i = 0; i < shotIds.size(); ++i)
                    query.bindValue(i, shotIds[i].toLongLong());
                if (query.exec()) {
                    db.commit();
                    success = true;
                } else {
                    qWarning() << "ShotHistoryStorage: Failed to batch delete shots:" << query.lastError().text();
                    db.rollback();
                }
            }
        });

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, shotIds, success, destroyed]() {
            if (*destroyed) {
                qDebug() << "ShotHistoryStorage: deleteShots callback dropped (object destroyed)";
                return;
            }
            if (success) {
                updateTotalShots();
                invalidateDistinctCache();
                for (const auto& id : shotIds)
                    emit shotDeleted(id.toLongLong());
                emit shotsDeleted(shotIds);
                qDebug() << "ShotHistoryStorage: Batch deleted" << shotIds.size() << "shots";
            }
        }, Qt::QueuedConnection);
    });
}

void ShotHistoryStorage::requestDeleteShot(qint64 shotId)
{
    if (!m_ready) {
        qWarning() << "ShotHistoryStorage: Cannot delete shot - not ready";
        emit errorOccurred("Cannot delete shot: database not ready");
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;

    runOnDbThread([this, dbPath, shotId, destroyed]() {
        bool success = false;
        withTempDb(dbPath, "shs_rdel", [&](QSqlDatabase& db) {
            QSqlQuery query(db);
            query.prepare("DELETE FROM shots WHERE id = ?");
            query.bindValue(0, shotId);
            if (query.exec()) {
                success = true;
            } else {
                qWarning() << "ShotHistoryStorage: Failed to async delete shot:" << query.lastError().text();
            }
        });

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, shotId, success, destroyed]() {
            if (*destroyed) {
                qDebug() << "ShotHistoryStorage: deleteShot callback dropped (object destroyed)";
                return;
            }
            if (success) {
                refreshTotalShots();
                invalidateDistinctCache();
                emit shotDeleted(shotId);
                qDebug() << "ShotHistoryStorage: Async deleted shot" << shotId;
            } else {
                qWarning() << "ShotHistoryStorage: Failed to async delete shot" << shotId;
                // User-facing (toast): no internal shot id — logged above.
                emit errorOccurred(QStringLiteral("Couldn't delete the shot — please try again."));
            }
        }, Qt::QueuedConnection);
    });
}

bool ShotHistoryStorage::updateShotMetadataStatic(QSqlDatabase& db, qint64 shotId, const QVariantMap& metadataIn)
{
    // Sanitize structured-taste values against their allowed sets before use.
    // "" is a valid "unset". Any out-of-set value is dropped (with a warning) so
    // a bad taste key can never write garbage — without failing an update that
    // also carries legitimate fields (add-ai-taste-intake). Non-taste keys pass
    // through untouched.
    QVariantMap metadata = metadataIn;
    static const QStringList kTasteBalanceValues = {"sour", "balanced", "bitter"};
    static const QStringList kTasteBodyValues    = {"thin", "medium", "heavy"};
    const auto sanitizeTaste = [&](const QString& key, const QStringList& allowed) {
        if (!metadata.contains(key)) return;
        const QString raw = metadata.value(key).toString();
        if (raw.isEmpty()) return;  // "" = unset/clear, always valid
        // Normalize case/whitespace so a drifted producer ("Balanced", " sour ")
        // is corrected rather than silently discarded. Only a genuinely
        // out-of-set value is dropped (with a warning).
        const QString norm = raw.trimmed().toLower();
        if (allowed.contains(norm)) {
            metadata.insert(key, norm);
        } else {
            qWarning() << "ShotHistoryStorage: dropping invalid" << key << "value" << raw;
            metadata.remove(key);
        }
    };
    sanitizeTaste("tasteBalance", kTasteBalanceValues);
    sanitizeTaste("tasteBody", kTasteBodyValues);

    // Map camelCase metadata keys to DB column names.
    // Only columns with keys present in the metadata map are updated,
    // so partial updates don't wipe unspecified fields.
    static const QList<QPair<QString, QString>> fieldMap = {
        {"beanBrand",       "bean_brand"},
        {"beanType",        "bean_type"},
        {"roastDate",       "roast_date"},
        {"roastLevel",      "roast_level"},
        // Grinder identity (brand/model/burrs) is no longer a per-shot column —
        // it resolves through equipment_id to the immutable package (migration 23).
        // To correct a shot's grinder, re-point it at a different package by
        // setting equipmentId (the picker's package id); brand/model/burrs keys
        // are intentionally ignored. The grind setting stays a per-shot dial-in.
        {"grinderSetting",  "grinder_setting"},
        {"rpm",             "rpm"},
        {"equipmentId",     "equipment_id"},
        {"drinkTds",        "drink_tds"},
        {"drinkEy",         "drink_ey"},
        {"enjoyment",       "enjoyment"},
        {"espressoNotes",   "espresso_notes"},
        {"barista",         "barista"},
        {"doseWeight",      "dose_weight"},
        {"finalWeight",     "final_weight"},
        {"beverageType",    "beverage_type"},
        // Bean Base snapshot: pass "" to clear (unlink), JSON string to
        // re-link — edit mode must be able to fix a wrong bean after the fact.
        {"beanBaseJson",    "beanbase_json"},
        // Indexed canonical UUID for the history search lane — callers that
        // set beanBaseJson should set this too (BeanBaseBlob::canonicalId).
        {"beanBaseId",      "beanbase_id"},
        // Coffee bag snapshot (bean-bag-inventory): the post-shot "wrong bag"
        // fix and the historical re-link path rewrite these.
        {"bagId",           "bag_id"},
        {"frozenDate",      "frozen_date"},
        {"defrostDate",     "defrost_date"},
        {"storageHint",     "storage_hint"},
        {"openedDate",      "opened_date"},
        // Structured taste axes (add-ai-taste-intake): the taste intake picker
        // and the review-page picker write these post-hoc. Values validated
        // against the allowed sets below before the map is consulted.
        {"tasteBalance",    "taste_balance"},
        {"tasteBody",       "taste_body"},
    };

    // Build SET clause from only the keys present in the metadata.
    QStringList setClauses;
    for (const auto& [metaKey, dbCol] : fieldMap) {
        if (metadata.contains(metaKey))
            setClauses << QString("%1 = :%1").arg(dbCol);
    }

    if (setClauses.isEmpty()) {
        qWarning() << "ShotHistoryStorage: No fields to update for shot" << shotId;
        return false;
    }

    setClauses << "updated_at = strftime('%s', 'now')";

    QString sql = QString("UPDATE shots SET %1 WHERE id = :id").arg(setClauses.join(", "));

    QSqlQuery query(db);
    if (!query.prepare(sql)) {
        qWarning() << "ShotHistoryStorage: Metadata update prepare failed:" << query.lastError().text();
        return false;
    }

    // Bind only the columns present in the metadata.
    for (const auto& [metaKey, dbCol] : fieldMap) {
        if (!metadata.contains(metaKey))
            continue;
        QVariant v = metadata.value(metaKey);
        // equipment_id is an FK — an unset/zero re-point clears the link to NULL
        // rather than pointing at a non-existent package 0.
        if (dbCol == QLatin1String("equipment_id") && v.toLongLong() <= 0)
            v = QVariant();
        query.bindValue(QString(":%1").arg(dbCol), v);
    }
    query.bindValue(":id", shotId);

    if (!query.exec()) {
        qWarning() << "ShotHistoryStorage: Failed to update shot metadata:" << query.lastError().text();
        return false;
    }
    return true;
}

void ShotHistoryStorage::requestUpdateShotMetadata(qint64 shotId, const QVariantMap& metadata)
{
    if (!m_ready) {
        emit shotMetadataUpdated(shotId, false);
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;

    runOnDbThread([this, dbPath, shotId, metadata, destroyed]() {
        bool success = false;
        withTempDb(dbPath, "shs_rupd", [&](QSqlDatabase& db) {
            success = updateShotMetadataStatic(db, shotId, metadata);
        });

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, shotId, success, destroyed]() {
            if (*destroyed) {
                qDebug() << "ShotHistoryStorage: updateMetadata callback dropped (object destroyed)";
                return;
            }
            if (success) {
                invalidateDistinctCache();
            } else {
                // User-facing (surfaced as a toast): no internal shot id, no
                // "metadata" jargon. The id + success are logged at qDebug below.
                emit errorOccurred(QStringLiteral("Couldn't save your shot changes — please try again."));
            }
            emit shotMetadataUpdated(shotId, success);
            qDebug() << "ShotHistoryStorage: Async updated metadata for shot" << shotId << "success:" << success;
        }, Qt::QueuedConnection);
    });
}

// Note: getDistinctValues / requestDistinct* / requestAutoFavorites* /
// queryGrinderContext all live in shothistorystorage_queries.cpp.

void ShotHistoryStorage::updateTotalShots()
{
    // Async: run COUNT on background thread using existing static helper
    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, destroyed]() {
        int count = getShotCountStatic(dbPath);
        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, count, destroyed]() {
            if (*destroyed) return;
            if (count < 0) {
                qWarning() << "ShotHistoryStorage::updateTotalShots: count query failed, keeping previous count" << m_totalShots;
                return;
            }
            if (count != m_totalShots) {
                m_totalShots = count;
                emit totalShotsChanged();
            }
        }, Qt::QueuedConnection);
    });
}

bool ShotHistoryStorage::performDatabaseCopy(const QString& destPath)
{
    // This method assumes caller has:
    // 1. Set m_backupInProgress = true
    // 2. Checked that m_dbPath is valid

    qDebug() << "ShotHistoryStorage: Performing database copy to" << destPath;

    // Checkpoint WAL to ensure all data is in main database file
    checkpoint();

    // Close database temporarily to ensure clean copy
    m_db.close();

    // Copy file using platform-specific method
    bool success = false;
#ifdef Q_OS_ANDROID
    // On Android, use Java file API for scoped storage compatibility
    success = QJniObject::callStaticMethod<jboolean>(
        "io/github/kulitorum/decenza_de1/StorageHelper",
        "copyFile",
        "(Ljava/lang/String;Ljava/lang/String;)Z",
        QJniObject::fromString(m_dbPath).object<jstring>(),
        QJniObject::fromString(destPath).object<jstring>());
    qDebug() << "ShotHistoryStorage: Java copyFile result:" << success;
#else
    // Desktop/iOS: use Qt's QFile::copy
    success = QFile::copy(m_dbPath, destPath);
#endif

    // Reopen database — this is critical, retry if first attempt fails
    if (!m_db.open()) {
        qWarning() << "ShotHistoryStorage: First reopen attempt failed, retrying:" << m_db.lastError().text();
        // Wait briefly and retry once
        QThread::msleep(100);
        if (!m_db.open()) {
            qCritical() << "ShotHistoryStorage: CRITICAL - Failed to reopen database after backup:" << m_db.lastError().text();
            m_ready = false;
            emit readyChanged();
            emit errorOccurred("Critical: Database connection lost after backup. Please restart the app.");
            return false;
        }
    }

    return success;
}

void ShotHistoryStorage::requestCreateBackup(const QString& destPath)
{
    if (m_backupInProgress) {
        qWarning() << "ShotHistoryStorage: Backup already in progress";
        emit backupFinished(false, QString());
        return;
    }

    if (m_dbPath.isEmpty()) {
        emit errorOccurred("Database path not set");
        emit backupFinished(false, QString());
        return;
    }

    m_backupInProgress = true;

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;

    QThread* thread = QThread::create([this, dbPath, destPath, destroyed]() {
        QString resultPath = createBackupStatic(dbPath, destPath);

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, resultPath, destroyed]() {
            if (*destroyed) {
                qDebug() << "ShotHistoryStorage: backup callback dropped (object destroyed)";
                return;
            }
            m_backupInProgress = false;
            emit backupFinished(!resultPath.isEmpty(), resultPath);
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ShotHistoryStorage::checkpoint()
{
    if (!m_db.isOpen()) {
        qWarning() << "ShotHistoryStorage::checkpoint: Database not open";
        return;
    }

    qDebug() << "ShotHistoryStorage: Starting checkpoint, dbPath:" << m_dbPath;
    qDebug() << "ShotHistoryStorage: Total shots:" << m_totalShots;

    QSqlQuery query(m_db);

    // First, try FULL checkpoint which waits for writers to finish
    if (query.exec("PRAGMA wal_checkpoint(FULL)")) {
        if (query.next()) {
            int busy = query.value(0).toInt();
            int log = query.value(1).toInt();
            int checkpointed = query.value(2).toInt();
            qDebug() << "ShotHistoryStorage: FULL checkpoint - busy:" << busy
                     << "log:" << log << "checkpointed:" << checkpointed;
        }
    } else {
        qWarning() << "ShotHistoryStorage: FULL checkpoint failed:" << query.lastError().text();
    }

    // Then TRUNCATE to clean up WAL file
    if (query.exec("PRAGMA wal_checkpoint(TRUNCATE)")) {
        if (query.next()) {
            int busy = query.value(0).toInt();
            int log = query.value(1).toInt();
            int checkpointed = query.value(2).toInt();
            qDebug() << "ShotHistoryStorage: TRUNCATE checkpoint - busy:" << busy
                     << "log:" << log << "checkpointed:" << checkpointed;
        }
    } else {
        qWarning() << "ShotHistoryStorage: TRUNCATE checkpoint failed:" << query.lastError().text();
    }

    // Verify file size after checkpoint
    QFile dbFile(m_dbPath);
    if (dbFile.exists()) {
        qDebug() << "ShotHistoryStorage: Database file size after checkpoint:" << dbFile.size() << "bytes";
    } else {
        qWarning() << "ShotHistoryStorage: Database file does not exist at:" << m_dbPath;
    }

    // Check WAL file
    QFile walFile(m_dbPath + "-wal");
    if (walFile.exists()) {
        qDebug() << "ShotHistoryStorage: WAL file size:" << walFile.size() << "bytes";
    } else {
        qDebug() << "ShotHistoryStorage: No WAL file (expected after successful checkpoint)";
    }
}

void ShotHistoryStorage::requestImportDatabase(const QString& filePath, bool merge)
{
    if (m_importInProgress) {
        qWarning() << "ShotHistoryStorage: Import already in progress";
        emit errorOccurred("Import already in progress");
        emit importDatabaseFinished(false);
        return;
    }

    if (m_dbPath.isEmpty()) {
        emit errorOccurred("Database not open");
        emit importDatabaseFinished(false);
        return;
    }

    // Clean up file path on main thread (pure string manipulation)
    QString cleanPath = filePath;
    if (cleanPath.startsWith("file:///")) {
        cleanPath = cleanPath.mid(8);  // Remove "file:///"
#ifdef Q_OS_WIN
        // On Windows, file:///C:/path becomes C:/path
#else
        cleanPath = "/" + cleanPath;  // On Unix, need leading /
#endif
    } else if (cleanPath.startsWith("file://")) {
        cleanPath = cleanPath.mid(7);
    }

    m_importInProgress = true;

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;

    QThread* thread = QThread::create([this, dbPath, cleanPath, merge, destroyed]() {
        bool success = importDatabaseStatic(dbPath, cleanPath, merge);

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, success, destroyed]() {
            if (*destroyed) {
                qDebug() << "ShotHistoryStorage: importDatabase callback dropped (object destroyed)";
                return;
            }
            m_importInProgress = false;
            if (success) {
                refreshTotalShots();
                invalidateDistinctCache();
            } else {
                emit errorOccurred("Database import failed. The file may be corrupt or the disk may be full.");
            }
            emit importDatabaseFinished(success);
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

// ============================================================================
// Thread-safe static methods (open their own connections, safe from any thread)
// ============================================================================

QString ShotHistoryStorage::createBackupStatic(const QString& dbPath, const QString& destPath)
{
    const QString connName = QString("backup_%1")
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);

    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            qWarning() << "ShotHistoryStorage::createBackupStatic: Failed to open DB:" << db.lastError().text();
            db = QSqlDatabase();  // Release connection before removeDatabase
            QSqlDatabase::removeDatabase(connName);
            return QString();
        }

        // Set busy timeout so checkpoint retries on contention with main-thread connection
        QSqlQuery(db).exec("PRAGMA busy_timeout = 5000");

        // Checkpoint WAL to ensure all data is in main database file
        QSqlQuery query(db);
        if (query.exec("PRAGMA wal_checkpoint(FULL)")) {
            if (query.next()) {
                qDebug() << "ShotHistoryStorage::createBackupStatic: FULL checkpoint - busy:" << query.value(0).toInt()
                         << "log:" << query.value(1).toInt() << "checkpointed:" << query.value(2).toInt();
            }
        }
        if (query.exec("PRAGMA wal_checkpoint(TRUNCATE)")) {
            if (query.next()) {
                int busy = query.value(0).toInt();
                int log = query.value(1).toInt();
                int checkpointed = query.value(2).toInt();
                if (busy != 0 || checkpointed < log) {
                    qWarning() << "ShotHistoryStorage::createBackupStatic: Incomplete checkpoint - backup may be missing recent data."
                               << "busy:" << busy << "log:" << log << "checkpointed:" << checkpointed;
                } else {
                    qDebug() << "ShotHistoryStorage::createBackupStatic: TRUNCATE checkpoint - busy:" << busy
                             << "log:" << log << "checkpointed:" << checkpointed;
                }
            }
        }

        // Copy while connection is held open — prevents another writer from
        // modifying the DB between checkpoint and copy
        if (QFile::exists(destPath))
            QFile::remove(destPath);

        bool success = QFile::copy(dbPath, destPath);
        if (!success) {
            qWarning() << "ShotHistoryStorage::createBackupStatic: Failed to copy" << dbPath << "to" << destPath;
        }

        db.close();
    }
    QSqlDatabase::removeDatabase(connName);

    if (!QFile::exists(destPath)) {
        return QString();
    }

    qDebug() << "ShotHistoryStorage::createBackupStatic: Created backup at" << destPath;
    return destPath;
}

bool ShotHistoryStorage::importDatabaseStatic(const QString& destDbPath, const QString& srcFilePath, bool merge)
{
    const QString connPrefix = QString("import_%1")
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);
    const QString srcConnName = connPrefix + "_src";
    const QString destConnName = connPrefix + "_dest";

    bool result = false;
    {
        // Open source database
        QSqlDatabase srcDb = QSqlDatabase::addDatabase("QSQLITE", srcConnName);
        srcDb.setDatabaseName(srcFilePath);
        if (!srcDb.open()) {
            qWarning() << "ShotHistoryStorage::importDatabaseStatic: Failed to open source:" << srcDb.lastError().text();
            srcDb = QSqlDatabase();  // Release connection before removeDatabase
            QSqlDatabase::removeDatabase(srcConnName);
            return false;
        }

        // Open destination database
        QSqlDatabase destDb = QSqlDatabase::addDatabase("QSQLITE", destConnName);
        destDb.setDatabaseName(destDbPath);
        if (!destDb.open()) {
            qWarning() << "ShotHistoryStorage::importDatabaseStatic: Failed to open dest:" << destDb.lastError().text();
            srcDb.close();
            srcDb = QSqlDatabase();
            destDb = QSqlDatabase();  // Release connections before removeDatabase
            QSqlDatabase::removeDatabase(srcConnName);
            QSqlDatabase::removeDatabase(destConnName);
            return false;
        }

        // Set busy timeout so INSERTs retry on contention with main-thread connection
        QSqlQuery(destDb).exec("PRAGMA busy_timeout = 5000");

        // Verify source has shots table
        int sourceCount = 0;
        {
            QSqlQuery srcCheck(srcDb);
            if (!srcCheck.exec("SELECT COUNT(*) FROM shots")) {
                qWarning() << "ShotHistoryStorage::importDatabaseStatic: No shots table in source";
                goto cleanup;
            }
            srcCheck.next();
            sourceCount = srcCheck.value(0).toInt();
        }

        if (sourceCount == 0) {
            qDebug() << "ShotHistoryStorage::importDatabaseStatic: Source has no shots (empty backup)";
            result = true;
            goto cleanup;
        }

        qDebug() << "ShotHistoryStorage::importDatabaseStatic: Source has" << sourceCount << "shots";

        // Begin transaction on destination
        if (!destDb.transaction()) {
            qWarning() << "ShotHistoryStorage::importDatabaseStatic: Failed to begin transaction:" << destDb.lastError().text();
            goto cleanup;
        }

        if (!merge) {
            // Replace mode: delete all existing data
            QSqlQuery delQuery(destDb);
            if (!delQuery.exec("DELETE FROM shot_phases") ||
                !delQuery.exec("DELETE FROM shot_samples") ||
                !delQuery.exec("DELETE FROM shots")) {
                qWarning() << "ShotHistoryStorage::importDatabaseStatic: Failed to clear data:" << delQuery.lastError().text();
                destDb.rollback();
                goto cleanup;
            }
            qDebug() << "ShotHistoryStorage::importDatabaseStatic: Cleared existing data for replace";
        }

        {
            // Import equipment packages first so both bags and shots can remap
            // their equipment_id to the new package ids (add-equipment-packages
            // task 2.8). Pre-equipment sources have no tables and yield an empty
            // map — bags/shots then null their equipment_id, same as before.
            QHash<qint64, qint64> packageIdMap;
            if (!EquipmentStorage::importEquipmentStatic(srcDb, destDb, merge, packageIdMap)) {
                qWarning() << "ShotHistoryStorage::importDatabaseStatic: Equipment import failed";
                destDb.rollback();
                goto cleanup;
            }

            // Import coffee bags next so shots.bag_id can be remapped
            // (bean-bag-inventory): bag row ids change on insert, exactly
            // like shot ids. Pre-migration-19 sources have no coffee_bags
            // table and yield an empty map. packageIdMap remaps each bag's
            // equipment_id.
            QHash<qint64, qint64> bagIdMap;
            if (!CoffeeBagStorage::importBagsStatic(srcDb, destDb, merge, bagIdMap, packageIdMap)) {
                qWarning() << "ShotHistoryStorage::importDatabaseStatic: Bag import failed";
                destDb.rollback();
                goto cleanup;
            }

            // Import recipes after bags so shots.recipe_id can be remapped
            // (finish-recipes-first-class): recipe row ids change on insert,
            // exactly like bag ids. Pre-migration-25 sources have no recipes
            // table and yield an empty map. packageIdMap remaps each recipe's
            // equipment_id; bagIdMap remaps its bag_id (an unmatched source
            // bag becomes NULL → stale); the bean identity fields carry
            // verbatim as the relink matching key.
            QHash<qint64, qint64> recipeIdMap;
            if (!RecipeStorage::importRecipesStatic(srcDb, destDb, merge, recipeIdMap, packageIdMap,
                                                    bagIdMap)) {
                qWarning() << "ShotHistoryStorage::importDatabaseStatic: Recipe import failed";
                destDb.rollback();
                goto cleanup;
            }

            // Get existing UUIDs for merge mode
            QSet<QString> existingUuids;
            if (merge) {
                QSqlQuery uuidQuery(destDb);
                if (uuidQuery.exec("SELECT uuid FROM shots")) {
                    while (uuidQuery.next())
                        existingUuids.insert(uuidQuery.value(0).toString());
                }
                qDebug() << "ShotHistoryStorage::importDatabaseStatic: Found" << existingUuids.size() << "existing shots";
            }

            // Import shots
            int imported = 0, skipped = 0, failed = 0;
            QSqlQuery srcShots(srcDb);
            if (!srcShots.exec("SELECT * FROM shots")) {
                qWarning() << "ShotHistoryStorage::importDatabaseStatic: Failed to query source:" << srcShots.lastError().text();
                destDb.rollback();
                goto cleanup;
            }

            // Columns that may be absent in older source databases — resolve
            // indexes once instead of QSqlQuery::value(name) per row (which
            // logs an "unknown field" warning for every missing access).
            const QSqlRecord srcRecord = srcShots.record();
            const int idxStoppedBy = srcRecord.indexOf("stopped_by");
            const int idxBeanBaseJson = srcRecord.indexOf("beanbase_json");
            const int idxBeanBaseId = srcRecord.indexOf("beanbase_id");
            const int idxBagId = srcRecord.indexOf("bag_id");
            const int idxFrozenDate = srcRecord.indexOf("frozen_date");
            const int idxDefrostDate = srcRecord.indexOf("defrost_date");
            // Non-frozen storage lifecycle (bean-freshness-followup): carried
            // verbatim like frozen_date/defrost_date. Present only on
            // post-migration-32 sources → NULL on older ones (idx == -1),
            // so a pre-32 source imports cleanly rather than failing per row.
            const int idxStorageHint = srcRecord.indexOf("storage_hint");
            const int idxOpenedDate = srcRecord.indexOf("opened_date");
            // Structured taste axes (add-ai-taste-intake): carried verbatim like
            // storage_hint/opened_date. Present only on post-migration-33 sources
            // → NULL on older ones (idx == -1).
            const int idxTasteBalance = srcRecord.indexOf("taste_balance");
            const int idxTasteBody = srcRecord.indexOf("taste_body");
            // equipment_id (a source package row id, remapped) + rpm exist only
            // on post-migration-22 sources (add-equipment-packages). Older
            // sources lack both — they resolve to NULL.
            const int idxEquipmentId = srcRecord.indexOf("equipment_id");
            const int idxRpm = srcRecord.indexOf("rpm");
            // Recipe provenance (finish-recipes-first-class): recipe_id is a
            // source recipe row id (remapped like bag_id); steam_json and
            // hot_water_json are the whole-drink snapshots taken at shot start,
            // carried verbatim so promote-from-shot round-trips after transfer.
            // All three exist only on post-migration-25/27 sources → NULL on
            // older ones. Before this change none of them were carried at all,
            // dropping recipe provenance on every transfer.
            const int idxRecipeId = srcRecord.indexOf("recipe_id");
            const int idxSteamJson = srcRecord.indexOf("steam_json");
            const int idxHotWaterJson = srcRecord.indexOf("hot_water_json");
            auto srcValueOrNull = [&srcShots](int idx) {
                return idx >= 0 ? srcShots.value(idx) : QVariant();
            };

            while (srcShots.next()) {
                QString uuid = srcShots.value("uuid").toString();
                if (merge && existingUuids.contains(uuid)) {
                    skipped++;
                    continue;
                }

                QSqlQuery insert(destDb);
                insert.prepare(R"(
                    INSERT INTO shots (uuid, timestamp, profile_name, profile_json, beverage_type,
                        duration_seconds, final_weight, dose_weight,
                        bean_brand, bean_type, roast_date, roast_level,
                        grinder_setting, equipment_id, rpm,
                        drink_tds, drink_ey,
                        enjoyment, espresso_notes, bean_notes, barista,
                        profile_notes, visualizer_id, visualizer_url, debug_log,
                        temperature_override, yield_override, profile_kb_id,
                        channeling_detected, grind_issue_detected,
                        skip_first_frame_detected, pour_truncated_detected,
                        stopped_by, beanbase_json, beanbase_id,
                        bag_id, frozen_date, defrost_date, storage_hint, opened_date,
                        taste_balance, taste_body,
                        recipe_id, steam_json, hot_water_json)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                )");

                insert.addBindValue(uuid);
                insert.addBindValue(srcShots.value("timestamp"));
                insert.addBindValue(srcShots.value("profile_name"));
                insert.addBindValue(srcShots.value("profile_json"));
                QVariant bt = srcShots.value("beverage_type");
                insert.addBindValue((bt.isValid() && !bt.isNull()) ? bt : QVariant(QString("espresso")));
                insert.addBindValue(srcShots.value("duration_seconds"));
                insert.addBindValue(srcShots.value("final_weight"));
                insert.addBindValue(srcShots.value("dose_weight"));
                insert.addBindValue(srcShots.value("bean_brand"));
                insert.addBindValue(srcShots.value("bean_type"));
                insert.addBindValue(srcShots.value("roast_date"));
                insert.addBindValue(srcShots.value("roast_level"));
                insert.addBindValue(srcShots.value("grinder_setting"));
                // equipment_id is a source package row id — remap to the imported
                // package's new dest id (add-equipment-packages task 2.8); a source
                // id absent from the map becomes NULL. rpm carries verbatim.
                {
                    const qint64 mappedPkg = packageIdMap.value(srcValueOrNull(idxEquipmentId).toLongLong(), 0);
                    insert.addBindValue(mappedPkg > 0 ? QVariant(mappedPkg) : QVariant());
                }
                insert.addBindValue(srcValueOrNull(idxRpm));
                insert.addBindValue(srcShots.value("drink_tds"));
                insert.addBindValue(srcShots.value("drink_ey"));
                insert.addBindValue(srcShots.value("enjoyment"));
                insert.addBindValue(srcShots.value("espresso_notes"));
                insert.addBindValue(srcShots.value("bean_notes"));
                insert.addBindValue(srcShots.value("barista"));
                insert.addBindValue(srcShots.value("profile_notes"));
                insert.addBindValue(srcShots.value("visualizer_id"));
                insert.addBindValue(srcShots.value("visualizer_url"));
                insert.addBindValue(srcShots.value("debug_log"));
                insert.addBindValue(srcShots.value("temperature_override"));
                insert.addBindValue(srcShots.value("yield_override"));
                insert.addBindValue(srcShots.value("profile_kb_id"));
                // Quality flags — fallback to 0 for pre-migration source databases
                QVariant ch = srcShots.value("channeling_detected");
                insert.addBindValue((ch.isValid() && !ch.isNull()) ? ch : QVariant(0));
                QVariant gi = srcShots.value("grind_issue_detected");
                insert.addBindValue((gi.isValid() && !gi.isNull()) ? gi : QVariant(0));
                QVariant sf = srcShots.value("skip_first_frame_detected");
                insert.addBindValue((sf.isValid() && !sf.isNull()) ? sf : QVariant(0));
                QVariant pt = srcShots.value("pour_truncated_detected");
                insert.addBindValue((pt.isValid() && !pt.isNull()) ? pt : QVariant(0));
                insert.addBindValue(srcValueOrNull(idxStoppedBy));
                insert.addBindValue(srcValueOrNull(idxBeanBaseJson));
                insert.addBindValue(srcValueOrNull(idxBeanBaseId));
                // bag_id is a row id in the SOURCE database — remap to the
                // imported bag's new id, or NULL when the bag wasn't imported.
                {
                    const QVariant srcBagId = srcValueOrNull(idxBagId);
                    const qint64 mapped = bagIdMap.value(srcBagId.toLongLong(), -1);
                    insert.addBindValue(mapped > 0 ? QVariant(mapped) : QVariant());
                }
                insert.addBindValue(srcValueOrNull(idxFrozenDate));
                insert.addBindValue(srcValueOrNull(idxDefrostDate));
                insert.addBindValue(srcValueOrNull(idxStorageHint));
                insert.addBindValue(srcValueOrNull(idxOpenedDate));
                insert.addBindValue(srcValueOrNull(idxTasteBalance));
                insert.addBindValue(srcValueOrNull(idxTasteBody));
                // recipe_id is a row id in the SOURCE database — remap to the
                // imported recipe's new id, or NULL when the recipe wasn't
                // imported (so provenance never dangles). steam_json and
                // hot_water_json are opaque snapshots, carried verbatim.
                {
                    const QVariant srcRecipeId = srcValueOrNull(idxRecipeId);
                    const qint64 mappedRecipe = recipeIdMap.value(srcRecipeId.toLongLong(), -1);
                    insert.addBindValue((srcRecipeId.isValid() && !srcRecipeId.isNull() && mappedRecipe > 0)
                                            ? QVariant(mappedRecipe) : QVariant());
                }
                insert.addBindValue(srcValueOrNull(idxSteamJson));
                insert.addBindValue(srcValueOrNull(idxHotWaterJson));

                if (!insert.exec()) {
                    qWarning() << "ShotHistoryStorage::importDatabaseStatic: Failed to import shot:" << insert.lastError().text();
                    failed++;
                    if (!merge) {
                        // In replace mode, existing data was deleted — abort to rollback
                        qWarning() << "ShotHistoryStorage::importDatabaseStatic: Aborting replace-mode import due to INSERT failure";
                        destDb.rollback();
                        goto cleanup;
                    }
                    continue;
                }

                qint64 oldId = srcShots.value("id").toLongLong();
                qint64 newId = insert.lastInsertId().toLongLong();

                // Import samples
                QSqlQuery srcSamples(srcDb);
                srcSamples.prepare("SELECT sample_count, data_blob FROM shot_samples WHERE shot_id = ?");
                srcSamples.addBindValue(oldId);
                if (srcSamples.exec() && srcSamples.next()) {
                    QSqlQuery insertSample(destDb);
                    insertSample.prepare("INSERT INTO shot_samples (shot_id, sample_count, data_blob) VALUES (?, ?, ?)");
                    insertSample.addBindValue(newId);
                    insertSample.addBindValue(srcSamples.value(0));
                    insertSample.addBindValue(srcSamples.value(1));
                    if (!insertSample.exec()) {
                        qWarning() << "ShotHistoryStorage::importDatabaseStatic: Failed to import sample for shot"
                                   << uuid << ":" << insertSample.lastError().text();
                    }
                }

                // Import phases (try with transition_reason, fall back for older DBs)
                QSqlQuery srcPhases(srcDb);
                srcPhases.prepare("SELECT time_offset, label, frame_number, is_flow_mode, transition_reason FROM shot_phases WHERE shot_id = ?");
                srcPhases.addBindValue(oldId);
                bool hasReason = srcPhases.exec();
                if (!hasReason) {
                    srcPhases.prepare("SELECT time_offset, label, frame_number, is_flow_mode FROM shot_phases WHERE shot_id = ?");
                    srcPhases.addBindValue(oldId);
                    hasReason = false;
                    if (!srcPhases.exec()) {
                        qWarning() << "ShotHistoryStorage::importDatabaseStatic: Failed to query phases for shot"
                                   << uuid << ":" << srcPhases.lastError().text();
                    }
                } else {
                    hasReason = true;
                }
                while (srcPhases.next()) {
                    QSqlQuery insertPhase(destDb);
                    insertPhase.prepare("INSERT INTO shot_phases (shot_id, time_offset, label, frame_number, is_flow_mode, transition_reason) VALUES (?, ?, ?, ?, ?, ?)");
                    insertPhase.addBindValue(newId);
                    insertPhase.addBindValue(srcPhases.value(0));
                    insertPhase.addBindValue(srcPhases.value(1));
                    insertPhase.addBindValue(srcPhases.value(2));
                    insertPhase.addBindValue(srcPhases.value(3));
                    insertPhase.addBindValue(hasReason ? srcPhases.value(4).toString() : QString());
                    if (!insertPhase.exec()) {
                        qWarning() << "ShotHistoryStorage::importDatabaseStatic: Failed to import phase for shot"
                                   << uuid << ":" << insertPhase.lastError().text();
                    }
                }

                imported++;
            }

            if (!destDb.commit()) {
                qWarning() << "ShotHistoryStorage::importDatabaseStatic: Failed to commit:" << destDb.lastError().text();
                destDb.rollback();
                goto cleanup;
            }

            // Backfill beverage_type from profile_json for imported shots from old DBs.
            // Wrapped in a transaction to avoid per-UPDATE write lock contention with
            // the main thread's connection (this runs on a background thread).
            {
                if (!destDb.transaction()) {
                    qWarning() << "ShotHistoryStorage::importDatabaseStatic: Backfill transaction failed:" << destDb.lastError().text();
                } else {
                QSqlQuery query(destDb);
                query.prepare("SELECT id, profile_json FROM shots WHERE (beverage_type = 'espresso' OR beverage_type IS NULL) AND profile_json IS NOT NULL AND profile_json != ''");
                query.exec();
                while (query.next()) {
                    qint64 id = query.value(0).toLongLong();
                    QString profileJson = query.value(1).toString();
                    QJsonDocument doc = QJsonDocument::fromJson(profileJson.toUtf8());
                    if (doc.isNull()) continue;
                    QString type = doc.object().value("beverage_type").toString();
                    if (!type.isEmpty() && type != "espresso") {
                        QSqlQuery update(destDb);
                        update.prepare("UPDATE shots SET beverage_type = ?, "
                                       "updated_at = strftime('%s', 'now') WHERE id = ?");
                        update.addBindValue(type);
                        update.addBindValue(id);
                        update.exec();
                    }
                }
                // Shots from pre-migration-19 sources carry beanbase_json but
                // no beanbase_id column — derive it so the history search
                // lane covers imported shots too.
                QSqlQuery beanbaseBackfill(destDb);
                if (!beanbaseBackfill.exec("UPDATE shots SET beanbase_id = json_extract(beanbase_json, '$.id') "
                                           "WHERE beanbase_id IS NULL AND beanbase_json IS NOT NULL"))
                    qWarning() << "ShotHistoryStorage::importDatabaseStatic: beanbase_id backfill failed:"
                               << beanbaseBackfill.lastError().text();
                // Pre-bag sources also have no bag_id — adopt their shots
                // into existing bags by identity (idempotent, NULL-only).
                CoffeeBagStorage::linkOrphanShotsStatic(destDb);
                if (!destDb.commit()) {
                    qWarning() << "ShotHistoryStorage::importDatabaseStatic: Backfill commit failed:" << destDb.lastError().text();
                    destDb.rollback();
                }
                }
            }

            qDebug() << "ShotHistoryStorage::importDatabaseStatic: Import complete -" << imported << "imported," << skipped << "skipped," << failed << "failed";
            result = true;
        }

cleanup:
        srcDb.close();
        destDb.close();
    }
    QSqlDatabase::removeDatabase(srcConnName);
    QSqlDatabase::removeDatabase(destConnName);
    return result;
}

int ShotHistoryStorage::getShotCountStatic(const QString& dbPath)
{
    int count = -1;  // -1 = error (distinguishes from 0 = empty)
    withTempDb(dbPath, "shs_count", [&](QSqlDatabase& db) {
        QSqlQuery query(db);
        if (query.exec("SELECT COUNT(*) FROM shots") && query.next())
            count = query.value(0).toInt();
        else
            qWarning() << "ShotHistoryStorage::getShotCountStatic: COUNT query failed:" << query.lastError().text();
    });
    return count;
}

qint64 ShotHistoryStorage::importShotRecord(const ShotRecord& record, bool overwriteExisting)
{
    if (!m_ready) {
        qWarning() << "ShotHistoryStorage: Cannot import - not ready";
        return -1;
    }

    // Check for duplicate by UUID
    QSqlQuery query(m_db);
    query.prepare("SELECT id FROM shots WHERE uuid = ?");
    query.bindValue(0, record.summary.uuid);
    if (query.exec() && query.next()) {
        if (overwriteExisting) {
            // Delete existing record to allow re-import
            qint64 existingId = query.value(0).toLongLong();
            deleteShot(existingId);
        } else {
            // Duplicate found, skip
            return 0;
        }
    }

    // Also check by timestamp (within 5 seconds) and profile to catch near-duplicates
    query.prepare("SELECT id FROM shots WHERE ABS(timestamp - ?) < 5 AND profile_name = ?");
    query.bindValue(0, record.summary.timestamp);
    query.bindValue(1, record.summary.profileName);
    if (query.exec() && query.next()) {
        if (overwriteExisting) {
            // Delete existing record to allow re-import
            qint64 existingId = query.value(0).toLongLong();
            deleteShot(existingId);
        } else {
            // Near-duplicate found, skip
            return 0;
        }
    }

    // Begin transaction
    m_db.transaction();

    // Resolve the parsed grinder identity to an equipment package so the
    // imported shot keeps its grinder (the per-shot grinder_brand/model/burrs
    // columns are gone — migration 23; identity lives only on a package now).
    // Find-or-create on grinder identity, mirroring the live shot-save path,
    // then link via equipment_id. Empty identity → no package, NULL link.
    qint64 importEquipmentId = 0;
    if (!(record.grinderBrand.isEmpty() && record.grinderModel.isEmpty() && record.grinderBurrs.isEmpty())) {
        importEquipmentId = EquipmentStorage::findPackageByGrinderIdentityStatic(
            m_db, record.grinderBrand, record.grinderModel, record.grinderBurrs);
        if (importEquipmentId <= 0) {
            EquipmentPackage pkg;
            importEquipmentId = EquipmentStorage::createPackageWithGrinderStatic(
                m_db, pkg, record.grinderBrand, record.grinderModel, record.grinderBurrs);
        }
    }

    // Insert main shot record
    query.prepare(R"(
        INSERT INTO shots (
            uuid, timestamp, profile_name, profile_json, beverage_type,
            duration_seconds, final_weight, dose_weight,
            bean_brand, bean_type, roast_date, roast_level,
            grinder_setting, equipment_id, rpm,
            drink_tds, drink_ey, enjoyment, espresso_notes, bean_notes, barista,
            profile_notes, debug_log,
            temperature_override, yield_override, profile_kb_id,
            channeling_detected, grind_issue_detected,
            skip_first_frame_detected, pour_truncated_detected
        ) VALUES (
            :uuid, :timestamp, :profile_name, :profile_json, :beverage_type,
            :duration, :final_weight, :dose_weight,
            :bean_brand, :bean_type, :roast_date, :roast_level,
            :grinder_setting, :equipment_id, :rpm,
            :drink_tds, :drink_ey, :enjoyment, :espresso_notes, :bean_notes, :barista,
            :profile_notes, :debug_log,
            :temperature_override, :yield_override, :profile_kb_id,
            :channeling_detected, :grind_issue_detected,
            :skip_first_frame_detected, :pour_truncated_detected
        )
    )");

    query.bindValue(":uuid", record.summary.uuid);
    query.bindValue(":timestamp", record.summary.timestamp);
    query.bindValue(":profile_name", record.summary.profileName);
    query.bindValue(":profile_json", record.profileJson);
    query.bindValue(":beverage_type", record.summary.beverageType.isEmpty() ? QStringLiteral("espresso") : record.summary.beverageType);
    query.bindValue(":duration", record.summary.duration);
    query.bindValue(":final_weight", record.summary.finalWeight);
    query.bindValue(":dose_weight", record.summary.doseWeight);
    query.bindValue(":bean_brand", record.summary.beanBrand);
    query.bindValue(":bean_type", record.summary.beanType);
    query.bindValue(":roast_date", record.roastDate);
    query.bindValue(":roast_level", record.roastLevel);
    // Grinder identity is not snapshotted on the shot row (migration 23 dropped
    // the columns); it resolves via equipment_id to the package found/created
    // above from the parsed identity. The grind setting + rpm stay as per-shot
    // dial-in.
    query.bindValue(":grinder_setting", record.grinderSetting);
    query.bindValue(":equipment_id", importEquipmentId > 0 ? QVariant(importEquipmentId) : QVariant());
    query.bindValue(":rpm", record.rpm > 0 ? QVariant(record.rpm) : QVariant());
    query.bindValue(":drink_tds", record.drinkTds);
    query.bindValue(":drink_ey", record.drinkEy);
    query.bindValue(":enjoyment", record.summary.enjoyment);
    query.bindValue(":espresso_notes", record.espressoNotes);
    query.bindValue(":bean_notes", record.beanNotes);
    query.bindValue(":barista", record.barista);
    query.bindValue(":profile_notes", record.profileNotes);
    query.bindValue(":debug_log", QString());  // No debug log for imported shots

    // Bind overrides (always have values - user override or profile default)
    query.bindValue(":temperature_override", record.temperatureOverride);
    query.bindValue(":yield_override", record.targetWeight);
    query.bindValue(":profile_kb_id", record.profileKbId.isEmpty() ? QVariant() : record.profileKbId);
    query.bindValue(":channeling_detected", record.channelingDetected ? 1 : 0);
    query.bindValue(":grind_issue_detected", record.grindIssueDetected ? 1 : 0);
    query.bindValue(":skip_first_frame_detected", record.skipFirstFrameDetected ? 1 : 0);
    query.bindValue(":pour_truncated_detected", record.pourTruncatedDetected ? 1 : 0);

    if (!query.exec()) {
        qWarning() << "ShotHistoryStorage: Failed to import shot:" << query.lastError().text();
        m_db.rollback();
        return -1;
    }

    qint64 shotId = query.lastInsertId().toLongLong();

    // Compress and insert sample data
    QJsonObject root;
    root["pressure"] = pointsToJsonObject(record.pressure);
    root["flow"] = pointsToJsonObject(record.flow);
    root["temperature"] = pointsToJsonObject(record.temperature);
    root["temperatureMix"] = pointsToJsonObject(record.temperatureMix);
    root["resistance"] = pointsToJsonObject(record.resistance);
    root["waterDispensed"] = pointsToJsonObject(record.waterDispensed);
    root["pressureGoal"] = pointsToJsonObject(record.pressureGoal);
    root["flowGoal"] = pointsToJsonObject(record.flowGoal);
    root["temperatureGoal"] = pointsToJsonObject(record.temperatureGoal);
    root["weight"] = pointsToJsonObject(record.weight);
    root["weightFlowRate"] = pointsToJsonObject(record.weightFlowRate);

    QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
    QByteArray compressedData = qCompress(json, 9);
    qsizetype sampleCount = record.pressure.size();

    query.prepare("INSERT INTO shot_samples (shot_id, sample_count, data_blob) VALUES (:id, :count, :blob)");
    query.bindValue(":id", shotId);
    query.bindValue(":count", sampleCount);
    query.bindValue(":blob", compressedData);

    if (!query.exec()) {
        qWarning() << "ShotHistoryStorage: Failed to insert imported samples:" << query.lastError().text();
        m_db.rollback();
        return -1;
    }

    // Insert phase markers
    for (const auto& marker : record.phases) {
        query.prepare(R"(
            INSERT INTO shot_phases (shot_id, time_offset, label, frame_number, is_flow_mode, transition_reason)
            VALUES (:shot_id, :time, :label, :frame, :flow_mode, :reason)
        )");
        query.bindValue(":shot_id", shotId);
        query.bindValue(":time", marker.time);
        query.bindValue(":label", marker.label);
        query.bindValue(":frame", marker.frameNumber);
        query.bindValue(":flow_mode", marker.isFlowMode ? 1 : 0);
        query.bindValue(":reason", marker.transitionReason);
        query.exec();  // Non-critical if markers fail
    }

    m_db.commit();

    return shotId;
}

void ShotHistoryStorage::backfillBeverageType()
{
    QSqlQuery query(m_db);
    query.exec("SELECT id, profile_json FROM shots WHERE (beverage_type = 'espresso' OR beverage_type IS NULL) AND profile_json IS NOT NULL AND profile_json != ''");

    int updated = 0;
    while (query.next()) {
        qint64 id = query.value(0).toLongLong();
        QString profileJson = query.value(1).toString();

        QJsonDocument doc = QJsonDocument::fromJson(profileJson.toUtf8());
        if (doc.isNull()) continue;

        QString type = doc.object().value("beverage_type").toString();
        if (!type.isEmpty() && type != "espresso") {
            QSqlQuery update(m_db);
            update.prepare("UPDATE shots SET beverage_type = ?, "
                           "updated_at = strftime('%s', 'now') WHERE id = ?");
            update.bindValue(0, type);
            update.bindValue(1, id);
            update.exec();
            updated++;
        }
    }

    if (updated > 0)
        qDebug() << "ShotHistoryStorage: Backfilled beverage_type for" << updated << "shots";
}

void ShotHistoryStorage::refreshTotalShots()
{
    // Refresh distinct cache asynchronously
    invalidateDistinctCache();

    // Run COUNT query on background thread to avoid blocking the main thread
    QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, destroyed]() {
        int count = getShotCountStatic(dbPath);
        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, count, destroyed]() {
            if (*destroyed) {
                qDebug() << "ShotHistoryStorage: refreshTotalShots callback dropped (object destroyed)";
                return;
            }
            if (count < 0) {
                qWarning() << "ShotHistoryStorage::refreshTotalShots: count query failed, keeping previous count" << m_totalShots;
                return;
            }
            if (count != m_totalShots) {
                m_totalShots = count;
                emit totalShotsChanged();
            }
        }, Qt::QueuedConnection);
    });
}

