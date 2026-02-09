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
#include <algorithm>

const QString ShotHistoryStorage::DB_CONNECTION_NAME = "ShotHistoryConnection";

ShotHistoryStorage::ShotHistoryStorage(QObject* parent)
    : QObject(parent)
{
}

ShotHistoryStorage::~ShotHistoryStorage()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    QSqlDatabase::removeDatabase(DB_CONNECTION_NAME);
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
    query.exec("INSERT OR IGNORE INTO schema_version (version) VALUES (1)");

    return true;
}

bool ShotHistoryStorage::runMigrations()
{
    QSqlQuery query(m_db);
    query.exec("SELECT version FROM schema_version LIMIT 1");
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
    if (!m_ready || !shotData) {
        qWarning() << "ShotHistoryStorage: Cannot save shot - not ready or no data";
        return -1;
    }

    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    qint64 timestamp = QDateTime::currentSecsSinceEpoch();

    // Serialize profile to JSON
    QString profileJson;
    QString profileName = "Unknown";
    if (profile) {
        profileName = profile->title();
        profileJson = QString::fromUtf8(profile->toJson().toJson(QJsonDocument::Compact));
    }

    QSqlQuery query(m_db);

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

    query.bindValue(":uuid", uuid);
    query.bindValue(":timestamp", timestamp);
    query.bindValue(":profile_name", profileName);
    query.bindValue(":profile_json", profileJson);
    query.bindValue(":beverage_type", profile ? profile->beverageType() : QStringLiteral("espresso"));
    query.bindValue(":duration", duration);
    query.bindValue(":final_weight", finalWeight);
    query.bindValue(":dose_weight", doseWeight);
    query.bindValue(":bean_brand", metadata.beanBrand);
    query.bindValue(":bean_type", metadata.beanType);
    query.bindValue(":roast_date", metadata.roastDate);
    query.bindValue(":roast_level", metadata.roastLevel);
    query.bindValue(":grinder_model", metadata.grinderModel);
    query.bindValue(":grinder_setting", metadata.grinderSetting);
    query.bindValue(":drink_tds", metadata.drinkTds);
    query.bindValue(":drink_ey", metadata.drinkEy);
    query.bindValue(":enjoyment", metadata.espressoEnjoyment);
    query.bindValue(":espresso_notes", metadata.espressoNotes);
    query.bindValue(":bean_notes", QString());  // Not in ShotMetadata yet
    query.bindValue(":barista", metadata.barista);
    query.bindValue(":profile_notes", profile ? profile->profileNotes() : QString());
    query.bindValue(":debug_log", debugLog);

    // Bind override values (always have values - user override or profile default)
    query.bindValue(":temperature_override", temperatureOverride);
    query.bindValue(":yield_override", yieldOverride);

    if (!query.exec()) {
        qWarning() << "ShotHistoryStorage: Failed to insert shot:" << query.lastError().text();
        m_db.rollback();
        emit errorOccurred("Failed to save shot: " + query.lastError().text());
        return -1;
    }

    qint64 shotId = query.lastInsertId().toLongLong();

    // Insert compressed sample data
    QByteArray compressedData = compressSampleData(shotData);
    int sampleCount = shotData->pressureData().size();

    query.prepare("INSERT INTO shot_samples (shot_id, sample_count, data_blob) VALUES (:id, :count, :blob)");
    query.bindValue(":id", shotId);
    query.bindValue(":count", sampleCount);
    query.bindValue(":blob", compressedData);

    if (!query.exec()) {
        qWarning() << "ShotHistoryStorage: Failed to insert samples:" << query.lastError().text();
        m_db.rollback();
        emit errorOccurred("Failed to save shot samples");
        return -1;
    }

    // Insert phase markers
    QVariantList markers = shotData->phaseMarkersVariant();
    for (const QVariant& markerVar : markers) {
        QVariantMap marker = markerVar.toMap();
        query.prepare(R"(
            INSERT INTO shot_phases (shot_id, time_offset, label, frame_number, is_flow_mode, transition_reason)
            VALUES (:shot_id, :time, :label, :frame, :flow_mode, :reason)
        )");
        query.bindValue(":shot_id", shotId);
        query.bindValue(":time", marker["time"].toDouble());
        query.bindValue(":label", marker["label"].toString());
        query.bindValue(":frame", marker["frameNumber"].toInt());
        query.bindValue(":flow_mode", marker["isFlowMode"].toBool() ? 1 : 0);
        query.bindValue(":reason", marker["transitionReason"].toString());
        query.exec();  // Non-critical if markers fail
    }

    m_db.commit();

    m_lastSavedShotId = shotId;
    updateTotalShots();

    // Checkpoint WAL to main database file after each shot
    // This ensures data is persisted to the .db file and not just in .db-wal
    QSqlQuery walQuery(m_db);
    walQuery.exec("PRAGMA wal_checkpoint(PASSIVE)");

    qDebug() << "ShotHistoryStorage: Saved shot" << shotId
             << "- Profile:" << profileName
             << "- Duration:" << duration << "s"
             << "- Samples:" << sampleCount
             << "- Compressed size:" << compressedData.size() << "bytes";

    emit shotSaved(shotId);
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
    filter.minEnjoyment = filterMap.value("minEnjoyment", 0).toInt();
    filter.maxEnjoyment = filterMap.value("maxEnjoyment", 100).toInt();
    filter.dateFrom = filterMap.value("dateFrom", 0).toLongLong();
    filter.dateTo = filterMap.value("dateTo", 0).toLongLong();
    filter.searchText = filterMap.value("searchText").toString();
    filter.onlyWithVisualizer = filterMap.value("onlyWithVisualizer", false).toBool();
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
    if (filter.minEnjoyment > 0) {
        conditions << "enjoyment >= ?";
        bindValues << filter.minEnjoyment;
    }
    if (filter.maxEnjoyment < 100) {
        conditions << "enjoyment <= ?";
        bindValues << filter.maxEnjoyment;
    }
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

QVariantList ShotHistoryStorage::getShotsFiltered(const QVariantMap& filterMap, int offset, int limit)
{
    QVariantList results;
    if (!m_ready) return results;

    ShotFilter filter = parseFilterMap(filterMap);
    QVariantList bindValues;
    QString whereClause = buildFilterQuery(filter, bindValues);

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
                   temperature_override, yield_override, beverage_type
            FROM shots
            WHERE id IN (SELECT rowid FROM shots_fts WHERE shots_fts MATCH '%1')
            %2
            ORDER BY timestamp DESC
            LIMIT ? OFFSET ?
        )").arg(ftsQuery)
           .arg(extraConditions);
    } else {
        sql = QString(R"(
            SELECT id, uuid, timestamp, profile_name, duration_seconds,
                   final_weight, dose_weight, bean_brand, bean_type,
                   enjoyment, visualizer_id, grinder_setting,
                   temperature_override, yield_override, beverage_type
            FROM shots
            %1
            ORDER BY timestamp DESC
            LIMIT ? OFFSET ?
        )").arg(whereClause);
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

        // Format date for display
        QDateTime dt = QDateTime::fromSecsSinceEpoch(query.value(2).toLongLong());
        shot["dateTime"] = dt.toString("yyyy-MM-dd HH:mm");

        results.append(shot);
    }

    return results;
}

QVariantMap ShotHistoryStorage::getShot(qint64 shotId)
{
    ShotRecord record = getShotRecord(shotId);
    QVariantMap result;

    if (record.summary.id == 0) {
        return result;
    }

    // Summary fields
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

    // Full metadata
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

    // Export overrides (always have values - user override or profile default)
    result["temperatureOverride"] = record.temperatureOverride;
    result["yieldOverride"] = record.yieldOverride;

    result["profileJson"] = record.profileJson;

    // Convert time-series to variant lists
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

    // Phase markers
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

    // Format date
    QDateTime dt = QDateTime::fromSecsSinceEpoch(record.summary.timestamp);
    result["dateTime"] = dt.toString("yyyy-MM-dd hh:mm:ss");

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
    emit shotDeleted(shotId);

    qDebug() << "ShotHistoryStorage: Deleted shot" << shotId;
    return true;
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

    qDebug() << "ShotHistoryStorage: Updated metadata for shot" << shotId;
    return true;
}

// Helper for all getDistinct* methods
QStringList ShotHistoryStorage::getDistinctValues(const QString& column)
{
    QStringList results;
    if (!m_ready) return results;

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
    return results;
}

// Helper for all getDistinct*Filtered methods
QStringList ShotHistoryStorage::getDistinctValuesFiltered(const QString& column,
                                                           const QString& excludeColumn,
                                                           const QVariantMap& filter)
{
    QStringList results;
    if (!m_ready) return results;

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

QString ShotHistoryStorage::exportDatabase()
{
    if (m_dbPath.isEmpty()) {
        emit errorOccurred("Database path not set");
        return QString();
    }

    // Export to Downloads folder
    QString downloadsDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (downloadsDir.isEmpty()) {
        // Fallback to Documents
        downloadsDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString destPath = downloadsDir + "/shots_" + timestamp + ".db";

    // Close database temporarily to ensure all data is flushed
    m_db.close();

    // Copy file
    bool success = QFile::copy(m_dbPath, destPath);

    // Reopen database
    m_db.open();

    if (success) {
        qDebug() << "ShotHistoryStorage: Exported database to" << destPath;
        return destPath;
    } else {
        QString error = "Failed to export database to " + destPath;
        qWarning() << "ShotHistoryStorage:" << error;
        emit errorOccurred(error);
        return QString();
    }
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
    if (!m_db.isOpen()) {
        emit errorOccurred("Database not open");
        return false;
    }

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
        return false;
    }

    qDebug() << "ShotHistoryStorage: Source has" << sourceCount << "shots";

    // Begin transaction on destination
    m_db.transaction();

    if (!merge) {
        // Replace mode: delete all existing data
        QSqlQuery delQuery(m_db);
        delQuery.exec("DELETE FROM shot_phases");
        delQuery.exec("DELETE FROM shot_samples");
        delQuery.exec("DELETE FROM shots");
        qDebug() << "ShotHistoryStorage: Cleared existing data for replace";
    }

    // Get existing UUIDs for merge mode
    QSet<QString> existingUuids;
    if (merge) {
        QSqlQuery uuidQuery(m_db);
        uuidQuery.exec("SELECT uuid FROM shots");
        while (uuidQuery.next()) {
            existingUuids.insert(uuidQuery.value(0).toString());
        }
        qDebug() << "ShotHistoryStorage: Found" << existingUuids.size() << "existing shots";
    }

    // Import shots
    int imported = 0, skipped = 0;
    QSqlQuery srcShots(srcDb);
    srcShots.exec("SELECT * FROM shots");

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

    m_db.commit();

    // Backfill beverage_type from profile_json for imported shots from old DBs
    backfillBeverageType();

    // Clean up source connection
    srcDb.close();
    QSqlDatabase::removeDatabase("import_connection");

    updateTotalShots();

    qDebug() << "ShotHistoryStorage: Import complete -" << imported << "imported," << skipped << "skipped";
    return true;
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
    query.exec("SELECT id, profile_json FROM shots WHERE beverage_type = 'espresso' AND profile_json IS NOT NULL AND profile_json != ''");

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
    updateTotalShots();
}
