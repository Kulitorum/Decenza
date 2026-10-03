#pragma once

#include "core/logcollapse.h"
#include <QAbstractSocket>
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSslError>
#include <QStringList>
#include <QTimer>
#include <algorithm>
#include <QtQmlIntegration/qqmlintegration.h>

class DE1Device;
class MachineState;
class Settings;
class SettingsMqtt;
class MainController;
class QMqttClient;
class QMqttSubscription;
class QTcpSocket;

/**
 * MQTT client for Home Assistant integration, on Qt MQTT (built in-tree, cmake/qtmqtt.cmake).
 *
 * One QMqttClient lives as long as this object. Each connection attempt opens its own
 * TCP or TLS socket and hands it over once connected (see connectWithHost()), which is
 * what lets TLS verify a certificate against the configured host name when the address
 * came from our own mDNS lookup, and keeps the real socket error for the status text.
 *
 * Topics under <base> (default "decenza"):
 *   availability (online/offline, LWT), state, phase, substate, connected, scale_connected,
 *   temperature/{head,mix,steam}, pressure, flow, weight, shot_time, target_weight,
 *   water_level, water_level_ml, profile, profile_filename, recipe, steam_mode,
 *   steam_state, espresso_count, last_shot (JSON), event/shot (JSON, not retained)
 * Subscribed: command (wake/sleep/steam_on/steam_off/stop), profile/set (filename),
 *   profile/select and recipe/select (titles, from the HA selects), and homeassistant/status.
 */
class MqttClient : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("MqttClient is created in C++ and reached via MainController")

    // True only once the broker has accepted the login AND the command subscriptions.
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int reconnectAttempts READ reconnectAttempts NOTIFY reconnectAttemptsChanged)
    Q_PROPERTY(QString currentProfile READ currentProfile WRITE setCurrentProfile NOTIFY currentProfileChanged)

public:
    explicit MqttClient(DE1Device* device, MachineState* machineState,
                       Settings* settings, SettingsMqtt* settingsMqtt,
                       QObject* parent = nullptr);
    ~MqttClient();

    bool isConnected() const { return m_connected; }
    QString status() const { return m_status; }
    int reconnectAttempts() const { return m_reconnectAttempts; }
    QString currentProfile() const { return m_currentProfile; }
    void setCurrentProfile(const QString& profile);
    void setCurrentProfileFilename(const QString& filename);

    // For the steam heater policy and the espresso count.
    void setMainController(MainController* controller);

    Q_INVOKABLE void connectToBroker();
    Q_INVOKABLE void disconnectFromBroker();
    Q_INVOKABLE void publishDiscovery();
    // SettingsMqtt::regenerateMqttDeviceId(), re-announced if connected.
    Q_INVOKABLE void newDeviceId();

    // For QML; see SettingsMqtt::describeCaCertificate().
    Q_INVOKABLE QString describeCaCertificate(const QString& pem) const;

    // User-facing reason for a failed attempt. Precedence: the broker's CONNACK/protocol
    // error, then the TLS errors, then the socket error.
    static QString failureReason(int clientError, QAbstractSocket::SocketError socketError,
                                 const QList<QSslError>& sslErrors);

    // A saved shot, as MainController reads it back from history.
    struct LastShot {
        qint64 startEpochSec = 0;
        double durationSec = 0.0;
        double doseG = 0.0;
        double yieldG = 0.0;
        QString profile;
    };
    // Plain-data inputs from MainController, so this class never queries ProfileManager
    // or reads shots from history itself.
    void setProfileTitles(const QStringList& titles);
    // Non-archived recipe names, and the active one ("" when none).
    void setRecipeTitles(const QStringList& titles);
    void setActiveRecipe(const QString& name);
    void setLastShot(const LastShot& shot);

public slots:
    void onScaleConnectedChanged(bool connected);
    void onSteamSettingsChanged();

    // Shot lifecycle, wired by MainController. `maintenance`: a cleaning, descale or
    // calibration profile.
    void onEspressoCycleStarted(bool maintenance);
    void onShotPersisted(double durationSec, double yieldG);
    void onShotNotSaved();

signals:
    void connectedChanged();
    void statusChanged();
    void reconnectAttemptsChanged();
    void commandReceived(const QString& command);
    void profileSelectRequested(const QString& profileFilename);
    // From the Home Assistant select, which shows titles; MainController resolves it.
    void profileTitleSelectRequested(const QString& profileTitle);
    // From the Home Assistant recipe select; MainController resolves the name.
    void recipeTitleSelectRequested(const QString& recipeName);
    // Asks MainController for the most recent saved shot (answered via setLastShot()).
    void lastShotRequested();
    void currentProfileChanged();
    void steamOnRequested();
    void steamOffRequested();
    // Only emitted when the phase allows a remote stop (see handleCommand()).
    void stopRequested();

#ifdef DECENZA_TESTING
    friend class tst_MqttClient;
#endif

private slots:
    void onClientStateChanged();
    void onPhaseChanged();
    void onWaterLevelChanged();
    void onDE1StateChanged();
    void onDE1ConnectedChanged();
    void onPublishTimerTick();
    void publishState();
    void onReconnectTimerTick();
    void onSettingsChanged();
    void onDiscoverySettingChanged();

private:
    // Availability class of a Home Assistant entity (applied in componentConfig()).
    enum class Source { App, Machine, Scale };
    struct DiscoveryEntry {
        QString component;
        QString objectId;
        QJsonObject config;   // unique_id holds only the suffix; availability and device are added on publish
        Source source = Source::App;
    };

    void connectWithHost(const QString& host);
    void onSocketReady(QTcpSocket* socket);
    void onSocketError(QTcpSocket* socket);
    void abandonSocket(QTcpSocket* socket);
    void onSessionUp();
    void onSessionDown();
    void endSession();
    void onConnectionFailed(const QString& reason);
    void onSubscriptionState(QMqttSubscription* subscription);
    void updateVerifiedState();
    void onMessageReceived(const QByteArray& message, const QString& topic);

    void setupSubscriptions();
    void handleCommand(const QString& command);
    QString topicPath(const QString& subtopic) const;
    // Retained only if `retain` AND the user's retain setting; QoS 0. The ordinary path.
    void publish(const QString& topic, const QString& payload, bool retain = true);
    // Flags exactly as given, for messages whose retain/QoS must not follow publish()'s rule.
    void publishRaw(const QString& topic, const QString& payload, bool retain, quint8 qos);
    void publishAvailability(bool online);
    void republishAll();
    void onNetworkReachabilityChanged(bool reachable);
    QString reconnectStatusText() const;
    void scheduleReconnect(const QString& reason);
    bool stopAllowedInCurrentPhase() const;

    QString deviceId() const;
    QJsonObject buildDeviceInfo() const;
    QList<DiscoveryEntry> discoveryEntries() const;
    QJsonObject componentConfig(const DiscoveryEntry& entry) const;
    // The device-based discovery message; `publishedComponents` receives the component
    // keys it describes, for SettingsMqtt's bookkeeping.
    QJsonObject deviceDiscoveryPayload(QStringList* publishedComponents = nullptr) const;
    QString deviceDiscoveryTopic() const;
    static QString legacyDiscoveryTopic(const QString& component, const QString& objectId);
    static QStringList legacyDiscoveryTopics();
    void publishHomeAssistantDiscovery();
    void retractDiscovery();

    static QJsonObject lastShotSummary(const LastShot& shot);
    void publishLastShot();
    void publishShotEvent(const QString& eventType, const QJsonObject& detail = {});

    DE1Device* m_device = nullptr;
    MachineState* m_machineState = nullptr;
    Settings* m_settings = nullptr;
    SettingsMqtt* m_settingsMqtt = nullptr;
    MainController* m_mainController = nullptr;

    // Deleted explicitly in ~MqttClient, before the socket children (see there).
    QMqttClient* m_client = nullptr;
#ifdef DECENZA_TESTING
    struct Published { QString topic; QString payload; bool retain; quint8 qos; };
    // When set, publishRaw() records here instead of sending (tests only).
    QList<Published>* m_publishRecorder = nullptr;
#endif
    // The attempt in flight, not yet handed to m_client.
    QPointer<QTcpSocket> m_pendingSocket;
    // The socket m_client holds. Kept alive until the NEXT hand-over replaces it:
    // setTransport() disconnects from the previous transport by pointer
    // (qmqttconnection.cpp:102-126), so deleting it earlier would leave m_client
    // holding a dangling QObject*.
    QPointer<QTcpSocket> m_activeSocket;
    QString m_attemptHost;            // the name the user configured, for TLS verification
    QString m_attemptAddress;         // what we dialled (the mDNS result, on Android .local)
    bool m_attemptTls = false;
    QList<QSslError> m_lastSslErrors;
    QString m_failureOverride;        // set when WE ended the attempt, e.g. the deadline
    bool m_useMqtt31 = false;         // broker rejected 3.1.1; reset when settings change

    QTimer m_publishTimer;
    QTimer m_reconnectTimer;
    // Deadline from dial to CONNACK. QTcpSocket has none (the OS SYN timeout runs to
    // minutes) and Qt MQTT none for CONNACK (it has only a ping timer).
    QTimer m_attemptDeadline;
    static constexpr int ATTEMPT_DEADLINE_MS = 30000;

    int m_reconnectAttempts = 0;
    bool m_isReconnecting = false;
    bool m_networkDown = false;

    static constexpr int MAX_FAST_RECONNECT_ATTEMPTS = 10;
    static constexpr int INITIAL_RECONNECT_DELAY_MS = 5000;
    static constexpr int MAX_RECONNECT_DELAY_MS = 60000;
    static constexpr int IDLE_RECONNECT_DELAY_MS = 15 * 60 * 1000;
    int reconnectDelayMs() const {
        if (m_reconnectAttempts >= MAX_FAST_RECONNECT_ATTEMPTS)
            return IDLE_RECONNECT_DELAY_MS;
        return std::min(INITIAL_RECONNECT_DELAY_MS * (1 << std::min(m_reconnectAttempts, 20)),
                        MAX_RECONNECT_DELAY_MS);
    }
    bool m_slowRetryAnnounced = false;

    LogCollapse m_logCollapse{LogCollapse::kChangesOnly};

    // One-shot: suppresses the reconnect that would otherwise follow the disconnect the
    // user asked for. Armed only when a session is up (see disconnectFromBroker()).
    bool m_userRequestedDisconnect = false;
    // Set only around connectWithHost()'s abort of the session it is replacing.
    bool m_replacingSession = false;

    static constexpr auto kWaitingForNetwork = "Waiting for network...";
    static constexpr auto kConnectedNetworkUnreachable = "Connected - network unreachable";
    static constexpr auto kConnecting = "Connecting...";

    QString m_status;
    bool m_sessionUp = false;     // CONNACK accepted — publishing works
    bool m_connected = false;     // session up AND command subscriptions acknowledged
    QList<QPointer<QMqttSubscription>> m_requiredSubscriptions;
    QList<QPointer<QMqttSubscription>> m_sessionSubscriptions;   // all of them, to delete
    QString m_refusedSubscription;

    int m_discoveryEntityCount = 0;
    QString m_lastPublishedState;
    QString m_lastPublishedPhase;
    QString m_lastPublishedSubstate;
    QString m_lastPublishedProfile;
    QString m_currentProfile;
    QString m_currentProfileFilename;
    QString m_lastPublishedSteamMode;
    bool m_lastPublishedScaleConnected = false;
    bool m_scaleConnected = false;
    int m_lastPublishedEspressoCount = -1;


    // Shot lifecycle: one terminal event per espresso cycle, none for maintenance runs.
    bool m_shotCycleOpen = false;
    bool m_haveLastShot = false;
    LastShot m_lastShot;
    QStringList m_profileTitles;
    QStringList m_recipeTitles;
    QString m_activeRecipe;
    void publishActiveRecipe();
};
