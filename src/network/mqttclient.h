#pragma once

#include <QObject>
#include <QTimer>
#include <QMutex>

extern "C" {
#include <MQTTAsync.h>
}

class DE1Device;
class MachineState;
class Settings;
class MainController;

class MqttClient : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int reconnectAttempts READ reconnectAttempts NOTIFY reconnectAttemptsChanged)
    Q_PROPERTY(QString currentProfile READ currentProfile WRITE setCurrentProfile NOTIFY currentProfileChanged)

public:
    explicit MqttClient(DE1Device* device, MachineState* machineState,
                       Settings* settings, QObject* parent = nullptr);
    ~MqttClient();

    bool isConnected() const;
    QString status() const { return m_status; }
    int reconnectAttempts() const { return m_reconnectAttempts; }
    QString currentProfile() const { return m_currentProfile; }
    void setCurrentProfile(const QString& profile);
    void setCurrentProfileFilename(const QString& filename);
    void setMainController(MainController* controller);

    Q_INVOKABLE void connectToBroker();
    Q_INVOKABLE void disconnectFromBroker();
    Q_INVOKABLE void publishDiscovery();

public slots:
    void onScaleConnectedChanged(bool connected);
    void onSteamSettingsChanged();

signals:
    void connectedChanged();
    void statusChanged();
    void reconnectAttemptsChanged();
    void commandReceived(const QString& command);
    void profileSelectRequested(const QString& profileName);
    void currentProfileChanged();
    void steamOnRequested();
    void steamOffRequested();

    // Internal signals for thread-safe callback handling
    void internalConnected();
    void internalDisconnected();
    void internalConnectionFailed(const QString& error);
    void internalMessageReceived(const QString& topic, const QString& payload);

private slots:
    void onInternalConnected();
    void onInternalDisconnected();
    void onInternalConnectionFailed(const QString& error);
    void onInternalMessageReceived(const QString& topic, const QString& payload);

    // Data source slots
    void onPhaseChanged();
    void onShotSampleReceived();
    void onWaterLevelChanged();
    void onDE1StateChanged();
    void onDE1ConnectedChanged();

    // Publishing
    void publishTelemetry();
    void publishState();

    // Reconnection
    void attemptReconnect();
    void onSettingsChanged();

private:
    void setupSubscriptions();
    void publishHomeAssistantDiscovery();
    void handleCommand(const QString& command);
    QString topicPath(const QString& subtopic) const;
    QJsonObject buildDeviceInfo() const;
    void publishDiscoveryConfig(const QString& component, const QString& objectId,
                                const QJsonObject& config);
    void publish(const QString& topic, const QString& payload, bool retain = true);
    void publishAvailability(bool online);
    QString generateClientId();

    // Paho callbacks (static, call instance methods via context)
    static void onConnectSuccess(void* context, MQTTAsync_successData* response);
    static void onConnectFailure(void* context, MQTTAsync_failureData* response);
    static void onConnectionLost(void* context, char* cause);
    static int onMessageArrived(void* context, char* topicName, int topicLen, MQTTAsync_message* message);
    static void onDisconnectSuccess(void* context, MQTTAsync_successData* response);
    static void onSubscribeSuccess(void* context, MQTTAsync_successData* response);
    static void onSubscribeFailure(void* context, MQTTAsync_failureData* response);

    MQTTAsync m_client = nullptr;
    DE1Device* m_device = nullptr;
    MachineState* m_machineState = nullptr;
    Settings* m_settings = nullptr;
    MainController* m_mainController = nullptr;

    QTimer m_publishTimer;
    QTimer m_reconnectTimer;
    int m_reconnectAttempts = 0;
    static constexpr int MAX_RECONNECT_ATTEMPTS = 10;
    static constexpr int RECONNECT_DELAY_MS = 5000;

    QString m_status;
    bool m_connected = false;
    bool m_discoveryPublished = false;
    QString m_lastPublishedState;
    QString m_lastPublishedPhase;
    QString m_lastPublishedProfile;
    QString m_currentProfile;
    QString m_currentProfileFilename;
    QString m_lastPublishedSteamMode;
    bool m_lastPublishedScaleConnected = false;
    int m_lastPublishedEspressoCount = -1;
    QString m_clientId;

    mutable QMutex m_mutex;
};
