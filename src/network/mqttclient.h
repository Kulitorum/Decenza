#pragma once

#include <QObject>
#include <QTimer>
#include <QMutex>
#include <algorithm>

extern "C" {
#include <MQTTAsync.h>
}

class DE1Device;
class MachineState;
class Settings;
class SettingsMqtt;
class MainController;

class MqttClient : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int reconnectAttempts READ reconnectAttempts NOTIFY reconnectAttemptsChanged)
    Q_PROPERTY(QString currentProfile READ currentProfile WRITE setCurrentProfile NOTIFY currentProfileChanged)

public:
    explicit MqttClient(DE1Device* device, MachineState* machineState,
                       Settings* settings, SettingsMqtt* settingsMqtt,
                       QObject* parent = nullptr);
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

#ifdef DECENZA_TESTING
    // The reconnect state machine is pure logic over a QTimer and a handful of flags,
    // and every one of its inputs is a private slot. Exposing it to the test is what
    // lets tst_mqttclient assert on timer state directly, with no broker and no waiting.
    // Added after a latched m_userRequestedDisconnect shipped in a change whose entire
    // purpose was to stop reconnection from dying — this file was in no test target at
    // all, so nothing could have caught it.
    friend class tst_MqttClient;
#endif

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
    void onPublishTimerTick();
    void publishState();

    // Reconnection
    void onReconnectTimerTick();
    void onSettingsChanged();

private:
    void setupSubscriptions();
    void publishHomeAssistantDiscovery();
    void handleCommand(const QString& command);
    QString topicPath(const QString& subtopic) const;
    QJsonObject buildDeviceInfo() const;
    void publishDiscoveryConfig(const QString& component, const QString& objectId,
                                const QJsonObject& config);
    void connectWithHost(const QString& host);
    void publish(const QString& topic, const QString& payload, bool retain = true);
    void publishAvailability(bool online);
    QString generateClientId();
    void onNetworkReachabilityChanged(bool reachable);
    QString reconnectStatusText() const;
    void scheduleReconnect(const QString& reason);

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
    SettingsMqtt* m_settingsMqtt = nullptr;
    MainController* m_mainController = nullptr;

    QTimer m_publishTimer;
    QTimer m_reconnectTimer;
    int m_reconnectAttempts = 0;
    bool m_isReconnecting = false;
    // True only while QNetworkInformation positively reports Disconnected (Unknown is
    // NOT offline — see the constructor). Reconnect attempts are not spent while it
    // holds, and the transition back to reachable is what resumes them.
    bool m_networkDown = false;
    // Attempts spent at the FAST cadence before dropping to the slow one. Not a
    // stopping point: retries continue indefinitely, just rarely. It used to be
    // terminal, which meant a broker outage longer than the budget (~7 min) killed
    // MQTT until someone intervened — a Home Assistant restart or a broker redeploy
    // was enough, and nothing about that is the user's fault or their job to notice.
    static constexpr int MAX_FAST_RECONNECT_ATTEMPTS = 10;
    static constexpr int INITIAL_RECONNECT_DELAY_MS = 5000;
    static constexpr int MAX_RECONNECT_DELAY_MS = 60000;
    // Slow cadence once the fast budget is spent. A TCP connect to a LAN broker is
    // negligible, so this is about log noise and not looking frantic, not cost —
    // 15 min recovers an unattended broker restart well within the time it takes
    // anyone to notice Home Assistant went quiet.
    static constexpr int IDLE_RECONNECT_DELAY_MS = 15 * 60 * 1000;
    // Exponential backoff: 5s, 10s, 20s, 40s, 60s, 60s… then every 15 min forever.
    int reconnectDelayMs() const {
        if (m_reconnectAttempts >= MAX_FAST_RECONNECT_ATTEMPTS)
            return IDLE_RECONNECT_DELAY_MS;
        return std::min(INITIAL_RECONNECT_DELAY_MS * (1 << std::min(m_reconnectAttempts, 20)),
                        MAX_RECONNECT_DELAY_MS);
    }
    // Latches when the slow cadence is announced, so the transition is logged once
    // rather than every 15 minutes for as long as the broker stays away.
    bool m_slowRetryAnnounced = false;
    // A stop the user asked for is not a fault, so it must not re-arm the retry loop.
    //
    // INVARIANT: this may only be true while a disconnect callback is actually pending.
    // It is set in the ONE branch of disconnectFromBroker() that triggers that callback,
    // consumed by onInternalDisconnected(), and cleared again in connectToBroker() so a
    // callback lost to MQTTAsync_destroy() cannot strand it. A latched true is not a
    // cosmetic bug: the next genuine broker drop reads as user-requested, stops the
    // timer, and leaves MQTT dead until the app restarts — the exact terminal death the
    // slow-retry cadence was written to end.
    bool m_userRequestedDisconnect = false;
    // One definition for a string that is both WRITTEN and COMPARED. It had three
    // copies; editing any one of them would have silently stopped the clear-on-resume
    // from matching, re-creating the permanently-latched status it exists to prevent,
    // with no compiler help.
    static constexpr auto kWaitingForNetwork = "Waiting for network...";
    // Same reasoning, same trap: the down-edge WRITES this and the up-edge COMPARES it.
    // Two copies of the literal is how the connected case came to be left latched on
    // screen forever while kWaitingForNetwork was handled correctly beside it.
    static constexpr auto kConnectedNetworkUnreachable = "Connected - network unreachable";

    QString m_status;
    bool m_connected = false;
    bool m_discoveryPublished = false;
    QString m_lastPublishedState;
    QString m_lastPublishedPhase;
    QString m_lastPublishedSubstate;
    QString m_lastPublishedProfile;
    QString m_currentProfile;
    QString m_currentProfileFilename;
    QString m_lastPublishedSteamMode;
    bool m_lastPublishedScaleConnected = false;
    int m_lastPublishedEspressoCount = -1;
    QString m_clientId;

    mutable QMutex m_mutex;
};
