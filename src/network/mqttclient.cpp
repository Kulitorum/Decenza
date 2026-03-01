#include "mqttclient.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../core/settings.h"
#include "../controllers/maincontroller.h"
#include "version.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHostInfo>
#include <QUuid>
#include <QMutexLocker>

MqttClient::MqttClient(DE1Device* device, MachineState* machineState,
                       Settings* settings, QObject* parent)
    : QObject(parent)
    , m_device(device)
    , m_machineState(machineState)
    , m_settings(settings)
{
    // Connect internal signals for thread-safe callback handling
    connect(this, &MqttClient::internalConnected, this, &MqttClient::onInternalConnected, Qt::QueuedConnection);
    connect(this, &MqttClient::internalDisconnected, this, &MqttClient::onInternalDisconnected, Qt::QueuedConnection);
    connect(this, &MqttClient::internalConnectionFailed, this, &MqttClient::onInternalConnectionFailed, Qt::QueuedConnection);
    connect(this, &MqttClient::internalMessageReceived, this, &MqttClient::onInternalMessageReceived, Qt::QueuedConnection);

    // Connect data source signals
    if (m_machineState) {
        connect(m_machineState, &MachineState::phaseChanged, this, &MqttClient::onPhaseChanged);
    }
    if (m_device) {
        connect(m_device, &DE1Device::shotSampleReceived, this, &MqttClient::onShotSampleReceived);
        connect(m_device, &DE1Device::waterLevelChanged, this, &MqttClient::onWaterLevelChanged);
        connect(m_device, &DE1Device::stateChanged, this, &MqttClient::onDE1StateChanged);
        connect(m_device, &DE1Device::connectedChanged, this, &MqttClient::onDE1ConnectedChanged);
    }

    // Settings changes
    if (m_settings) {
        connect(m_settings, &Settings::mqttEnabledChanged, this, &MqttClient::onSettingsChanged);
        connect(m_settings, &Settings::mqttBrokerHostChanged, this, &MqttClient::onSettingsChanged);
        connect(m_settings, &Settings::mqttBrokerPortChanged, this, &MqttClient::onSettingsChanged);
        connect(m_settings, &Settings::mqttUsernameChanged, this, &MqttClient::onSettingsChanged);
        connect(m_settings, &Settings::mqttPasswordChanged, this, &MqttClient::onSettingsChanged);
        connect(m_settings, &Settings::mqttPublishIntervalChanged, this, [this]() {
            if (m_publishTimer.isActive()) {
                m_publishTimer.setInterval(m_settings->mqttPublishInterval());
            }
        });
    }

    // Publish timer
    connect(&m_publishTimer, &QTimer::timeout, this, &MqttClient::publishTelemetry);

    // Reconnect timer
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &MqttClient::attemptReconnect);

    m_status = "Disconnected";
}

MqttClient::~MqttClient()
{
    if (m_client) {
        if (m_connected) {
            // Publish offline status synchronously
            QString topic = topicPath("availability");
            QByteArray topicBytes = topic.toUtf8();
            QByteArray payload = "offline";

            MQTTAsync_message msg = MQTTAsync_message_initializer;
            msg.payload = payload.data();
            msg.payloadlen = payload.length();
            msg.qos = 0;
            msg.retained = 1;

            MQTTAsync_sendMessage(m_client, topicBytes.constData(), &msg, nullptr);

            MQTTAsync_disconnect(m_client, nullptr);
        }
        MQTTAsync_destroy(&m_client);
    }
}

bool MqttClient::isConnected() const
{
    QMutexLocker locker(&m_mutex);
    return m_connected;
}

QString MqttClient::generateClientId()
{
    QString clientId = m_settings ? m_settings->mqttClientId() : "";
    if (clientId.isEmpty()) {
        QString hostname = QHostInfo::localHostName();
        if (hostname.isEmpty()) {
            hostname = "decenza";
        }
        clientId = QString("decenza_%1_%2")
            .arg(hostname)
            .arg(QUuid::createUuid().toString(QUuid::Id128).left(8));
        // Persist the generated client ID so it survives app restarts/updates
        if (m_settings) {
            m_settings->setMqttClientId(clientId);
            qDebug() << "MqttClient: Generated and saved new client ID:" << clientId;
        }
    }
    return clientId;
}

// Paho callback implementations
void MqttClient::onConnectSuccess(void* context, MQTTAsync_successData* /*response*/)
{
    MqttClient* self = static_cast<MqttClient*>(context);
    emit self->internalConnected();
}

void MqttClient::onConnectFailure(void* context, MQTTAsync_failureData* response)
{
    MqttClient* self = static_cast<MqttClient*>(context);
    QString error = response && response->message ? QString::fromUtf8(response->message) : "Connection failed";
    emit self->internalConnectionFailed(error);
}

void MqttClient::onConnectionLost(void* context, char* cause)
{
    MqttClient* self = static_cast<MqttClient*>(context);
    Q_UNUSED(cause);
    emit self->internalDisconnected();
}

int MqttClient::onMessageArrived(void* context, char* topicName, int /*topicLen*/, MQTTAsync_message* message)
{
    MqttClient* self = static_cast<MqttClient*>(context);

    QString topic = QString::fromUtf8(topicName);
    QString payload = QString::fromUtf8(static_cast<char*>(message->payload), message->payloadlen);

    emit self->internalMessageReceived(topic, payload);

    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1; // Message handled
}

void MqttClient::onDisconnectSuccess(void* context, MQTTAsync_successData* /*response*/)
{
    MqttClient* self = static_cast<MqttClient*>(context);
    emit self->internalDisconnected();
}

void MqttClient::onSubscribeSuccess(void* context, MQTTAsync_successData* /*response*/)
{
    Q_UNUSED(context);
    qDebug() << "MqttClient: Subscription successful";
}

void MqttClient::onSubscribeFailure(void* context, MQTTAsync_failureData* response)
{
    Q_UNUSED(context);
    QString error = response && response->message ? QString::fromUtf8(response->message) : "Unknown error";
    qWarning() << "MqttClient: Subscription failed -" << error;
}

void MqttClient::connectToBroker()
{
    if (!m_settings) {
        m_status = "Error: No settings";
        emit statusChanged();
        return;
    }

    QString host = m_settings->mqttBrokerHost();
    if (host.isEmpty()) {
        m_status = "Error: No broker host configured";
        emit statusChanged();
        return;
    }

    // Clean up old client if exists
    if (m_client) {
        if (m_connected) {
            MQTTAsync_disconnect(m_client, nullptr);
        }
        MQTTAsync_destroy(&m_client);
        m_client = nullptr;
    }

    m_reconnectAttempts = 0;
    emit reconnectAttemptsChanged();

    // Build server URI
    int port = m_settings->mqttBrokerPort();
    QString serverUri = QString("tcp://%1:%2").arg(host).arg(port);
    QByteArray serverUriBytes = serverUri.toUtf8();

    // Generate client ID
    m_clientId = generateClientId();
    QByteArray clientIdBytes = m_clientId.toUtf8();

    // Create client
    int rc = MQTTAsync_create(&m_client, serverUriBytes.constData(), clientIdBytes.constData(),
                              MQTTCLIENT_PERSISTENCE_NONE, nullptr);
    if (rc != MQTTASYNC_SUCCESS) {
        m_status = QString("Error: Failed to create client (%1)").arg(rc);
        emit statusChanged();
        return;
    }

    // Set callbacks
    rc = MQTTAsync_setCallbacks(m_client, this, onConnectionLost, onMessageArrived, nullptr);
    if (rc != MQTTASYNC_SUCCESS) {
        m_status = QString("Error: Failed to set callbacks (%1)").arg(rc);
        emit statusChanged();
        MQTTAsync_destroy(&m_client);
        m_client = nullptr;
        return;
    }

    // Prepare connection options
    MQTTAsync_connectOptions connOpts = MQTTAsync_connectOptions_initializer;
    connOpts.keepAliveInterval = 60;
    connOpts.cleansession = 1;
    connOpts.onSuccess = onConnectSuccess;
    connOpts.onFailure = onConnectFailure;
    connOpts.context = this;
    connOpts.automaticReconnect = 0; // We handle reconnection ourselves

    // Set credentials if provided
    QString username = m_settings->mqttUsername();
    QString password = m_settings->mqttPassword();
    QByteArray usernameBytes = username.toUtf8();
    QByteArray passwordBytes = password.toUtf8();
    if (!username.isEmpty()) {
        connOpts.username = usernameBytes.constData();
        connOpts.password = passwordBytes.constData();
    }

    // Set Last Will and Testament
    QString willTopic = topicPath("availability");
    QByteArray willTopicBytes = willTopic.toUtf8();
    QByteArray willPayload = "offline";

    MQTTAsync_willOptions willOpts = MQTTAsync_willOptions_initializer;
    willOpts.topicName = willTopicBytes.constData();
    willOpts.message = willPayload.constData();
    willOpts.retained = 1;
    willOpts.qos = 1;
    connOpts.will = &willOpts;

    m_status = "Connecting...";
    emit statusChanged();

    qDebug() << "MqttClient: Connecting to" << serverUri;

    rc = MQTTAsync_connect(m_client, &connOpts);
    if (rc != MQTTASYNC_SUCCESS) {
        m_status = QString("Error: Connect failed (%1)").arg(rc);
        emit statusChanged();
        MQTTAsync_destroy(&m_client);
        m_client = nullptr;
    }
}

void MqttClient::disconnectFromBroker()
{
    m_reconnectTimer.stop();
    m_publishTimer.stop();
    m_reconnectAttempts = 0;
    emit reconnectAttemptsChanged();

    if (m_client && m_connected) {
        publishAvailability(false);

        MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
        opts.onSuccess = onDisconnectSuccess;
        opts.context = this;
        MQTTAsync_disconnect(m_client, &opts);
    } else {
        {
            QMutexLocker locker(&m_mutex);
            m_connected = false;
        }
        m_status = "Disconnected";
        emit statusChanged();
        emit connectedChanged();
    }
}

void MqttClient::onInternalConnected()
{
    qDebug() << "MqttClient: Connected to broker";

    {
        QMutexLocker locker(&m_mutex);
        m_connected = true;
    }

    m_status = "Connected";
    emit statusChanged();
    emit connectedChanged();

    m_reconnectAttempts = 0;
    emit reconnectAttemptsChanged();

    // Publish availability
    publishAvailability(true);

    // Subscribe to command topic
    setupSubscriptions();

    // Publish Home Assistant discovery if enabled
    if (m_settings && m_settings->mqttHomeAssistantDiscovery()) {
        publishHomeAssistantDiscovery();
    }

    // Start publishing telemetry
    int interval = m_settings ? m_settings->mqttPublishInterval() : 1000;
    m_publishTimer.start(interval);

    // Publish initial state
    publishState();
    publishTelemetry();
}

void MqttClient::onInternalDisconnected()
{
    qDebug() << "MqttClient: Disconnected from broker";

    {
        QMutexLocker locker(&m_mutex);
        m_connected = false;
    }

    m_publishTimer.stop();
    emit connectedChanged();

    // Attempt reconnection if MQTT is still enabled
    if (m_settings && m_settings->mqttEnabled() && m_reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
        m_status = QString("Disconnected - reconnecting (%1/%2)...")
            .arg(m_reconnectAttempts + 1)
            .arg(MAX_RECONNECT_ATTEMPTS);
        emit statusChanged();
        m_reconnectTimer.start(reconnectDelayMs());
    } else if (m_reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
        m_status = "Disconnected - max retries reached";
        emit statusChanged();
    } else {
        m_status = "Disconnected";
        emit statusChanged();
    }
}

void MqttClient::onInternalConnectionFailed(const QString& error)
{
    qWarning() << "MqttClient: Connection failed -" << error;

    {
        QMutexLocker locker(&m_mutex);
        m_connected = false;
    }

    m_status = "Error: " + error;
    emit statusChanged();
    emit connectedChanged();

    // Attempt reconnection with exponential backoff
    if (m_settings && m_settings->mqttEnabled() && m_reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
        int delay = reconnectDelayMs();
        qDebug() << "MqttClient: Retrying in" << delay / 1000 << "seconds";
        m_reconnectTimer.start(delay);
    }
}

void MqttClient::onInternalMessageReceived(const QString& topic, const QString& payload)
{
    qDebug() << "MqttClient: Received message on" << topic << ":" << payload;

    if (topic.endsWith("/command")) {
        handleCommand(payload.trimmed().toLower());
    } else if (topic.endsWith("/profile/set")) {
        // Profile selection - payload is the profile name (filename without .json)
        QString profileName = payload.trimmed();
        if (!profileName.isEmpty()) {
            qDebug() << "MqttClient: Profile selection requested:" << profileName;
            emit profileSelectRequested(profileName);
        }
    }
}

void MqttClient::setupSubscriptions()
{
    if (!m_client || !isConnected() || !m_settings) return;

    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
    opts.onSuccess = onSubscribeSuccess;
    opts.onFailure = onSubscribeFailure;
    opts.context = this;

    // Subscribe to command topic
    QString commandTopic = topicPath("command");
    QByteArray commandBytes = commandTopic.toUtf8();
    int rc = MQTTAsync_subscribe(m_client, commandBytes.constData(), 1, &opts);
    if (rc != MQTTASYNC_SUCCESS) {
        qWarning() << "MqttClient: Failed to subscribe to" << commandTopic << "- error" << rc;
    } else {
        qDebug() << "MqttClient: Subscribing to" << commandTopic;
    }

    // Subscribe to profile/set topic for profile selection
    QString profileTopic = topicPath("profile/set");
    QByteArray profileBytes = profileTopic.toUtf8();
    rc = MQTTAsync_subscribe(m_client, profileBytes.constData(), 1, &opts);
    if (rc != MQTTASYNC_SUCCESS) {
        qWarning() << "MqttClient: Failed to subscribe to" << profileTopic << "- error" << rc;
    } else {
        qDebug() << "MqttClient: Subscribing to" << profileTopic;
    }
}

void MqttClient::handleCommand(const QString& command)
{
    if (command == "wake") {
        if (m_device) {
            m_device->wakeUp();
            qDebug() << "MqttClient: Wake command executed";
        }
        emit commandReceived("wake");
    } else if (command == "sleep") {
        if (m_device) {
            m_device->goToSleep();
            qDebug() << "MqttClient: Sleep command executed";
        }
        emit commandReceived("sleep");
    } else if (command == "steam_on") {
        emit steamOnRequested();
        emit commandReceived("steam_on");
        qDebug() << "MqttClient: Steam on command executed";
    } else if (command == "steam_off") {
        emit steamOffRequested();
        emit commandReceived("steam_off");
        qDebug() << "MqttClient: Steam off command executed";
    } else {
        qWarning() << "MqttClient: Unknown command:" << command;
    }
}

void MqttClient::setCurrentProfile(const QString& profile)
{
    if (m_currentProfile != profile) {
        m_currentProfile = profile;
        emit currentProfileChanged();

        // Publish profile change
        if (isConnected() && profile != m_lastPublishedProfile) {
            publish(topicPath("profile"), profile, true);
            m_lastPublishedProfile = profile;
            qDebug() << "MqttClient: Published profile change:" << profile;
        }
    }
}

void MqttClient::setCurrentProfileFilename(const QString& filename)
{
    if (m_currentProfileFilename != filename) {
        m_currentProfileFilename = filename;

        // Publish filename change
        if (isConnected() && !filename.isEmpty()) {
            publish(topicPath("profile_filename"), filename, true);
            qDebug() << "MqttClient: Published profile filename change:" << filename;
        }
    }
}

void MqttClient::setMainController(MainController* controller)
{
    m_mainController = controller;
}

void MqttClient::attemptReconnect()
{
    if (!m_settings || !m_settings->mqttEnabled()) {
        return;
    }

    m_reconnectAttempts++;
    emit reconnectAttemptsChanged();

    qDebug() << "MqttClient: Reconnection attempt" << m_reconnectAttempts << "of" << MAX_RECONNECT_ATTEMPTS;

    connectToBroker();
}

void MqttClient::onSettingsChanged()
{
    // If settings changed significantly, reconnect
    if (isConnected()) {
        disconnectFromBroker();
    }

    if (m_settings && m_settings->mqttEnabled()) {
        connectToBroker();
    }
}

QString MqttClient::topicPath(const QString& subtopic) const
{
    QString baseTopic = m_settings ? m_settings->mqttBaseTopic() : "decenza";
    return baseTopic + "/" + subtopic;
}

void MqttClient::publish(const QString& topic, const QString& payload, bool retain)
{
    if (!isConnected() || !m_client) return;

    bool shouldRetain = retain && m_settings && m_settings->mqttRetainMessages();

    QByteArray topicBytes = topic.toUtf8();
    QByteArray payloadBytes = payload.toUtf8();

    MQTTAsync_message msg = MQTTAsync_message_initializer;
    msg.payload = payloadBytes.data();
    msg.payloadlen = payloadBytes.length();
    msg.qos = 0;
    msg.retained = shouldRetain ? 1 : 0;

    MQTTAsync_sendMessage(m_client, topicBytes.constData(), &msg, nullptr);
}

void MqttClient::publishAvailability(bool online)
{
    publish(topicPath("availability"), online ? "online" : "offline", true);
}

void MqttClient::onPhaseChanged()
{
    publishState();
}

void MqttClient::onDE1StateChanged()
{
    publishState();
}

void MqttClient::onDE1ConnectedChanged()
{
    if (!isConnected()) return;

    bool connected = m_device && m_device->isConnected();
    publish(topicPath("connected"), connected ? "true" : "false", true);
}

void MqttClient::onShotSampleReceived()
{
    // During shots, publish at higher rate if needed
    // The regular timer handles normal publishing
}

void MqttClient::onWaterLevelChanged()
{
    if (!isConnected() || !m_device) return;

    publish(topicPath("water_level"), QString::number(static_cast<int>(m_device->waterLevel())), true);
    publish(topicPath("water_level_ml"), QString::number(m_device->waterLevelMl()), true);
}

void MqttClient::onScaleConnectedChanged(bool connected)
{
    if (!isConnected()) return;

    if (connected != m_lastPublishedScaleConnected) {
        publish(topicPath("scale_connected"), connected ? "true" : "false", true);
        m_lastPublishedScaleConnected = connected;
        qDebug() << "MqttClient: Published scale connected:" << connected;
    }
}

void MqttClient::onSteamSettingsChanged()
{
    // Trigger state republish to update steam mode
    publishState();
}

void MqttClient::publishState()
{
    if (!isConnected()) return;

    QString state = m_device ? m_device->stateString() : "Unknown";
    QString substate = m_device ? m_device->subStateString() : "unknown";
    QString phase = m_machineState ? m_machineState->phaseString() : "Unknown";

    // Only publish if changed to reduce traffic
    if (state != m_lastPublishedState) {
        publish(topicPath("state"), state, true);
        m_lastPublishedState = state;
    }

    if (phase != m_lastPublishedPhase) {
        publish(topicPath("phase"), phase, true);
        m_lastPublishedPhase = phase;
    }

    // Publish profile if changed
    if (!m_currentProfile.isEmpty() && m_currentProfile != m_lastPublishedProfile) {
        publish(topicPath("profile"), m_currentProfile, true);
        m_lastPublishedProfile = m_currentProfile;
    }

    // Profile filename
    if (!m_currentProfileFilename.isEmpty()) {
        publish(topicPath("profile_filename"), m_currentProfileFilename, true);
    }

    // Steam mode: derive from Settings and current phase
    // Per CLAUDE.md: steam is active in Ready/Steaming phases regardless of keepSteamHeaterOn
    QString steamMode;
    if (!m_device || !m_settings || m_device->stateString() == "Sleep") {
        steamMode = "Off";
    } else if (m_settings->steamDisabled()) {
        steamMode = "Off";
    } else if (phase == "Ready" || phase == "Steaming") {
        steamMode = "On";
    } else if (!m_settings->keepSteamHeaterOn()) {
        steamMode = "Off";
    } else {
        steamMode = "On";
    }
    if (steamMode != m_lastPublishedSteamMode) {
        publish(topicPath("steam_mode"), steamMode, true);
        publish(topicPath("steam_state"), steamMode != "Off" ? "true" : "false", true);
        m_lastPublishedSteamMode = steamMode;
    }

    publish(topicPath("substate"), substate, true);
}

void MqttClient::publishTelemetry()
{
    if (!isConnected()) return;

    if (m_device) {
        publish(topicPath("temperature/head"), QString::number(m_device->temperature(), 'f', 1), true);
        publish(topicPath("temperature/mix"), QString::number(m_device->mixTemperature(), 'f', 1), true);
        publish(topicPath("temperature/steam"), QString::number(m_device->steamTemperature(), 'f', 1), true);
        publish(topicPath("pressure"), QString::number(m_device->pressure(), 'f', 2), true);
        publish(topicPath("flow"), QString::number(m_device->flow(), 'f', 2), true);
    }

    if (m_machineState) {
        publish(topicPath("weight"), QString::number(m_machineState->scaleWeight(), 'f', 1), true);
        publish(topicPath("shot_time"), QString::number(m_machineState->shotTime(), 'f', 1), true);
        publish(topicPath("target_weight"), QString::number(m_machineState->targetWeight(), 'f', 1), true);
    }

    // Espresso count from shot history (only publish on change)
    if (m_mainController && m_mainController->shotHistory()) {
        int count = m_mainController->shotHistory()->totalShots();
        if (count != m_lastPublishedEspressoCount) {
            publish(topicPath("espresso_count"), QString::number(count), true);
            m_lastPublishedEspressoCount = count;
        }
    }
}

void MqttClient::publishDiscovery()
{
    if (isConnected()) {
        publishHomeAssistantDiscovery();
    }
}

QJsonObject MqttClient::buildDeviceInfo() const
{
    QJsonObject device;
    device["identifiers"] = QJsonArray{QString("decenza_de1_%1").arg(m_clientId)};
    device["name"] = "DE1 Espresso Machine";
    device["manufacturer"] = "Decent Espresso";
    device["model"] = "DE1";
    device["sw_version"] = VERSION_STRING;
    return device;
}

void MqttClient::publishDiscoveryConfig(const QString& component, const QString& objectId,
                                         const QJsonObject& config)
{
    // Discovery topic: homeassistant/{component}/de1_{objectId}/config
    QString topic = QString("homeassistant/%1/de1_%2/config").arg(component, objectId);
    QString payload = QJsonDocument(config).toJson(QJsonDocument::Compact);

    publish(topic, payload, true);
    qDebug() << "MqttClient: Published discovery for" << objectId;
}

void MqttClient::publishHomeAssistantDiscovery()
{
    if (!m_settings) return;

    QString baseTopic = m_settings->mqttBaseTopic();
    QJsonObject device = buildDeviceInfo();

    // State sensor
    {
        QJsonObject config;
        config["name"] = "DE1 State";
        config["state_topic"] = baseTopic + "/state";
        config["icon"] = "mdi:coffee-maker";
        config["unique_id"] = QString("de1_%1_state").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("sensor", "state", config);
    }

    // Head temperature sensor
    {
        QJsonObject config;
        config["name"] = "DE1 Head Temperature";
        config["state_topic"] = baseTopic + "/temperature/head";
        config["device_class"] = "temperature";
        config["unit_of_measurement"] = "\u00B0C";
        config["unique_id"] = QString("de1_%1_temp_head").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("sensor", "temperature_head", config);
    }

    // Mix temperature sensor
    {
        QJsonObject config;
        config["name"] = "DE1 Mix Temperature";
        config["state_topic"] = baseTopic + "/temperature/mix";
        config["device_class"] = "temperature";
        config["unit_of_measurement"] = "\u00B0C";
        config["unique_id"] = QString("de1_%1_temp_mix").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("sensor", "temperature_mix", config);
    }

    // Pressure sensor
    {
        QJsonObject config;
        config["name"] = "DE1 Pressure";
        config["state_topic"] = baseTopic + "/pressure";
        config["device_class"] = "pressure";
        config["unit_of_measurement"] = "bar";
        config["unique_id"] = QString("de1_%1_pressure").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("sensor", "pressure", config);
    }

    // Flow sensor
    {
        QJsonObject config;
        config["name"] = "DE1 Flow";
        config["state_topic"] = baseTopic + "/flow";
        config["unit_of_measurement"] = "ml/s";
        config["icon"] = "mdi:water-flow";
        config["unique_id"] = QString("de1_%1_flow").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("sensor", "flow", config);
    }

    // Weight sensor
    {
        QJsonObject config;
        config["name"] = "DE1 Weight";
        config["state_topic"] = baseTopic + "/weight";
        config["device_class"] = "weight";
        config["unit_of_measurement"] = "g";
        config["unique_id"] = QString("de1_%1_weight").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("sensor", "weight", config);
    }

    // Water level sensor
    {
        QJsonObject config;
        config["name"] = "DE1 Water Level";
        config["state_topic"] = baseTopic + "/water_level";
        config["unit_of_measurement"] = "%";
        config["icon"] = "mdi:water";
        config["unique_id"] = QString("de1_%1_water_level").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("sensor", "water_level", config);
    }

    // Shot time sensor
    {
        QJsonObject config;
        config["name"] = "DE1 Shot Time";
        config["state_topic"] = baseTopic + "/shot_time";
        config["unit_of_measurement"] = "s";
        config["icon"] = "mdi:timer";
        config["unique_id"] = QString("de1_%1_shot_time").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("sensor", "shot_time", config);
    }

    // Profile sensor (text sensor for current profile name)
    // To set profile from HA, publish to decenza/profile/set with profile name
    {
        QJsonObject config;
        config["name"] = "DE1 Profile";
        config["state_topic"] = baseTopic + "/profile";
        config["command_topic"] = baseTopic + "/profile/set";
        config["icon"] = "mdi:coffee";
        config["unique_id"] = QString("de1_%1_profile").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("text", "profile", config);
    }

    // Power switch (wake/sleep)
    {
        QJsonObject config;
        config["name"] = "DE1 Power";
        config["command_topic"] = baseTopic + "/command";
        config["state_topic"] = baseTopic + "/state";
        config["payload_on"] = "wake";
        config["payload_off"] = "sleep";
        config["state_on"] = "Idle";
        config["state_off"] = "Sleep";
        config["icon"] = "mdi:power";
        config["unique_id"] = QString("de1_%1_power").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("switch", "power", config);
    }

    // Scale connected (binary sensor)
    {
        QJsonObject config;
        config["name"] = "DE1 Scale Connected";
        config["state_topic"] = baseTopic + "/scale_connected";
        config["payload_on"] = "true";
        config["payload_off"] = "false";
        config["device_class"] = "connectivity";
        config["icon"] = "mdi:scale";
        config["unique_id"] = QString("de1_%1_scale_connected").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("binary_sensor", "scale_connected", config);
    }

    // Steam mode sensor
    {
        QJsonObject config;
        config["name"] = "DE1 Steam Mode";
        config["state_topic"] = baseTopic + "/steam_mode";
        config["icon"] = "mdi:water-boiler";
        config["unique_id"] = QString("de1_%1_steam_mode").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("sensor", "steam_mode", config);
    }

    // Steam switch
    {
        QJsonObject config;
        config["name"] = "DE1 Steam";
        config["command_topic"] = baseTopic + "/command";
        config["state_topic"] = baseTopic + "/steam_state";
        config["payload_on"] = "steam_on";
        config["payload_off"] = "steam_off";
        config["state_on"] = "true";
        config["state_off"] = "false";
        config["icon"] = "mdi:water-boiler";
        config["unique_id"] = QString("de1_%1_steam").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("switch", "steam", config);
    }

    // Espresso count sensor
    {
        QJsonObject config;
        config["name"] = "DE1 Espresso Count";
        config["state_topic"] = baseTopic + "/espresso_count";
        config["icon"] = "mdi:counter";
        config["state_class"] = "total_increasing";
        config["unique_id"] = QString("de1_%1_espresso_count").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("sensor", "espresso_count", config);
    }

    // Profile filename sensor
    {
        QJsonObject config;
        config["name"] = "DE1 Profile Filename";
        config["state_topic"] = baseTopic + "/profile_filename";
        config["icon"] = "mdi:file-document";
        config["unique_id"] = QString("de1_%1_profile_filename").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("sensor", "profile_filename", config);
    }

    m_discoveryPublished = true;
    qDebug() << "MqttClient: Home Assistant discovery published";
}
