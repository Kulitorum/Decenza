#include "shothistorystorage.h"
#include "shothistorystorage_internal.h"
#include "ai/conductance.h"
#include "ai/shotanalysis.h"
#include "ai/shotsummarizer.h"
#include "history/shotbadgeprojection.h"
#include "core/grinderaliases.h"
#include "models/shotdatamodel.h"
#include "profile/profile.h"
#include "network/visualizeruploader.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QLocale>
#include <QDebug>
#include <QThread>
#include <algorithm>
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

ShotHistoryStorage::ShotHistoryStorage(QObject* parent)
    : QObject(parent)
{
}

ShotHistoryStorage::~ShotHistoryStorage()
{
    *m_destroyed = true;
    close();
}

void ShotHistoryStorage::close()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
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

    // Remove existing connection if any
    if (QSqlDatabase::contains(DB_CONNECTION_NAME)) {
        QSqlDatabase::removeDatabase(DB_CONNECTION_NAME);
    }

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
    query.exec("CREATE INDEX IF NOT EXISTS idx_shots_grinder ON shots(grinder_brand, grinder_model)");
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
    // (puck offered no resistance — channeling/temp/grind detectors stay silent
    // or fire wrong because the curves they read off never built). When this
    // flag is true the other three quality flags stay false because
    // ShotAnalysis::analyzeShot's suppression cascade skips the
    // channeling/temp/grind blocks, leaving those DetectorResults fields at
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

    // Migration 14: enjoyment_source column. Tracks whether
    // enjoyment0to100 came from the user (manual editor, QuickRatingRow,
    // conversational reply) or was inferred by the post-shot detector
    // pipeline. Lets bestRecentShot prefer user-rated candidates and
    // lets the system prompt teach the LLM to treat inferred ratings
    // as hints rather than ground truth (issue #1055 Layer 3).
    if (currentVersion < 14) {
        qDebug() << "ShotHistoryStorage: Running migration to version 14 (enjoyment_source)";

        if (!hasColumn("shots", "enjoyment_source")) {
            query.exec("ALTER TABLE shots ADD COLUMN enjoyment_source TEXT NOT NULL DEFAULT 'none'");
            // Back-fill: existing rated rows are user-rated by definition
            // (the column is new; only the manual editor / QuickRatingRow
            // ever wrote those values).
            query.exec("UPDATE shots SET enjoyment_source = 'user' WHERE enjoyment > 0");
        }

        query.exec("DELETE FROM schema_version");
        query.exec("INSERT INTO schema_version (version) VALUES (14)");
        currentVersion = 14;
    }

    m_schemaVersion = currentVersion;
    return true;
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
                                     double targetWeight)
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
    data.beanBrand = metadata.beanBrand;
    data.beanType = metadata.beanType;
    data.roastDate = metadata.roastDate;
    data.roastLevel = metadata.roastLevel;
    data.grinderBrand = metadata.grinderBrand;
    data.grinderModel = metadata.grinderModel;
    data.grinderBurrs = metadata.grinderBurrs;
    data.grinderSetting = metadata.grinderSetting;
    data.drinkTds = metadata.drinkTds;
    data.drinkEy = metadata.drinkEy;
    data.espressoEnjoyment = metadata.espressoEnjoyment;
    data.espressoNotes = metadata.espressoNotes;
    data.barista = metadata.barista;
    data.profileNotes = profile ? profile->profileNotes() : QString();
    data.debugLog = debugLog;

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

        // Compute all five quality badges via a single ShotAnalysis::analyzeShot
        // pass and project the booleans from DetectorResults using the
        // documented mapping. This unifies the save-time, load-time, and
        // dialog/AI/MCP cascades on one pipeline — the cascade lives in exactly
        // one place (analyzeShot's body). See docs/SHOT_REVIEW.md §4 for the
        // full mapping table and decenza::deriveBadgesFromAnalysis (in
        // history/shotbadgeprojection.h) for the projection rules.
        const AnalysisInputs inputs = prepareAnalysisInputs(data.profileKbId, data.profileJson);
        const auto analysis = ShotAnalysis::analyzeShot(
            tmpRecord.pressure, shotData->flowData(),
            shotData->cumulativeWeightData(),
            shotData->temperatureData(), shotData->temperatureGoalData(),
            shotData->conductanceDerivativeData(),
            tmpRecord.phases, data.beverageType, duration,
            shotData->pressureGoalData(), shotData->flowGoalData(),
            inputs.analysisFlags, inputs.firstFrameSeconds,
            data.targetWeight, data.finalWeight,
            inputs.frameCount);
        decenza::applyBadgesToTarget(data, analysis.detectors);

        // Issue #1055 Layer 3: inferred-good auto-rating. When the user
        // didn't manually rate this shot AND the deterministic detectors
        // all fire clean, provisionally set enjoyment to 75 with source
        // "inferred". The bestRecentShot block can then anchor on the
        // shot until the user provides their own rating (which flips
        // enjoymentSource back to "user" via updateShotMetadataStatic).
        // The system prompt teaches the LLM to treat inferred ratings
        // as a hint requiring user confirmation.
        if (data.espressoEnjoyment <= 0) {
            const auto& d = analysis.detectors;
            const bool clean = d.verdictCategory == QStringLiteral("clean")
                            && d.channelingSeverity == QStringLiteral("none")
                            && d.grindDirection == QStringLiteral("onTarget");
            if (clean) {
                constexpr int kInferredScore = 75;
                data.espressoEnjoyment = kInferredScore;
                data.enjoymentSource = QStringLiteral("inferred");
                qDebug() << "ShotHistoryStorage: inferred-good auto-rating —"
                         << "shot saved with enjoyment" << kInferredScore
                         << "(clean+onTarget+no-channeling)";
            }
        } else if (data.enjoymentSource.isEmpty() ||
                   data.enjoymentSource == QStringLiteral("none")) {
            // The save path carries an explicit user-set enjoyment;
            // record the source as "user" so future filtering / queries
            // can distinguish from inferred ratings.
            data.enjoymentSource = QStringLiteral("user");
        }
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
    QThread* thread = QThread::create([this, dbPath, data = std::move(data), destroyed]() {
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

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();

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
        // Use do-while(false) so error paths 'break' out while db/query are still
        // in scope.
        do {
            if (!db.transaction()) {
                qWarning() << "ShotHistoryStorage: Failed to start transaction:" << db.lastError().text();
                break;
            }

            QSqlQuery query(db);
            query.prepare(R"(
                INSERT INTO shots (
                    uuid, timestamp, profile_name, profile_json, beverage_type,
                    duration_seconds, final_weight, dose_weight,
                    bean_brand, bean_type, roast_date, roast_level,
                    grinder_brand, grinder_model, grinder_burrs, grinder_setting,
                    drink_tds, drink_ey, enjoyment, espresso_notes, bean_notes, barista,
                    profile_notes, debug_log,
                    temperature_override, yield_override, profile_kb_id,
                    channeling_detected, temperature_unstable, grind_issue_detected,
                    skip_first_frame_detected, pour_truncated_detected, enjoyment_source
                ) VALUES (
                    :uuid, :timestamp, :profile_name, :profile_json, :beverage_type,
                    :duration, :final_weight, :dose_weight,
                    :bean_brand, :bean_type, :roast_date, :roast_level,
                    :grinder_brand, :grinder_model, :grinder_burrs, :grinder_setting,
                    :drink_tds, :drink_ey, :enjoyment, :espresso_notes, :bean_notes, :barista,
                    :profile_notes, :debug_log,
                    :temperature_override, :yield_override, :profile_kb_id,
                    :channeling_detected, :temperature_unstable, :grind_issue_detected,
                    :skip_first_frame_detected, :pour_truncated_detected, :enjoyment_source
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
            query.bindValue(":grinder_brand", data.grinderBrand);
            query.bindValue(":grinder_model", data.grinderModel);
            query.bindValue(":grinder_burrs", data.grinderBurrs);
            query.bindValue(":grinder_setting", data.grinderSetting);
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
            query.bindValue(":temperature_unstable", data.temperatureUnstable ? 1 : 0);
            query.bindValue(":grind_issue_detected", data.grindIssueDetected ? 1 : 0);
            query.bindValue(":skip_first_frame_detected", data.skipFirstFrameDetected ? 1 : 0);
            query.bindValue(":pour_truncated_detected", data.pourTruncatedDetected ? 1 : 0);
            // Issue #1055 Layer 3: enjoymentSource defaults to "user"
            // when the save path carries an explicit enjoyment value
            // (manual save flow), and to "inferred" / "none" otherwise
            // depending on whether the inferred-good evaluator fired.
            // The caller (saveShot pre-write hook) is responsible for
            // setting data.enjoymentSource appropriately.
            query.bindValue(":enjoyment_source",
                data.enjoymentSource.isEmpty() ? QStringLiteral("none") : data.enjoymentSource);

            if (!query.exec()) {
                qWarning() << "ShotHistoryStorage: Failed to insert shot:" << query.lastError().text();
                db.rollback();
                break;
            }

            shotId = query.lastInsertId().toLongLong();

            // Insert compressed sample data
            query.prepare("INSERT INTO shot_samples (shot_id, sample_count, data_blob) VALUES (:id, :count, :blob)");
            query.bindValue(":id", shotId);
            query.bindValue(":count", data.sampleCount);
            query.bindValue(":blob", data.compressedSamples);

            if (!query.exec()) {
                qWarning() << "ShotHistoryStorage: Failed to insert samples:" << query.lastError().text();
                db.rollback();
                shotId = -1;
                break;
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

            db.commit();

            // Checkpoint WAL
            QSqlQuery walQuery(db);
            walQuery.exec("PRAGMA wal_checkpoint(PASSIVE)");
        } while (false);
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
    QThread* thread = QThread::create([this, dbPath, shotId, visualizerId, visualizerUrl, destroyed]() {
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
    thread->start();
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
}

void ShotHistoryStorage::requestMostRecentShotId()
{
    if (!m_ready) {
        emit mostRecentShotIdReady(-1);
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    QThread* thread = QThread::create([this, dbPath, destroyed]() {
        qint64 shotId = -1;
        bool opened = withTempDb(dbPath, "shs_recent", [&](QSqlDatabase& db) {
            QSqlQuery query(db);
            if (query.exec("SELECT id FROM shots ORDER BY timestamp DESC LIMIT 1") && query.next())
                shotId = query.value(0).toLongLong();
        });
        if (!opened)
            qWarning() << "ShotHistoryStorage: requestMostRecentShotId failed - could not open DB";

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, shotId, destroyed]() {
            if (*destroyed) return;
            emit mostRecentShotIdReady(shotId);
        }, Qt::QueuedConnection);
    });
    thread->start();
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
}

void ShotHistoryStorage::requestShot(qint64 shotId)
{
    if (!m_ready) {
        emit shotReady(shotId, ShotProjection());
        return;
    }

    const QString dbPath = m_dbPath;

    auto destroyed = m_destroyed;
    QThread* thread = QThread::create([this, dbPath, shotId, destroyed]() {
        ShotRecord record;
        bool badgesPersisted = false;
        withTempDb(dbPath, "shs_shot", [&](QSqlDatabase& db) {
            record = loadShotRecordStatic(db, shotId, &badgesPersisted);
        });

        // Convert to QVariantMap on main thread (touches QML-visible data).
        // shotReady carries the recomputed badges already; shotBadgesUpdated
        // fires only when the load actually rewrote the stored columns, so
        // listeners that care about "this shot just got its badges corrected"
        // (e.g., a future history-list filter that wants to refresh) get a
        // signal without having to re-query.
        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, shotId, record = std::move(record), badgesPersisted, destroyed]() {
            if (*destroyed) {
                qDebug() << "ShotHistoryStorage: requestShot callback dropped (object destroyed)";
                return;
            }
            emit shotReady(shotId, convertShotRecord(record));
            if (badgesPersisted) {
                emit shotBadgesUpdated(shotId,
                    record.channelingDetected,
                    record.temperatureUnstable,
                    record.grindIssueDetected,
                    record.skipFirstFrameDetected,
                    record.pourTruncatedDetected);
            }
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
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
    QThread* thread = QThread::create([this, dbPath, shotId, destroyed]() {
        bool recordFound = false;
        bool badgesPersisted = false;
        bool newChanneling = false, newTempUnstable = false;
        bool newGrindIssue = false, newSkipFirstFrame = false, newPourTruncated = false;

        withTempDb(dbPath, "shs_badges", [&](QSqlDatabase& db) {
            ShotRecord record = loadShotRecordStatic(db, shotId, &badgesPersisted);
            if (record.summary.id == 0) return;
            recordFound = true;
            newChanneling = record.channelingDetected;
            newTempUnstable = record.temperatureUnstable;
            newGrindIssue = record.grindIssueDetected;
            newSkipFirstFrame = record.skipFirstFrameDetected;
            newPourTruncated = record.pourTruncatedDetected;
        });

        if (!recordFound || !badgesPersisted || *destroyed) return;
        QMetaObject::invokeMethod(
            this,
            [this, shotId, newChanneling, newTempUnstable, newGrindIssue, newSkipFirstFrame, newPourTruncated, destroyed]() {
                if (*destroyed) return;
                emit shotBadgesUpdated(shotId, newChanneling, newTempUnstable, newGrindIssue, newSkipFirstFrame, newPourTruncated);
            },
            Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
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
    if (!query.prepare(R"(
        SELECT id, uuid, timestamp, profile_name, profile_json,
               duration_seconds, final_weight, dose_weight,
               bean_brand, bean_type, roast_date, roast_level,
               grinder_brand, grinder_model, grinder_burrs, grinder_setting,
               drink_tds, drink_ey, enjoyment, espresso_notes, bean_notes, barista,
               profile_notes, visualizer_id, visualizer_url, debug_log,
               temperature_override, yield_override, beverage_type, profile_kb_id,
               channeling_detected, temperature_unstable, grind_issue_detected,
               skip_first_frame_detected, pour_truncated_detected, enjoyment_source
        FROM shots WHERE id = ?
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
    record.temperatureUnstable = query.value(31).toInt() != 0;
    record.grindIssueDetected = query.value(32).toInt() != 0;
    record.skipFirstFrameDetected = query.value(33).toInt() != 0;
    record.pourTruncatedDetected = query.value(34).toInt() != 0;
    record.enjoymentSource = query.value(35).toString();
    if (record.enjoymentSource.isEmpty()) record.enjoymentSource = QStringLiteral("none");
    record.summary.hasVisualizerUpload = !record.visualizerId.isEmpty();

    // Snapshot stored badge values before the recompute block overwrites them, so
    // we can detect drift and persist the corrected flags below.
    const bool storedChanneling = record.channelingDetected;
    const bool storedTempUnstable = record.temperatureUnstable;
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
    // Compute all five quality badges via a single ShotAnalysis::analyzeShot
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
        auto analysis = ShotAnalysis::analyzeShot(
            record.pressure, record.flow, record.weight,
            record.temperature, record.temperatureGoal,
            record.conductanceDerivative,
            record.phases, record.summary.beverageType, record.summary.duration,
            record.pressureGoal, record.flowGoal,
            inputs.analysisFlags, inputs.firstFrameSeconds,
            record.targetWeight, record.summary.finalWeight,
            inputs.frameCount);
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
        || storedTempUnstable != record.temperatureUnstable
        || storedGrindIssue != record.grindIssueDetected
        || storedSkipFirstFrame != record.skipFirstFrameDetected
        || storedPourTruncated != record.pourTruncatedDetected);
    if (flagsChanged) {
        QSqlQuery upd(db);
        upd.prepare("UPDATE shots SET channeling_detected=:c,"
                    " temperature_unstable=:t, grind_issue_detected=:g,"
                    " skip_first_frame_detected=:s,"
                    " pour_truncated_detected=:p,"
                    " updated_at = strftime('%s', 'now') WHERE id=:id");
        upd.bindValue(":c", record.channelingDetected ? 1 : 0);
        upd.bindValue(":t", record.temperatureUnstable ? 1 : 0);
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
    QThread* thread = QThread::create([this, dbPath, sql, shotIds, destroyed]() {
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

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
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

    QThread* thread = QThread::create([this, dbPath, shotId, destroyed]() {
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
                emit errorOccurred(QString("Failed to delete shot %1").arg(shotId));
            }
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

bool ShotHistoryStorage::updateShotMetadataStatic(QSqlDatabase& db, qint64 shotId, const QVariantMap& metadata)
{
    // Map camelCase metadata keys to DB column names.
    // Only columns with keys present in the metadata map are updated,
    // so partial updates don't wipe unspecified fields.
    static const QList<QPair<QString, QString>> fieldMap = {
        {"beanBrand",       "bean_brand"},
        {"beanType",        "bean_type"},
        {"roastDate",       "roast_date"},
        {"roastLevel",      "roast_level"},
        {"grinderBrand",    "grinder_brand"},
        {"grinderModel",    "grinder_model"},
        {"grinderBurrs",    "grinder_burrs"},
        {"grinderSetting",  "grinder_setting"},
        {"drinkTds",        "drink_tds"},
        {"drinkEy",         "drink_ey"},
        {"enjoyment",       "enjoyment"},
        {"espressoNotes",   "espresso_notes"},
        {"barista",         "barista"},
        {"doseWeight",      "dose_weight"},
        {"finalWeight",     "final_weight"},
        {"beverageType",    "beverage_type"},
        {"enjoymentSource", "enjoyment_source"},  // issue #1055 Layer 3
    };

    // Issue #1055 Layer 3: when the caller writes `enjoyment` without
    // an explicit `enjoymentSource`, default the source to "user". Any
    // explicit user-driven write path (manual editor, QuickRatingRow,
    // conversational reply) is by definition a user rating; only the
    // inferred-good evaluator passes "inferred". Materialize a local
    // copy of the map so we don't mutate the caller's argument.
    QVariantMap effective = metadata;
    if (effective.contains(QStringLiteral("enjoyment")) &&
        !effective.contains(QStringLiteral("enjoymentSource"))) {
        effective.insert(QStringLiteral("enjoymentSource"), QStringLiteral("user"));
    }

    // Build SET clause from only the keys present in the (effective) metadata.
    QStringList setClauses;
    for (const auto& [metaKey, dbCol] : fieldMap) {
        if (effective.contains(metaKey))
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

    // Bind only the columns present in the (effective) metadata.
    for (const auto& [metaKey, dbCol] : fieldMap) {
        if (effective.contains(metaKey))
            query.bindValue(QString(":%1").arg(dbCol), effective.value(metaKey));
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

    QThread* thread = QThread::create([this, dbPath, shotId, metadata, destroyed]() {
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
                emit errorOccurred(QString("Failed to save metadata for shot %1").arg(shotId));
            }
            emit shotMetadataUpdated(shotId, success);
            qDebug() << "ShotHistoryStorage: Async updated metadata for shot" << shotId << "success:" << success;
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

// Note: getDistinctValues / requestDistinct* / requestAutoFavorites* /
// queryGrinderContext / requestUpdateGrinderFields all live in
// shothistorystorage_queries.cpp.

void ShotHistoryStorage::updateTotalShots()
{
    // Async: run COUNT on background thread using existing static helper
    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    QThread* thread = QThread::create([this, dbPath, destroyed]() {
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
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
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
                        grinder_brand, grinder_model, grinder_burrs, grinder_setting,
                        drink_tds, drink_ey,
                        enjoyment, espresso_notes, bean_notes, barista,
                        profile_notes, visualizer_id, visualizer_url, debug_log,
                        temperature_override, yield_override, profile_kb_id,
                        channeling_detected, temperature_unstable, grind_issue_detected,
                        skip_first_frame_detected)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
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
                insert.addBindValue(srcShots.value("grinder_brand"));
                insert.addBindValue(srcShots.value("grinder_model"));
                insert.addBindValue(srcShots.value("grinder_burrs"));
                insert.addBindValue(srcShots.value("grinder_setting"));
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
                QVariant tu = srcShots.value("temperature_unstable");
                insert.addBindValue((tu.isValid() && !tu.isNull()) ? tu : QVariant(0));
                QVariant gi = srcShots.value("grind_issue_detected");
                insert.addBindValue((gi.isValid() && !gi.isNull()) ? gi : QVariant(0));
                QVariant sf = srcShots.value("skip_first_frame_detected");
                insert.addBindValue((sf.isValid() && !sf.isNull()) ? sf : QVariant(0));

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

    // Insert main shot record
    query.prepare(R"(
        INSERT INTO shots (
            uuid, timestamp, profile_name, profile_json, beverage_type,
            duration_seconds, final_weight, dose_weight,
            bean_brand, bean_type, roast_date, roast_level,
            grinder_brand, grinder_model, grinder_burrs, grinder_setting,
            drink_tds, drink_ey, enjoyment, espresso_notes, bean_notes, barista,
            profile_notes, debug_log,
            temperature_override, yield_override, profile_kb_id,
            channeling_detected, temperature_unstable, grind_issue_detected,
            skip_first_frame_detected, pour_truncated_detected
        ) VALUES (
            :uuid, :timestamp, :profile_name, :profile_json, :beverage_type,
            :duration, :final_weight, :dose_weight,
            :bean_brand, :bean_type, :roast_date, :roast_level,
            :grinder_brand, :grinder_model, :grinder_burrs, :grinder_setting,
            :drink_tds, :drink_ey, :enjoyment, :espresso_notes, :bean_notes, :barista,
            :profile_notes, :debug_log,
            :temperature_override, :yield_override, :profile_kb_id,
            :channeling_detected, :temperature_unstable, :grind_issue_detected,
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
    query.bindValue(":grinder_brand", record.grinderBrand);
    query.bindValue(":grinder_model", record.grinderModel);
    query.bindValue(":grinder_burrs", record.grinderBurrs);
    query.bindValue(":grinder_setting", record.grinderSetting);
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
    query.bindValue(":temperature_unstable", record.temperatureUnstable ? 1 : 0);
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
    QThread* thread = QThread::create([this, dbPath, destroyed]() {
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
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

