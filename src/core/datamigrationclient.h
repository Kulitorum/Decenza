#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUdpSocket>
#include <QTemporaryDir>
#include <QList>
#include <QTimer>
#include <QPointer>

class Settings;
class ProfileStorage;
class ShotHistoryStorage;
class ScreensaverVideoManager;
class AIManager;

/**
 * @brief Client for importing data from another Decenza device over WiFi.
 *
 * Connects to a remote Decenza device running the shot server and imports
 * settings, profiles, shot history, personal media, and AI conversations.
 */
class DataMigrationClient : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool isConnecting READ isConnecting NOTIFY isConnectingChanged)
    Q_PROPERTY(bool isImporting READ isImporting NOTIFY isImportingChanged)
    Q_PROPERTY(bool isSearching READ isSearching NOTIFY isSearchingChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString currentOperation READ currentOperation NOTIFY currentOperationChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QVariantMap manifest READ manifest NOTIFY manifestChanged)
    Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY serverUrlChanged)
    Q_PROPERTY(QVariantList discoveredDevices READ discoveredDevices NOTIFY discoveredDevicesChanged)

public:
    explicit DataMigrationClient(QObject* parent = nullptr);
    ~DataMigrationClient();

    // Dependencies (set by MainController)
    void setSettings(Settings* settings) { m_settings = settings; }
    void setProfileStorage(ProfileStorage* profileStorage) { m_profileStorage = profileStorage; }
    void setShotHistoryStorage(ShotHistoryStorage* shotHistory) { m_shotHistory = shotHistory; }
    void setScreensaverVideoManager(ScreensaverVideoManager* screensaver) { m_screensaver = screensaver; }
    void setAIManager(AIManager* aiManager) { m_aiManager = aiManager; }

    // Property getters
    bool isConnecting() const { return m_connecting; }
    bool isImporting() const { return m_importing; }
    bool isSearching() const { return m_searching; }
    double progress() const { return m_progress; }
    QString currentOperation() const { return m_currentOperation; }
    QString errorMessage() const { return m_errorMessage; }
    QVariantMap manifest() const { return m_manifest; }
    QString serverUrl() const { return m_serverUrl; }
    QVariantList discoveredDevices() const { return m_discoveredDevices; }

    // Device discovery
    Q_INVOKABLE void startDiscovery();
    Q_INVOKABLE void stopDiscovery();

    // Connect to a server and fetch its manifest
    Q_INVOKABLE void connectToServer(const QString& serverUrl);

    // Disconnect from current server
    Q_INVOKABLE void disconnect();

    // Import all data types
    Q_INVOKABLE void importAll();

    // Import individual data types (can be called from QML)
    Q_INVOKABLE void importOnlySettings();
    Q_INVOKABLE void importOnlyProfiles();
    Q_INVOKABLE void importOnlyShots();
    Q_INVOKABLE void importOnlyMedia();
    Q_INVOKABLE void importOnlyAIConversations();

    // Cancel ongoing import
    Q_INVOKABLE void cancel();

signals:
    void isConnectingChanged();
    void isImportingChanged();
    void isSearchingChanged();
    void progressChanged();
    void currentOperationChanged();
    void errorMessageChanged();
    void manifestChanged();
    void serverUrlChanged();
    void discoveredDevicesChanged();
    void connected();
    void connectionFailed(const QString& error);
    void importComplete(int settingsImported, int profilesImported, int shotsImported, int mediaImported, int aiConversationsImported);
    void importFailed(const QString& error);
    void discoveryComplete();

private slots:
    void onManifestReply();
    void onSettingsReply();
    void onProfileListReply();
    void onProfileFileReply();
    void onShotsReply();
    void onMediaListReply();
    void onMediaFileReply();
    void onAIConversationsReply();
    void onDownloadProgress(qint64 received, qint64 total);
    void onDiscoveryDatagram();
    void onDiscoveryTimeout();

private:
    // Helper structs for tracking downloads
    struct ProfileDownload {
        QString category;
        QString filename;
        qint64 size;
    };

    struct MediaDownload {
        QString filename;
        qint64 size;
    };

    void setProgress(double progress);
    void setCurrentOperation(const QString& operation);
    void setError(const QString& error);
    void startImport(const QStringList& types);  // Common setup for all import methods
    void startNextImport();
    void downloadNextProfile();
    void downloadNextMedia();

    // Internal import methods (used by queue)
    void doImportSettings();
    void doImportProfiles();
    void doImportShots();
    void doImportMedia();
    void doImportAIConversations();

    QNetworkAccessManager* m_networkManager;
    QPointer<QNetworkReply> m_currentReply;
    QTemporaryDir* m_tempDir = nullptr;

    Settings* m_settings = nullptr;
    ProfileStorage* m_profileStorage = nullptr;
    ShotHistoryStorage* m_shotHistory = nullptr;
    ScreensaverVideoManager* m_screensaver = nullptr;
    AIManager* m_aiManager = nullptr;

    QString m_serverUrl;
    QVariantMap m_manifest;
    bool m_connecting = false;
    bool m_importing = false;
    bool m_cancelled = false;
    double m_progress = 0.0;
    QString m_currentOperation;
    QString m_errorMessage;

    // Import queue for importAll()
    QStringList m_importQueue;
    int m_settingsImported = 0;
    int m_profilesImported = 0;
    int m_shotsImported = 0;
    int m_mediaImported = 0;
    int m_aiConversationsImported = 0;

    // Pending downloads
    QList<ProfileDownload> m_pendingProfiles;
    QList<MediaDownload> m_pendingMedia;

    // For progress calculation
    qint64 m_totalBytes = 0;
    qint64 m_receivedBytes = 0;

    // Device discovery
    QUdpSocket* m_discoverySocket = nullptr;
    QTimer* m_discoveryTimer = nullptr;
    QVariantList m_discoveredDevices;
    bool m_searching = false;
    static constexpr int DISCOVERY_PORT = 8889;
    static constexpr int DISCOVERY_TIMEOUT_MS = 3000;  // Search for 3 seconds
};
