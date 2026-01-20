#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVariantList>
#include "../profile/profile.h"

class MainController;
class Settings;

// Status of a shared profile relative to local profiles
struct SharedProfileStatus {
    Q_GADGET
public:
    enum Status {
        New,        // Profile doesn't exist locally
        Identical,  // Exists and has same parameters
        Different   // Exists but has different parameters
    };
    Q_ENUM(Status)
};

class VisualizerImporter : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool importing READ isImporting NOTIFY importingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool fetching READ isFetching NOTIFY fetchingChanged)
    Q_PROPERTY(QVariantList sharedShots READ sharedShots NOTIFY sharedShotsChanged)

public:
    explicit VisualizerImporter(MainController* controller, Settings* settings, QObject* parent = nullptr);

    bool isImporting() const { return m_importing; }
    bool isFetching() const { return m_fetching; }
    QString lastError() const { return m_lastError; }
    QVariantList sharedShots() const { return m_sharedShots; }

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
    Q_INVOKABLE void saveAsNew();  // Auto-generate unique name with _1, _2 suffix
    Q_INVOKABLE void saveWithNewName(const QString& newTitle);  // User-provided name

signals:
    void importingChanged();
    void lastErrorChanged();
    void fetchingChanged();
    void sharedShotsChanged();
    void importSuccess(const QString& profileTitle);
    void importFailed(const QString& error);
    void duplicateFound(const QString& profileTitle, const QString& existingPath);
    void batchImportComplete(int imported, int skipped);

private slots:
    void onFetchFinished(QNetworkReply* reply);
    void onProfileFetchFinished(QNetworkReply* reply);

private:
    // Convert Visualizer JSON format to our Profile format
    Profile parseVisualizerProfile(const QJsonObject& json);
    ProfileFrame parseVisualizerStep(const QJsonObject& stepJson);

    // Save profile to disk and refresh profile list
    // Returns: 1 = saved, 0 = waiting for user (duplicate), -1 = failed
    int saveImportedProfile(const Profile& profile);

    // Check profile status against local profiles
    QVariantMap checkProfileStatus(const QString& profileTitle, const Profile* incomingProfile = nullptr);

    // Compare two profiles' frames (returns true if identical)
    bool compareProfileFrames(const Profile& a, const Profile& b) const;

    // Load a local profile by filename
    Profile loadLocalProfile(const QString& filename) const;

    // Auth header for API requests
    QString authHeader() const;

    // Fetch profile details for shared shots (chained after fetchSharedShots)
    void fetchProfileDetailsForShots();
    void onProfileDetailsFetched(QNetworkReply* reply, int shotIndex);

    MainController* m_controller;
    Settings* m_settings;
    QNetworkAccessManager* m_networkManager;
    bool m_importing = false;
    bool m_fetching = false;
    QString m_lastError;

    // Shared shots list for multi-import
    QVariantList m_sharedShots;

    // Pending profile for duplicate handling
    Profile m_pendingProfile;
    QString m_pendingPath;

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

    // Pending shots while fetching profile details
    QVariantList m_pendingShots;
    int m_pendingProfileFetches = 0;

    static constexpr const char* VISUALIZER_PROFILE_API = "https://visualizer.coffee/api/shots/%1/profile?format=json";
    static constexpr const char* VISUALIZER_SHARED_API = "https://visualizer.coffee/api/shots/shared?code=%1";
};
