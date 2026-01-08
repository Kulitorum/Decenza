#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QVariantList>
#include <QVector>
#include <QPointF>
#include <QDateTime>

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
};

// Phase marker for shot display
struct HistoryPhaseMarker {
    double time = 0;
    QString label;
    int frameNumber = 0;
    bool isFlowMode = false;
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
    QString barista;
    QString visualizerId;
    QString visualizerUrl;

    // Time-series data (lazily loaded)
    QVector<QPointF> pressure;
    QVector<QPointF> flow;
    QVector<QPointF> temperature;
    QVector<QPointF> pressureGoal;
    QVector<QPointF> flowGoal;
    QVector<QPointF> temperatureGoal;
    QVector<QPointF> weight;

    // Phase markers
    QList<HistoryPhaseMarker> phases;

    // Debug log
    QString debugLog;

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
    int minEnjoyment = 0;
    int maxEnjoyment = 100;
    qint64 dateFrom = 0;       // Unix timestamp
    qint64 dateTo = 0;
    QString searchText;        // FTS search in notes
    bool onlyWithVisualizer = false;
};

class ShotHistoryStorage : public QObject {
    Q_OBJECT

    Q_PROPERTY(int totalShots READ totalShots NOTIFY totalShotsChanged)
    Q_PROPERTY(bool isReady READ isReady NOTIFY readyChanged)

public:
    explicit ShotHistoryStorage(QObject* parent = nullptr);
    ~ShotHistoryStorage();

    // Database lifecycle
    bool initialize(const QString& dbPath = QString());
    bool isReady() const { return m_ready; }
    int totalShots() const { return m_totalShots; }

    // Save a completed shot
    qint64 saveShot(ShotDataModel* shotData,
                    const Profile* profile,
                    double duration,
                    double finalWeight,
                    double doseWeight,
                    const ShotMetadata& metadata,
                    const QString& debugLog);

    // Update visualizer info after upload
    Q_INVOKABLE bool updateVisualizerInfo(qint64 shotId,
                                           const QString& visualizerId,
                                           const QString& visualizerUrl);

    // Query shots (paginated)
    Q_INVOKABLE QVariantList getShots(int offset = 0, int limit = 50);
    Q_INVOKABLE QVariantList getShotsFiltered(const QVariantMap& filter, int offset = 0, int limit = 50);

    // Get full shot record (loads time-series data)
    Q_INVOKABLE QVariantMap getShot(qint64 shotId);
    ShotRecord getShotRecord(qint64 shotId);

    // Get multiple shots for comparison (efficient batch load)
    QList<ShotRecord> getShotsForComparison(const QList<qint64>& shotIds);

    // Delete shot
    Q_INVOKABLE bool deleteShot(qint64 shotId);

    // Update shot metadata (for editing existing shots)
    Q_INVOKABLE bool updateShotMetadata(qint64 shotId, const QVariantMap& metadata);

    // Get filter options (for dropdowns)
    Q_INVOKABLE QStringList getDistinctProfiles();
    Q_INVOKABLE QStringList getDistinctBeanBrands();
    Q_INVOKABLE QStringList getDistinctBeanTypes();
    Q_INVOKABLE QStringList getDistinctGrinders();
    Q_INVOKABLE QStringList getDistinctRoastLevels();

    // Get filter options with cascading filter (for dependent dropdowns)
    Q_INVOKABLE QStringList getDistinctProfilesFiltered(const QVariantMap& filter);
    Q_INVOKABLE QStringList getDistinctBeanBrandsFiltered(const QVariantMap& filter);
    Q_INVOKABLE QStringList getDistinctBeanTypesFiltered(const QVariantMap& filter);

    // Get count of shots matching filter
    Q_INVOKABLE int getFilteredShotCount(const QVariantMap& filter);

    // Export debug log for bug report
    Q_INVOKABLE QString exportShotData(qint64 shotId);

    // Export database to Downloads folder (for debugging)
    Q_INVOKABLE QString exportDatabase();

    // Import database from file path (merge=true adds new entries, merge=false replaces all)
    Q_INVOKABLE bool importDatabase(const QString& filePath, bool merge);

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

    // Checkpoint WAL to main database file
    void checkpoint();

signals:
    void readyChanged();
    void totalShotsChanged();
    void shotSaved(qint64 shotId);
    void shotDeleted(qint64 shotId);
    void errorOccurred(const QString& message);

private:
    bool createTables();
    bool runMigrations();
    QByteArray compressSampleData(ShotDataModel* shotData);
    void decompressSampleData(const QByteArray& blob, ShotRecord* record);
    void updateTotalShots();
    QString buildFilterQuery(const ShotFilter& filter, QVariantList& bindValues);
    ShotFilter parseFilterMap(const QVariantMap& filterMap);
    QString formatFtsQuery(const QString& userInput);

    QSqlDatabase m_db;
    QString m_dbPath;
    bool m_ready = false;
    int m_totalShots = 0;
    int m_schemaVersion = 1;
    qint64 m_lastSavedShotId = 0;

    static const QString DB_CONNECTION_NAME;
};
