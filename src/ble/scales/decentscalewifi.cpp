#include "decentscalewifi.h"
#include "scalelogging.h"

#include <QWebSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <QAbstractSocket>
#include <QHostAddress>
#include <QThread>
#include <QPointer>
#include <algorithm>

#include "../../network/mdnsresolver.h"

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
    m_triedHostnameFallback = false;
    m_pendingHostnameFallback = false;
    m_socketErrorThisConnect = false;
    m_lastSocketErrorString.clear();
    // New connection cycle — invalidate any in-flight resolve from a prior call.
    ++m_resolveGeneration;

    // Try the cached IP first if we have one. The recognition-timer guard
    // catches the case where DHCP has reassigned the IP and we're connecting
    // to the wrong device.
    const QString cachedIp = m_ipResolver ? m_ipResolver(hostname) : QString();
    if (!cachedIp.isEmpty() && cachedIp != hostname) {
        WIFI_LOG(QString("Trying cached IP %1 for %2").arg(cachedIp, hostname));
        attemptTarget(cachedIp, /*isHostname=*/false);
    } else {
        attemptHostname();
    }
}

void DecentScaleWifi::attemptTarget(const QString& target, bool isHostname) {
    m_currentTarget = target;
    m_currentTargetIsHostname = isHostname;
    m_recognized = false;
    m_socketErrorThisConnect = false;
    m_lastSocketErrorString.clear();
    if (isHostname) m_triedHostnameFallback = true;

    const QUrl url(QStringLiteral("ws://%1/snapshot").arg(target));
    WIFI_LOG(QString("Connecting to %1 (%2)").arg(
        url.toString(), isHostname ? QStringLiteral("hostname") : QStringLiteral("cached IP")));
    m_socket->open(url);
    m_recognitionTimer->start(kRecognitionTimeoutMs);
}

void DecentScaleWifi::attemptHostname() {
    // We've moved past any cached-IP attempt to our best hostname-based
    // resolution — don't loop back to another raw-hostname fallback after this.
    m_triedHostnameFallback = true;

#ifdef Q_OS_ANDROID
    // Qt's resolver (getaddrinfo) doesn't speak mDNS on Android, so opening
    // ws://<name>.local fails with HostNotFoundError. Resolve the ".local"
    // name to an IP via a direct mDNS A-query — the same MdnsResolver path
    // MqttClient uses — on a worker thread (resolveHostname blocks), then dial
    // the IP. The generation guard drops a stale result if a newer connect or
    // disconnect superseded this attempt while the worker was in flight.
    if (m_hostname.endsWith(QStringLiteral(".local"), Qt::CaseInsensitive)) {
        const QString host = m_hostname;
        const int generation = ++m_resolveGeneration;
        QPointer<DecentScaleWifi> guard(this);
        QThread* thread = QThread::create([this, guard, host, generation]() {
            const QString ip = MdnsResolver::resolveHostname(host);
            // Back on the object's thread. `guard` gates liveness — the object
            // may have been destroyed during the blocking resolve. The `!guard`
            // check runs first (short-circuit), so member access via `this`
            // (incl. the WIFI_LOG macro's `emit logMessage`) only happens once
            // the object is confirmed alive.
            QMetaObject::invokeMethod(guard.data(), [this, guard, ip, host, generation]() {
                if (!guard || generation != m_resolveGeneration) return;
                if (!ip.isEmpty()) {
                    WIFI_LOG(QString("Resolved %1 to %2 via mDNS").arg(host, ip));
                    // Persist the peer IP so the next connect skips resolution.
                    // A stale answer self-heals: the cached-IP attempt fails the
                    // recognition window and falls back here to re-resolve.
                    if (m_ipCacheUpdate) m_ipCacheUpdate(host, ip);
                    attemptTarget(ip, /*isHostname=*/false);
                } else {
                    // The mDNS A-query found no responder. On Android the OS
                    // resolver can't resolve ".local" either, so we don't dial
                    // ws://<host> (it would fail later with a misleading generic
                    // HostNotFound). This is a TRANSIENT connect failure — e.g. a
                    // power-cycled scale still booting/rejoining WiFi — so log it
                    // but do NOT pop a modal. The connect isn't abandoned —
                    // BLEManager's connection timer (onScaleConnectionTimeout) is
                    // the backstop: it retries, recovers a still-booting scale, and
                    // for a genuinely-gone scale emits the FlowScale-fallback notice
                    // that informs the user. (See #1253.)
                    WIFI_WARN(QString("mDNS resolution failed for %1 — no responder; "
                                      "not dialing (transient; auto-reconnect will retry)").arg(host));
                    m_userInitiatedShutdown = true;  // mark expected; reconnect owned by main.cpp
                }
            }, Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, thread, &QThread::deleteLater);
        thread->start();
        return;
    }
#endif

    // Non-Android (OS resolver speaks mDNS), or a non-".local" name: let Qt
    // resolve the hostname directly.
    attemptTarget(m_hostname, /*isHostname=*/true);
}

void DecentScaleWifi::disconnectFromScale() {
    // User-initiated close. Mark so onDisconnected logs it as expected.
    // (Reconnect is owned by main.cpp's scaleReconnectTimer — this flag
    // does not gate reconnect anymore; it just controls the log line.)
    m_userInitiatedShutdown = true;
    // Invalidate any in-flight mDNS resolve so its late callback can't reopen
    // the socket after the user has asked to disconnect.
    ++m_resolveGeneration;
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

    // Classify the disconnect for triage. A genuine transport error means the
    // link dropped under us, so it must read "(unexpected)" regardless of any
    // shutdown flag; power-off and deliberate closes read "(expected)". Do NOT
    // use closeCode() as the abnormality signal: Qt sets closeCode()/
    // closeReason() only when a real close frame is received, and the reused
    // socket keeps a stale value across reconnects, so on an abnormal drop
    // closeCode() is stale/default (1000), never 1006. onError records a
    // transport error ONLY for a genuine (not self-inflicted) failure, so the
    // transport-error branch always means an abnormal drop and the peer-close
    // branch is reached only when a clean close frame was received (where
    // closeCode()/closeReason() ARE meaningful).
    QString disconnectLog;
    if (!m_lastPowerEventReason.isEmpty()) {
        disconnectLog = QStringLiteral("WebSocket disconnected (expected) — scale power-off: ")
                        + m_lastPowerEventReason;
    } else if (m_socketErrorThisConnect) {
        disconnectLog = QStringLiteral("WebSocket disconnected (unexpected) — transport error: ")
                        + m_lastSocketErrorString;
    } else if (m_userInitiatedShutdown) {
        disconnectLog = QStringLiteral("WebSocket disconnected (expected)");
    } else {
        const int closeCode = m_socket ? static_cast<int>(m_socket->closeCode()) : -1;
        const QString closeReason = m_socket ? m_socket->closeReason() : QString();
        disconnectLog = QString("WebSocket disconnected (unexpected) — peer close (code %1").arg(closeCode);
        if (!closeReason.isEmpty())
            disconnectLog += QString(", reason=\"%1\"").arg(closeReason);
        disconnectLog += QStringLiteral(")");
    }
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
        // Re-resolve via attemptHostname() (mDNS on Android) rather than dialing
        // the bare ".local" name, which Qt can't resolve there. This also picks
        // up a new IP if DHCP moved the scale since the cached entry.
        attemptHostname();
        return;
    }

    setConnected(false);

    // Clear per-connect state so the next connect re-captures it fresh.
    m_firmwareVersion.clear();
    m_loggedProtoVersion = -1;
    m_loggedFrameShapes.clear();
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

    // Frame routing (per the openscale WS README): a frame with NO "type" is a
    // weight snapshot; every typed frame (status/button/power/rate/error)
    // carries "type". A status frame ALSO carries a "grams" field, so snapshots
    // must be discriminated by the ABSENCE of "type" — keying on the presence
    // of "grams" (as we used to) swallowed every status frame as a snapshot,
    // which is why firmware_version, battery, and charging were never seen over
    // WiFi.
    const QString type = obj.value(QStringLiteral("type")).toString();
    if (type.isEmpty()) {
        handleSnapshotFrame(obj);
        return;
    }

    // Diagnostic: log one sample of each distinct typed frame per connect, so
    // the firmware's actual WS surface is visible (frame types and, for status,
    // its fields incl. firmware_version).
    if (!m_loggedFrameShapes.contains(type)) {
        m_loggedFrameShapes.insert(type);
        WIFI_LOG(QString("First '%1' frame this connect: %2")
                 .arg(type, QString::fromUtf8(bytes.left(200))));
    }

    if (type == QStringLiteral("status")) {
        handleStatusFrame(obj);
    } else if (type == QStringLiteral("button")) {
        handleButtonFrame(obj);
    } else if (type == QStringLiteral("power")) {
        handlePowerFrame(obj);
    } else if (type == QStringLiteral("rate")) {
        handleRateFrame(obj);
    }
    // "error" frames and any unknown type are captured by the diagnostic log
    // above; no further action.
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

    // Protocol version — log once per connect (reset on disconnect, like
    // m_firmwareVersion) for diagnostics only.
    const QJsonValue pv = obj.value(QStringLiteral("protocol_version"));
    if (pv.isDouble()) {
        const int v = pv.toInt();
        if (v != m_loggedProtoVersion) {
            m_loggedProtoVersion = v;
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

    // Reject malformed power_off frames that omit BOTH the reason string and
    // the numeric reason code — without either, we can't tell the user why
    // the scale is going down, and marking the disconnect as "expected" would
    // hide a potentially-real failure. Let the following unexpected disconnect
    // log surface so it's diagnosable.
    if (reason.isEmpty() && reasonCode == -1) {
        WIFI_WARN("Malformed power_off frame (no reason or reason_code) — "
                  "treating as unexpected disconnect");
        return;
    }

    m_lastPowerEventReason = reason;
    m_lastPowerEventCode = reasonCode;
    // Mark the imminent disconnect as expected so onDisconnected logs it
    // accordingly instead of as an unexpected drop. Reconnect itself is
    // owned by main.cpp's scaleReconnectTimer.
    m_userInitiatedShutdown = true;
    const QString reasonText = reason.isEmpty()
        ? QString("code %1").arg(reasonCode)
        : reason;
    WIFI_WARN(QString("Scale shut down: %1 (code %2)").arg(reasonText).arg(reasonCode));
    emit errorOccurred(translateUiString("wifi.scale.error.scaleShutdown",
        "Scale shut down: %1").arg(reasonText));
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
        // abort() (hard close), not close() (graceful), to avoid Qt warning
        // "QNativeSocketEngine::write() was not called in ConnectedState" —
        // the recognition window expired without the WS upgrade completing,
        // so any in-flight handshake writes Qt has queued internally would
        // try to flush against a half-open socket on the graceful path.
        m_socket->abort();
        return;
    }

    // Hostname attempt also failed recognition — give up THIS attempt. Transient
    // connect failure: log it but don't pop a modal. BLEManager's connection timer
    // (onScaleConnectionTimeout) is the backstop — it retries, and a genuinely-gone
    // scale is surfaced by the FlowScale-fallback notice. #1253
    WIFI_WARN(QStringLiteral("WiFi scale did not respond as HDS — giving up this attempt"));
    m_userInitiatedShutdown = true;  // mark expected; reconnect owned by main.cpp
    m_socket->abort();  // Same rationale as the fallback path above.
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
    // Firmware-level power off — the WS analog of BT's `0A 02 00`. The scale
    // must be physically woken via its button afterward; this matches the BT
    // transport's behavior exactly. `soft_sleep on` is the lighter reversible
    // state and is wrong here: leaving the ESP32 radio active while the DE1
    // sleeps drains a battery-only HDS.
    send(QStringLiteral("{\"command\":\"power\",\"action\":\"off\"}"));
    // BT waits for `characteristicWritten` as a "command left the radio" ack.
    // The WS analog is sendTextMessage returning, which already happened above.
    // Emit immediately to match BT's intent.
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
    const QAbstractSocket::SocketError err = m_socket
        ? m_socket->error() : QAbstractSocket::UnknownSocketError;
    const QString errStr = m_socket ? m_socket->errorString() : QStringLiteral("<no socket>");
    WIFI_WARN(QString("WebSocket error: %1 (code %2)").arg(errStr).arg(static_cast<int>(err)));

    // 503 detection — firmware refuses additional clients past its cap. Qt's
    // WebSocket error path surfaces this in errorString(). Treated as an expected
    // refusal (we surface a modal), so it must NOT be recorded as a transport
    // error — return before the transport-error capture below.
    if (errStr.contains(QStringLiteral("503"))) {
        emit errorOccurred(translateUiString("wifi.scale.error.serverBusy",
            "Another client is connected to the scale"));
        m_userInitiatedShutdown = true;
        return;
    }

    // Record a GENUINE transport error — one that dropped the link under us — so
    // onDisconnected can flag an abnormal drop (closeCode() can't; see note there).
    // Skip it when we already initiated a close: a deliberate abort()/close() can
    // also surface here, and that is not an external transport failure. Reset per
    // connect cycle/attempt (connectToHost/attemptTarget).
    if (!m_userInitiatedShutdown) {
        m_socketErrorThisConnect = true;
        m_lastSocketErrorString = errStr;
    }

    // Classify common socket-level failures. These are all TRANSIENT connect
    // failures (scale unreachable / refusing / still booting after a power-cycle):
    // we log them via WIFI_WARN above but do NOT pop a modal. The connect isn't
    // abandoned — BLEManager's per-connect connection timer (onScaleConnectionTimeout)
    // is the backstop: it retries, recovers a still-booting scale, and for a
    // genuinely-gone scale emits the FlowScale-fallback notice that informs the
    // user. The 503 "another client connected" case handled above is the one
    // socket error we DO surface here, because the retry loop can never resolve it.
    // #1253
    switch (err) {
    case QAbstractSocket::HostNotFoundError:
    case QAbstractSocket::ConnectionRefusedError:
    case QAbstractSocket::NetworkError:
    case QAbstractSocket::SocketTimeoutError:
        m_userInitiatedShutdown = true;
        break;
    default:
        // Other errors fall through — the recognition timeout will catch the
        // case where the WS upgrade hangs without a clean socket error.
        break;
    }
}

QString DecentScaleWifi::translateUiString(const QString& key, const QString& fallback) const {
    if (m_uiTranslator) {
        return m_uiTranslator(key, fallback);
    }
    return fallback;
}
