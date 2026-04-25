#include "mqttclient.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../core/settings.h"
#include "../core/settings_mqtt.h"
#include "../controllers/maincontroller.h"
#include "version.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHostInfo>
#include <QUuid>
#include <QMutexLocker>
#ifdef Q_OS_ANDROID
#include <QElapsedTimer>
#include <QThread>
#include <QPointer>
#define MDNS_IMPLEMENTATION
#include <mdns.h>
#include <QHostAddress>
#endif

MqttClient::MqttClient(DE1Device* device, MachineState* machineState,
                       Settings* settings, SettingsMqtt* settingsMqtt,
                       QObject* parent)
    : QObject(parent)
    , m_device(device)
    , m_machineState(machineState)
    , m_settings(settings)
    , m_settingsMqtt(settingsMqtt)
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
        connect(m_device, &DE1Device::subStateChanged, this, &MqttClient::onDE1StateChanged);
        connect(m_device, &DE1Device::connectedChanged, this, &MqttClient::onDE1ConnectedChanged);
    }

    // Settings changes
    if (m_settingsMqtt) {
        connect(m_settingsMqtt, &SettingsMqtt::mqttEnabledChanged, this, &MqttClient::onSettingsChanged);
        connect(m_settingsMqtt, &SettingsMqtt::mqttBrokerHostChanged, this, &MqttClient::onSettingsChanged);
        connect(m_settingsMqtt, &SettingsMqtt::mqttBrokerPortChanged, this, &MqttClient::onSettingsChanged);
        connect(m_settingsMqtt, &SettingsMqtt::mqttUsernameChanged, this, &MqttClient::onSettingsChanged);
        connect(m_settingsMqtt, &SettingsMqtt::mqttPasswordChanged, this, &MqttClient::onSettingsChanged);
        connect(m_settingsMqtt, &SettingsMqtt::mqttPublishIntervalChanged, this, [this]() {
            if (m_publishTimer.isActive()) {
                m_publishTimer.setInterval(m_settingsMqtt->mqttPublishInterval());
            }
        });
    }

    // Publish timer
    connect(&m_publishTimer, &QTimer::timeout, this, &MqttClient::onPublishTimerTick);

    // Reconnect timer
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &MqttClient::onReconnectTimerTick);

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
            msg.payloadlen = static_cast<int>(payload.length());
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
    QString clientId = m_settingsMqtt ? m_settingsMqtt->mqttClientId() : "";
    if (clientId.isEmpty()) {
        QString hostname = QHostInfo::localHostName();
        if (hostname.isEmpty()) {
            hostname = "decenza";
        }
        clientId = QString("decenza_%1_%2")
            .arg(hostname)
            .arg(QUuid::createUuid().toString(QUuid::Id128).left(8));
        // Persist the generated client ID so it survives app restarts/updates
        if (m_settingsMqtt) {
            m_settingsMqtt->setMqttClientId(clientId);
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

#ifdef Q_OS_ANDROID

struct MdnsResolveContext {
    QByteArray hostname;
    QString resolvedIp;
};

static int mdnsResolveCallback(int sock, const struct sockaddr* from, size_t addrlen,
                                mdns_entry_type_t entry, uint16_t query_id,
                                uint16_t rtype, uint16_t rclass, uint32_t ttl,
                                const void* data, size_t size,
                                size_t name_offset, size_t name_length,
                                size_t record_offset, size_t record_length,
                                void* user_data)
{
    Q_UNUSED(sock); Q_UNUSED(from); Q_UNUSED(addrlen);
    Q_UNUSED(query_id); Q_UNUSED(rclass); Q_UNUSED(ttl);
    Q_UNUSED(name_length);

    auto* ctx = static_cast<MdnsResolveContext*>(user_data);
    if (!ctx->resolvedIp.isEmpty())
        return 0; // Already found

    if (entry != MDNS_ENTRYTYPE_ANSWER || rtype != MDNS_RECORDTYPE_A)
        return 0;

    // Extract record name (handles DNS compression pointers)
    char namebuf[256];
    size_t nameOffset = name_offset;
    mdns_string_t name = mdns_string_extract(data, size, &nameOffset, namebuf, sizeof(namebuf));

    QByteArray recordName = QByteArray(name.str, static_cast<qsizetype>(name.length));
    if (recordName.endsWith('.'))
        recordName.chop(1);

    if (recordName.toLower() != ctx->hostname.toLower())
        return 0;

    // Parse the A record
    struct sockaddr_in addr;
    mdns_record_parse_a(data, size, record_offset, record_length, &addr);
    ctx->resolvedIp = QHostAddress(ntohl(addr.sin_addr.s_addr)).toString();

    return 0;
}

// Resolve a .local hostname via mDNS using mjansson/mdns library.
// Called on a background thread — safe to block.
static QString resolveMdns(const QString& hostname, int timeoutMs = 2000)
{
    // Open mDNS socket (binds to ephemeral port, joins multicast group)
    struct sockaddr_in bindAddr;
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = INADDR_ANY;
    int sock = mdns_socket_open_ipv4(&bindAddr);
    if (sock < 0)
        return {};

    // Send A record query
    char buffer[2048];
    QByteArray hostBytes = hostname.toUtf8();
    if (mdns_query_send(sock, MDNS_RECORDTYPE_A, hostBytes.constData(),
                        static_cast<size_t>(hostBytes.size()),
                        buffer, sizeof(buffer), 0) < 0) {
        mdns_socket_close(sock);
        return {};
    }

    MdnsResolveContext ctx;
    ctx.hostname = hostBytes;
    if (ctx.hostname.endsWith('.'))
        ctx.hostname.chop(1);

    // Poll for responses with deadline tracking
    QElapsedTimer deadline;
    deadline.start();

    while (deadline.elapsed() < timeoutMs && ctx.resolvedIp.isEmpty()) {
        int remaining = static_cast<int>(timeoutMs - deadline.elapsed());
        if (remaining <= 0) break;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        struct timeval tv;
        tv.tv_sec = remaining / 1000;
        tv.tv_usec = (remaining % 1000) * 1000;

        int ret = select(sock + 1, &readfds, nullptr, nullptr, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) break;

        mdns_query_recv(sock, buffer, sizeof(buffer),
                        mdnsResolveCallback, &ctx, 0);
    }

    mdns_socket_close(sock);
    return ctx.resolvedIp;
}

#endif // Q_OS_ANDROID

void MqttClient::connectToBroker()
{
    if (!m_settings) {
        m_status = "Error: No settings";
        emit statusChanged();
        return;
    }

    QString host = m_settingsMqtt->mqttBrokerHost().trimmed();
    if (host.isEmpty()) {
        m_status = "Error: No broker host configured";
        emit statusChanged();
        return;
    }

#ifdef Q_OS_ANDROID
    // Android's getaddrinfo() doesn't reliably resolve .local mDNS hostnames.
    // Resolve on a background thread to avoid blocking the UI.
    if (host.endsWith(".local", Qt::CaseInsensitive)) {
        m_status = "Resolving...";
        emit statusChanged();

        QPointer<MqttClient> guard(this);
        QThread* thread = QThread::create([guard, host]() {
            QString resolved = resolveMdns(host);
            QMetaObject::invokeMethod(guard.data(), [guard, resolved, host]() {
                if (!guard) return;
                if (!resolved.isEmpty()) {
                    qDebug() << "MqttClient: Resolved" << host << "to" << resolved << "via mDNS";
                    guard->connectWithHost(resolved);
                } else {
                    qWarning() << "MqttClient: mDNS resolution failed for" << host
                               << "- trying direct connection";
                    guard->connectWithHost(host);
                }
            }, Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, thread, &QThread::deleteLater);
        thread->start();
        return;
    }
#endif

    connectWithHost(host);
}

void MqttClient::connectWithHost(const QString& host)
{
    // Clean up old client if exists
    if (m_client) {
        if (m_connected) {
            MQTTAsync_disconnect(m_client, nullptr);
        }
        MQTTAsync_destroy(&m_client);
        m_client = nullptr;
    }

    if (!m_isReconnecting) {
        m_reconnectAttempts = 0;
        emit reconnectAttemptsChanged();
    }
    m_isReconnecting = false;

    // Build server URI
    int port = m_settingsMqtt ? m_settingsMqtt->mqttBrokerPort() : 1883;
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
    QString username = m_settingsMqtt->mqttUsername();
    QString password = m_settingsMqtt->mqttPassword();
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
    if (m_settingsMqtt && m_settingsMqtt->mqttHomeAssistantDiscovery()) {
        publishHomeAssistantDiscovery();
    }

    // Start publishing telemetry
    int interval = m_settingsMqtt ? m_settingsMqtt->mqttPublishInterval() : 1000;
    m_publishTimer.start(interval);

    // Clear last-published values so the full state is re-sent to the broker
    // (the broker may have lost retained messages during the disconnect)
    m_lastPublishedState.clear();
    m_lastPublishedPhase.clear();
    m_lastPublishedSubstate.clear();
    m_lastPublishedProfile.clear();
    m_lastPublishedSteamMode.clear();
    m_lastPublishedEspressoCount = -1;

    // Publish initial state
    publishState();
    onPublishTimerTick();
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

    // Attempt reconnection with exponential backoff if MQTT is still enabled
    if (m_settingsMqtt && m_settingsMqtt->mqttEnabled() && m_reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
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
    if (m_settingsMqtt && m_settingsMqtt->mqttEnabled() && m_reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
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

void MqttClient::onReconnectTimerTick()
{
    if (!m_settingsMqtt || !m_settingsMqtt->mqttEnabled()) {
        return;
    }

    m_reconnectAttempts++;
    emit reconnectAttemptsChanged();

    qDebug() << "MqttClient: Reconnection attempt" << m_reconnectAttempts << "of" << MAX_RECONNECT_ATTEMPTS;

    // Flag preserved until connectWithHost() checks it (may be async on Android)
    m_isReconnecting = true;
    connectToBroker();
}

void MqttClient::onSettingsChanged()
{
    // If settings changed significantly, reconnect
    if (isConnected()) {
        disconnectFromBroker();
    }

    if (m_settingsMqtt && m_settingsMqtt->mqttEnabled()) {
        connectToBroker();
    }
}

QString MqttClient::topicPath(const QString& subtopic) const
{
    QString baseTopic = m_settingsMqtt ? m_settingsMqtt->mqttBaseTopic() : "decenza";
    return baseTopic + "/" + subtopic;
}

void MqttClient::publish(const QString& topic, const QString& payload, bool retain)
{
    if (!isConnected() || !m_client) return;

    bool shouldRetain = retain && m_settingsMqtt && m_settingsMqtt->mqttRetainMessages();

    QByteArray topicBytes = topic.toUtf8();
    QByteArray payloadBytes = payload.toUtf8();

    MQTTAsync_message msg = MQTTAsync_message_initializer;
    msg.payload = payloadBytes.data();
    msg.payloadlen = static_cast<int>(payloadBytes.length());
    msg.qos = 0;
    msg.retained = shouldRetain ? 1 : 0;

    int rc = MQTTAsync_sendMessage(m_client, topicBytes.constData(), &msg, nullptr);
    if (rc != MQTTASYNC_SUCCESS) {
        qWarning() << "MqttClient: Failed to publish to" << topic << "- error" << rc;
    }
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
    if (!isConnected() || !m_device) return;

    QString state = m_device->stateString();
    QString substate = m_device->subStateString();
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

    if (substate != m_lastPublishedSubstate) {
        publish(topicPath("substate"), substate, true);
        m_lastPublishedSubstate = substate;
    }
}

void MqttClient::onPublishTimerTick()
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

    QString baseTopic = m_settingsMqtt->mqttBaseTopic();
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

    // Phase sensor
    {
        QJsonObject config;
        config["name"] = "DE1 Phase";
        config["state_topic"] = baseTopic + "/phase";
        config["icon"] = "mdi:coffee-maker-outline";
        config["unique_id"] = QString("de1_%1_phase").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("sensor", "phase", config);
    }

    // Substate sensor
    {
        QJsonObject config;
        config["name"] = "DE1 Substate";
        config["state_topic"] = baseTopic + "/substate";
        config["icon"] = "mdi:information-outline";
        config["unique_id"] = QString("de1_%1_substate").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("sensor", "substate", config);
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

    // Steam temperature sensor
    {
        QJsonObject config;
        config["name"] = "DE1 Steam Temperature";
        config["state_topic"] = baseTopic + "/temperature/steam";
        config["device_class"] = "temperature";
        config["unit_of_measurement"] = "\u00B0C";
        config["icon"] = "mdi:water-boiler";
        config["unique_id"] = QString("de1_%1_temp_steam").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("sensor", "temperature_steam", config);
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

    // Target weight sensor
    {
        QJsonObject config;
        config["name"] = "DE1 Target Weight";
        config["state_topic"] = baseTopic + "/target_weight";
        config["device_class"] = "weight";
        config["unit_of_measurement"] = "g";
        config["icon"] = "mdi:scale-balance";
        config["unique_id"] = QString("de1_%1_target_weight").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("sensor", "target_weight", config);
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

    // Water level (ml) sensor
    {
        QJsonObject config;
        config["name"] = "DE1 Water Level (ml)";
        config["state_topic"] = baseTopic + "/water_level_ml";
        config["unit_of_measurement"] = "ml";
        config["icon"] = "mdi:water";
        config["unique_id"] = QString("de1_%1_water_level_ml").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("sensor", "water_level_ml", config);
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
        config["state_on"] = "ON";
        config["state_off"] = "OFF";
        // Map all machine states to ON/OFF — only Sleep and GoingToSleep are "off"
        config["value_template"] = "{{ 'OFF' if value in ['Sleep', 'GoingToSleep'] else 'ON' }}";
        config["icon"] = "mdi:power";
        config["unique_id"] = QString("de1_%1_power").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("switch", "power", config);
    }

    // DE1 connected (binary sensor)
    {
        QJsonObject config;
        config["name"] = "DE1 Connected";
        config["state_topic"] = baseTopic + "/connected";
        config["payload_on"] = "true";
        config["payload_off"] = "false";
        config["device_class"] = "connectivity";
        config["icon"] = "mdi:bluetooth-connect";
        config["unique_id"] = QString("de1_%1_connected").arg(m_clientId);
        config["availability_topic"] = baseTopic + "/availability";
        config["device"] = device;
        publishDiscoveryConfig("binary_sensor", "connected", config);
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
    // total_increasing: HA treats value decreases (e.g. history cleared) as meter resets
    // and continues accumulating from the new value — no inflation.
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
