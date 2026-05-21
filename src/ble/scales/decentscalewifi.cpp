#include "decentscalewifi.h"
#include "scalelogging.h"

#include <QWebSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <QAbstractSocket>
#include <QHostAddress>
#include <algorithm>

#define WIFI_LOG(msg)  SCALE_LOG("DecentScaleWifi", msg)
#define WIFI_WARN(msg) SCALE_WARN("DecentScaleWifi", msg)

DecentScaleWifi::DecentScaleWifi(QObject* parent)
    : ScaleDevice(parent)
    , m_socket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    , m_recognitionTimer(new QTimer(this))
{
    m_recognitionTimer->setSingleShot(true);
    connect(m_recognitionTimer, &QTimer::timeout,
            this, &DecentScaleWifi::onRecognitionTimeout);

    connect(m_socket, &QWebSocket::connected,
            this, &DecentScaleWifi::onConnected);
    connect(m_socket, &QWebSocket::disconnected,
            this, &DecentScaleWifi::onDisconnected);
    connect(m_socket, &QWebSocket::textMessageReceived,
            this, &DecentScaleWifi::onTextMessageReceived);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::errorOccurred),
            this, &DecentScaleWifi::onError);
}

DecentScaleWifi::~DecentScaleWifi() {
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->close();
    }
}

void DecentScaleWifi::connectToDevice(const QBluetoothDeviceInfo& device) {
    // device is unused on the WiFi path (no BLE info exists). We rely on
    // connectToHost being called instead; if connectToDevice is the only
    // entry point taken, fall back to the default hostname.
    Q_UNUSED(device);
    connectToHost(m_hostname.isEmpty() ? QStringLiteral("hds.local") : m_hostname);
}

void DecentScaleWifi::connectToHost(const QString& hostname) {
    m_hostname = hostname;
    m_userInitiatedShutdown = false;
    m_reconnectAttempted = false;
    m_triedHostnameFallback = false;
    m_pendingHostnameFallback = false;

    // Try the cached IP first if we have one. The recognition-timer guard
    // catches the case where DHCP has reassigned the IP and we're connecting
    // to the wrong device.
    const QString cachedIp = m_ipResolver ? m_ipResolver(hostname) : QString();
    if (!cachedIp.isEmpty() && cachedIp != hostname) {
        WIFI_LOG(QString("Trying cached IP %1 for %2").arg(cachedIp, hostname));
        attemptTarget(cachedIp, /*isHostname=*/false);
    } else {
        attemptTarget(hostname, /*isHostname=*/true);
    }
}

void DecentScaleWifi::attemptTarget(const QString& target, bool isHostname) {
    m_currentTarget = target;
    m_currentTargetIsHostname = isHostname;
    m_recognized = false;
    if (isHostname) m_triedHostnameFallback = true;

    const QUrl url(QStringLiteral("ws://%1/snapshot").arg(target));
    WIFI_LOG(QString("Connecting to %1 (%2)").arg(
        url.toString(), isHostname ? QStringLiteral("hostname") : QStringLiteral("cached IP")));
    m_socket->open(url);
    m_recognitionTimer->start(kRecognitionTimeoutMs);
}

void DecentScaleWifi::disconnectFromScale() {
    // User-initiated close. Mark so onDisconnected does NOT schedule reconnect.
    m_userInitiatedShutdown = true;
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->close();
    }
}

void DecentScaleWifi::onConnected() {
    WIFI_LOG("WebSocket connected");
    setConnected(true);

    // Bump to the highest cadence the firmware supports, opt in to events,
    // and seed an initial status frame so battery/firmware_version populate
    // immediately rather than waiting on the 5 s heartbeat.
    send(QStringLiteral("rate 10k"));
    send(QStringLiteral("events on"));
    send(QStringLiteral("status"));
}

void DecentScaleWifi::onDisconnected() {
    if (m_recognitionTimer) m_recognitionTimer->stop();
    const QString disconnectLog = m_userInitiatedShutdown
        ? QString("WebSocket disconnected (expected: %1)").arg(m_lastPowerEventReason)
        : QStringLiteral("WebSocket disconnected (unexpected)");
    WIFI_LOG(disconnectLog);

    // Pending hostname fallback (cached IP didn't validate): the recognition
    // timer marked this disconnect as the one we were waiting for. Run the
    // fallback inline now that the socket has fully closed — event-driven,
    // no zero-delay timer needed.
    if (m_pendingHostnameFallback) {
        m_pendingHostnameFallback = false;
        m_userInitiatedShutdown = false;
        // Don't propagate the brief intermediate disconnect to consumers —
        // the connection cycle is still in progress. setConnected(false) is
        // skipped here; it will fire only if the hostname attempt also fails.
        attemptTarget(m_hostname, /*isHostname=*/true);
        return;
    }

    setConnected(false);

    // Clear per-connect state so the next connect re-captures it fresh.
    m_firmwareVersion.clear();
    m_lastPowerEventReason.clear();
    m_lastPowerEventCode = -1;
    m_userInitiatedShutdown = false;

    // Reconnect is owned by main.cpp's scaleReconnectTimer (it already runs
    // an exponential-backoff loop against settings.scaleAddress() and will
    // re-fire BLEManager::tryDirectConnectToScale, which routes back through
    // this driver). Keeping a second reconnect path inside the driver would
    // race with that timer and require a debounce-style delay — neither is
    // appropriate per the project's "no timer-as-guard" rule.
}

void DecentScaleWifi::onTextMessageReceived(const QString& message) {
    const QByteArray bytes = message.toUtf8();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        // Silently drop malformed frames at debug level only.
        qDebug() << "[DecentScaleWifi] dropping malformed frame:" << err.errorString();
        return;
    }
    const QJsonObject obj = doc.object();

    // Contract per protocol: absence of "type" means weight snapshot.
    if (obj.contains(QStringLiteral("grams"))) {
        handleSnapshotFrame(obj);
        return;
    }

    const QString type = obj.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("status")) {
        handleStatusFrame(obj);
    } else if (type == QStringLiteral("button")) {
        handleButtonFrame(obj);
    } else if (type == QStringLiteral("power")) {
        handlePowerFrame(obj);
    } else if (type == QStringLiteral("rate")) {
        handleRateFrame(obj);
    }
    // Unknown types are silently ignored (forward-compat).
}

void DecentScaleWifi::handleSnapshotFrame(const QJsonObject& obj) {
    const QJsonValue grams = obj.value(QStringLiteral("grams"));
    if (!grams.isDouble()) return;
    onRecognizedAsHds();
    setWeight(grams.toDouble());
}

void DecentScaleWifi::handleStatusFrame(const QJsonObject& obj) {
    onRecognizedAsHds();

    // Battery + charging — independent fields per protocol.
    const QJsonValue batt = obj.value(QStringLiteral("battery_percent"));
    if (batt.isDouble()) {
        const int pct = std::clamp(batt.toInt(), 0, 100);
        setBatteryLevel(pct);
    }
    const QJsonValue charging = obj.value(QStringLiteral("charging"));
    if (charging.isBool()) {
        setCharging(charging.toBool());
    }

    // Firmware version — log once per connect, warn-log on mid-connect change.
    const QJsonValue fwv = obj.value(QStringLiteral("firmware_version"));
    if (fwv.isString()) {
        const QString version = fwv.toString();
        if (!version.isEmpty() && m_firmwareVersion != version) {
            if (m_firmwareVersion.isEmpty()) {
                WIFI_LOG(QString("Firmware version: %1").arg(version));
            } else {
                WIFI_WARN(QString("Firmware version changed mid-connect: %1 -> %2")
                          .arg(m_firmwareVersion, version));
            }
            m_firmwareVersion = version;
        }
    }

    // Protocol version — log on first capture for diagnostics only.
    const QJsonValue pv = obj.value(QStringLiteral("protocol_version"));
    if (pv.isDouble()) {
        static thread_local int s_lastLoggedProtoVersion = -1;
        const int v = pv.toInt();
        if (v != s_lastLoggedProtoVersion) {
            s_lastLoggedProtoVersion = v;
            WIFI_LOG(QString("Protocol version: %1").arg(v));
        }
    }
}

void DecentScaleWifi::handleButtonFrame(const QJsonObject& obj) {
    const int buttonNumber = obj.value(QStringLiteral("button_number")).toInt(0);
    const int pressCode    = obj.value(QStringLiteral("press_code")).toInt(0);
    if (buttonNumber == 0 || pressCode == 0) return;
    emit buttonPressed(encodeButton(buttonNumber, pressCode));
}

void DecentScaleWifi::handlePowerFrame(const QJsonObject& obj) {
    const QString event   = obj.value(QStringLiteral("event")).toString();
    const QString reason  = obj.value(QStringLiteral("reason")).toString();
    const int reasonCode  = obj.value(QStringLiteral("reason_code")).toInt(-1);
    if (event != QStringLiteral("power_off")) return;

    m_lastPowerEventReason = reason;
    m_lastPowerEventCode = reasonCode;
    // Tag this disconnect as expected so onDisconnected suppresses the
    // 3 s reconnect attempt — the scale told us it's going down.
    m_userInitiatedShutdown = true;
    WIFI_WARN(QString("Scale shut down: %1 (code %2)").arg(reason).arg(reasonCode));
    emit errorOccurred(QStringLiteral("Scale shut down: %1").arg(reason));
}

void DecentScaleWifi::handleRateFrame(const QJsonObject& obj) {
    const int hz = obj.value(QStringLiteral("hz")).toInt(0);
    const int intervalMs = obj.value(QStringLiteral("interval_ms")).toInt(0);
    WIFI_LOG(QString("Rate acknowledged: %1 Hz (interval %2 ms)").arg(hz).arg(intervalMs));
}

void DecentScaleWifi::onRecognizedAsHds() {
    if (m_recognized) return;
    m_recognized = true;
    m_recognitionTimer->stop();

    // Cache the peer IP after a hostname connect succeeds, so the next
    // connect can skip the OS resolver entirely.
    if (m_currentTargetIsHostname && m_ipCacheUpdate) {
        const QString peerIp = m_socket ? m_socket->peerAddress().toString() : QString();
        if (!peerIp.isEmpty() && peerIp != m_hostname) {
            WIFI_LOG(QString("Caching peer IP %1 for %2").arg(peerIp, m_hostname));
            m_ipCacheUpdate(m_hostname, peerIp);
        }
    }
}

void DecentScaleWifi::onRecognitionTimeout() {
    WIFI_WARN(QString("No recognizable HDS frame within %1 ms from %2")
              .arg(kRecognitionTimeoutMs).arg(m_currentTarget));

    // Cached-IP attempt failed validation → fall back to the hostname.
    // (If we were already on the hostname, we've exhausted options.)
    if (!m_currentTargetIsHostname && !m_triedHostnameFallback) {
        WIFI_LOG(QString("Cached IP %1 didn't validate as HDS — falling back to hostname %2")
                 .arg(m_currentTarget, m_hostname));
        // Hand the fallback off to onDisconnected via an event-driven flag:
        // when the socket-close completes, onDisconnected sees the flag and
        // runs attemptTarget(hostname) inline. No timer-as-guard.
        m_pendingHostnameFallback = true;
        m_userInitiatedShutdown = true;
        m_socket->close();
        return;
    }

    // Hostname attempt also failed recognition — give up.
    emit errorOccurred(QStringLiteral("WiFi scale did not respond as HDS"));
    m_userInitiatedShutdown = true;  // Suppress reconnect attempt.
    m_socket->close();
}

int DecentScaleWifi::encodeButton(int buttonNumber, int pressCode) {
    return kWifiButtonFlag | ((buttonNumber & 0xF) << 8) | (pressCode & 0xFF);
}

void DecentScaleWifi::send(const QString& text) {
    if (!m_socket) return;
    m_socket->sendTextMessage(text);
}

void DecentScaleWifi::tare()       { send(QStringLiteral("tare")); }
void DecentScaleWifi::startTimer() { send(QStringLiteral("timer start")); }
void DecentScaleWifi::stopTimer()  { send(QStringLiteral("timer stop")); }
void DecentScaleWifi::resetTimer() { send(QStringLiteral("timer reset")); }
void DecentScaleWifi::disableLcd() { send(QStringLiteral("display off")); }

void DecentScaleWifi::wake() {
    // Order: restore sensors/loop first, then OLED.
    send(QStringLiteral("soft_sleep off"));
    send(QStringLiteral("display on"));
}

void DecentScaleWifi::sleep() {
    send(QStringLiteral("soft_sleep on"));
    // BLE driver waits for characteristicWritten as the "command left the
    // radio" ack. The WS analog is sendTextMessage returning — already
    // happened above. Emit immediately to match BLE's intent.
    emit sleepCompleted();
}

void DecentScaleWifi::setLed(int r, int g, int b) {
    const int rc = std::clamp(r, 0, 255);
    const int gc = std::clamp(g, 0, 255);
    const int bc = std::clamp(b, 0, 255);
    send(QStringLiteral("led %1 %2 %3").arg(rc).arg(gc).arg(bc));
}

void DecentScaleWifi::sendKeepAlive() {
    // No-op. The 5 s status frame from the scale (after `events on`) plus
    // TCP-level keepalive cover liveness — an app-level ping would just
    // be noise on a healthy link. See design.md decision 11.
}

void DecentScaleWifi::onError() {
    const QString errStr = m_socket ? m_socket->errorString() : QStringLiteral("<no socket>");
    WIFI_WARN(QString("WebSocket error: %1").arg(errStr));

    // 503 detection — firmware refuses additional clients per webserver.h.
    // Qt's WebSocket error path may surface this in errorString().
    if (errStr.contains(QStringLiteral("503"))) {
        emit errorOccurred(QStringLiteral("Another client is connected to the scale"));
        // Suppress reconnect on 503; don't fight for the slot.
        m_userInitiatedShutdown = true;
    }
}
