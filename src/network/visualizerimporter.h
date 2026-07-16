#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVariantList>
#include <QVector>
#include "../profile/profile.h"

class MainController;
class ProfileSaveHelper;
class Settings;

class VisualizerImporter : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool importing READ isImporting NOTIFY importingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool fetching READ isFetching NOTIFY fetchingChanged)
    Q_PROPERTY(QVariantList sharedShots READ sharedShots NOTIFY sharedShotsChanged)
    // "Recover shots from Visualizer" (date-range history import) state.
    Q_PROPERTY(bool recovering READ isRecovering NOTIFY recoveringChanged)

public:
    explicit VisualizerImporter(QNetworkAccessManager* networkManager, MainController* controller, Settings* settings, QObject* parent = nullptr);

    bool isImporting() const { return m_importing; }
    bool isFetching() const { return m_fetching; }
    QString lastError() const { return m_lastError; }
    QVariantList sharedShots() const { return m_sharedShots; }
    bool isRecovering() const { return m_recovering; }

    // Import profile from a Visualizer shot ID
    Q_INVOKABLE void importFromShotId(const QString& shotId);

    // Import profile from a shot ID with a custom name (for D profile renaming)
    Q_INVOKABLE void importFromShotIdWithName(const QString& shotId, const QString& customName);

    // Import profile from a 4-character share code
    Q_INVOKABLE void importFromShareCode(const QString& shareCode);

    // Fetch shared shots list (for multi-import page)
    Q_INVOKABLE void fetchSharedShots();

    // Import selected shots by their IDs
    Q_INVOKABLE void importSelectedShots(const QStringList& shotIds, bool overwriteExisting);

    // Extract shot ID from a Visualizer URL
    // Returns empty string if URL is not a valid Visualizer shot URL
    Q_INVOKABLE QString extractShotId(const QString& url) const;

    // Called after duplicate dialog - save with overwrite or new name
    Q_INVOKABLE void saveOverwrite();
    Q_INVOKABLE void saveAsNew();
    Q_INVOKABLE void saveWithNewName(const QString& newTitle);
    Q_INVOKABLE void cancelPending();

    // --- Recover shots from Visualizer (date-range history import) ---
    //
    // Pulls the user's own FULL shot records (telemetry + metadata) from
    // visualizer.coffee back into local history, for shots whose start time
    // falls within [fromEpoch, toEpoch] (Unix seconds, inclusive). Async and
    // non-blocking. The recovery:
    //   1. pages GET /api/shots (authenticated => the user's own shots),
    //      filtering by each entry's `clock`;
    //   2. for each in-range shot, downloads the full record
    //      (GET /api/shots/{id}/download) and its profile
    //      (GET /api/shots/{id}/profile?format=json);
    //   3. parses via ShotFileParser::parseVisualizerShot and inserts through
    //      ShotHistoryStorage::importShotRecord, which DEDUPES against local
    //      history (by uuid, then timestamp+profile) so a shot the user still
    //      has is skipped and re-running is idempotent.
    // Emits recoveryProgress after each shot and recoveryComplete when done.
    // Requires Visualizer credentials; fails clearly via recoveryFailed if
    // they are not configured. A no-op if a recovery is already running.
    Q_INVOKABLE void recoverShots(qint64 fromEpoch, qint64 toEpoch);

signals:
    void importingChanged();
    void lastErrorChanged();
    void fetchingChanged();
    void sharedShotsChanged();
    void importSuccess(const QString& profileTitle);
    void importFailed(const QString& error);
    void duplicateFound(const QString& profileTitle, const QString& existingPath);
    void batchImportComplete(int imported, int skipped, int failed);

    // --- Recovery signals ---
    void recoveringChanged();
    // Fired once the in-range shot list is known (total) and then after each
    // shot is processed: imported so far, skipped as already-present, failed.
    void recoveryProgress(int total, int imported, int skipped, int failed);
    // Terminal success: final counts. imported+skipped+failed == total.
    void recoveryComplete(int total, int imported, int skipped, int failed);
    // Terminal failure before per-shot processing (no credentials, list
    // fetch error). Per-shot failures do NOT abort the batch — they are
    // counted in `failed` and surfaced via recoveryComplete.
    void recoveryFailed(const QString& error);

private slots:
    void onFetchFinished(QNetworkReply* reply);
    void onProfileFetchFinished(QNetworkReply* reply);

private:
    // Convert Visualizer JSON format to our Profile format
    Profile parseVisualizerProfile(const QJsonObject& json);

    // Auth header for API requests
    QString authHeader() const;

    // --- Recovery internals ---
    // A shot to recover: its Visualizer id and start time (from the list).
    struct RecoveryShot {
        QString visualizerId;
        qint64 clockEpoch = 0;
    };
    // Page GET /api/shots collecting in-window ids, then start downloads.
    void recoverFetchListPage(int page);
    // Download the next queued shot's full record + profile, parse, insert.
    void recoverNextShot();
    // Finish the run: emit recoveryComplete and reset state.
    void finishRecovery();

    // Recovery state (single run at a time — guarded by m_recovering).
    bool m_recovering = false;
    qint64 m_recoverFromEpoch = 0;
    qint64 m_recoverToEpoch = 0;
    QVector<RecoveryShot> m_recoverQueue;   // in-window shots still to fetch
    RecoveryShot m_recoverCurrent;          // shot whose download is in flight
    int m_recoverTotal = 0;
    int m_recoverImported = 0;
    int m_recoverSkipped = 0;
    int m_recoverFailed = 0;

    static constexpr const char* VISUALIZER_SHOT_DOWNLOAD_API =
        "https://visualizer.coffee/api/shots/%1/download";
    static constexpr const char* VISUALIZER_SHOTS_LIST_API =
        "https://visualizer.coffee/api/shots";

    // Fetch profile details for shared shots (chained after fetchSharedShots)
    void fetchProfileDetailsForShots();
    void onProfileDetailsFetched(QNetworkReply* reply, int shotIndex);

    MainController* m_controller;
    Settings* m_settings;
    QNetworkAccessManager* m_networkManager;
    ProfileSaveHelper* m_saveHelper;
    bool m_importing = false;
    bool m_fetching = false;
    QString m_lastError;

    // Shared shots list for multi-import
    QVariantList m_sharedShots;

    // Track request type for response handling
    enum class RequestType {
        None,
        ShareCode,      // Single import from share code
        FetchList,      // Fetching list for multi-import
        FetchProfile,   // Fetching individual profile for batch import
        BatchImport,    // Batch importing profiles
        RenamedImport   // Single import with custom name (for D profiles)
    };
    RequestType m_requestType = RequestType::None;

    // Custom name for renamed import
    QString m_customImportName;

    // Batch import state
    QStringList m_batchShotIds;
    bool m_batchOverwrite = false;
    int m_batchImported = 0;
    int m_batchSkipped = 0;
    int m_batchFailed = 0;

    // Pending shots while fetching profile details
    QVariantList m_pendingShots;
    int m_pendingProfileFetches = 0;

    static constexpr const char* VISUALIZER_PROFILE_API = "https://visualizer.coffee/api/shots/%1/profile?format=json";
    static constexpr const char* VISUALIZER_SHARED_API = "https://visualizer.coffee/api/shots/shared?code=%1";
};
