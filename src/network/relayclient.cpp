#include "relayclient.h"
#include "network/screencaptureservice.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../core/settings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QQuickWindow>

static constexpr int kReconnectBaseMs = 5000;
static constexpr int kReconnectMaxMs = 60000;
static constexpr int kPingIntervalMs = 5 * 60 * 1000;  // 5 minutes
static constexpr int kStatusPushIntervalMs = 5000;      // 5 seconds
static const QString kRelayUrl = QStringLiteral("wss://ws.decenza.coffee");

RelayClient::RelayClient(DE1Device* device, MachineState* machineState,
                         Settings* settings, QObject* parent)
    : QObject(parent)
    , m_device(device)
    , m_machineState(machineState)
    , m_settings(settings)
{
    connect(&m_socket, &QWebSocket::connected, this, &RelayClient::onConnected);
    connect(&m_socket, &QWebSocket::disconnected, this, &RelayClient::onDisconnected);
    connect(&m_socket, &QWebSocket::textMessageReceived, this, &RelayClient::onTextMessageReceived);
    connect(&m_socket, &QWebSocket::binaryMessageReceived, this, &RelayClient::onBinaryMessageReceived);

    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &RelayClient::onReconnectTimer);

    connect(&m_pingTimer, &QTimer::timeout, this, &RelayClient::onPingTimer);

    connect(&m_statusPushTimer, &QTimer::timeout, this, &RelayClient::pushStatus);

    // Trigger immediate status push on state changes
    if (m_device) {
        connect(m_device, &DE1Device::stateChanged, this, &RelayClient::onStateChanged);
    }
    if (m_machineState) {
        connect(m_machineState, &MachineState::phaseChanged, this, &RelayClient::onStateChanged);
    }
}

RelayClient::~RelayClient() = default;

bool RelayClient::isConnected() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

void RelayClient::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;

    m_enabled = enabled;
    emit enabledChanged();

    if (m_enabled) {
        QString token = m_settings ? m_settings->pocketPairingToken() : QString();
        if (!token.isEmpty()) {
            connectToRelay();
        } else {
            qDebug() << "RelayClient: Enabled but no pairing token set";
        }
    } else {
        m_reconnectTimer.stop();
        m_pingTimer.stop();
        m_statusPushTimer.stop();
        m_reconnectAttempts = 0;
        if (m_socket.state() != QAbstractSocket::UnconnectedState) {
            m_socket.close();
        }
    }
}

void RelayClient::connectToRelay()
{
    if (!m_enabled || !m_settings)
        return;

    QString deviceId = m_settings->deviceId();
    if (deviceId.isEmpty()) {
        qWarning() << "RelayClient: No device ID available";
        return;
    }

    QString url = kRelayUrl + "?device_id=" + deviceId + "&role=device";
    qDebug() << "RelayClient: Connecting to" << url;
    m_socket.open(QUrl(url));
}

void RelayClient::onConnected()
{
    qDebug() << "RelayClient: WebSocket connected";
    m_reconnectAttempts = 0;

    // Send register message
    QJsonObject msg;
    msg["action"] = QStringLiteral("register");
    msg["device_id"] = m_settings ? m_settings->deviceId() : QString();
    msg["role"] = QStringLiteral("device");
    msg["pairing_token"] = m_settings ? m_settings->pocketPairingToken() : QString();
    m_socket.sendTextMessage(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));

    // Start ping timer
    m_pingTimer.start(kPingIntervalMs);

    // Start status push timer
    m_statusPushTimer.start(kStatusPushIntervalMs);

    // Push initial status
    m_lastStatusJson.clear();
    pushStatus();

    emit connectedChanged();
}

void RelayClient::onDisconnected()
{
    qDebug() << "RelayClient: WebSocket disconnected";
    m_pingTimer.stop();
    m_statusPushTimer.stop();
    m_captureService.reset();
    emit connectedChanged();

    if (m_enabled) {
        // Exponential backoff: 5s, 10s, 20s, 40s, max 60s
        int delayMs = qMin(kReconnectBaseMs * (1 << m_reconnectAttempts), kReconnectMaxMs);
        m_reconnectAttempts++;
        qDebug() << "RelayClient: Reconnecting in" << delayMs << "ms (attempt" << m_reconnectAttempts << ")";
        m_reconnectTimer.start(delayMs);
    }
}

void RelayClient::onTextMessageReceived(const QString& message)
{
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject())
        return;

    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();

    if (type == "relay_command") {
        QString commandId = obj["command_id"].toString();
        QString command = obj["command"].toString();
        handleCommand(commandId, command);
    } else if (type == "registered") {
        qDebug() << "RelayClient: Successfully registered with relay";
    } else if (type == "binary_relay") {
        // Decode base64 data and handle as binary
        QByteArray binaryData = QByteArray::fromBase64(obj["data"].toString().toLatin1());
        onBinaryMessageReceived(binaryData);
    } else {
        qDebug() << "RelayClient: Received message type:" << type;
    }
}

void RelayClient::onStateChanged()
{
    // Trigger an immediate status push on state change
    if (isConnected()) {
        pushStatus();
    }
}

void RelayClient::onReconnectTimer()
{
    if (m_enabled) {
        connectToRelay();
    }
}

void RelayClient::onPingTimer()
{
    if (isConnected()) {
        QJsonObject msg;
        msg["action"] = QStringLiteral("ping");
        m_socket.sendTextMessage(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    }
}

void RelayClient::handleCommand(const QString& commandId, const QString& command)
{
    qDebug() << "RelayClient: Handling command:" << command << "id:" << commandId;

    if (command == "wake") {
        if (m_device) m_device->wakeUp();
    } else if (command == "sleep") {
        if (m_device) m_device->goToSleep();
    } else if (command == "status") {
        // No action needed, status is pushed separately
    } else if (command == "start_remote") {
        if (m_window && !m_captureService && m_settings && m_settings->screenCaptureEnabled()) {
            double scale = 0.5;
            m_captureService = std::make_unique<ScreenCaptureService>(m_window, &m_socket, scale);
        } else if (m_settings && !m_settings->screenCaptureEnabled()) {
            qDebug() << "RelayClient: Screen capture denied — disabled in settings";
        }
    } else if (command == "stop_remote") {
        m_captureService.reset();
    } else {
        qDebug() << "RelayClient: Unknown command:" << command;
    }

    // Send response
    QJsonObject msg;
    msg["action"] = QStringLiteral("command_response");
    msg["command_id"] = commandId;
    msg["success"] = true;
    msg["data"] = buildStatusJson();
    m_socket.sendTextMessage(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

void RelayClient::pushStatus()
{
    if (!isConnected())
        return;

    QJsonObject status = buildStatusJson();
    QString statusJson = QString::fromUtf8(QJsonDocument(status).toJson(QJsonDocument::Compact));

    // Deduplicate: only send if changed
    if (statusJson == m_lastStatusJson)
        return;

    m_lastStatusJson = statusJson;

    QJsonObject msg;
    msg["action"] = QStringLiteral("status_push");
    // Flatten status fields into the message
    for (auto it = status.begin(); it != status.end(); ++it) {
        msg[it.key()] = it.value();
    }
    m_socket.sendTextMessage(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

QJsonObject RelayClient::buildStatusJson() const
{
    QJsonObject status;
    if (m_device) {
        status["connected"] = m_device->isConnected();
        status["state"] = m_device->stateString();
        status["temperature"] = m_device->temperature();
        status["goalTemperature"] = m_device->goalTemperature();
        status["waterLevelMl"] = m_device->waterLevelMl();
        bool isAwake = m_device->isConnected() &&
                      (m_device->state() != DE1::State::Sleep &&
                       m_device->state() != DE1::State::GoingToSleep);
        status["isAwake"] = isAwake;
    }
    if (m_machineState) {
        status["phase"] = m_machineState->phaseString();
        status["isHeating"] = m_machineState->isHeating();
        status["isReady"] = m_machineState->isReady();
    }
    return status;
}

void RelayClient::setWindow(QQuickWindow* window) { m_window = window; }

void RelayClient::onBinaryMessageReceived(const QByteArray& data)
{
    if (data.isEmpty()) return;
    quint8 type = static_cast<quint8>(data[0]);
    if (type == 0x02 && m_captureService) {
        m_captureService->handleTouchEvent(data);
    }
}
