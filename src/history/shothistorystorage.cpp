#include "shothistorystorage.h"
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
#include <QDebug>
#include <QThread>
#include <algorithm>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

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

    updateTotalShots();

    m_ready = true;
    emit readyChanged();

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
            grinder_model TEXT,
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
            grinder_model,
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

    // Indexes
    query.exec("CREATE INDEX IF NOT EXISTS idx_shots_timestamp ON shots(timestamp DESC)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_shots_profile ON shots(profile_name)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_shots_bean ON shots(bean_brand, bean_type)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_shots_grinder ON shots(grinder_model)");
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
                int n = qMin(timeArr.size(), valueArr.size());
                if (n < 3) continue;

                // Centered moving average with window=5 (11-point, ~2.2s at 5Hz)
                constexpr int window = 5;
                QJsonArray smoothedArr;
                for (int i = 0; i < n; i++) {
                    int lo = qMax(0, i - window);
                    int hi = qMin(n - 1, i + window);
                    double sum = 0;
                    for (int j = lo; j <= hi; j++) {
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

QByteArray ShotHistoryStorage::compressSampleData(ShotDataModel* shotData)
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
    root["waterDispensed"] = pointsToJsonObject(shotData->waterDispensedData());

    // Weight data - store cumulative weight for history
    root["weight"] = pointsToJsonObject(shotData->cumulativeWeightData());
    // Also store flow rate from scale for future graph display
    root["weightFlow"] = pointsToJsonObject(shotData->weightData());
    // Weight-based flow rate (g/s) for visualizer export
    root["weightFlowRate"] = pointsToJsonObject(shotData->weightFlowRateData());

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
        int count = qMin(timeArr.size(), valueArr.size());
        points.reserve(count);
        for (int i = 0; i < count; ++i) {
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
    if (root.contains("waterDispensed"))
        record->waterDispensed = arrayToPoints(root["waterDispensed"].toObject());
    record->weight = arrayToPoints(root["weight"].toObject());
    if (root.contains("weightFlowRate"))
        record->weightFlowRate = arrayToPoints(root["weightFlowRate"].toObject());
}

qint64 ShotHistoryStorage::saveShot(ShotDataModel* shotData,
                                     const Profile* profile,
                                     double duration,
                                     double finalWeight,
                                     double doseWeight,
                                     const ShotMetadata& metadata,
                                     const QString& debugLog,
                                     double temperatureOverride,
                                     double yieldOverride)
{
    if (!m_ready || m_backupInProgress || !shotData) {
        qWarning() << "ShotHistoryStorage: Cannot save shot - not ready, backup in progress, or no data";
        emit shotSaved(-1);
        return -1;
    }

    // Extract all data from QObject pointers on the main thread into a POD struct
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
    data.yieldOverride = yieldOverride;
    data.beanBrand = metadata.beanBrand;
    data.beanType = metadata.beanType;
    data.roastDate = metadata.roastDate;
    data.roastLevel = metadata.roastLevel;
    data.grinderModel = metadata.grinderModel;
    data.grinderSetting = metadata.grinderSetting;
    data.drinkTds = metadata.drinkTds;
    data.drinkEy = metadata.drinkEy;
    data.espressoEnjoyment = metadata.espressoEnjoyment;
    data.espressoNotes = metadata.espressoNotes;
    data.barista = metadata.barista;
    data.profileNotes = profile ? profile->profileNotes() : QString();
    data.debugLog = debugLog;

    // Compress sample data on main thread (reads QObject data vectors)
    data.compressedSamples = compressSampleData(shotData);
    data.sampleCount = shotData->pressureData().size();

    // Extract phase markers on main thread
    QVariantList markers = shotData->phaseMarkersVariant();
    for (const QVariant& markerVar : markers) {
        QVariantMap marker = markerVar.toMap();
        ShotSaveData::PhaseMarker pm;
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
        int compressedSize = data.compressedSamples.size();

        QMetaObject::invokeMethod(this, [this, shotId, destroyed,
                                         profileName, duration, sampleCount, compressedSize]() {
            if (*destroyed) return;

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
    const QString connName = QString("shs_save_%1")
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);

    qint64 shotId = -1;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
        db.setDatabaseName(dbPath);

        // Use do-while(false) so error paths 'break' out while db/query are still
        // in scope — QSqlDatabase::removeDatabase() runs only after they're destroyed.
        do {
            if (!db.open()) {
                qWarning() << "ShotHistoryStorage: Failed to open DB for async saveShot";
                break;
            }

            QSqlQuery(db).exec("PRAGMA busy_timeout = 5000");
            db.transaction();

            QSqlQuery query(db);
            query.prepare(R"(
                INSERT INTO shots (
                    uuid, timestamp, profile_name, profile_json, beverage_type,
                    duration_seconds, final_weight, dose_weight,
                    bean_brand, bean_type, roast_date, roast_level,
                    grinder_model, grinder_setting,
                    drink_tds, drink_ey, enjoyment, espresso_notes, bean_notes, barista,
                    profile_notes, debug_log,
                    temperature_override, yield_override
                ) VALUES (
                    :uuid, :timestamp, :profile_name, :profile_json, :beverage_type,
                    :duration, :final_weight, :dose_weight,
                    :bean_brand, :bean_type, :roast_date, :roast_level,
                    :grinder_model, :grinder_setting,
                    :drink_tds, :drink_ey, :enjoyment, :espresso_notes, :bean_notes, :barista,
                    :profile_notes, :debug_log,
                    :temperature_override, :yield_override
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
            query.bindValue(":grinder_model", data.grinderModel);
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
            query.bindValue(":yield_override", data.yieldOverride);

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
            for (const ShotSaveData::PhaseMarker& pm : data.phaseMarkers) {
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
    }
    QSqlDatabase::removeDatabase(connName);

    return shotId;
}

bool ShotHistoryStorage::updateVisualizerInfo(qint64 shotId, const QString& visualizerId, const QString& visualizerUrl)
{
    if (!m_ready) return false;

    QSqlQuery query(m_db);
    query.prepare("UPDATE shots SET visualizer_id = :viz_id, visualizer_url = :viz_url, updated_at = strftime('%s', 'now') WHERE id = :id");
    query.bindValue(":viz_id", visualizerId);
    query.bindValue(":viz_url", visualizerUrl);
    query.bindValue(":id", shotId);

    if (!query.exec()) {
        qWarning() << "ShotHistoryStorage: Failed to update visualizer info:" << query.lastError().text();
        return false;
    }

    qDebug() << "ShotHistoryStorage: Updated shot" << shotId << "with visualizer ID:" << visualizerId;
    return true;
}

QVariantList ShotHistoryStorage::getShots(int offset, int limit)
{
    return getShotsFiltered(QVariantMap(), offset, limit);
}

ShotFilter ShotHistoryStorage::parseFilterMap(const QVariantMap& filterMap)
{
    ShotFilter filter;
    filter.profileName = filterMap.value("profileName").toString();
    filter.beanBrand = filterMap.value("beanBrand").toString();
    filter.beanType = filterMap.value("beanType").toString();
    filter.grinderModel = filterMap.value("grinderModel").toString();
    filter.grinderSetting = filterMap.value("grinderSetting").toString();
    filter.roastLevel = filterMap.value("roastLevel").toString();
    filter.minEnjoyment = filterMap.value("minEnjoyment", -1).toInt();
    filter.maxEnjoyment = filterMap.value("maxEnjoyment", -1).toInt();
    filter.minDose = filterMap.value("minDose", -1).toDouble();
    filter.maxDose = filterMap.value("maxDose", -1).toDouble();
    filter.minYield = filterMap.value("minYield", -1).toDouble();
    filter.maxYield = filterMap.value("maxYield", -1).toDouble();
    filter.minDuration = filterMap.value("minDuration", -1).toDouble();
    filter.maxDuration = filterMap.value("maxDuration", -1).toDouble();
    filter.minTds = filterMap.value("minTds", -1).toDouble();
    filter.maxTds = filterMap.value("maxTds", -1).toDouble();
    filter.minEy = filterMap.value("minEy", -1).toDouble();
    filter.maxEy = filterMap.value("maxEy", -1).toDouble();
    filter.dateFrom = filterMap.value("dateFrom", 0).toLongLong();
    filter.dateTo = filterMap.value("dateTo", 0).toLongLong();
    filter.searchText = filterMap.value("searchText").toString();
    filter.onlyWithVisualizer = filterMap.value("onlyWithVisualizer", false).toBool();
    filter.sortColumn = filterMap.value("sortField", "timestamp").toString();
    filter.sortDirection = filterMap.value("sortDirection", "DESC").toString();
    return filter;
}

QString ShotHistoryStorage::buildFilterQuery(const ShotFilter& filter, QVariantList& bindValues)
{
    QStringList conditions;

    if (!filter.profileName.isEmpty()) {
        conditions << "profile_name = ?";
        bindValues << filter.profileName;
    }
    if (!filter.beanBrand.isEmpty()) {
        conditions << "bean_brand = ?";
        bindValues << filter.beanBrand;
    }
    if (!filter.beanType.isEmpty()) {
        conditions << "bean_type = ?";
        bindValues << filter.beanType;
    }
    if (!filter.grinderModel.isEmpty()) {
        conditions << "grinder_model = ?";
        bindValues << filter.grinderModel;
    }
    if (!filter.grinderSetting.isEmpty()) {
        conditions << "grinder_setting = ?";
        bindValues << filter.grinderSetting;
    }
    if (!filter.roastLevel.isEmpty()) {
        conditions << "roast_level = ?";
        bindValues << filter.roastLevel;
    }
    if (filter.minEnjoyment >= 0) {
        conditions << "enjoyment >= ?";
        bindValues << filter.minEnjoyment;
    }
    if (filter.maxEnjoyment >= 0) {
        conditions << "enjoyment <= ?";
        bindValues << filter.maxEnjoyment;
    }
    if (filter.minDose >= 0) { conditions << "dose_weight >= ?"; bindValues << filter.minDose; }
    if (filter.maxDose >= 0) { conditions << "dose_weight <= ?"; bindValues << filter.maxDose; }
    if (filter.minYield >= 0) { conditions << "final_weight >= ?"; bindValues << filter.minYield; }
    if (filter.maxYield >= 0) { conditions << "final_weight <= ?"; bindValues << filter.maxYield; }
    if (filter.minDuration >= 0) { conditions << "duration_seconds >= ?"; bindValues << filter.minDuration; }
    if (filter.maxDuration >= 0) { conditions << "duration_seconds <= ?"; bindValues << filter.maxDuration; }
    if (filter.minTds >= 0) { conditions << "drink_tds >= ?"; bindValues << filter.minTds; }
    if (filter.maxTds >= 0) { conditions << "drink_tds <= ?"; bindValues << filter.maxTds; }
    if (filter.minEy >= 0) { conditions << "drink_ey >= ?"; bindValues << filter.minEy; }
    if (filter.maxEy >= 0) { conditions << "drink_ey <= ?"; bindValues << filter.maxEy; }
    if (filter.dateFrom > 0) {
        conditions << "timestamp >= ?";
        bindValues << filter.dateFrom;
    }
    if (filter.dateTo > 0) {
        conditions << "timestamp <= ?";
        bindValues << filter.dateTo;
    }
    if (filter.onlyWithVisualizer) {
        conditions << "visualizer_id IS NOT NULL";
    }

    if (conditions.isEmpty()) {
        return QString();
    }
    return " WHERE " + conditions.join(" AND ");
}

QString ShotHistoryStorage::formatFtsQuery(const QString& userInput)
{
    // FTS5 tokenizes on punctuation (hyphens, slashes, etc)
    // So "D-Flow / Q" becomes tokens: "D", "Flow", "Q"
    // We need to split user input the same way to match

    QString cleaned = userInput.simplified();
    if (cleaned.isEmpty()) {
        return QString();
    }

    // Replace common punctuation with spaces so "d-flo" becomes "d flo"
    // This matches how FTS5 tokenizes the indexed data
    QString normalized = cleaned;
    normalized.replace(QRegularExpression("[\\-/\\.]"), " ");

    QStringList words = normalized.split(' ', Qt::SkipEmptyParts);
    QStringList terms;

    for (const QString& word : words) {
        // Escape double quotes by doubling them
        QString escaped = word;
        escaped.replace('"', "\"\"");
        // Escape single quotes (for SQL string literal embedding)
        escaped.replace('\'', "''");
        // Use prefix matching with * for partial word matches
        // Wrap in quotes to handle special characters
        terms << QString("\"%1\"*").arg(escaped);
    }

    // Join with AND (implicit in FTS5 when space-separated)
    return terms.join(" ");
}

// Whitelist for sort columns — maps user-facing keys to SQL ORDER BY expressions
static const QHash<QString, QString> s_sortColumnMap = {
    {"timestamp",        "timestamp"},
    {"profile_name",     "LOWER(profile_name)"},
    {"bean_brand",       "LOWER(bean_brand)"},
    {"bean_type",        "LOWER(bean_type)"},
    {"enjoyment",        "enjoyment"},
    {"ratio",            "CASE WHEN dose_weight > 0 THEN CAST(final_weight AS REAL) / dose_weight ELSE 0 END"},
    {"duration_seconds", "duration_seconds"},
    {"dose_weight",      "dose_weight"},
    {"final_weight",     "final_weight"},
};

QVariantList ShotHistoryStorage::getShotsFiltered(const QVariantMap& filterMap, int offset, int limit)
{
    QVariantList results;
    if (!m_ready) return results;

    ShotFilter filter = parseFilterMap(filterMap);
    QVariantList bindValues;
    QString whereClause = buildFilterQuery(filter, bindValues);

    // Build dynamic ORDER BY from filter sort settings
    QString orderByExpr = s_sortColumnMap.value(filter.sortColumn, "timestamp");
    QString sortDir = (filter.sortDirection == "ASC") ? "ASC" : "DESC";
    QString orderByClause = QString("ORDER BY %1 %2").arg(orderByExpr, sortDir);

    // Handle FTS search separately
    QString sql;
    QString ftsQuery;

    if (!filter.searchText.isEmpty()) {
        // Format search text for FTS5: escape special chars and add prefix wildcards
        ftsQuery = formatFtsQuery(filter.searchText);

        // If formatFtsQuery returns empty (invalid input), skip FTS search
        if (ftsQuery.isEmpty()) {
            qWarning() << "ShotHistoryStorage: Empty FTS query from input:" << filter.searchText;
        }
    }

    if (!ftsQuery.isEmpty()) {
        // FTS5 query - embed FTS query directly in SQL (Qt's SQLite driver
        // doesn't support parameterized queries for FTS5 MATCH)
        // ftsQuery is already sanitized by formatFtsQuery() (quoted + escaped)
        // whereClause starts with " WHERE ..." but we already have a WHERE,
        // so replace the leading WHERE with AND
        QString extraConditions;
        if (!whereClause.isEmpty()) {
            extraConditions = whereClause;
            extraConditions.replace(extraConditions.indexOf("WHERE"), 5, "AND");
        }
        sql = QString(R"(
            SELECT id, uuid, timestamp, profile_name, duration_seconds,
                   final_weight, dose_weight, bean_brand, bean_type,
                   enjoyment, visualizer_id, grinder_setting,
                   temperature_override, yield_override, beverage_type,
                   drink_tds, drink_ey
            FROM shots
            WHERE id IN (SELECT rowid FROM shots_fts WHERE shots_fts MATCH '%1')
            %2
            %3
            LIMIT ? OFFSET ?
        )").arg(ftsQuery)
           .arg(extraConditions)
           .arg(orderByClause);
    } else {
        sql = QString(R"(
            SELECT id, uuid, timestamp, profile_name, duration_seconds,
                   final_weight, dose_weight, bean_brand, bean_type,
                   enjoyment, visualizer_id, grinder_setting,
                   temperature_override, yield_override, beverage_type,
                   drink_tds, drink_ey
            FROM shots
            %1
            %2
            LIMIT ? OFFSET ?
        )").arg(whereClause)
           .arg(orderByClause);
    }

    bindValues << limit << offset;

    QSqlQuery query(m_db);
    if (!query.prepare(sql)) {
        qWarning() << "ShotHistoryStorage: Query prepare failed:" << query.lastError().text();
        return results;
    }

    for (int i = 0; i < bindValues.size(); ++i) {
        query.bindValue(i, bindValues[i]);
    }

    if (!query.exec()) {
        qWarning() << "[ShotHistory] Query exec failed:" << query.lastError().text();
        return results;
    }

    while (query.next()) {
        QVariantMap shot;
        shot["id"] = query.value(0).toLongLong();
        shot["uuid"] = query.value(1).toString();
        shot["timestamp"] = query.value(2).toLongLong();
        shot["profileName"] = query.value(3).toString();
        shot["duration"] = query.value(4).toDouble();
        shot["finalWeight"] = query.value(5).toDouble();
        shot["doseWeight"] = query.value(6).toDouble();
        shot["beanBrand"] = query.value(7).toString();
        shot["beanType"] = query.value(8).toString();
        shot["enjoyment"] = query.value(9).toInt();
        shot["hasVisualizerUpload"] = !query.value(10).isNull();
        shot["grinderSetting"] = query.value(11).toString();
        shot["temperatureOverride"] = query.value(12).toDouble();  // 0.0 for NULL
        shot["yieldOverride"] = query.value(13).toDouble();  // 0.0 for NULL
        shot["beverageType"] = query.value(14).toString();
        shot["drinkTds"] = query.value(15).toDouble();
        shot["drinkEy"] = query.value(16).toDouble();

        // Format date for display
        QDateTime dt = QDateTime::fromSecsSinceEpoch(query.value(2).toLongLong());
        shot["dateTime"] = dt.toString("yyyy-MM-dd HH:mm");

        results.append(shot);
    }

    return results;
}

void ShotHistoryStorage::requestShotsFiltered(const QVariantMap& filterMap, int offset, int limit)
{
    bool isAppend = (offset > 0);

    if (!m_ready) {
        emit shotsFilteredReady(QVariantList(), isAppend, 0);
        return;
    }

    ++m_filterSerial;
    int serial = m_filterSerial;
    const QString dbPath = m_dbPath;

    // Build SQL on main thread (pure computation, fast)
    ShotFilter filter = parseFilterMap(filterMap);
    QVariantList bindValues;
    QString whereClause = buildFilterQuery(filter, bindValues);

    QString orderByExpr = s_sortColumnMap.value(filter.sortColumn, "timestamp");
    QString sortDir = (filter.sortDirection == "ASC") ? "ASC" : "DESC";
    QString orderByClause = QString("ORDER BY %1 %2").arg(orderByExpr, sortDir);

    QString ftsQuery;
    if (!filter.searchText.isEmpty())
        ftsQuery = formatFtsQuery(filter.searchText);

    QString sql;
    if (!ftsQuery.isEmpty()) {
        QString extraConditions;
        if (!whereClause.isEmpty()) {
            extraConditions = whereClause;
            extraConditions.replace(extraConditions.indexOf("WHERE"), 5, "AND");
        }
        sql = QString(R"(
            SELECT id, uuid, timestamp, profile_name, duration_seconds,
                   final_weight, dose_weight, bean_brand, bean_type,
                   enjoyment, visualizer_id, grinder_setting,
                   temperature_override, yield_override, beverage_type,
                   drink_tds, drink_ey
            FROM shots
            WHERE id IN (SELECT rowid FROM shots_fts WHERE shots_fts MATCH '%1')
            %2
            %3
            LIMIT ? OFFSET ?
        )").arg(ftsQuery).arg(extraConditions).arg(orderByClause);
    } else {
        sql = QString(R"(
            SELECT id, uuid, timestamp, profile_name, duration_seconds,
                   final_weight, dose_weight, bean_brand, bean_type,
                   enjoyment, visualizer_id, grinder_setting,
                   temperature_override, yield_override, beverage_type,
                   drink_tds, drink_ey
            FROM shots
            %1
            %2
            LIMIT ? OFFSET ?
        )").arg(whereClause).arg(orderByClause);
    }

    // Count SQL
    QString countSql;
    if (!ftsQuery.isEmpty()) {
        QString extraConditions;
        if (!whereClause.isEmpty()) {
            extraConditions = whereClause;
            extraConditions.replace(extraConditions.indexOf("WHERE"), 5, "AND");
        }
        countSql = QString("SELECT COUNT(*) FROM shots WHERE id IN "
                           "(SELECT rowid FROM shots_fts WHERE shots_fts MATCH '%1') %2")
                       .arg(ftsQuery).arg(extraConditions);
    } else {
        countSql = "SELECT COUNT(*) FROM shots" + whereClause;
    }

    // Separate bind values: data query gets limit+offset appended
    QVariantList countBindValues = bindValues;
    bindValues << limit << offset;

    if (!m_loadingFiltered) {
        m_loadingFiltered = true;
        emit loadingFilteredChanged();
    }

    auto destroyed = m_destroyed;
    QThread* thread = QThread::create(
        [this, dbPath, sql, countSql, bindValues, countBindValues, serial, isAppend, destroyed]() {
            const QString connName = QString("shs_filter_%1")
                .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);

            QVariantList results;
            int totalCount = 0;

            {
                QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
                db.setDatabaseName(dbPath);
                if (db.open()) {
                    // Data query
                    QSqlQuery query(db);
                    if (query.prepare(sql)) {
                        for (int i = 0; i < bindValues.size(); ++i)
                            query.bindValue(i, bindValues[i]);

                        if (query.exec()) {
                            while (query.next()) {
                                QVariantMap shot;
                                shot["id"] = query.value(0).toLongLong();
                                shot["uuid"] = query.value(1).toString();
                                shot["timestamp"] = query.value(2).toLongLong();
                                shot["profileName"] = query.value(3).toString();
                                shot["duration"] = query.value(4).toDouble();
                                shot["finalWeight"] = query.value(5).toDouble();
                                shot["doseWeight"] = query.value(6).toDouble();
                                shot["beanBrand"] = query.value(7).toString();
                                shot["beanType"] = query.value(8).toString();
                                shot["enjoyment"] = query.value(9).toInt();
                                shot["hasVisualizerUpload"] = !query.value(10).isNull();
                                shot["grinderSetting"] = query.value(11).toString();
                                shot["temperatureOverride"] = query.value(12).toDouble();
                                shot["yieldOverride"] = query.value(13).toDouble();
                                shot["beverageType"] = query.value(14).toString();
                                shot["drinkTds"] = query.value(15).toDouble();
                                shot["drinkEy"] = query.value(16).toDouble();

                                QDateTime dt = QDateTime::fromSecsSinceEpoch(
                                    query.value(2).toLongLong());
                                shot["dateTime"] = dt.toString("yyyy-MM-dd HH:mm");

                                results.append(shot);
                            }
                        }
                    }

                    // Count query
                    QSqlQuery countQuery(db);
                    if (countQuery.prepare(countSql)) {
                        for (int i = 0; i < countBindValues.size(); ++i)
                            countQuery.bindValue(i, countBindValues[i]);
                        if (countQuery.exec() && countQuery.next())
                            totalCount = countQuery.value(0).toInt();
                    }
                } else {
                    qWarning() << "ShotHistoryStorage: Failed to open DB for async filter";
                }
            }
            QSqlDatabase::removeDatabase(connName);

            QMetaObject::invokeMethod(
                this,
                [this, results = std::move(results), serial, isAppend, totalCount, destroyed]() mutable {
                    if (*destroyed) return;
                    if (serial != m_filterSerial) return;
                    m_loadingFiltered = false;
                    emit loadingFilteredChanged();
                    emit shotsFilteredReady(results, isAppend, totalCount);
                },
                Qt::QueuedConnection);
        });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

qint64 ShotHistoryStorage::getShotTimestamp(qint64 shotId)
{
    if (!m_ready) return 0;
    QSqlQuery query(m_db);
    query.prepare("SELECT timestamp FROM shots WHERE id = ?");
    query.bindValue(0, shotId);
    if (query.exec() && query.next())
        return query.value(0).toLongLong();
    return 0;
}

QVariantMap ShotHistoryStorage::getShot(qint64 shotId)
{
    return convertShotRecord(getShotRecord(shotId));
}

void ShotHistoryStorage::requestShot(qint64 shotId)
{
    if (!m_ready) {
        emit shotReady(shotId, QVariantMap());
        return;
    }

    const QString dbPath = m_dbPath;

    auto destroyed = m_destroyed;
    QThread* thread = QThread::create([this, dbPath, shotId, destroyed]() {
        const QString connName = QString("shs_shot_%1")
            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);

        ShotRecord record;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
            db.setDatabaseName(dbPath);
            if (db.open())
                record = loadShotRecordStatic(db, shotId);
            else
                qWarning() << "ShotHistoryStorage: Failed to open DB for async getShot";
        }
        QSqlDatabase::removeDatabase(connName);

        // Convert to QVariantMap on main thread (touches QML-visible data)
        QMetaObject::invokeMethod(this, [this, shotId, record = std::move(record), destroyed]() {
            if (*destroyed) return;
            emit shotReady(shotId, convertShotRecord(record));
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

QVariantMap ShotHistoryStorage::convertShotRecord(const ShotRecord& record)
{
    QVariantMap result;
    if (record.summary.id == 0) return result;

    result["id"] = record.summary.id;
    result["uuid"] = record.summary.uuid;
    result["timestamp"] = record.summary.timestamp;
    result["profileName"] = record.summary.profileName;
    result["duration"] = record.summary.duration;
    result["finalWeight"] = record.summary.finalWeight;
    result["doseWeight"] = record.summary.doseWeight;
    result["beanBrand"] = record.summary.beanBrand;
    result["beanType"] = record.summary.beanType;
    result["enjoyment"] = record.summary.enjoyment;
    result["hasVisualizerUpload"] = record.summary.hasVisualizerUpload;
    result["beverageType"] = record.summary.beverageType;
    result["roastDate"] = record.roastDate;
    result["roastLevel"] = record.roastLevel;
    result["grinderModel"] = record.grinderModel;
    result["grinderSetting"] = record.grinderSetting;
    result["drinkTds"] = record.drinkTds;
    result["drinkEy"] = record.drinkEy;
    result["espressoNotes"] = record.espressoNotes;
    result["beanNotes"] = record.beanNotes;
    result["barista"] = record.barista;
    result["profileNotes"] = record.profileNotes;
    result["visualizerId"] = record.visualizerId;
    result["visualizerUrl"] = record.visualizerUrl;
    result["debugLog"] = record.debugLog;
    result["temperatureOverride"] = record.temperatureOverride;
    result["yieldOverride"] = record.yieldOverride;
    result["profileJson"] = record.profileJson;

    auto pointsToVariant = [](const QVector<QPointF>& points) {
        QVariantList list;
        for (const auto& pt : points) {
            QVariantMap p;
            p["x"] = pt.x();
            p["y"] = pt.y();
            list.append(p);
        }
        return list;
    };

    result["pressure"] = pointsToVariant(record.pressure);
    result["flow"] = pointsToVariant(record.flow);
    result["temperature"] = pointsToVariant(record.temperature);
    result["temperatureMix"] = pointsToVariant(record.temperatureMix);
    result["resistance"] = pointsToVariant(record.resistance);
    result["waterDispensed"] = pointsToVariant(record.waterDispensed);
    result["pressureGoal"] = pointsToVariant(record.pressureGoal);
    result["flowGoal"] = pointsToVariant(record.flowGoal);
    result["temperatureGoal"] = pointsToVariant(record.temperatureGoal);
    result["weight"] = pointsToVariant(record.weight);
    result["weightFlowRate"] = pointsToVariant(record.weightFlowRate);

    QVariantList phases;
    for (const auto& phase : record.phases) {
        QVariantMap p;
        p["time"] = phase.time;
        p["label"] = phase.label;
        p["frameNumber"] = phase.frameNumber;
        p["isFlowMode"] = phase.isFlowMode;
        p["transitionReason"] = phase.transitionReason;
        phases.append(p);
    }
    result["phases"] = phases;

    QDateTime dt = QDateTime::fromSecsSinceEpoch(record.summary.timestamp);
    result["dateTime"] = dt.toString("yyyy-MM-dd HH:mm:ss");

    return result;
}

ShotRecord ShotHistoryStorage::getShotRecord(qint64 shotId)
{
    ShotRecord record;
    if (!m_ready) return record;

    QSqlQuery query(m_db);
    query.prepare(R"(
        SELECT id, uuid, timestamp, profile_name, profile_json,
               duration_seconds, final_weight, dose_weight,
               bean_brand, bean_type, roast_date, roast_level,
               grinder_model, grinder_setting,
               drink_tds, drink_ey, enjoyment, espresso_notes, bean_notes, barista,
               profile_notes, visualizer_id, visualizer_url, debug_log,
               temperature_override, yield_override, beverage_type
        FROM shots WHERE id = ?
    )");
    query.bindValue(0, shotId);

    if (!query.exec() || !query.next()) {
        qWarning() << "ShotHistoryStorage: Shot not found:" << shotId;
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
    record.grinderModel = query.value(12).toString();
    record.grinderSetting = query.value(13).toString();
    record.drinkTds = query.value(14).toDouble();
    record.drinkEy = query.value(15).toDouble();
    record.summary.enjoyment = query.value(16).toInt();
    record.espressoNotes = query.value(17).toString();
    record.beanNotes = query.value(18).toString();
    record.barista = query.value(19).toString();
    record.profileNotes = query.value(20).toString();
    record.visualizerId = query.value(21).toString();
    record.visualizerUrl = query.value(22).toString();
    record.debugLog = query.value(23).toString();

    // Load overrides (always have values, default to 0 if database has NULL for old records)
    record.temperatureOverride = query.value(24).toDouble();  // toDouble() returns 0.0 for NULL
    record.yieldOverride = query.value(25).toDouble();  // toDouble() returns 0.0 for NULL
    record.summary.beverageType = query.value(26).toString();

    record.summary.hasVisualizerUpload = !record.visualizerId.isEmpty();

    // Load sample data
    query.prepare("SELECT data_blob FROM shot_samples WHERE shot_id = ?");
    query.bindValue(0, shotId);
    if (query.exec() && query.next()) {
        QByteArray blob = query.value(0).toByteArray();
        decompressSampleData(blob, &record);
    }

    // Load phase markers
    query.prepare("SELECT time_offset, label, frame_number, is_flow_mode, transition_reason FROM shot_phases WHERE shot_id = ? ORDER BY time_offset");
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

    return record;
}

QList<ShotRecord> ShotHistoryStorage::getShotsForComparison(const QList<qint64>& shotIds)
{
    QList<ShotRecord> records;
    for (qint64 id : shotIds) {
        ShotRecord record = getShotRecord(id);
        if (record.summary.id != 0) {
            records.append(record);
        }
    }
    return records;
}

ShotRecord ShotHistoryStorage::loadShotRecordStatic(QSqlDatabase& db, qint64 shotId)
{
    ShotRecord record;

    QSqlQuery query(db);
    query.prepare(R"(
        SELECT id, uuid, timestamp, profile_name, profile_json,
               duration_seconds, final_weight, dose_weight,
               bean_brand, bean_type, roast_date, roast_level,
               grinder_model, grinder_setting,
               drink_tds, drink_ey, enjoyment, espresso_notes, bean_notes, barista,
               profile_notes, visualizer_id, visualizer_url, debug_log,
               temperature_override, yield_override, beverage_type
        FROM shots WHERE id = ?
    )");
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
    record.grinderModel = query.value(12).toString();
    record.grinderSetting = query.value(13).toString();
    record.drinkTds = query.value(14).toDouble();
    record.drinkEy = query.value(15).toDouble();
    record.summary.enjoyment = query.value(16).toInt();
    record.espressoNotes = query.value(17).toString();
    record.beanNotes = query.value(18).toString();
    record.barista = query.value(19).toString();
    record.profileNotes = query.value(20).toString();
    record.visualizerId = query.value(21).toString();
    record.visualizerUrl = query.value(22).toString();
    record.debugLog = query.value(23).toString();
    record.temperatureOverride = query.value(24).toDouble();
    record.yieldOverride = query.value(25).toDouble();
    record.summary.beverageType = query.value(26).toString();
    record.summary.hasVisualizerUpload = !record.visualizerId.isEmpty();

    query.prepare("SELECT data_blob FROM shot_samples WHERE shot_id = ?");
    query.bindValue(0, shotId);
    if (query.exec() && query.next()) {
        QByteArray blob = query.value(0).toByteArray();
        decompressSampleData(blob, &record);
    }

    query.prepare("SELECT time_offset, label, frame_number, is_flow_mode, transition_reason "
                  "FROM shot_phases WHERE shot_id = ? ORDER BY time_offset");
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

    updateTotalShots();
    invalidateDistinctCache();
    emit shotDeleted(shotId);

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
        const QString connName = QString("shs_delete_%1")
            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);

        bool success = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
            db.setDatabaseName(dbPath);
            if (db.open()) {
                QSqlQuery(db).exec("PRAGMA busy_timeout = 5000");
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
            } else {
                qWarning() << "ShotHistoryStorage: Failed to open DB for async deleteShots";
            }
        }
        QSqlDatabase::removeDatabase(connName);

        QMetaObject::invokeMethod(this, [this, shotIds, success, destroyed]() {
            if (*destroyed) return;
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
    if (!m_ready) return;

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;

    QThread* thread = QThread::create([this, dbPath, shotId, destroyed]() {
        const QString connName = QString("shs_rdel_%1")
            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);

        bool success = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
            db.setDatabaseName(dbPath);
            if (db.open()) {
                QSqlQuery(db).exec("PRAGMA busy_timeout = 5000");
                QSqlQuery query(db);
                query.prepare("DELETE FROM shots WHERE id = ?");
                query.bindValue(0, shotId);
                if (query.exec()) {
                    success = true;
                } else {
                    qWarning() << "ShotHistoryStorage: Failed to async delete shot:" << query.lastError().text();
                }
            } else {
                qWarning() << "ShotHistoryStorage: Failed to open DB for async deleteShot";
            }
        }
        QSqlDatabase::removeDatabase(connName);

        QMetaObject::invokeMethod(this, [this, shotId, success, destroyed]() {
            if (*destroyed) return;
            if (success) {
                refreshTotalShots();
                invalidateDistinctCache();
                emit shotDeleted(shotId);
                qDebug() << "ShotHistoryStorage: Async deleted shot" << shotId;
            }
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

bool ShotHistoryStorage::updateShotMetadata(qint64 shotId, const QVariantMap& metadata)
{
    if (!m_ready) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        UPDATE shots SET
            bean_brand = :bean_brand,
            bean_type = :bean_type,
            roast_date = :roast_date,
            roast_level = :roast_level,
            grinder_model = :grinder_model,
            grinder_setting = :grinder_setting,
            drink_tds = :drink_tds,
            drink_ey = :drink_ey,
            enjoyment = :enjoyment,
            espresso_notes = :espresso_notes,
            barista = :barista,
            dose_weight = :dose_weight,
            final_weight = :final_weight,
            beverage_type = :beverage_type,
            updated_at = strftime('%s', 'now')
        WHERE id = :id
    )");

    query.bindValue(":bean_brand", metadata.value("beanBrand").toString());
    query.bindValue(":bean_type", metadata.value("beanType").toString());
    query.bindValue(":roast_date", metadata.value("roastDate").toString());
    query.bindValue(":roast_level", metadata.value("roastLevel").toString());
    query.bindValue(":grinder_model", metadata.value("grinderModel").toString());
    query.bindValue(":grinder_setting", metadata.value("grinderSetting").toString());
    query.bindValue(":drink_tds", metadata.value("drinkTds").toDouble());
    query.bindValue(":drink_ey", metadata.value("drinkEy").toDouble());
    query.bindValue(":enjoyment", metadata.value("enjoyment").toInt());
    query.bindValue(":espresso_notes", metadata.value("espressoNotes").toString());
    query.bindValue(":barista", metadata.value("barista").toString());
    query.bindValue(":dose_weight", metadata.value("doseWeight").toDouble());
    query.bindValue(":final_weight", metadata.value("finalWeight").toDouble());
    query.bindValue(":beverage_type", metadata.value("beverageType", "espresso").toString());
    query.bindValue(":id", shotId);

    if (!query.exec()) {
        qWarning() << "ShotHistoryStorage: Failed to update shot metadata:" << query.lastError().text();
        return false;
    }

    invalidateDistinctCache();
    qDebug() << "ShotHistoryStorage: Updated metadata for shot" << shotId;
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
        const QString connName = QString("shs_rupd_%1")
            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);

        bool success = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
            db.setDatabaseName(dbPath);
            if (db.open()) {
                QSqlQuery(db).exec("PRAGMA busy_timeout = 5000");
                QSqlQuery query(db);
                query.prepare(R"(
                    UPDATE shots SET
                        bean_brand = :bean_brand,
                        bean_type = :bean_type,
                        roast_date = :roast_date,
                        roast_level = :roast_level,
                        grinder_model = :grinder_model,
                        grinder_setting = :grinder_setting,
                        drink_tds = :drink_tds,
                        drink_ey = :drink_ey,
                        enjoyment = :enjoyment,
                        espresso_notes = :espresso_notes,
                        barista = :barista,
                        dose_weight = :dose_weight,
                        final_weight = :final_weight,
                        beverage_type = :beverage_type,
                        updated_at = strftime('%s', 'now')
                    WHERE id = :id
                )");

                query.bindValue(":bean_brand", metadata.value("beanBrand").toString());
                query.bindValue(":bean_type", metadata.value("beanType").toString());
                query.bindValue(":roast_date", metadata.value("roastDate").toString());
                query.bindValue(":roast_level", metadata.value("roastLevel").toString());
                query.bindValue(":grinder_model", metadata.value("grinderModel").toString());
                query.bindValue(":grinder_setting", metadata.value("grinderSetting").toString());
                query.bindValue(":drink_tds", metadata.value("drinkTds").toDouble());
                query.bindValue(":drink_ey", metadata.value("drinkEy").toDouble());
                query.bindValue(":enjoyment", metadata.value("enjoyment").toInt());
                query.bindValue(":espresso_notes", metadata.value("espressoNotes").toString());
                query.bindValue(":barista", metadata.value("barista").toString());
                query.bindValue(":dose_weight", metadata.value("doseWeight").toDouble());
                query.bindValue(":final_weight", metadata.value("finalWeight").toDouble());
                query.bindValue(":beverage_type", metadata.value("beverageType", "espresso").toString());
                query.bindValue(":id", shotId);

                if (query.exec()) {
                    success = true;
                } else {
                    qWarning() << "ShotHistoryStorage: Failed to async update metadata:" << query.lastError().text();
                }
            } else {
                qWarning() << "ShotHistoryStorage: Failed to open DB for async updateShotMetadata";
            }
        }
        QSqlDatabase::removeDatabase(connName);

        QMetaObject::invokeMethod(this, [this, shotId, success, destroyed]() {
            if (*destroyed) return;
            if (success)
                invalidateDistinctCache();
            emit shotMetadataUpdated(shotId, success);
            qDebug() << "ShotHistoryStorage: Async updated metadata for shot" << shotId << "success:" << success;
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

// Helper for all getDistinct* methods
static const QStringList s_allowedColumns = {
    "profile_name", "bean_brand", "bean_type",
    "grinder_model", "grinder_setting", "barista", "roast_level"
};

QStringList ShotHistoryStorage::getDistinctValues(const QString& column)
{
    // Return cached result if available
    if (m_distinctCache.contains(column))
        return m_distinctCache.value(column);

    QStringList results;
    if (!m_ready) return results;
    if (!s_allowedColumns.contains(column)) {
        qWarning() << "ShotHistoryStorage::getDistinctValues: rejected column" << column;
        return results;
    }

    QString sql = QString("SELECT DISTINCT %1 FROM shots WHERE %1 IS NOT NULL AND %1 != '' ORDER BY %1")
                      .arg(column);

    QSqlQuery query(m_db);
    query.exec(sql);
    while (query.next()) {
        QString value = query.value(0).toString();
        if (!value.isEmpty()) {
            results << value;
        }
    }

    m_distinctCache.insert(column, results);
    return results;
}

void ShotHistoryStorage::invalidateDistinctCache()
{
    m_distinctCache.clear();
}

// Helper for all getDistinct*Filtered methods
QStringList ShotHistoryStorage::getDistinctValuesFiltered(const QString& column,
                                                           const QString& excludeColumn,
                                                           const QVariantMap& filter)
{
    QStringList results;
    if (!m_ready) return results;
    if (!s_allowedColumns.contains(column)) {
        qWarning() << "ShotHistoryStorage::getDistinctValuesFiltered: rejected column" << column;
        return results;
    }

    QString sql = QString("SELECT DISTINCT %1 FROM shots WHERE %1 IS NOT NULL AND %1 != ''")
                      .arg(column);
    QVariantList bindValues;

    // Map of filter keys to database columns
    static const QHash<QString, QString> filterToColumn = {
        {"profileName", "profile_name"},
        {"beanBrand", "bean_brand"},
        {"beanType", "bean_type"}
    };

    for (auto it = filterToColumn.constBegin(); it != filterToColumn.constEnd(); ++it) {
        // Skip if this is the column we're querying (don't filter on self)
        if (it.value() == excludeColumn) continue;

        if (filter.contains(it.key()) && !filter.value(it.key()).toString().isEmpty()) {
            sql += QString(" AND %1 = ?").arg(it.value());
            bindValues << filter.value(it.key()).toString();
        }
    }

    sql += QString(" ORDER BY %1").arg(column);

    QSqlQuery query(m_db);
    query.prepare(sql);
    for (int i = 0; i < bindValues.size(); ++i) {
        query.bindValue(i, bindValues[i]);
    }
    query.exec();

    while (query.next()) {
        QString value = query.value(0).toString();
        if (!value.isEmpty()) {
            results << value;
        }
    }
    return results;
}

QStringList ShotHistoryStorage::getDistinctProfiles()
{
    return getDistinctValues("profile_name");
}

QStringList ShotHistoryStorage::getDistinctBeanBrands()
{
    return getDistinctValues("bean_brand");
}

QStringList ShotHistoryStorage::getDistinctBeanTypes()
{
    return getDistinctValues("bean_type");
}

QStringList ShotHistoryStorage::getDistinctGrinders()
{
    return getDistinctValues("grinder_model");
}

QStringList ShotHistoryStorage::getDistinctGrinderSettings()
{
    QStringList settings = getDistinctValues("grinder_setting");
    sortGrinderSettings(settings);
    return settings;
}

QStringList ShotHistoryStorage::getDistinctBaristas()
{
    return getDistinctValues("barista");
}

QStringList ShotHistoryStorage::getDistinctRoastLevels()
{
    return getDistinctValues("roast_level");
}

QStringList ShotHistoryStorage::getDistinctProfilesFiltered(const QVariantMap& filter)
{
    return getDistinctValuesFiltered("profile_name", "profile_name", filter);
}

QStringList ShotHistoryStorage::getDistinctBeanBrandsFiltered(const QVariantMap& filter)
{
    return getDistinctValuesFiltered("bean_brand", "bean_brand", filter);
}

QStringList ShotHistoryStorage::getDistinctBeanTypesFiltered(const QVariantMap& filter)
{
    return getDistinctValuesFiltered("bean_type", "bean_type", filter);
}

int ShotHistoryStorage::getFilteredShotCount(const QVariantMap& filterMap)
{
    if (!m_ready) return 0;

    ShotFilter filter = parseFilterMap(filterMap);
    QVariantList bindValues;
    QString whereClause = buildFilterQuery(filter, bindValues);

    QString sql = "SELECT COUNT(*) FROM shots" + whereClause;

    QSqlQuery query(m_db);
    query.prepare(sql);
    for (int i = 0; i < bindValues.size(); ++i) {
        query.bindValue(i, bindValues[i]);
    }

    if (query.exec() && query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

QVariantList ShotHistoryStorage::getAutoFavorites(const QString& groupBy, int maxItems)
{
    QVariantList results;
    if (!m_ready) return results;

    // Build GROUP BY and SELECT columns based on groupBy setting
    // selectColumns needs AS aliases for the subquery
    // groupColumns is for GROUP BY clause
    // joinConditions matches outer table to subquery
    QString selectColumns;
    QString groupColumns;
    QString joinConditions;

    if (groupBy == "bean") {
        selectColumns = "COALESCE(bean_brand, '') AS gb_bean_brand, "
                        "COALESCE(bean_type, '') AS gb_bean_type";
        groupColumns = "COALESCE(bean_brand, ''), COALESCE(bean_type, '')";
        joinConditions = "COALESCE(s.bean_brand, '') = g.gb_bean_brand "
                         "AND COALESCE(s.bean_type, '') = g.gb_bean_type";
    } else if (groupBy == "profile") {
        selectColumns = "COALESCE(profile_name, '') AS gb_profile_name";
        groupColumns = "COALESCE(profile_name, '')";
        joinConditions = "COALESCE(s.profile_name, '') = g.gb_profile_name";
    } else if (groupBy == "bean_profile_grinder") {
        selectColumns = "COALESCE(bean_brand, '') AS gb_bean_brand, "
                        "COALESCE(bean_type, '') AS gb_bean_type, "
                        "COALESCE(profile_name, '') AS gb_profile_name, "
                        "COALESCE(grinder_model, '') AS gb_grinder_model, "
                        "COALESCE(grinder_setting, '') AS gb_grinder_setting";
        groupColumns = "COALESCE(bean_brand, ''), COALESCE(bean_type, ''), "
                       "COALESCE(profile_name, ''), COALESCE(grinder_model, ''), "
                       "COALESCE(grinder_setting, '')";
        joinConditions = "COALESCE(s.bean_brand, '') = g.gb_bean_brand "
                         "AND COALESCE(s.bean_type, '') = g.gb_bean_type "
                         "AND COALESCE(s.profile_name, '') = g.gb_profile_name "
                         "AND COALESCE(s.grinder_model, '') = g.gb_grinder_model "
                         "AND COALESCE(s.grinder_setting, '') = g.gb_grinder_setting";
    } else {
        // Default: bean_profile
        selectColumns = "COALESCE(bean_brand, '') AS gb_bean_brand, "
                        "COALESCE(bean_type, '') AS gb_bean_type, "
                        "COALESCE(profile_name, '') AS gb_profile_name";
        groupColumns = "COALESCE(bean_brand, ''), COALESCE(bean_type, ''), COALESCE(profile_name, '')";
        joinConditions = "COALESCE(s.bean_brand, '') = g.gb_bean_brand "
                         "AND COALESCE(s.bean_type, '') = g.gb_bean_type "
                         "AND COALESCE(s.profile_name, '') = g.gb_profile_name";
    }

    // Query: Get most recent shot for each unique combination
    // We need to match the shot table back to the grouped results to get the full shot data
    QString sql = QString(
        "SELECT s.id, s.profile_name, s.bean_brand, s.bean_type, "
        "s.grinder_model, s.grinder_setting, s.dose_weight, s.final_weight, "
        "s.timestamp, g.shot_count, g.avg_enjoyment "
        "FROM shots s "
        "INNER JOIN ("
        "  SELECT %1, MAX(timestamp) as max_ts, "
        "  COUNT(*) as shot_count, "
        "  AVG(CASE WHEN enjoyment > 0 THEN enjoyment ELSE NULL END) as avg_enjoyment "
        "  FROM shots "
        "  WHERE (bean_brand IS NOT NULL AND bean_brand != '') "
        "     OR (profile_name IS NOT NULL AND profile_name != '') "
        "  GROUP BY %2"
        ") g ON s.timestamp = g.max_ts AND %3 "
        "ORDER BY s.timestamp DESC "
        "LIMIT %4"
    ).arg(selectColumns, groupColumns, joinConditions).arg(maxItems);

    QSqlQuery query(m_db);
    if (!query.exec(sql)) {
        qWarning() << "getAutoFavorites query failed:" << query.lastError().text();
        qWarning() << "SQL:" << sql;
        return results;
    }

    while (query.next()) {
        QVariantMap entry;
        entry["shotId"] = query.value("id").toLongLong();
        entry["profileName"] = query.value("profile_name").toString();
        entry["beanBrand"] = query.value("bean_brand").toString();
        entry["beanType"] = query.value("bean_type").toString();
        entry["grinderModel"] = query.value("grinder_model").toString();
        entry["grinderSetting"] = query.value("grinder_setting").toString();
        entry["doseWeight"] = query.value("dose_weight").toDouble();
        entry["finalWeight"] = query.value("final_weight").toDouble();
        entry["lastUsedTimestamp"] = query.value("timestamp").toLongLong();
        entry["shotCount"] = query.value("shot_count").toInt();
        entry["avgEnjoyment"] = query.value("avg_enjoyment").toInt();
        results.append(entry);
    }

    return results;
}

void ShotHistoryStorage::requestAutoFavorites(const QString& groupBy, int maxItems)
{
    if (!m_ready) {
        emit autoFavoritesReady(QVariantList());
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;

    // Build SQL on main thread (pure string manipulation, fast)
    QString selectColumns;
    QString groupColumns;
    QString joinConditions;

    if (groupBy == "bean") {
        selectColumns = "COALESCE(bean_brand, '') AS gb_bean_brand, "
                        "COALESCE(bean_type, '') AS gb_bean_type";
        groupColumns = "COALESCE(bean_brand, ''), COALESCE(bean_type, '')";
        joinConditions = "COALESCE(s.bean_brand, '') = g.gb_bean_brand "
                         "AND COALESCE(s.bean_type, '') = g.gb_bean_type";
    } else if (groupBy == "profile") {
        selectColumns = "COALESCE(profile_name, '') AS gb_profile_name";
        groupColumns = "COALESCE(profile_name, '')";
        joinConditions = "COALESCE(s.profile_name, '') = g.gb_profile_name";
    } else if (groupBy == "bean_profile_grinder") {
        selectColumns = "COALESCE(bean_brand, '') AS gb_bean_brand, "
                        "COALESCE(bean_type, '') AS gb_bean_type, "
                        "COALESCE(profile_name, '') AS gb_profile_name, "
                        "COALESCE(grinder_model, '') AS gb_grinder_model, "
                        "COALESCE(grinder_setting, '') AS gb_grinder_setting";
        groupColumns = "COALESCE(bean_brand, ''), COALESCE(bean_type, ''), "
                       "COALESCE(profile_name, ''), COALESCE(grinder_model, ''), "
                       "COALESCE(grinder_setting, '')";
        joinConditions = "COALESCE(s.bean_brand, '') = g.gb_bean_brand "
                         "AND COALESCE(s.bean_type, '') = g.gb_bean_type "
                         "AND COALESCE(s.profile_name, '') = g.gb_profile_name "
                         "AND COALESCE(s.grinder_model, '') = g.gb_grinder_model "
                         "AND COALESCE(s.grinder_setting, '') = g.gb_grinder_setting";
    } else {
        // Default: bean_profile
        selectColumns = "COALESCE(bean_brand, '') AS gb_bean_brand, "
                        "COALESCE(bean_type, '') AS gb_bean_type, "
                        "COALESCE(profile_name, '') AS gb_profile_name";
        groupColumns = "COALESCE(bean_brand, ''), COALESCE(bean_type, ''), COALESCE(profile_name, '')";
        joinConditions = "COALESCE(s.bean_brand, '') = g.gb_bean_brand "
                         "AND COALESCE(s.bean_type, '') = g.gb_bean_type "
                         "AND COALESCE(s.profile_name, '') = g.gb_profile_name";
    }

    QString sql = QString(
        "SELECT s.id, s.profile_name, s.bean_brand, s.bean_type, "
        "s.grinder_model, s.grinder_setting, s.dose_weight, s.final_weight, "
        "s.timestamp, g.shot_count, g.avg_enjoyment "
        "FROM shots s "
        "INNER JOIN ("
        "  SELECT %1, MAX(timestamp) as max_ts, "
        "  COUNT(*) as shot_count, "
        "  AVG(CASE WHEN enjoyment > 0 THEN enjoyment ELSE NULL END) as avg_enjoyment "
        "  FROM shots "
        "  WHERE (bean_brand IS NOT NULL AND bean_brand != '') "
        "     OR (profile_name IS NOT NULL AND profile_name != '') "
        "  GROUP BY %2"
        ") g ON s.timestamp = g.max_ts AND %3 "
        "ORDER BY s.timestamp DESC "
        "LIMIT %4"
    ).arg(selectColumns, groupColumns, joinConditions).arg(maxItems);

    QThread* thread = QThread::create([this, dbPath, sql, destroyed]() {
        const QString connName = QString("shs_raf_%1")
            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);

        QVariantList results;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
            db.setDatabaseName(dbPath);
            if (db.open()) {
                QSqlQuery(db).exec("PRAGMA busy_timeout = 5000");
                QSqlQuery query(db);
                if (query.exec(sql)) {
                    while (query.next()) {
                        QVariantMap entry;
                        entry["shotId"] = query.value("id").toLongLong();
                        entry["profileName"] = query.value("profile_name").toString();
                        entry["beanBrand"] = query.value("bean_brand").toString();
                        entry["beanType"] = query.value("bean_type").toString();
                        entry["grinderModel"] = query.value("grinder_model").toString();
                        entry["grinderSetting"] = query.value("grinder_setting").toString();
                        entry["doseWeight"] = query.value("dose_weight").toDouble();
                        entry["finalWeight"] = query.value("final_weight").toDouble();
                        entry["lastUsedTimestamp"] = query.value("timestamp").toLongLong();
                        entry["shotCount"] = query.value("shot_count").toInt();
                        entry["avgEnjoyment"] = query.value("avg_enjoyment").toInt();
                        results.append(entry);
                    }
                } else {
                    qWarning() << "ShotHistoryStorage: Async getAutoFavorites query failed:" << query.lastError().text();
                }
            } else {
                qWarning() << "ShotHistoryStorage: Failed to open DB for async getAutoFavorites";
            }
        }
        QSqlDatabase::removeDatabase(connName);

        QMetaObject::invokeMethod(this, [this, results, destroyed]() {
            if (*destroyed) return;
            emit autoFavoritesReady(results);
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

QVariantMap ShotHistoryStorage::getAutoFavoriteGroupDetails(const QString& groupBy,
                                                            const QString& beanBrand,
                                                            const QString& beanType,
                                                            const QString& profileName,
                                                            const QString& grinderModel,
                                                            const QString& grinderSetting)
{
    QVariantMap result;
    if (!m_ready) return result;

    // Build WHERE clause matching the COALESCE pattern from getAutoFavorites
    QStringList conditions;
    QVariantList bindValues;

    auto addCondition = [&](const QString& column, const QString& value) {
        conditions << QString("COALESCE(%1, '') = ?").arg(column);
        bindValues << value;
    };

    if (groupBy == "bean") {
        addCondition("bean_brand", beanBrand);
        addCondition("bean_type", beanType);
    } else if (groupBy == "profile") {
        addCondition("profile_name", profileName);
    } else if (groupBy == "bean_profile_grinder") {
        addCondition("bean_brand", beanBrand);
        addCondition("bean_type", beanType);
        addCondition("profile_name", profileName);
        addCondition("grinder_model", grinderModel);
        addCondition("grinder_setting", grinderSetting);
    } else {
        // bean_profile (default)
        addCondition("bean_brand", beanBrand);
        addCondition("bean_type", beanType);
        addCondition("profile_name", profileName);
    }

    QString whereClause = " WHERE " + conditions.join(" AND ");

    // Aggregated stats query
    QString statsSql = "SELECT "
        "AVG(CASE WHEN drink_tds > 0 THEN drink_tds ELSE NULL END) as avg_tds, "
        "AVG(CASE WHEN drink_ey > 0 THEN drink_ey ELSE NULL END) as avg_ey, "
        "AVG(CASE WHEN duration_seconds > 0 THEN duration_seconds ELSE NULL END) as avg_duration, "
        "AVG(CASE WHEN dose_weight > 0 THEN dose_weight ELSE NULL END) as avg_dose, "
        "AVG(CASE WHEN final_weight > 0 THEN final_weight ELSE NULL END) as avg_yield, "
        "AVG(CASE WHEN temperature_override > 0 THEN temperature_override ELSE NULL END) as avg_temperature "
        "FROM shots" + whereClause;

    QSqlQuery statsQuery(m_db);
    statsQuery.prepare(statsSql);
    for (int i = 0; i < bindValues.size(); ++i)
        statsQuery.bindValue(i, bindValues[i]);

    if (statsQuery.exec() && statsQuery.next()) {
        result["avgTds"] = statsQuery.value("avg_tds").toDouble();
        result["avgEy"] = statsQuery.value("avg_ey").toDouble();
        result["avgDuration"] = statsQuery.value("avg_duration").toDouble();
        result["avgDose"] = statsQuery.value("avg_dose").toDouble();
        result["avgYield"] = statsQuery.value("avg_yield").toDouble();
        result["avgTemperature"] = statsQuery.value("avg_temperature").toDouble();
    }

    // Notes query: non-empty notes, newest first
    QString notesSql = "SELECT espresso_notes, timestamp FROM shots" + whereClause +
        " AND espresso_notes IS NOT NULL AND espresso_notes != '' "
        "ORDER BY timestamp DESC";

    QSqlQuery notesQuery(m_db);
    notesQuery.prepare(notesSql);
    for (int i = 0; i < bindValues.size(); ++i)
        notesQuery.bindValue(i, bindValues[i]);

    QVariantList notes;
    if (notesQuery.exec()) {
        while (notesQuery.next()) {
            QVariantMap note;
            note["text"] = notesQuery.value("espresso_notes").toString();
            qint64 ts = notesQuery.value("timestamp").toLongLong();
            note["timestamp"] = ts;
            note["dateTime"] = QDateTime::fromSecsSinceEpoch(ts).toString("yyyy-MM-dd hh:mm");
            notes.append(note);
        }
    }
    result["notes"] = notes;

    return result;
}

void ShotHistoryStorage::requestAutoFavoriteGroupDetails(const QString& groupBy,
                                                          const QString& beanBrand,
                                                          const QString& beanType,
                                                          const QString& profileName,
                                                          const QString& grinderModel,
                                                          const QString& grinderSetting)
{
    if (!m_ready) {
        emit autoFavoriteGroupDetailsReady(QVariantMap());
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;

    // Build WHERE clause on main thread (pure computation, fast)
    QStringList conditions;
    QVariantList bindValues;

    auto addCondition = [&](const QString& column, const QString& value) {
        conditions << QString("COALESCE(%1, '') = ?").arg(column);
        bindValues << value;
    };

    if (groupBy == "bean") {
        addCondition("bean_brand", beanBrand);
        addCondition("bean_type", beanType);
    } else if (groupBy == "profile") {
        addCondition("profile_name", profileName);
    } else if (groupBy == "bean_profile_grinder") {
        addCondition("bean_brand", beanBrand);
        addCondition("bean_type", beanType);
        addCondition("profile_name", profileName);
        addCondition("grinder_model", grinderModel);
        addCondition("grinder_setting", grinderSetting);
    } else {
        // bean_profile (default)
        addCondition("bean_brand", beanBrand);
        addCondition("bean_type", beanType);
        addCondition("profile_name", profileName);
    }

    QString whereClause = " WHERE " + conditions.join(" AND ");

    QString statsSql = "SELECT "
        "AVG(CASE WHEN drink_tds > 0 THEN drink_tds ELSE NULL END) as avg_tds, "
        "AVG(CASE WHEN drink_ey > 0 THEN drink_ey ELSE NULL END) as avg_ey, "
        "AVG(CASE WHEN duration_seconds > 0 THEN duration_seconds ELSE NULL END) as avg_duration, "
        "AVG(CASE WHEN dose_weight > 0 THEN dose_weight ELSE NULL END) as avg_dose, "
        "AVG(CASE WHEN final_weight > 0 THEN final_weight ELSE NULL END) as avg_yield, "
        "AVG(CASE WHEN temperature_override > 0 THEN temperature_override ELSE NULL END) as avg_temperature "
        "FROM shots" + whereClause;

    QString notesSql = "SELECT espresso_notes, timestamp FROM shots" + whereClause +
        " AND espresso_notes IS NOT NULL AND espresso_notes != '' "
        "ORDER BY timestamp DESC";

    QThread* thread = QThread::create([this, dbPath, statsSql, notesSql, bindValues, destroyed]() {
        const QString connName = QString("shs_ragd_%1")
            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);

        QVariantMap result;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
            db.setDatabaseName(dbPath);
            if (db.open()) {
                QSqlQuery(db).exec("PRAGMA busy_timeout = 5000");
                // Stats query
                QSqlQuery statsQuery(db);
                statsQuery.prepare(statsSql);
                for (int i = 0; i < bindValues.size(); ++i)
                    statsQuery.bindValue(i, bindValues[i]);

                if (statsQuery.exec() && statsQuery.next()) {
                    result["avgTds"] = statsQuery.value("avg_tds").toDouble();
                    result["avgEy"] = statsQuery.value("avg_ey").toDouble();
                    result["avgDuration"] = statsQuery.value("avg_duration").toDouble();
                    result["avgDose"] = statsQuery.value("avg_dose").toDouble();
                    result["avgYield"] = statsQuery.value("avg_yield").toDouble();
                    result["avgTemperature"] = statsQuery.value("avg_temperature").toDouble();
                }

                // Notes query
                QSqlQuery notesQuery(db);
                notesQuery.prepare(notesSql);
                for (int i = 0; i < bindValues.size(); ++i)
                    notesQuery.bindValue(i, bindValues[i]);

                QVariantList notes;
                if (notesQuery.exec()) {
                    while (notesQuery.next()) {
                        QVariantMap note;
                        note["text"] = notesQuery.value("espresso_notes").toString();
                        qint64 ts = notesQuery.value("timestamp").toLongLong();
                        note["timestamp"] = ts;
                        note["dateTime"] = QDateTime::fromSecsSinceEpoch(ts).toString("yyyy-MM-dd hh:mm");
                        notes.append(note);
                    }
                }
                result["notes"] = notes;
            } else {
                qWarning() << "ShotHistoryStorage: Failed to open DB for async getAutoFavoriteGroupDetails";
            }
        }
        QSqlDatabase::removeDatabase(connName);

        QMetaObject::invokeMethod(this, [this, result, destroyed]() {
            if (*destroyed) return;
            emit autoFavoriteGroupDetailsReady(result);
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

QString ShotHistoryStorage::exportShotData(qint64 shotId)
{
    ShotRecord record = getShotRecord(shotId);
    if (record.summary.id == 0) {
        return QString();
    }

    QString output;
    QTextStream stream(&output);

    stream << "=== Decenza DE1 Shot Export ===" << Qt::endl;
    stream << "Shot ID: " << record.summary.id << Qt::endl;
    stream << "UUID: " << record.summary.uuid << Qt::endl;
    stream << "Date: " << QDateTime::fromSecsSinceEpoch(record.summary.timestamp).toString(Qt::ISODate) << Qt::endl;
    stream << Qt::endl;

    stream << "--- Profile ---" << Qt::endl;
    stream << "Name: " << record.summary.profileName << Qt::endl;
    stream << Qt::endl;

    stream << "--- Shot Metrics ---" << Qt::endl;
    stream << "Duration: " << record.summary.duration << "s" << Qt::endl;
    stream << "Dose: " << record.summary.doseWeight << "g" << Qt::endl;
    stream << "Output: " << record.summary.finalWeight << "g" << Qt::endl;
    if (record.summary.doseWeight > 0) {
        stream << "Ratio: 1:" << QString::number(record.summary.finalWeight / record.summary.doseWeight, 'f', 1) << Qt::endl;
    }
    stream << Qt::endl;

    stream << "--- Bean Info ---" << Qt::endl;
    stream << "Brand: " << record.summary.beanBrand << Qt::endl;
    stream << "Type: " << record.summary.beanType << Qt::endl;
    stream << "Roast Date: " << record.roastDate << Qt::endl;
    stream << "Roast Level: " << record.roastLevel << Qt::endl;
    stream << Qt::endl;

    stream << "--- Grinder ---" << Qt::endl;
    stream << "Model: " << record.grinderModel << Qt::endl;
    stream << "Setting: " << record.grinderSetting << Qt::endl;
    stream << Qt::endl;

    stream << "--- Analysis ---" << Qt::endl;
    stream << "TDS: " << record.drinkTds << "%" << Qt::endl;
    stream << "EY: " << record.drinkEy << "%" << Qt::endl;
    stream << "Enjoyment: " << record.summary.enjoyment << "%" << Qt::endl;
    stream << "Notes: " << record.espressoNotes << Qt::endl;
    stream << "Barista: " << record.barista << Qt::endl;
    stream << Qt::endl;

    if (!record.visualizerId.isEmpty()) {
        stream << "--- Visualizer ---" << Qt::endl;
        stream << "ID: " << record.visualizerId << Qt::endl;
        stream << "URL: " << record.visualizerUrl << Qt::endl;
        stream << Qt::endl;
    }

    stream << "--- Debug Log ---" << Qt::endl;
    stream << record.debugLog << Qt::endl;
    stream << Qt::endl;

    stream << "--- Sample Data Summary ---" << Qt::endl;
    stream << "Pressure samples: " << record.pressure.size() << Qt::endl;
    stream << "Flow samples: " << record.flow.size() << Qt::endl;
    stream << "Temperature samples: " << record.temperature.size() << Qt::endl;
    stream << "Weight samples: " << record.weight.size() << Qt::endl;

    return output;
}

void ShotHistoryStorage::updateTotalShots()
{
    QSqlQuery query(m_db);
    query.exec("SELECT COUNT(*) FROM shots");
    if (query.next()) {
        int newCount = query.value(0).toInt();
        if (newCount != m_totalShots) {
            m_totalShots = newCount;
            emit totalShotsChanged();
        }
    }
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

QString ShotHistoryStorage::createBackup(const QString& destPath)
{
    // Prevent concurrent backup operations
    if (m_backupInProgress) {
        qWarning() << "ShotHistoryStorage: Backup already in progress";
        return QString();
    }

    if (m_dbPath.isEmpty()) {
        emit errorOccurred("Database path not set");
        return QString();
    }

    m_backupInProgress = true;

    // Use common helper for checkpoint + close + copy + reopen
    bool success = performDatabaseCopy(destPath);

    m_backupInProgress = false;

    if (success) {
        qDebug() << "ShotHistoryStorage: Created backup at" << destPath;
        return destPath;
    } else {
        QString error = "Failed to create backup at " + destPath;
        qWarning() << "ShotHistoryStorage:" << error;
        emit errorOccurred(error);
        return QString();
    }
}

void ShotHistoryStorage::requestCreateBackup(const QString& destPath)
{
    if (m_backupInProgress) {
        qWarning() << "ShotHistoryStorage: Backup already in progress";
        emit backupFinished(QString());
        return;
    }

    if (m_dbPath.isEmpty()) {
        emit errorOccurred("Database path not set");
        emit backupFinished(QString());
        return;
    }

    m_backupInProgress = true;

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;

    QThread* thread = QThread::create([this, dbPath, destPath, destroyed]() {
        QString resultPath = createBackupStatic(dbPath, destPath);

        QMetaObject::invokeMethod(this, [this, resultPath, destroyed]() {
            if (*destroyed) return;
            m_backupInProgress = false;
            emit backupFinished(resultPath);
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

bool ShotHistoryStorage::importDatabase(const QString& filePath, bool merge)
{
    // Prevent concurrent import operations
    if (m_importInProgress) {
        qWarning() << "ShotHistoryStorage: Import already in progress";
        emit errorOccurred("Import already in progress");
        return false;
    }

    if (!m_db.isOpen()) {
        emit errorOccurred("Database not open");
        return false;
    }

    m_importInProgress = true;

    // Clean up file path (remove file:// prefix if present)
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

    qDebug() << "ShotHistoryStorage: Importing from" << cleanPath << (merge ? "(merge)" : "(replace)");

    // Open source database
    QSqlDatabase srcDb = QSqlDatabase::addDatabase("QSQLITE", "import_connection");
    srcDb.setDatabaseName(cleanPath);

    if (!srcDb.open()) {
        QString error = "Failed to open import database: " + srcDb.lastError().text();
        qWarning() << "ShotHistoryStorage:" << error;
        emit errorOccurred(error);
        QSqlDatabase::removeDatabase("import_connection");
        m_importInProgress = false;
        return false;
    }

    // Verify source has shots table
    int sourceCount = 0;
    {
        QSqlQuery srcCheck(srcDb);
        if (!srcCheck.exec("SELECT COUNT(*) FROM shots")) {
            QString error = "Import file is not a valid shots database (no 'shots' table found)";
            qWarning() << "ShotHistoryStorage:" << error;
            emit errorOccurred(error);
            srcCheck.finish();  // Release query before closing connection
            srcDb.close();
            QSqlDatabase::removeDatabase("import_connection");
            m_importInProgress = false;
            return false;
        }
        srcCheck.next();
        sourceCount = srcCheck.value(0).toInt();
        srcCheck.finish();  // Release query before we might close connection
    }  // srcCheck destroyed here

    if (sourceCount == 0) {
        QString error = "Import file contains no shots (database is empty)";
        qWarning() << "ShotHistoryStorage:" << error;
        emit errorOccurred(error);
        srcDb.close();
        QSqlDatabase::removeDatabase("import_connection");
        m_importInProgress = false;
        return false;
    }

    qDebug() << "ShotHistoryStorage: Source has" << sourceCount << "shots";

    // Begin transaction on destination
    if (!m_db.transaction()) {
        qWarning() << "ShotHistoryStorage: Failed to begin transaction:" << m_db.lastError().text();
        emit errorOccurred("Failed to begin import transaction");
        srcDb.close();
        QSqlDatabase::removeDatabase("import_connection");
        m_importInProgress = false;
        return false;
    }

    if (!merge) {
        // Replace mode: delete all existing data
        QSqlQuery delQuery(m_db);
        if (!delQuery.exec("DELETE FROM shot_phases") ||
            !delQuery.exec("DELETE FROM shot_samples") ||
            !delQuery.exec("DELETE FROM shots")) {
            qWarning() << "ShotHistoryStorage: Failed to clear existing data:" << delQuery.lastError().text();
            m_db.rollback();
            emit errorOccurred("Failed to clear existing data for replace");
            srcDb.close();
            QSqlDatabase::removeDatabase("import_connection");
            m_importInProgress = false;
            return false;
        }
        qDebug() << "ShotHistoryStorage: Cleared existing data for replace";
    }

    // Get existing UUIDs for merge mode
    QSet<QString> existingUuids;
    if (merge) {
        QSqlQuery uuidQuery(m_db);
        if (!uuidQuery.exec("SELECT uuid FROM shots")) {
            qWarning() << "ShotHistoryStorage: Failed to query existing UUIDs:" << uuidQuery.lastError().text();
        }
        while (uuidQuery.next()) {
            existingUuids.insert(uuidQuery.value(0).toString());
        }
        qDebug() << "ShotHistoryStorage: Found" << existingUuids.size() << "existing shots";
    }

    // Import shots
    int imported = 0, skipped = 0;
    QSqlQuery srcShots(srcDb);
    if (!srcShots.exec("SELECT * FROM shots")) {
        qWarning() << "ShotHistoryStorage: Failed to query source shots:" << srcShots.lastError().text();
        m_db.rollback();
        emit errorOccurred("Failed to read shots from backup");
        srcDb.close();
        QSqlDatabase::removeDatabase("import_connection");
        m_importInProgress = false;
        return false;
    }

    while (srcShots.next()) {
        QString uuid = srcShots.value("uuid").toString();

        if (merge && existingUuids.contains(uuid)) {
            skipped++;
            continue;
        }

        // Insert shot
        QSqlQuery insert(m_db);
        insert.prepare(R"(
            INSERT INTO shots (uuid, timestamp, profile_name, profile_json, beverage_type,
                duration_seconds, final_weight, dose_weight,
                bean_brand, bean_type, roast_date, roast_level,
                grinder_model, grinder_setting, drink_tds, drink_ey,
                enjoyment, espresso_notes, bean_notes, barista,
                profile_notes, visualizer_id, visualizer_url, debug_log,
                temperature_override, yield_override)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        )");

        insert.addBindValue(uuid);
        insert.addBindValue(srcShots.value("timestamp"));
        insert.addBindValue(srcShots.value("profile_name"));
        insert.addBindValue(srcShots.value("profile_json"));
        insert.addBindValue(srcShots.value("beverage_type"));
        insert.addBindValue(srcShots.value("duration_seconds"));
        insert.addBindValue(srcShots.value("final_weight"));
        insert.addBindValue(srcShots.value("dose_weight"));
        insert.addBindValue(srcShots.value("bean_brand"));
        insert.addBindValue(srcShots.value("bean_type"));
        insert.addBindValue(srcShots.value("roast_date"));
        insert.addBindValue(srcShots.value("roast_level"));
        insert.addBindValue(srcShots.value("grinder_model"));
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

        if (!insert.exec()) {
            qWarning() << "ShotHistoryStorage: Failed to import shot:" << insert.lastError().text();
            continue;
        }

        qint64 oldId = srcShots.value("id").toLongLong();
        qint64 newId = insert.lastInsertId().toLongLong();

        // Import samples for this shot
        QSqlQuery srcSamples(srcDb);
        srcSamples.prepare("SELECT sample_count, data_blob FROM shot_samples WHERE shot_id = ?");
        srcSamples.addBindValue(oldId);
        if (srcSamples.exec() && srcSamples.next()) {
            QSqlQuery insertSample(m_db);
            insertSample.prepare("INSERT INTO shot_samples (shot_id, sample_count, data_blob) VALUES (?, ?, ?)");
            insertSample.addBindValue(newId);
            insertSample.addBindValue(srcSamples.value(0));
            insertSample.addBindValue(srcSamples.value(1));
            insertSample.exec();
        }

        // Import phases for this shot (try with transition_reason, fall back for older DBs)
        QSqlQuery srcPhases(srcDb);
        srcPhases.prepare("SELECT time_offset, label, frame_number, is_flow_mode, transition_reason FROM shot_phases WHERE shot_id = ?");
        srcPhases.addBindValue(oldId);
        bool hasReason = srcPhases.exec();
        if (!hasReason) {
            srcPhases.prepare("SELECT time_offset, label, frame_number, is_flow_mode FROM shot_phases WHERE shot_id = ?");
            srcPhases.addBindValue(oldId);
            hasReason = false;
            srcPhases.exec();
        } else {
            hasReason = true;
        }
        while (srcPhases.next()) {
            QSqlQuery insertPhase(m_db);
            insertPhase.prepare("INSERT INTO shot_phases (shot_id, time_offset, label, frame_number, is_flow_mode, transition_reason) VALUES (?, ?, ?, ?, ?, ?)");
            insertPhase.addBindValue(newId);
            insertPhase.addBindValue(srcPhases.value(0));
            insertPhase.addBindValue(srcPhases.value(1));
            insertPhase.addBindValue(srcPhases.value(2));
            insertPhase.addBindValue(srcPhases.value(3));
            insertPhase.addBindValue(hasReason ? srcPhases.value(4).toString() : QString());
            insertPhase.exec();
        }

        imported++;
    }

    if (!m_db.commit()) {
        qWarning() << "ShotHistoryStorage: Failed to commit import transaction:" << m_db.lastError().text();
        m_db.rollback();
        emit errorOccurred("Failed to commit imported data (disk full?)");
        srcDb.close();
        QSqlDatabase::removeDatabase("import_connection");
        m_importInProgress = false;
        return false;
    }

    // Backfill beverage_type from profile_json for imported shots from old DBs
    backfillBeverageType();

    // Clean up source connection
    srcDb.close();
    QSqlDatabase::removeDatabase("import_connection");

    updateTotalShots();
    invalidateDistinctCache();

    m_importInProgress = false;

    qDebug() << "ShotHistoryStorage: Import complete -" << imported << "imported," << skipped << "skipped";
    return true;
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

        QMetaObject::invokeMethod(this, [this, success, destroyed]() {
            if (*destroyed) return;
            m_importInProgress = false;
            if (success) {
                refreshTotalShots();
                invalidateDistinctCache();
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
                        grinder_model, grinder_setting, drink_tds, drink_ey,
                        enjoyment, espresso_notes, bean_notes, barista,
                        profile_notes, visualizer_id, visualizer_url, debug_log,
                        temperature_override, yield_override)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
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
                insert.addBindValue(srcShots.value("grinder_model"));
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
                        update.prepare("UPDATE shots SET beverage_type = ? WHERE id = ?");
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
    const QString connName = QString("count_%1")
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);

    int count = -1;  // -1 = error (distinguishes from 0 = empty)
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery query(db);
            if (query.exec("SELECT COUNT(*) FROM shots") && query.next())
                count = query.value(0).toInt();
            db.close();
        } else {
            qWarning() << "ShotHistoryStorage::getShotCountStatic: Failed to open DB:" << db.lastError().text();
        }
    }
    QSqlDatabase::removeDatabase(connName);
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
            grinder_model, grinder_setting,
            drink_tds, drink_ey, enjoyment, espresso_notes, bean_notes, barista,
            profile_notes, debug_log,
            temperature_override, yield_override
        ) VALUES (
            :uuid, :timestamp, :profile_name, :profile_json, :beverage_type,
            :duration, :final_weight, :dose_weight,
            :bean_brand, :bean_type, :roast_date, :roast_level,
            :grinder_model, :grinder_setting,
            :drink_tds, :drink_ey, :enjoyment, :espresso_notes, :bean_notes, :barista,
            :profile_notes, :debug_log,
            :temperature_override, :yield_override
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
    query.bindValue(":grinder_model", record.grinderModel);
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
    query.bindValue(":yield_override", record.yieldOverride);

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
    int sampleCount = record.pressure.size();

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

QStringList ShotHistoryStorage::getDistinctBeanTypesForBrand(const QString& beanBrand)
{
    QStringList results;
    if (!m_ready) return results;

    QString sql;
    QSqlQuery query(m_db);

    if (beanBrand.isEmpty()) {
        // Fallback to all bean types if no brand specified
        return getDistinctBeanTypes();
    }

    sql = "SELECT DISTINCT bean_type FROM shots "
          "WHERE bean_brand = ? AND bean_type IS NOT NULL AND bean_type != '' "
          "ORDER BY bean_type";

    query.prepare(sql);
    query.bindValue(0, beanBrand);
    query.exec();

    while (query.next()) {
        QString value = query.value(0).toString();
        if (!value.isEmpty()) {
            results << value;
        }
    }

    return results;
}

QStringList ShotHistoryStorage::getDistinctGrinderSettingsForGrinder(const QString& grinderModel)
{
    QStringList results;
    if (!m_ready) return results;

    QString sql;
    QSqlQuery query(m_db);

    if (grinderModel.isEmpty()) {
        // Fallback to all grinder settings if no grinder specified
        return getDistinctGrinderSettings();
    }

    sql = "SELECT DISTINCT grinder_setting FROM shots "
          "WHERE grinder_model = ? AND grinder_setting IS NOT NULL AND grinder_setting != '' "
          "ORDER BY grinder_setting";

    query.prepare(sql);
    query.bindValue(0, grinderModel);
    query.exec();

    while (query.next()) {
        QString value = query.value(0).toString();
        if (!value.isEmpty()) {
            results << value;
        }
    }

    // Apply smart sorting
    sortGrinderSettings(results);
    return results;
}

void ShotHistoryStorage::sortGrinderSettings(QStringList& settings)
{
    if (settings.isEmpty()) {
        return;
    }

    // Check if all values parse as numbers
    bool allNumeric = true;
    for (const QString& setting : settings) {
        bool ok = false;
        setting.toDouble(&ok);
        if (!ok) {
            allNumeric = false;
            break;
        }
    }

    if (allNumeric) {
        // Sort numerically
        std::sort(settings.begin(), settings.end(), [](const QString& a, const QString& b) {
            return a.toDouble() < b.toDouble();
        });
    } else {
        // Sort alphabetically with natural ordering
        std::sort(settings.begin(), settings.end(), [](const QString& a, const QString& b) {
            return QString::localeAwareCompare(a, b) < 0;
        });
    }
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
            update.prepare("UPDATE shots SET beverage_type = ? WHERE id = ?");
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
    // Invalidate caches immediately (no DB I/O)
    invalidateDistinctCache();

    // Run COUNT query on background thread to avoid blocking the main thread
    QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    QThread* thread = QThread::create([this, dbPath, destroyed]() {
        int count = getShotCountStatic(dbPath);
        QMetaObject::invokeMethod(this, [this, count, destroyed]() {
            if (*destroyed) return;
            if (count < 0) return;  // Ignore errors, keep previous count
            if (count != m_totalShots) {
                m_totalShots = count;
                emit totalShotsChanged();
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
