#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QHash>
#include <QSet>
#include <QVariantList>
#include <QVector>
#include <QPointF>
#include <QDateTime>
#include <atomic>
#include <memory>

class QThread;

class ShotDataModel;
class Profile;
struct ShotMetadata;

// Lightweight shot summary for list display
struct HistoryShotSummary {
    qint64 id = 0;
    QString uuid;
    qint64 timestamp = 0;
    QString profileName;
    double duration = 0;
    double finalWeight = 0;
    double doseWeight = 0;
    QString beanBrand;
    QString beanType;
    int enjoyment = 0;
    bool hasVisualizerUpload = false;
    QString beverageType;
};

// Phase marker for shot display
struct HistoryPhaseMarker {
    double time = 0;
    QString label;
    int frameNumber = 0;
    bool isFlowMode = false;
    QString transitionReason;  // "weight", "pressure", "flow", "time", or "" (unknown/old data)
};

// Full shot record for detail view / comparison
struct ShotRecord {
    HistoryShotSummary summary;

    // Full metadata
    QString roastDate;
    QString roastLevel;
    QString grinderBrand;
    QString grinderModel;
    QString grinderBurrs;
    QString grinderSetting;
    double drinkTds = 0;
    double drinkEy = 0;
    QString espressoNotes;
    QString beanNotes;
    QString barista;
    QString profileNotes;
    QString visualizerId;
    QString visualizerUrl;

    // Time-series data (lazily loaded)
    QVector<QPointF> pressure;
    QVector<QPointF> flow;
    QVector<QPointF> temperature;
    QVector<QPointF> pressureGoal;
    QVector<QPointF> flowGoal;
    QVector<QPointF> temperatureGoal;
    QVector<QPointF> temperatureMix;
    QVector<QPointF> resistance;
    QVector<QPointF> waterDispensed;
    QVector<QPointF> weight;
    QVector<QPointF> weightFlowRate;  // Flow rate from scale (g/s) for visualizer export

    // Phase markers
    QList<HistoryPhaseMarker> phases;

    // Debug log
    QString debugLog;

    // Brew overrides (always have values - user override or profile default)
    double temperatureOverride = 0.0;
    double yieldOverride = 0.0;

    // Profile snapshot
    QString profileJson;

    // AI knowledge base ID (e.g. "d-flow", "blooming espresso") for profile-aware analysis
    QString profileKbId;
};

// Grinder settings context from shot history (shared by MCP and in-app AI)
struct GrinderContext {
    QString model;
    QString beverageType;
    QStringList settingsObserved;
    bool allNumeric = false;
    double minSetting = 0;
    double maxSetting = 0;
    double smallestStep = 0;
};

// Filter criteria for queries
struct ShotFilter {
    QString profileName;
    QString beanBrand;
    QString beanType;
    QString grinderBrand;
    QString grinderModel;
    QString grinderBurrs;
    QString grinderSetting;
    QString roastLevel;
    int minEnjoyment = -1;
    int maxEnjoyment = -1;
    double minDose = -1;
    double maxDose = -1;
    double minYield = -1;
    double maxYield = -1;
    double minDuration = -1;
    double maxDuration = -1;
    double minTds = -1;
    double maxTds = -1;
    double minEy = -1;
    double maxEy = -1;
    qint64 dateFrom = 0;       // Unix timestamp
    qint64 dateTo = 0;
    QString searchText;        // FTS search in notes
    bool onlyWithVisualizer = false;
    QString sortColumn = "timestamp";
    QString sortDirection = "DESC";
};

// Pre-extracted data for async shot saving (no QObject pointers, thread-safe by value)
struct ShotSaveData {
    QString uuid;
    qint64 timestamp = 0;
    QString profileName;
    QString profileJson;
    QString beverageType;
    double duration = 0;
    double finalWeight = 0;
    double doseWeight = 0;
    double temperatureOverride = 0;
    double yieldOverride = 0;

    // Metadata
    QString beanBrand;
    QString beanType;
    QString roastDate;
    QString roastLevel;
    QString grinderBrand;
    QString grinderModel;
    QString grinderBurrs;
    QString grinderSetting;
    double drinkTds = 0;
    double drinkEy = 0;
    int espressoEnjoyment = 0;
    QString espressoNotes;
    QString barista;
    QString profileNotes;
    QString debugLog;

    // AI knowledge base ID (e.g. "d-flow", "blooming espresso") — computed at save time
    QString profileKbId;

    // Pre-compressed sample data blob
    QByteArray compressedSamples;
    int sampleCount = 0;

    // Phase markers (pre-extracted from QVariantList)
    QList<HistoryPhaseMarker> phaseMarkers;
};

class ShotHistoryStorage : public QObject {
    Q_OBJECT

    Q_PROPERTY(int totalShots READ totalShots NOTIFY totalShotsChanged)
    Q_PROPERTY(bool isReady READ isReady NOTIFY readyChanged)
    Q_PROPERTY(bool loadingFiltered READ loadingFiltered NOTIFY loadingFilteredChanged)

public:
    explicit ShotHistoryStorage(QObject* parent = nullptr);
    ~ShotHistoryStorage();

    // Database lifecycle
    bool initialize(const QString& dbPath = QString());
    bool isReady() const { return m_ready; }
    int totalShots() const { return m_totalShots; }
    bool loadingFiltered() const { return m_loadingFiltered; }

    // Save a completed shot (async). Extracts data on main thread, runs DB work on background thread.
    // Returns 0 if async save started, -1 if preconditions not met (shotSaved(-1) also emitted).
    // Actual shot ID delivered via shotSaved() signal.
    qint64 saveShot(ShotDataModel* shotData,
                    const Profile* profile,
                    double duration,
                    double finalWeight,
                    double doseWeight,
                    const ShotMetadata& metadata,
                    const QString& debugLog,
                    double temperatureOverride,
                    double yieldOverride);

    // Async: runs update on background thread, emits visualizerInfoUpdated()
    Q_INVOKABLE void requestUpdateVisualizerInfo(qint64 shotId,
                                                  const QString& visualizerId,
                                                  const QString& visualizerUrl);

    // Async: runs SQL on a background thread and emits shotsFilteredReady()
    Q_INVOKABLE void requestShotsFiltered(const QVariantMap& filter, int offset = 0, int limit = 50);

    // Async: runs on background thread, emits shotReady()
    Q_INVOKABLE void requestShot(qint64 shotId);

    // Async: runs on background thread, emits recentShotsByKbIdReady()
    // Returns summary data (not full time-series) for dial-in history queries.
    Q_INVOKABLE void requestRecentShotsByKbId(const QString& kbId, int limit = 10);

    // Static version for background-thread use — caller provides their own connection.
    static ShotRecord loadShotRecordStatic(QSqlDatabase& db, qint64 shotId);

    // Query observed grinder settings for a grinder model + beverage type.
    // Thread-safe: caller provides their own connection. Shared by MCP and in-app AI.
    static GrinderContext queryGrinderContext(QSqlDatabase& db, const QString& grinderModel, const QString& beverageType);

    // Convert ShotRecord to QVariantMap (shared by requestShot, ShotServer, AIManager)
    static QVariantMap convertShotRecord(const ShotRecord& record);

    // Thread-safe shot save: opens a temporary connection, does all INSERTs + WAL checkpoint.
    // Safe to call from any thread (does not use m_db). Returns shotId or -1 on failure.
    static qint64 saveShotStatic(const QString& dbPath, const ShotSaveData& data);

    // Delete shot(s)
    Q_INVOKABLE void deleteShots(const QVariantList& shotIds);

    // Async: runs delete on background thread, emits shotDeleted()
    Q_INVOKABLE void requestDeleteShot(qint64 shotId);

    // Thread-safe metadata update: caller provides their own connection.
    // Safe to call from any thread (does not use m_db). Returns true on success.
    static bool updateShotMetadataStatic(QSqlDatabase& db, qint64 shotId, const QVariantMap& metadata);

    // Async: runs update on background thread, emits shotMetadataUpdated()
    Q_INVOKABLE void requestUpdateShotMetadata(qint64 shotId, const QVariantMap& metadata);

    // Async: fetch most recent shot ID on background thread, emits mostRecentShotIdReady()
    Q_INVOKABLE void requestMostRecentShotId();

    // Async: refresh the distinct-value cache on a background thread, emits distinctCacheReady().
    // Called at init (pre-warm) and by invalidateDistinctCache() after data changes.
    Q_INVOKABLE void requestDistinctCache();

    // Get filter options (cache-only, returns {} on miss and triggers async fetch)
    Q_INVOKABLE QStringList getDistinctBeanBrands();
    Q_INVOKABLE QStringList getDistinctBaristas();

    // Get filter options with parent-based filtering (cache-only)
    Q_INVOKABLE QStringList getDistinctBeanTypesForBrand(const QString& beanBrand);
    Q_INVOKABLE QStringList getDistinctGrinderBrands();
    Q_INVOKABLE QStringList getDistinctGrinderModelsForBrand(const QString& grinderBrand);
    Q_INVOKABLE QStringList getDistinctGrinderBurrsForModel(const QString& grinderBrand, const QString& grinderModel);
    Q_INVOKABLE QStringList getDistinctGrinderSettingsForGrinder(const QString& grinderModel);

    // Bulk update grinder fields for historical shots matching old values
    Q_INVOKABLE void requestUpdateGrinderFields(const QString& oldBrand, const QString& oldModel,
                                                 const QString& newBrand, const QString& newModel,
                                                 const QString& newBurrs);

    // Async: runs query on background thread, emits autoFavoritesReady()
    Q_INVOKABLE void requestAutoFavorites(const QString& groupBy, int maxItems);

    // Async: runs query on background thread, emits autoFavoriteGroupDetailsReady()
    Q_INVOKABLE void requestAutoFavoriteGroupDetails(const QString& groupBy,
                                                      const QString& beanBrand,
                                                      const QString& beanType,
                                                      const QString& profileName,
                                                      const QString& grinderBrand,
                                                      const QString& grinderModel,
                                                      const QString& grinderSetting);

    // Async: runs backup on background thread, emits backupFinished()
    Q_INVOKABLE void requestCreateBackup(const QString& destPath);

    // Async: runs import on background thread, emits importDatabaseFinished()
    Q_INVOKABLE void requestImportDatabase(const QString& filePath, bool merge);

    // Import a shot record directly (for .shot file import)
    // Returns: shot ID on success, 0 if duplicate (skipped), -1 on error
    // If overwriteExisting is true, duplicates will be replaced instead of skipped
    qint64 importShotRecord(const ShotRecord& record, bool overwriteExisting = false);

    // Refresh the total shots count (call after bulk import)
    Q_INVOKABLE void refreshTotalShots();

    // Get most recent shot ID (for linking after save)
    Q_INVOKABLE qint64 lastSavedShotId() const { return m_lastSavedShotId; }

    // Get database path
    QString databasePath() const { return m_dbPath; }

    // Invalidate all cached getDistinct*() results (call after save/delete/import/update)
    void invalidateDistinctCache();

    // Close the database (for factory reset before file deletion)
    void close();

    // Checkpoint WAL to main database file
    void checkpoint();

    // Thread-safe backup: opens a temporary connection, checkpoints, copies the file.
    // Safe to call from any thread (does not use m_db).
    static QString createBackupStatic(const QString& dbPath, const QString& destPath);

    // Thread-safe import: opens separate connections for source and destination.
    // Safe to call from any thread (does not use m_db).
    // Caller must invoke refreshTotalShots() on the main thread afterward (which also invalidates the distinct cache).
    static bool importDatabaseStatic(const QString& destDbPath, const QString& srcFilePath, bool merge);

    // Thread-safe shot count: opens a temporary connection.
    // Safe to call from any thread (does not use m_db).
    static int getShotCountStatic(const QString& dbPath);

signals:
    void readyChanged();
    void totalShotsChanged();
    void shotSaved(qint64 shotId);
    void shotDeleted(qint64 shotId);
    void shotsDeleted(const QVariantList& shotIds);
    void errorOccurred(const QString& message);
    void shotsFilteredReady(const QVariantList& results, bool isAppend, int totalCount);
    void loadingFilteredChanged();
    void shotReady(qint64 shotId, const QVariantMap& shot);
    void recentShotsByKbIdReady(const QString& kbId, const QVariantList& shots);
    void importDatabaseFinished(bool success);
    void shotMetadataUpdated(qint64 shotId, bool success);
    void autoFavoritesReady(const QVariantList& results);
    void autoFavoriteGroupDetailsReady(const QVariantMap& details);
    void backupFinished(bool success, const QString& resultPath);
    void visualizerInfoUpdated(qint64 shotId, bool success);
    void mostRecentShotIdReady(qint64 shotId);
    void distinctCacheReady();
    void grinderFieldsUpdated(int updatedCount);

private:
    bool createTables();
    bool runMigrations();
    QByteArray compressSampleData(ShotDataModel* shotData);
    static void decompressSampleData(const QByteArray& blob, ShotRecord* record);
    void updateTotalShots();
    QString buildFilterQuery(const ShotFilter& filter, QVariantList& bindValues);
    ShotFilter parseFilterMap(const QVariantMap& filterMap);
    QString formatFtsQuery(const QString& userInput);

    // Helper for getDistinct* methods — cache-only, triggers async fetch on miss
    QStringList getDistinctValues(const QString& column);
    // Internal wrappers used as fallbacks by parametric methods when parameter is empty.
    // Delegate to getDistinctValues() (cache-only).
    QStringList getDistinctBeanTypes();
    QStringList getDistinctGrinders();
    QStringList getDistinctGrinderSettings();
    // Helper to apply smart sorting for grinder settings
    void sortGrinderSettings(QStringList& settings);
    // Async cache-miss fetcher: runs SQL on background thread, populates cache, emits distinctCacheReady()
    void requestDistinctValueAsync(const QString& cacheKey, const QString& sql,
                                    const QVariantList& bindValues = {});

    // Internal sync delete — only called from importShotRecord() (main-thread, see TODO)
    bool deleteShot(qint64 shotId);

    // Backfill beverage_type from profile_json for existing rows
    void backfillBeverageType();

    // Helper for converting QVector<QPointF> to JSON object with t/v arrays
    static QJsonObject pointsToJsonObject(const QVector<QPointF>& points);

    // Core backup helper: checkpoint + close + copy + reopen
    // Returns true on success, false on failure
    bool performDatabaseCopy(const QString& destPath);

    QSqlDatabase m_db;
    QString m_dbPath;
    bool m_ready = false;
    int m_totalShots = 0;
    int m_schemaVersion = 1;
    qint64 m_lastSavedShotId = 0;
    std::atomic<bool> m_backupInProgress{false};  // Prevent concurrent backup/export operations (thread-safe)
    std::atomic<bool> m_importInProgress{false};   // Prevent concurrent import/restore operations (thread-safe)

    // Cache for getDistinct*() results (invalidated on save/delete/import)
    QHash<QString, QStringList> m_distinctCache;
    bool m_distinctCacheRefreshing = false;  // Debounce guard for requestDistinctCache()
    bool m_distinctCacheDirty = false;       // Re-queue flag: set when invalidation arrives during refresh
    QSet<QString> m_pendingDistinctKeys;     // De-duplicate in-flight requestDistinctValueAsync() calls

    // Async filter support
    bool m_loadingFiltered = false;
    int m_filterSerial = 0;

    // Shared flag for destructor safety in background thread lambdas.
    // Atomic because the flag is written on the main thread (destructor) and
    // read on background threads (before QMetaObject::invokeMethod).
    std::shared_ptr<std::atomic<bool>> m_destroyed = std::make_shared<std::atomic<bool>>(false);

    static const QString DB_CONNECTION_NAME;
};
