#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QHash>
#include <QSet>
#include <QFile>
#include <QTimer>
#include <QElapsedTimer>

class ShotHistoryStorage;
class DE1Device;
class MachineState;
class ScreensaverVideoManager;
class Settings;
class ProfileStorage;
class AIManager;
class WidgetLibrary;
class LibrarySharing;

struct PendingRequest {
    QByteArray headerData;          // Only headers stored in memory
    qint64 contentLength = -1;
    int headerEnd = -1;
    qint64 bodyReceived = 0;        // Track bytes received
    QFile* tempFile = nullptr;      // Stream body to temp file for large uploads
    QString tempFilePath;           // Path to temp file
    QElapsedTimer lastActivity;     // For timeout tracking
    bool isMediaUpload = false;     // Flag for media upload requests
    bool isBackupRestore = false;   // Flag for backup restore uploads
};

class ShotServer : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(QString url READ url NOTIFY urlChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY portChanged)

public:
    explicit ShotServer(ShotHistoryStorage* storage, DE1Device* device, QObject* parent = nullptr);
    ~ShotServer();

    bool isRunning() const { return m_server && m_server->isListening(); }
    QString url() const;
    int port() const { return m_port; }
    void setPort(int port);

    Q_INVOKABLE bool start();
    Q_INVOKABLE void stop();

    // Screensaver video manager for personal media upload
    void setScreensaverVideoManager(ScreensaverVideoManager* manager) { m_screensaverManager = manager; }

    // Settings and profiles for data migration
    void setSettings(Settings* settings);

    void setProfileStorage(ProfileStorage* profileStorage) { m_profileStorage = profileStorage; }

    // Machine state for home automation API
    void setMachineState(MachineState* machineState) { m_machineState = machineState; }

    // AI manager for layout AI assistant
    void setAIManager(AIManager* aiManager) { m_aiManager = aiManager; }

    // Widget library and community sharing for layout editor
    void setWidgetLibrary(WidgetLibrary* library) { m_widgetLibrary = library; }
    void setLibrarySharing(LibrarySharing* sharing) { m_librarySharing = sharing; }

signals:
    void runningChanged();
    void urlChanged();
    void portChanged();
    void clientConnected(const QString& address);
    void sleepRequested();  // Emitted when sleep command received via REST API

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
    void cleanupStaleConnections();
    void onDiscoveryDatagram();
    void onLayoutChanged();

private:
    void handleRequest(QTcpSocket* socket, const QByteArray& request);
    void sendResponse(QTcpSocket* socket, int statusCode, const QString& contentType,
                      const QByteArray& body, const QByteArray& extraHeaders = QByteArray());
    void sendJson(QTcpSocket* socket, const QByteArray& json);
    void sendHtml(QTcpSocket* socket, const QString& html);
    void sendFile(QTcpSocket* socket, const QString& path, const QString& contentType);

    QString getLocalIpAddress() const;
    QString generateIndexPage() const;
    QString generateShotListPage() const;
    QString generateShotDetailPage(qint64 shotId) const;
    QString generateComparisonPage(const QList<qint64>& shotIds) const;
    QString generateDebugPage() const;
    QString generateUploadPage() const;
    void handleUpload(QTcpSocket* socket, const QByteArray& request);
    void installApk(const QString& apkPath);

    // Personal media upload
    QString generateMediaUploadPage() const;
    void handleMediaUpload(QTcpSocket* socket, const QString& tempFilePath, const QString& headers);
    bool resizeImage(const QString& inputPath, const QString& outputPath, int maxWidth, int maxHeight);
    bool resizeVideo(const QString& inputPath, const QString& outputPath, int maxWidth, int maxHeight);
    QDateTime extractImageDate(const QString& imagePath) const;
    QDateTime extractVideoDate(const QString& videoPath) const;
    QDateTime extractDateWithExiftool(const QString& filePath) const;
    void cleanupPendingRequest(QTcpSocket* socket);

    // Data migration backup API
    void handleBackupManifest(QTcpSocket* socket);
    void handleBackupSettings(QTcpSocket* socket, bool includeSensitive);
    void handleBackupProfilesList(QTcpSocket* socket);
    void handleBackupProfileFile(QTcpSocket* socket, const QString& category, const QString& filename);
    void handleBackupMediaList(QTcpSocket* socket);
    void handleBackupMediaFile(QTcpSocket* socket, const QString& filename);

    // Full backup download/restore
    void handleBackupFull(QTcpSocket* socket);
    QString generateRestorePage() const;
    void handleBackupRestore(QTcpSocket* socket, const QString& tempFilePath, const QString& headers);

    // Layout editor web UI
    QString generateLayoutPage() const;
    void handleLayoutApi(QTcpSocket* socket, const QString& method, const QString& path, const QByteArray& body);

    // Settings web UI
    QString generateSettingsPage() const;
    void handleGetSettings(QTcpSocket* socket);
    void handleSaveSettings(QTcpSocket* socket, const QByteArray& body);

    QTcpServer* m_server = nullptr;
    QUdpSocket* m_discoverySocket = nullptr;
    ShotHistoryStorage* m_storage = nullptr;
    DE1Device* m_device = nullptr;
    ScreensaverVideoManager* m_screensaverManager = nullptr;
    Settings* m_settings = nullptr;
    ProfileStorage* m_profileStorage = nullptr;
    MachineState* m_machineState = nullptr;
    AIManager* m_aiManager = nullptr;
    WidgetLibrary* m_widgetLibrary = nullptr;
    LibrarySharing* m_librarySharing = nullptr;
    QTcpSocket* m_pendingLibrarySocket = nullptr;  // Socket waiting for community response
    QTimer* m_cleanupTimer = nullptr;
    int m_port = 8888;
    int m_activeMediaUploads = 0;
    QHash<QTcpSocket*, PendingRequest> m_pendingRequests;
    QSet<QTcpSocket*> m_sseLayoutClients;  // SSE connections for layout change notifications

    // Limits to prevent resource exhaustion
    static constexpr qint64 MAX_HEADER_SIZE = 64 * 1024;           // 64 KB for headers
    static constexpr qint64 MAX_SMALL_BODY_SIZE = 1024 * 1024;     // 1 MB kept in memory
    static constexpr qint64 MAX_UPLOAD_SIZE = 500 * 1024 * 1024;   // 500 MB max per file
    static constexpr int MAX_CONCURRENT_UPLOADS = 2;               // Limit concurrent media uploads
    static constexpr int CONNECTION_TIMEOUT_MS = 300000;           // 5 minute timeout
    static constexpr int DISCOVERY_PORT = 8889;                    // UDP port for device discovery
};
