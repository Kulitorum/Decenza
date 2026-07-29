#include "mqttclient.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../core/settings.h"
#include "../core/settings_brew.h"
#include "../core/settings_mqtt.h"
#include "../controllers/maincontroller.h"
#include "version.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHostInfo>
#include <QNetworkInformation>
#include <QUuid>
#include <QMutexLocker>
#ifdef Q_OS_ANDROID
#include <QThread>
#include <QPointer>
#include "mdnsresolver.h"
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

    // The FAST reconnect budget is for "the broker is not answering us", not for "this
    // device has no network" — a connect attempt against a down interface tells us
    // nothing about the broker, so spending the budget on an outage just burns through
    // the responsive phase while nothing could have worked anyway.
    //
    // Observed on-device 2026-07-25: a Wi-Fi drop consumed the budget down to the last
    // attempt, which happened to land seconds after the network returned. At the time
    // that budget was TERMINAL, so a slightly longer outage would have left Home
    // Assistant dark until someone noticed and intervened. It is no longer terminal:
    // the connect-failure paths route through scheduleReconnect(), which re-arms at the
    // slow cadence rather than stopping. (Not "every path" — connectToBroker()'s two
    // configuration guards deliberately do not, and scheduleReconnect() itself does not
    // re-arm while MQTT is disabled; both are documented at those sites.) Exhausting the
    // fast budget still means a 15-minute hole, which reachability avoids entirely.
    //
    // (The ~7 min figure is the sum of the backoff delays. Wall-clock is longer against
    // a blackholed host: Paho's connectTimeout defaults to 30 s and is not overridden,
    // so ten attempts can take ~12 min. It is ~7 min only when the broker actively
    // refuses.)
    //
    // So: watch reachability, don't spend attempts while it is positively down, and
    // treat the return of the network as a fresh budget. Only "Disconnected" counts
    // as down — Unknown is what a backend reports when it cannot tell (macOS does
    // exactly this at startup), and treating it as offline would stall a client that
    // can in fact reach its broker. Same rule as scheduleLanguageUpdateCheck().
    if (QNetworkInformation::loadDefaultBackend()) {
        if (auto* info = QNetworkInformation::instance()) {
            m_networkDown = (info->reachability() == QNetworkInformation::Reachability::Disconnected);
            connect(info, &QNetworkInformation::reachabilityChanged, this,
                    [this](QNetworkInformation::Reachability reachability) {
                        onNetworkReachabilityChanged(
                            reachability != QNetworkInformation::Reachability::Disconnected);
                    });
        } else {
            // Same reasoning as the else below, and it was missing here: a backend that
            // loads but yields no instance leaves the whole feature inert while the log
            // looks exactly like a healthy install.
            qInfo() << "MqttClient: QNetworkInformation backend loaded but no instance - "
                       "reconnect attempts will be spent while offline";
        }
    }
    else {
        // Not an error — but without it a field debug log cannot distinguish "the
        // network was fine" from "we had no way to tell", which matters when the
        // whole reachability feature is inert.
        qInfo() << "MqttClient: no QNetworkInformation backend - reconnect attempts "
                   "will be spent while offline (pre-existing behaviour)";
    }

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

// Decode an MQTT 3.1.1 CONNACK return code into something a user can act on.
// Returns a null QString for anything that is not a CONNACK code — in
// particular the negative MQTTASYNC_* transport errors, where Paho's own
// `message` ("TCP connect timeout", "TCP/TLS connect failure", …) is already
// self-describing and a bare number would only add noise.
static QString connackReasonText(int code)
{
    switch (code) {
    case 1: return QStringLiteral("broker rejected the MQTT protocol version");
    case 2: return QStringLiteral("broker rejected this client ID");
    case 3: return QStringLiteral("broker unavailable");
    case 4: return QStringLiteral("bad username or password");
    case 5: return QStringLiteral("not authorized");
    default: return QString();
    }
}

void MqttClient::onConnectFailure(void* context, MQTTAsync_failureData* response)
{
    MqttClient* self = static_cast<MqttClient*>(context);
    // Paho's `message` for a broker that answered but REJECTED the session is
    // the constant string "CONNACK return code" — it carries no information at
    // all. The reason lives in `code`, which Paho sets unconditionally
    // (MQTTAsyncUtils.c nextOrClose): either an MQTT CONNACK return code, or a
    // negative MQTTASYNC_* error for a failure before CONNACK. Dropping it made
    // every rejection read identically in the log AND in the status text the
    // MQTT settings tab shows the user, so "wrong password" was indistinguish-
    // able from "broker doesn't want this client id".
    //
    // Decode rather than print the raw number: the status string reaches the
    // user verbatim (see setStatus below and SettingsHomeAutomationTab.qml), and
    // "(code 4)" is no more actionable to them than no code at all. The raw
    // number is still appended when it is NOT a known CONNACK value, so an
    // unexpected code is never swallowed. Note the common "broker unreachable"
    // failures arrive as MQTTASYNC_FAILURE (-1) with a descriptive message, so
    // they deliberately get no suffix.
    //
    // v3 only, by construction: connect() registers onFailure (not onFailure5)
    // and sets no MQTTVersion, so MQTTVERSION_DEFAULT applies and the codes
    // below are the 3.1.1 set. MQTTAsync_failureData has no reasonCode field —
    // that is failureData5 — so nothing is lost by not reading one.
    QString error = QStringLiteral("Connection failed");
    if (response) {
        if (response->message)
            error = QString::fromUtf8(response->message);
        const QString reason = connackReasonText(response->code);
        if (!reason.isEmpty())
            error = reason;
        else
            error += QStringLiteral(" (code %1)").arg(response->code);
    }
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
    // Second, independent guard on the same latch. Setting the flag only on the
    // callback-producing branch of disconnectFromBroker() closes the reachable case,
    // but not this one: onSettingsChanged() disconnects and immediately reconnects, and
    // connectWithHost() calls MQTTAsync_destroy() on the client whose disconnect
    // callback is still in flight. Whether Paho still delivers onDisconnectSuccess
    // after a destroy is not something to depend on — and every mqtt* setter fires
    // onSettingsChanged() synchronously, so one settings save runs that race several
    // times. A user-requested disconnect only ever describes the connection it ended;
    // once we are dialling again it is meaningless, so clear it unconditionally here.
    m_userRequestedDisconnect = false;

    if (!m_settingsMqtt) {
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
            QString resolved = MdnsResolver::resolveHostname(host);
            QMetaObject::invokeMethod(guard.data(), [guard, resolved, host]() {
                if (!guard) return;
                // The resolve takes up to 2 s and conditions can change inside it —
                // newly relevant now that connectToBroker() also fires on a reachability
                // edge, one of several non-user callers. A flapping AP would otherwise
                // land here with the network down again, overwrite the accurate
                // "Waiting for network..." status, and dial a dead interface.
                if (guard->m_networkDown || !guard->m_settingsMqtt
                    || !guard->m_settingsMqtt->mqttEnabled()) {
                    qDebug() << "MqttClient: mDNS resolve finished but conditions changed"
                             << "(networkDown=" << guard->m_networkDown << ") - not connecting";
                    return;
                }
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
        m_slowRetryAnnounced = false;
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
        scheduleReconnect(QString("Failed to create client (%1)").arg(rc));
        return;
    }

    // Set callbacks
    rc = MQTTAsync_setCallbacks(m_client, this, onConnectionLost, onMessageArrived, nullptr);
    if (rc != MQTTASYNC_SUCCESS) {
        MQTTAsync_destroy(&m_client);
        m_client = nullptr;
        scheduleReconnect(QString("Failed to set callbacks (%1)").arg(rc));
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
        MQTTAsync_destroy(&m_client);
        m_client = nullptr;
        // Synchronous refusal — Paho validated the request and rejected it without ever
        // dialing, so NO onConnectFailure callback will follow (verified in MQTTAsync.c:
        // every synchronous error exit is a bare validation `goto exit`, and
        // m->connect.onFailure is assigned only after all of them). Before this, the
        // synchronous exits returned with no timer armed and no callback pending, which
        // is the terminal death the slow-retry change exists to end — MQTT stayed dead
        // until the app restarted, with only a status string to show for it.
        //
        // No worked example here on purpose: an earlier version claimed a `tcp://tcp://`
        // host typo produced MQTTASYNC_BAD_PROTOCOL on this line. Both halves were wrong
        // — that code comes from the create call above, and the URI is always built as
        // "tcp://%1:%2" so the scheme prefix check cannot fail. The structural point
        // stands regardless of which rc gets us here.
        scheduleReconnect(QString("Connect failed (%1)").arg(rc));
    }
}

void MqttClient::disconnectFromBroker()
{
    m_reconnectTimer.stop();
    m_publishTimer.stop();
    m_reconnectAttempts = 0;
    m_slowRetryAnnounced = false;
    emit reconnectAttemptsChanged();

    if (m_client && m_connected) {
        // Set ONLY here. The flag is consumed by onInternalDisconnected(), and this is
        // the one branch that causes that callback to run — so arming it is safe only
        // on this path. Setting it unconditionally (as this did) latched it forever
        // whenever the user disconnected while ALREADY disconnected: the else branch
        // below returns without any callback, nothing clears the flag, and the next
        // GENUINE broker drop is then misread as user-requested, stops the timer and
        // arms nothing. That is precisely the terminal reconnect death this change set
        // exists to remove, reintroduced by the flag added to remove it.
        //
        // Reachable from two unguarded callers: the Home Automation tab's Disconnect
        // button (SettingsHomeAutomationTab.qml) and ShotServer::handleMqttDisconnect —
        // neither checks isConnected() first, and tapping Disconnect while the status
        // reads "reconnecting (3/10)…" is the obvious thing a user does.
        m_userRequestedDisconnect = true;

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
    m_slowRetryAnnounced = false;
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

    // A disconnect the USER asked for is not a fault to recover from. Without this,
    // tapping Disconnect (or calling the mqtt_disconnect MCP tool, or the web
    // endpoint) immediately re-armed the timer and dialled back 5 s later — the
    // status would read "reconnecting (1/10)..." while the tool that just returned
    // {"success": true, "message": "MQTT disconnected"} was already being undone.
    // Bounded to 10 attempts before; unbounded once retries stopped being terminal.
    if (m_userRequestedDisconnect) {
        m_userRequestedDisconnect = false;
        m_reconnectTimer.stop();
        m_status = "Disconnected";
        emit statusChanged();
        return;
    }

    // Otherwise keep trying while MQTT is enabled — only the cadence changes.
    // Routed through the shared helper so this path announces the fast→slow
    // transition too: a broker that accepts TCP then drops the session (a stale
    // ACL, or a duplicate client-id from a second install) only ever reaches here,
    // never onInternalConnectionFailed, and used to loop every 15 minutes forever
    // with one qDebug line per cycle and no warning at all.
    if (m_settingsMqtt && m_settingsMqtt->mqttEnabled()) {
        scheduleReconnect(QStringLiteral("broker closed the connection"));
        return;
    }

    m_status = "Disconnected";
    emit statusChanged();
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

    scheduleReconnect(error);
}

// Single arming point for every failure that should be RETRIED, and the only place the
// three synchronous exits in connectWithHost() report at all — they used to set their own
// "Error: …" status and this consolidation removed it, so anything that returns from here
// without writing a status leaves the tab reading "Connecting..." forever.
//
// Not universal, deliberately: connectToBroker()'s two configuration guards (null settings,
// empty host) end a connect attempt without coming through here. Both set an accurate,
// actionable status and are recovered by onSettingsChanged() when the user fixes the
// setting, so arming a retry against an unfixable config would only produce log spam.
void MqttClient::scheduleReconnect(const QString& reason)
{
    if (!m_settingsMqtt || !m_settingsMqtt->mqttEnabled()) {
        // Do NOT retry — but do not swallow the failure either. A connect can be
        // initiated while MQTT is disabled: the Home Automation tab's Connect button
        // gates only on host-non-empty, and neither the mqtt_connect MCP tool nor the
        // ShotServer endpoint checks mqttEnabled(). Returning silently here left the
        // status latched at "Connecting...", discarded the Paho rc, and turned a precise
        // BAD_PROTOCOL (the tcp://tcp:// typo) into the ShotServer poller's generic
        // "Connection timed out" — pointing the user at their network instead of the typo.
        qWarning() << "MqttClient: connect attempt failed while MQTT is disabled -"
                   << "not retrying. Reason:" << reason;
        m_status = "Error: " + reason;
        emit statusChanged();
        return;
    }

    const int delay = reconnectDelayMs();
    if (m_reconnectAttempts >= MAX_FAST_RECONNECT_ATTEMPTS && !m_slowRetryAnnounced) {
        // Announce the transition ONCE. Past this point the fast budget is spent
        // against a network that is up, so this is the broker refusing or absent —
        // a bad credential, a broker that moved, a container still restarting. Worth
        // one warning naming the cause; not worth one every 15 minutes thereafter.
        m_slowRetryAnnounced = true;
        qWarning() << "MqttClient: broker unreachable after" << m_reconnectAttempts
                   << "attempts - backing off to one retry every" << delay / 60000
                   << "min. Reason:" << reason;
    }
    qDebug() << "MqttClient: Retrying in" << delay / 1000 << "seconds -" << reason;
    m_reconnectTimer.start(delay);

    // Keep the broker's own words. The caller has usually just set an "Error: …"
    // status, and both assignments land in one event-loop turn, so a bare
    // reconnectStatusText() would erase "bad username or password" (the decoded
    // CONNACK 4 text from connackReasonText()) before it could
    // ever be painted — and the status line (Home Automation tab, and the ShotServer
    // settings page which mirrors it) is the main place
    // that reason reaches the user. With retries no longer stopping, there would be
    // no later moment when anything more specific appeared: the tab would read
    // "reconnecting (1/10)..." forever while the real problem sat in a log nobody opens.
    m_status = reconnectStatusText() + " (" + reason + ")";
    emit statusChanged();
}

QString MqttClient::reconnectStatusText() const
{
    if (m_reconnectAttempts >= MAX_FAST_RECONNECT_ATTEMPTS) {
        return QString("Disconnected - retrying every %1 min")
            .arg(IDLE_RECONNECT_DELAY_MS / 60000);
    }
    return QString("Disconnected - reconnecting (%1/%2)...")
        .arg(m_reconnectAttempts + 1)
        .arg(MAX_FAST_RECONNECT_ATTEMPTS);
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

    // No network: the attempt would fail on the interface, not at the broker, so
    // don't charge it to the budget. Deliberately does NOT re-arm the timer —
    // onNetworkReachabilityChanged() resumes on the event, rather than polling a
    // condition we are already told about.
    if (m_networkDown) {
        qDebug() << "MqttClient: reconnect deferred - no network (attempt"
                 << (m_reconnectAttempts + 1) << "not spent)";
        m_status = kWaitingForNetwork;
        emit statusChanged();
        return;
    }

    m_reconnectAttempts++;
    emit reconnectAttemptsChanged();

    if (m_reconnectAttempts > MAX_FAST_RECONNECT_ATTEMPTS) {
        qDebug() << "MqttClient: Reconnection attempt" << m_reconnectAttempts << "(slow retry)";
    } else {
        qDebug() << "MqttClient: Reconnection attempt" << m_reconnectAttempts
                 << "of" << MAX_FAST_RECONNECT_ATTEMPTS << "before backing off";
    }

    // Flag preserved until connectWithHost() checks it (may be async on Android)
    m_isReconnecting = true;
    connectToBroker();
}

void MqttClient::onNetworkReachabilityChanged(bool reachable)
{
    const bool nowDown = !reachable;
    if (m_networkDown == nowDown)
        return;
    m_networkDown = nowDown;

    if (m_networkDown) {
        qDebug() << "MqttClient: network down - reconnect attempts suspended";
        // Cancel a pending tick rather than let it fire and log a deferral. The
        // flag is cleared by the reachable event below, not by waiting this out.
        m_reconnectTimer.stop();
        // Say so even while still nominally connected. Paho will not notice for up to
        // a keepalive (60 s), and in the meantime publish() silently drops every
        // update the user's Home Assistant automations depend on, behind a green dot
        // reading "Connected". The app knows at t=0; there is no reason to hide it.
        m_status = isConnected() ? kConnectedNetworkUnreachable : kWaitingForNetwork;
        emit statusChanged();
        return;
    }

    qDebug() << "MqttClient: network back - resuming reconnect";

    // Clear BOTH statuses the down-edge can write, BEFORE any early return. They describe
    // a condition that has just ended, so leaving either in place strands the Home
    // Automation tab describing a network problem that is over.
    //
    // Covering only kWaitingForNetwork — as this did — missed the connected case
    // entirely, and that one is the worse of the two: a brief reachability blip the TCP
    // session survives (well inside the 60 s keepalive) leaves "Connected - network
    // unreachable" on screen INDEFINITELY, because the isConnected() early return below
    // means nothing else ever rewrites it while the session holds. Publishing works fine
    // the whole time. This is the same fixed-for-one-string-and-not-the-others mistake
    // called out in onSettingsChanged(), made in the very comment warning about it.
    if (m_status == QLatin1String(kWaitingForNetwork)) {
        m_status = "Disconnected";
        emit statusChanged();
    } else if (m_status == QLatin1String(kConnectedNetworkUnreachable)) {
        m_status = isConnected() ? "Connected" : "Disconnected";
        emit statusChanged();
    }

    if (!m_settingsMqtt || !m_settingsMqtt->mqttEnabled() || isConnected())
        return;

    // A network we just regained is a different proposition from the one we lost:
    // give it a full FAST budget rather than leaving it on the 15-minute cadence,
    // and reconnect now instead of waiting out the current slow interval.
    //
    // Several sites zero m_reconnectAttempts — success, user/settings action, a fresh
    // user-initiated connect (connectWithHost only when !m_isReconnecting, which is
    // precisely what stops the reconnect tick from refunding its own budget). What is
    // specific to THIS one, and the reason it is worth a comment: it refunds a LIVE
    // budget and reconnects on its own, with nobody asking. (No count here on purpose —
    // an earlier version said "four sites" and was wrong within its own commit.)
    m_reconnectAttempts = 0;
    m_slowRetryAnnounced = false;
    emit reconnectAttemptsChanged();
    m_reconnectTimer.stop();
    m_isReconnecting = false;
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
        return;
    }

    // MQTT switched off. Nothing below runs otherwise, and the retry timer would
    // survive: onReconnectTimerTick() early-returns on !mqttEnabled() WITHOUT
    // touching the status, so whatever string was showing — "retrying every 15 min",
    // or kWaitingForNetwork — stayed on the Home Automation tab forever, describing
    // a loop that is no longer running. Same permanently-latched status this branch
    // was already fixed for once; it was fixed for one string and not the others.
    m_reconnectTimer.stop();
    m_reconnectAttempts = 0;
    m_slowRetryAnnounced = false;
    emit reconnectAttemptsChanged();
    m_status = "Disabled";
    emit statusChanged();
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
    } else if (m_settings->brew()->steamDisabled()) {
        steamMode = "Off";
    } else if (phase == "Ready" || phase == "Steaming") {
        steamMode = "On";
    } else if (!m_settings->brew()->keepSteamHeaterOn()) {
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
    if (!m_settingsMqtt) return;

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
