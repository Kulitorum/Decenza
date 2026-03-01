#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QHash>
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
    QString grinderModel;
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
};

// Filter criteria for queries
struct ShotFilter {
    QString profileName;
    QString beanBrand;
    QString beanType;
    QString grinderModel;
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
    QString grinderModel;
    QString grinderSetting;
    double drinkTds = 0;
    double drinkEy = 0;
    int espressoEnjoyment = 0;
    QString espressoNotes;
    QString barista;
    QString profileNotes;
    QString debugLog;

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

    // Update visualizer info after upload
    Q_INVOKABLE bool updateVisualizerInfo(qint64 shotId,
                                           const QString& visualizerId,
                                           const QString& visualizerUrl);

    // Query shots (paginated)
    Q_INVOKABLE QVariantList getShots(int offset = 0, int limit = 50);
    Q_INVOKABLE QVariantList getShotsFiltered(const QVariantMap& filter, int offset = 0, int limit = 50);

    // Async version: runs SQL on a background thread and emits shotsFilteredReady()
    Q_INVOKABLE void requestShotsFiltered(const QVariantMap& filter, int offset = 0, int limit = 50);

    // Get just the timestamp of a shot (lightweight, no time-series)
    Q_INVOKABLE qint64 getShotTimestamp(qint64 shotId);

    // Get full shot record (loads time-series data)
    Q_INVOKABLE QVariantMap getShot(qint64 shotId);
    ShotRecord getShotRecord(qint64 shotId);

    // Async version: runs on background thread, emits shotReady()
    Q_INVOKABLE void requestShot(qint64 shotId);

    // Get multiple shots for comparison (efficient batch load)
    QList<ShotRecord> getShotsForComparison(const QList<qint64>& shotIds);

    // Static version for background-thread use — caller provides their own connection.
    static ShotRecord loadShotRecordStatic(QSqlDatabase& db, qint64 shotId);

    // Thread-safe shot save: opens a temporary connection, does all INSERTs + WAL checkpoint.
    // Safe to call from any thread (does not use m_db). Returns shotId or -1 on failure.
    static qint64 saveShotStatic(const QString& dbPath, const ShotSaveData& data);

    // Delete shot(s)
    Q_INVOKABLE bool deleteShot(qint64 shotId);
    Q_INVOKABLE void deleteShots(const QVariantList& shotIds);

    // Async version: runs delete on background thread, emits shotDeleted()
    Q_INVOKABLE void requestDeleteShot(qint64 shotId);

    // Update shot metadata (for editing existing shots)
    Q_INVOKABLE bool updateShotMetadata(qint64 shotId, const QVariantMap& metadata);

    // Async version: runs update on background thread, emits shotMetadataUpdated()
    Q_INVOKABLE void requestUpdateShotMetadata(qint64 shotId, const QVariantMap& metadata);

    // Get filter options (for dropdowns)
    Q_INVOKABLE QStringList getDistinctProfiles();
    Q_INVOKABLE QStringList getDistinctBeanBrands();
    Q_INVOKABLE QStringList getDistinctBeanTypes();
    Q_INVOKABLE QStringList getDistinctGrinders();
    Q_INVOKABLE QStringList getDistinctGrinderSettings();
    Q_INVOKABLE QStringList getDistinctBaristas();
    Q_INVOKABLE QStringList getDistinctRoastLevels();

    // Get filter options with parent-based filtering
    Q_INVOKABLE QStringList getDistinctBeanTypesForBrand(const QString& beanBrand);
    Q_INVOKABLE QStringList getDistinctGrinderSettingsForGrinder(const QString& grinderModel);

    // Get filter options with cascading filter (for dependent dropdowns)
    Q_INVOKABLE QStringList getDistinctProfilesFiltered(const QVariantMap& filter);
    Q_INVOKABLE QStringList getDistinctBeanBrandsFiltered(const QVariantMap& filter);
    Q_INVOKABLE QStringList getDistinctBeanTypesFiltered(const QVariantMap& filter);

    // Get count of shots matching filter
    Q_INVOKABLE int getFilteredShotCount(const QVariantMap& filter);

    // Get auto-favorites: unique combinations of bean/profile/grinder from history
    // groupBy: "bean", "profile", "bean_profile", "bean_profile_grinder"
    Q_INVOKABLE QVariantList getAutoFavorites(const QString& groupBy, int maxItems);

    // Async version: runs query on background thread, emits autoFavoritesReady()
    Q_INVOKABLE void requestAutoFavorites(const QString& groupBy, int maxItems);

    // Get aggregated details for a specific auto-favorite group
    // Returns: avgTds, avgEy, avgDuration, avgDose, avgYield, avgTemperature, notes[]
    Q_INVOKABLE QVariantMap getAutoFavoriteGroupDetails(const QString& groupBy,
                                                         const QString& beanBrand,
                                                         const QString& beanType,
                                                         const QString& profileName,
                                                         const QString& grinderModel,
                                                         const QString& grinderSetting);

    // Async version: runs query on background thread, emits autoFavoriteGroupDetailsReady()
    Q_INVOKABLE void requestAutoFavoriteGroupDetails(const QString& groupBy,
                                                      const QString& beanBrand,
                                                      const QString& beanType,
                                                      const QString& profileName,
                                                      const QString& grinderModel,
                                                      const QString& grinderSetting);

    // Export debug log for bug report
    Q_INVOKABLE QString exportShotData(qint64 shotId);

    // Create backup at specified path (for scheduled backups)
    Q_INVOKABLE QString createBackup(const QString& destPath);

    // Async version: runs backup on background thread, emits backupFinished()
    Q_INVOKABLE void requestCreateBackup(const QString& destPath);

    // Import database from file path (merge=true adds new entries, merge=false replaces all)
    Q_INVOKABLE bool importDatabase(const QString& filePath, bool merge);

    // Async version: runs import on background thread, emits importDatabaseFinished()
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

    // Close the database (for factory reset before file deletion)
    void close();

    // Checkpoint WAL to main database file
    void checkpoint();

    // Thread-safe backup: opens a temporary connection, checkpoints, copies the file.
    // Safe to call from any thread (does not use m_db).
    static QString createBackupStatic(const QString& dbPath, const QString& destPath);

    // Thread-safe import: opens separate connections for source and destination.
    // Safe to call from any thread (does not use m_db).
    // Caller must invoke updateTotalShots()/invalidateDistinctCache() on the main thread afterward.
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
    void importDatabaseFinished(bool success);
    void shotMetadataUpdated(qint64 shotId, bool success);
    void autoFavoritesReady(const QVariantList& results);
    void autoFavoriteGroupDetailsReady(const QVariantMap& details);
    void backupFinished(bool success, const QString& resultPath);

private:
    bool createTables();
    bool runMigrations();
    QByteArray compressSampleData(ShotDataModel* shotData);
    static void decompressSampleData(const QByteArray& blob, ShotRecord* record);
    void updateTotalShots();
    QString buildFilterQuery(const ShotFilter& filter, QVariantList& bindValues);
    ShotFilter parseFilterMap(const QVariantMap& filterMap);
    QString formatFtsQuery(const QString& userInput);

    // Helper for getDistinct* methods - column is the DB column name
    QStringList getDistinctValues(const QString& column);
    // Helper for getDistinct*Filtered methods - excludeColumn is not filtered on itself
    QStringList getDistinctValuesFiltered(const QString& column, const QString& excludeColumn,
                                          const QVariantMap& filter);
    // Helper to apply smart sorting for grinder settings
    void sortGrinderSettings(QStringList& settings);

    // Invalidate all cached getDistinct*() results (call after save/delete/import)
    void invalidateDistinctCache();

    // Convert ShotRecord to QVariantMap (shared by getShot and requestShot)
    QVariantMap convertShotRecord(const ShotRecord& record);

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

    // Async filter support
    bool m_loadingFiltered = false;
    int m_filterSerial = 0;

    // Shared flag for destructor safety in background thread lambdas
    std::shared_ptr<bool> m_destroyed = std::make_shared<bool>(false);

    static const QString DB_CONNECTION_NAME;
};
