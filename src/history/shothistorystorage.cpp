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
#include <QDebug>

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

    // Full-text search
    QString createFts = R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS shots_fts USING fts5(
            espresso_notes,
            bean_brand,
            bean_type,
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
            INSERT INTO shots_fts(rowid, espresso_notes, bean_brand, bean_type)
            VALUES (new.id, new.espresso_notes, new.bean_brand, new.bean_type);
        END
    )");

    query.exec(R"(
        CREATE TRIGGER IF NOT EXISTS shots_ad AFTER DELETE ON shots BEGIN
            INSERT INTO shots_fts(shots_fts, rowid, espresso_notes, bean_brand, bean_type)
            VALUES ('delete', old.id, old.espresso_notes, old.bean_brand, old.bean_type);
        END
    )");

    query.exec(R"(
        CREATE TRIGGER IF NOT EXISTS shots_au AFTER UPDATE ON shots BEGIN
            INSERT INTO shots_fts(shots_fts, rowid, espresso_notes, bean_brand, bean_type)
            VALUES ('delete', old.id, old.espresso_notes, old.bean_brand, old.bean_type);
            INSERT INTO shots_fts(rowid, espresso_notes, bean_brand, bean_type)
            VALUES (new.id, new.espresso_notes, new.bean_brand, new.bean_type);
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

    // Future migrations go here
    // if (currentVersion < 2) { ... }

    m_schemaVersion = currentVersion;
    return true;
}

QByteArray ShotHistoryStorage::compressSampleData(ShotDataModel* shotData)
{
    QJsonObject root;

    // Convert QVector<QPointF> to JSON arrays
    auto pointsToArray = [](const QVector<QPointF>& points) {
        QJsonArray timeArr, valueArr;
        for (const auto& pt : points) {
            timeArr.append(pt.x());
            valueArr.append(pt.y());
        }
        QJsonObject obj;
        obj["t"] = timeArr;
        obj["v"] = valueArr;
        return obj;
    };

    root["pressure"] = pointsToArray(shotData->pressureData());
    root["flow"] = pointsToArray(shotData->flowData());
    root["temperature"] = pointsToArray(shotData->temperatureData());
    root["pressureGoal"] = pointsToArray(shotData->pressureGoalData());
    root["flowGoal"] = pointsToArray(shotData->flowGoalData());
    root["temperatureGoal"] = pointsToArray(shotData->temperatureGoalData());

    // Weight data needs to be scaled back (stored as /5 in model)
    QVector<QPointF> weightData = shotData->weightData();
    for (auto& pt : weightData) {
        pt.setY(pt.y() * 5.0);  // Undo the /5 scaling
    }
    root["weight"] = pointsToArray(weightData);

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
    record->weight = arrayToPoints(root["weight"].toObject());
}

qint64 ShotHistoryStorage::saveShot(ShotDataModel* shotData,
                                     const Profile* profile,
                                     double duration,
                                     double finalWeight,
                                     double doseWeight,
                                     const ShotMetadata& metadata,
                                     const QString& debugLog)
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
            uuid, timestamp, profile_name, profile_json,
            duration_seconds, final_weight, dose_weight,
            bean_brand, bean_type, roast_date, roast_level,
            grinder_model, grinder_setting,
            drink_tds, drink_ey, enjoyment, espresso_notes, barista,
            debug_log
        ) VALUES (
            :uuid, :timestamp, :profile_name, :profile_json,
            :duration, :final_weight, :dose_weight,
            :bean_brand, :bean_type, :roast_date, :roast_level,
            :grinder_model, :grinder_setting,
            :drink_tds, :drink_ey, :enjoyment, :espresso_notes, :barista,
            :debug_log
        )
    )");

    query.bindValue(":uuid", uuid);
    query.bindValue(":timestamp", timestamp);
    query.bindValue(":profile_name", profileName);
    query.bindValue(":profile_json", profileJson);
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
    query.bindValue(":barista", metadata.barista);
    query.bindValue(":debug_log", debugLog);

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
            INSERT INTO shot_phases (shot_id, time_offset, label, frame_number, is_flow_mode)
            VALUES (:shot_id, :time, :label, :frame, :flow_mode)
        )");
        query.bindValue(":shot_id", shotId);
        query.bindValue(":time", marker["time"].toDouble());
        query.bindValue(":label", marker["label"].toString());
        query.bindValue(":frame", marker["frameNumber"].toInt());
        query.bindValue(":flow_mode", marker["isFlowMode"].toBool() ? 1 : 0);
        query.exec();  // Non-critical if markers fail
    }

    m_db.commit();

    m_lastSavedShotId = shotId;
    updateTotalShots();

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

QVariantList ShotHistoryStorage::getShotsFiltered(const QVariantMap& filterMap, int offset, int limit)
{
    QVariantList results;
    if (!m_ready) return results;

    ShotFilter filter = parseFilterMap(filterMap);
    QVariantList bindValues;
    QString whereClause = buildFilterQuery(filter, bindValues);

    // Handle FTS search separately
    QString sql;
    if (!filter.searchText.isEmpty()) {
        sql = QString(R"(
            SELECT s.id, s.uuid, s.timestamp, s.profile_name, s.duration_seconds,
                   s.final_weight, s.dose_weight, s.bean_brand, s.bean_type,
                   s.enjoyment, s.visualizer_id
            FROM shots s
            JOIN shots_fts fts ON s.id = fts.rowid
            WHERE shots_fts MATCH ?
            %1
            ORDER BY s.timestamp DESC
            LIMIT ? OFFSET ?
        )").arg(whereClause.isEmpty() ? "" : " AND " + whereClause.mid(7));  // Remove " WHERE "
        bindValues.prepend(filter.searchText);
    } else {
        sql = QString(R"(
            SELECT id, uuid, timestamp, profile_name, duration_seconds,
                   final_weight, dose_weight, bean_brand, bean_type,
                   enjoyment, visualizer_id
            FROM shots
            %1
            ORDER BY timestamp DESC
            LIMIT ? OFFSET ?
        )").arg(whereClause);
    }

    bindValues << limit << offset;

    QSqlQuery query(m_db);
    query.prepare(sql);
    for (int i = 0; i < bindValues.size(); ++i) {
        query.bindValue(i, bindValues[i]);
    }

    if (!query.exec()) {
        qWarning() << "ShotHistoryStorage: Query failed:" << query.lastError().text();
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

        // Format date for display
        QDateTime dt = QDateTime::fromSecsSinceEpoch(query.value(2).toLongLong());
        shot["dateTime"] = dt.toString("yyyy-MM-dd hh:mm");

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

    // Full metadata
    result["roastDate"] = record.roastDate;
    result["roastLevel"] = record.roastLevel;
    result["grinderModel"] = record.grinderModel;
    result["grinderSetting"] = record.grinderSetting;
    result["drinkTds"] = record.drinkTds;
    result["drinkEy"] = record.drinkEy;
    result["espressoNotes"] = record.espressoNotes;
    result["barista"] = record.barista;
    result["visualizerId"] = record.visualizerId;
    result["visualizerUrl"] = record.visualizerUrl;
    result["debugLog"] = record.debugLog;
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
    result["pressureGoal"] = pointsToVariant(record.pressureGoal);
    result["flowGoal"] = pointsToVariant(record.flowGoal);
    result["temperatureGoal"] = pointsToVariant(record.temperatureGoal);
    result["weight"] = pointsToVariant(record.weight);

    // Phase markers
    QVariantList phases;
    for (const auto& phase : record.phases) {
        QVariantMap p;
        p["time"] = phase.time;
        p["label"] = phase.label;
        p["frameNumber"] = phase.frameNumber;
        p["isFlowMode"] = phase.isFlowMode;
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
               drink_tds, drink_ey, enjoyment, espresso_notes, barista,
               visualizer_id, visualizer_url, debug_log
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
    record.barista = query.value(18).toString();
    record.visualizerId = query.value(19).toString();
    record.visualizerUrl = query.value(20).toString();
    record.debugLog = query.value(21).toString();
    record.summary.hasVisualizerUpload = !record.visualizerId.isEmpty();

    // Load sample data
    query.prepare("SELECT data_blob FROM shot_samples WHERE shot_id = ?");
    query.bindValue(0, shotId);
    if (query.exec() && query.next()) {
        QByteArray blob = query.value(0).toByteArray();
        decompressSampleData(blob, &record);
    }

    // Load phase markers
    query.prepare("SELECT time_offset, label, frame_number, is_flow_mode FROM shot_phases WHERE shot_id = ? ORDER BY time_offset");
    query.bindValue(0, shotId);
    if (query.exec()) {
        while (query.next()) {
            HistoryPhaseMarker marker;
            marker.time = query.value(0).toDouble();
            marker.label = query.value(1).toString();
            marker.frameNumber = query.value(2).toInt();
            marker.isFlowMode = query.value(3).toInt() != 0;
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

QStringList ShotHistoryStorage::getDistinctProfiles()
{
    QStringList results;
    if (!m_ready) return results;

    QSqlQuery query(m_db);
    query.exec("SELECT DISTINCT profile_name FROM shots WHERE profile_name IS NOT NULL ORDER BY profile_name");
    while (query.next()) {
        QString name = query.value(0).toString();
        if (!name.isEmpty()) {
            results << name;
        }
    }
    return results;
}

QStringList ShotHistoryStorage::getDistinctBeanBrands()
{
    QStringList results;
    if (!m_ready) return results;

    QSqlQuery query(m_db);
    query.exec("SELECT DISTINCT bean_brand FROM shots WHERE bean_brand IS NOT NULL AND bean_brand != '' ORDER BY bean_brand");
    while (query.next()) {
        results << query.value(0).toString();
    }
    return results;
}

QStringList ShotHistoryStorage::getDistinctBeanTypes()
{
    QStringList results;
    if (!m_ready) return results;

    QSqlQuery query(m_db);
    query.exec("SELECT DISTINCT bean_type FROM shots WHERE bean_type IS NOT NULL AND bean_type != '' ORDER BY bean_type");
    while (query.next()) {
        results << query.value(0).toString();
    }
    return results;
}

QStringList ShotHistoryStorage::getDistinctGrinders()
{
    QStringList results;
    if (!m_ready) return results;

    QSqlQuery query(m_db);
    query.exec("SELECT DISTINCT grinder_model FROM shots WHERE grinder_model IS NOT NULL AND grinder_model != '' ORDER BY grinder_model");
    while (query.next()) {
        results << query.value(0).toString();
    }
    return results;
}

QStringList ShotHistoryStorage::getDistinctRoastLevels()
{
    QStringList results;
    if (!m_ready) return results;

    QSqlQuery query(m_db);
    query.exec("SELECT DISTINCT roast_level FROM shots WHERE roast_level IS NOT NULL AND roast_level != '' ORDER BY roast_level");
    while (query.next()) {
        results << query.value(0).toString();
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
