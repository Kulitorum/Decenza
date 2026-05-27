#include "decentscalewifi.h"
#include "scalelogging.h"

#include <QWebSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <QAbstractSocket>
#include <QTcpSocket>
#include <QHostAddress>
#include <QThread>
#include <QPointer>
#include <algorithm>

// Private headers — reach the QTcpSocket inside QWebSocket so we can set DSCP
// and TCP_NODELAY (see applyTcpQos). Project already uses Qt private headers
// elsewhere (QZipReader/QZipWriter) — same pattern, same risks (no API
// guarantees across Qt versions). On a Qt upgrade, re-verify:
//   1. `QWebSocketPrivate::m_pSocket` still exists and is still a `QTcpSocket*`
//      (used by AccessBypass below; a type/name change → compile error here).
//   2. `QWebSocketPrivate` still derives from `QObjectPrivate` so
//      `static_cast<QWebSocketPrivate*>(QObjectPrivate::get(...))` is well-formed.
#include <private/qobject_p.h>
#include <private/qwebsocket_p.h>

// Access-bypass for QWebSocketPrivate::m_pSocket. The member is declared
// `private` (not just inside a private header), so we cannot name it
// directly. [temp.spec]/2 lets explicit template instantiations name
// otherwise-inaccessible class members through a member-pointer template
// parameter, exposing them via a friend function. Standards-conforming;
// preferred over the more common `#define private public` hack (which is UB
// and varies by compiler).
namespace {
template <typename Tag, typename Tag::Type M>
struct AccessBypass {
    friend typename Tag::Type get(Tag) { return M; }
};
struct WsPrivateSocketTag {
    using Type = QTcpSocket* QWebSocketPrivate::*;
    friend Type get(WsPrivateSocketTag);
};
template struct AccessBypass<WsPrivateSocketTag, &QWebSocketPrivate::m_pSocket>;
} // namespace

#include "../../network/mdnsresolver.h"

#define WIFI_LOG(msg)  SCALE_LOG("DecentScaleWifi", msg)
#define WIFI_WARN(msg) SCALE_WARN("DecentScaleWifi", msg)

DecentScaleWifi::DecentScaleWifi(QObject* parent)
    : ScaleDevice(parent)
    , m_socket(nullptr)
    , m_recognitionTimer(new QTimer(this))
{
    m_recognitionTimer->setSingleShot(true);
    connect(m_recognitionTimer, &QTimer::timeout,
            this, &DecentScaleWifi::onRecognitionTimeout);

    // Initial socket — recreateSocket() will swap it on every connect attempt.
    // Called here rather than deferring to the first attemptTarget() because
    // several code paths assume m_socket is non-null without a guard
    // (e.g. onRecognitionTimeout calls m_socket->abort() directly). The
    // destructor's own check IS null-safe, so this isn't about destructor
    // safety — it's about the invariant "m_socket is always live" that the
    // signal handlers rely on.
    recreateSocket();
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
    // Reset the app-initiated-power-off latch — if a prior cycle armed it via
    // sleep() but the firmware echo never arrived (e.g. socket dropped first),
    // the next connection's first real power_off frame (low battery, button)
    // must surface, not be silently suppressed.
    m_powerOffInitiatedByApp = false;
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

    // Fresh QWebSocket per attempt — see recreateSocket()'s docstring for
    // the staleness bug this works around. Cheap (microseconds) and
    // architecturally correct.
    recreateSocket();

    const QUrl url(QStringLiteral("ws://%1/snapshot").arg(target));
    WIFI_LOG(QString("Connecting to %1 (%2)").arg(
        url.toString(), isHostname ? QStringLiteral("hostname") : QStringLiteral("cached IP")));
    m_socket->open(url);
    m_recognitionTimer->start(kRecognitionTimeoutMs);
}

void DecentScaleWifi::recreateSocket() {
    // Stop the recognition timer first: if the old socket was mid-attempt,
    // the timer's onRecognitionTimeout slot would otherwise fire against
    // the new socket (the slot calls m_socket->abort() which would kill the
    // fresh attempt).
    m_recognitionTimer->stop();

    if (m_socket) {
        // Detach the old socket's signals from this object. These connections
        // are Qt::AutoConnection → DirectConnection (same thread), so they
        // fire synchronously, not through the signal queue. The risk we're
        // guarding against: the underlying QSocketNotifier / QAbstractSocket
        // may already have a state-change event posted to the event loop
        // from before we call abort() below. When that event runs, the
        // dying socket re-emits disconnected/errorOccurred, and without
        // this disconnect those would invoke onDisconnected/onError against
        // `this` — which by then references the NEW m_socket, and the stale
        // event would corrupt the new attempt's state machine.
        m_socket->disconnect(this);
        // Hard close (abort) before scheduling deletion — close() is graceful
        // and would queue a close frame that might race the delete. abort()
        // tears the TCP down immediately. deleteLater defers destruction past
        // the current event loop tick, which is safe even while signals from
        // the old socket are unwinding.
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            m_socket->abort();
        }
        m_socket->deleteLater();
    }

    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);

    connect(m_socket, &QWebSocket::connected,
            this, &DecentScaleWifi::onConnected);
    connect(m_socket, &QWebSocket::disconnected,
            this, &DecentScaleWifi::onDisconnected);
    connect(m_socket, &QWebSocket::textMessageReceived,
            this, &DecentScaleWifi::onTextMessageReceived);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::errorOccurred),
            this, &DecentScaleWifi::onError);
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
        WIFI_LOG(QString("Resolving %1 via mDNS (Android)...").arg(host));
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
    const QString peerIp   = m_socket ? m_socket->peerAddress().toString() : QString();
    const quint16 peerPort = m_socket ? m_socket->peerPort() : 0;
    const quint16 localPort = m_socket ? m_socket->localPort() : 0;
    WIFI_LOG(QString("WebSocket connected — peer=%1:%2 localPort=%3")
             .arg(peerIp).arg(peerPort).arg(localPort));
    setConnected(true);

    // Promote latency-critical traffic to Voice AC via DSCP, and kill Nagle.
    applyTcpQos();

    // Bump to the highest cadence the firmware supports, opt in to events,
    // and seed an initial status frame so battery/firmware_version populate
    // immediately. Periodic battery/charging refreshes are driven by
    // sendKeepAlive() below — the WS firmware does not push status
    // unsolicited, so we must request it ourselves (matches the BT driver's
    // ~4-minute battery poll; see decentscale.cpp kBatteryPollHeartbeatTicks).
    send(QStringLiteral("rate 10k"));
    send(QStringLiteral("events on"));
    send(QStringLiteral("status"));
    m_ticksSinceBatteryPoll = 0;
    // Restore the LCD on every (re)connect. Idempotent on a fresh-connect
    // scale (LCD defaults on); required on a reconnect after the DE1-sleep
    // keepScaleOn=true+WiFi path, where disableLcd() was sent before the WS
    // was gracefully closed. Not a full wake(): soft_sleep off isn't needed
    // here because sleep() (which would have soft-slept) wasn't called on
    // that path — only the LCD was disabled.
    send(QStringLiteral("display on"));
}

void DecentScaleWifi::onDisconnected() {
    if (m_recognitionTimer) m_recognitionTimer->stop();
    m_ticksSinceBatteryPoll = 0;

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
    m_lastResetReason.clear();
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
    } else if (type == QStringLiteral("session_info")) {
        handleSessionInfoFrame(obj);
    } else if (type == QStringLiteral("debug")) {
        handleDebugFrame(obj);
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

// Newer firmware separates connect-time identity (firmware_version,
// protocol_version) and the boot-cause (reset_reason) into its own session_info
// frame, sent unsolicited right after the WS handshake; the live status frame
// no longer carries firmware_version / protocol_version. Older firmware put
// them in status — handleStatusFrame still reads them there. Both paths assign
// to the same m_firmwareVersion / m_loggedProtoVersion members with
// change-detection, so a transitional firmware that sends both shapes only
// logs once.
void DecentScaleWifi::handleSessionInfoFrame(const QJsonObject& obj) {
    onRecognizedAsHds();

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

    const QJsonValue pv = obj.value(QStringLiteral("protocol_version"));
    if (pv.isDouble()) {
        const int v = pv.toInt();
        if (v != m_loggedProtoVersion) {
            m_loggedProtoVersion = v;
            WIFI_LOG(QString("Protocol version: %1").arg(v));
        }
    }

    // reset_reason is the scale's boot-cause for the current session (e.g.
    // "poweron", "brownout", "watchdog"). Useful for triage — a watchdog reset
    // mid-session that we recover from looks identical at the WS layer to a
    // user power-cycling the scale; reset_reason disambiguates them.
    const QJsonValue rr = obj.value(QStringLiteral("reset_reason"));
    if (rr.isString()) {
        const QString reason = rr.toString();
        if (!reason.isEmpty() && m_lastResetReason != reason) {
            m_lastResetReason = reason;
            WIFI_LOG(QString("Scale reset reason: %1").arg(reason));
        }
    }
}

// Debug frames carry firmware-internal health telemetry. Two shapes arrive
// here:
//   • Event-driven (unprompted, after `events on`): {"type":"debug",
//     "event":"stall_start"|"stall_end"|"adc_recovery"|"temp_peak", ...}.
//   • Full-state snapshot (response to a "debug" request — see
//     requestDebugSnapshot()): {"type":"debug","status":"ok","soc_temp_c":...,
//     "weight_stalled":..., "stall_count":..., ...}.
// Log each verbatim — the firmware decides what to send, and a compact
// one-line dump is the cheapest way to surface every signal for triage. We
// don't filter or shape further until we know which fields actually matter.
void DecentScaleWifi::handleDebugFrame(const QJsonObject& obj) {
    const QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    WIFI_LOG(QString("Debug frame: %1").arg(QString::fromUtf8(json)));
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
    // the numeric reason code — without either, we have no diagnostic detail
    // to log, and marking the disconnect as "expected" would hide a
    // potentially-real failure. Let the following unexpected disconnect log
    // surface so it's diagnosable.
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

    // Consume-and-clear the app-initiated flag so the next firmware-initiated
    // power_off still logs with full detail. We don't surface a dialog either
    // way — the existing "Scale Disconnected" notice (only after reconnect
    // gives up) is the user-facing signal.
    if (m_powerOffInitiatedByApp) {
        m_powerOffInitiatedByApp = false;
        WIFI_LOG(QString("Scale shut down: %1 (code %2) — app-initiated")
                 .arg(reasonText).arg(reasonCode));
        return;
    }

    WIFI_WARN(QString("Scale shut down: %1 (code %2)").arg(reasonText).arg(reasonCode));
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

    emit recognizedAsHds();
}

void DecentScaleWifi::onRecognitionTimeout() {
    WIFI_WARN(QString("No recognizable HDS frame within %1 ms from %2")
              .arg(kRecognitionTimeoutMs).arg(m_currentTarget));

    // Cached-IP attempt failed validation → evict the bad IP and fall back to
    // the hostname. (If we were already on the hostname, we've exhausted options.)
    if (!m_currentTargetIsHostname && !m_triedHostnameFallback) {
        WIFI_LOG(QString("Cached IP %1 didn't validate as HDS — evicting cache and falling back to hostname %2")
                 .arg(m_currentTarget, m_hostname));
        if (m_ipCacheUpdate) m_ipCacheUpdate(m_hostname, QString());
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
    // (onScaleConnectionTimeout) is the backstop for the saved-scale reconnect
    // case — it retries, and a genuinely-gone scale is surfaced by the
    // FlowScale-fallback notice. #1253
    //
    // For the MANUAL "Add WiFi Scale" path, the outer connection timer has
    // already been stopped (onScaleConnectedChanged stops it when setConnected(true)
    // fires from WS-connect, which happens before recognition), so its
    // manualWifiValidationFailed emission won't fire. Emit recognitionFailed so
    // main.cpp's deferred-persistence handler can route the failure to the
    // user-visible dialog. The signal is only meaningful in the give-up branch
    // — the cached-IP fallback branch above starts a new attempt, so recognition
    // could still succeed there. (#1281)
    WIFI_WARN(QStringLiteral("WiFi scale did not respond as HDS — giving up this attempt"));
    // ORDER MATTERS: write internal state and abort the socket BEFORE emitting
    // recognitionFailed. The signal's only slot (in main.cpp's deferred-
    // persistence wiring) emits disconnectScaleRequested synchronously, whose
    // handler calls physicalScale.reset() — destroying THIS object while we're
    // still on the call stack of onRecognitionTimeout. Any access to `this`
    // (m_userInitiatedShutdown, m_socket) after the emit would be a use-after-
    // free. By writing before the emit, we ensure those mutations complete
    // while `this` is still alive; the implicit function return after the
    // emit doesn't touch `this`, so it's safe to be destroyed during the emit.
    m_userInitiatedShutdown = true;  // mark expected; reconnect owned by main.cpp
    m_socket->abort();  // Same rationale as the fallback path above.
    emit recognitionFailed();  // MUST be last — slot may destroy `this`.
}

int DecentScaleWifi::encodeButton(int buttonNumber, int pressCode) {
    return kWifiButtonFlag | ((buttonNumber & 0xF) << 8) | (pressCode & 0xFF);
}

bool DecentScaleWifi::send(const QString& text) {
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        // Qt's sendTextMessage silently returns 0 bytes when the socket isn't
        // ConnectedState (ClosingState, UnconnectedState, mid-handshake), with
        // no errorOccurred signal — so without this check, commands queued
        // during teardown would vanish without trace. Most observed at app
        // exit when the DE1-sleep handler already initiated a graceful close.
        WIFI_WARN(QStringLiteral("send() dropped — socket not connected: %1")
                  .arg(text.left(80)));
        return false;
    }
    m_socket->sendTextMessage(text);
    return true;
}

void DecentScaleWifi::applyTcpQos() {
    if (!m_socket) {
        WIFI_WARN(QStringLiteral("applyTcpQos: no QWebSocket — skipping"));
        return;
    }

    // Pull the underlying QTcpSocket out of QWebSocketPrivate. The cast
    // pattern (QObjectPrivate::get on the public object, then downcast to the
    // concrete *Private) is the standard way Qt itself talks to its
    // d-pointers; reaching the `private` m_pSocket member uses
    // `AccessBypass<WsPrivateSocketTag, &QWebSocketPrivate::m_pSocket>` defined
    // at file scope above, with `get(WsPrivateSocketTag{})` returning the
    // member pointer that the `->*` then applies to d.
    auto* d = static_cast<QWebSocketPrivate*>(QObjectPrivate::get(m_socket));
    QTcpSocket* tcp = d ? d->*get(WsPrivateSocketTag{}) : nullptr;
    if (!tcp) {
        WIFI_WARN(QStringLiteral("applyTcpQos: QWebSocketPrivate has no QTcpSocket — skipping"));
        return;
    }

    // DSCP EF (Expedited Forwarding) = 0x2E in the 6-bit DSCP field, which is
    // 0xB8 when shifted into the 8-bit TOS byte (DSCP occupies the top 6 bits;
    // the bottom 2 are ECN, left zero). Android/iOS/macOS/Linux/Windows map
    // this to WMM AC_VO when the AP supports WMM. If the router/AP strips or
    // ignores DSCP, no harm done — the byte still goes out.
    constexpr int kDscpEfTosByte = 0xB8;
    tcp->setSocketOption(QAbstractSocket::TypeOfServiceOption, kDscpEfTosByte);
    tcp->setSocketOption(QAbstractSocket::LowDelayOption, 1);  // TCP_NODELAY

    // Read back via the same option getters. On platforms that silently
    // accept-but-discard (e.g. some Android OEM stacks for TOS), the read-back
    // value won't match what we wrote — that's diagnostically valuable, so
    // log both regardless, and escalate to WARN on mismatch so a support
    // workflow grepping for warnings can see when QoS didn't take.
    //
    // For TOS, the exact byte matters (it's the DSCP marker the kernel will
    // stamp onto every outgoing packet), so log it verbatim. For TCP_NODELAY
    // we only care whether Nagle is off — some platforms (e.g. macOS) return a
    // truthy-but-not-1 value from getsockopt(TCP_NODELAY), so booleanize the
    // read-back instead of printing the raw integer.
    const QVariant tosRead = tcp->socketOption(QAbstractSocket::TypeOfServiceOption);
    const QVariant lowDelayRead = tcp->socketOption(QAbstractSocket::LowDelayOption);

    const int tosReadInt = tosRead.isValid() ? tosRead.toInt() : -1;
    const bool tosOk = (tosReadInt == kDscpEfTosByte);
    const bool nodelayOk = lowDelayRead.isValid() && lowDelayRead.toInt() != 0;
    const QString tosStr =
        tosReadInt < 0 ? QStringLiteral("<n/a>") : QString::number(tosReadInt, 16);
    const QString lowDelayStr =
        !lowDelayRead.isValid() ? QStringLiteral("<n/a>")
        : (nodelayOk ? QStringLiteral("enabled") : QStringLiteral("disabled"));

    const QString line = QString("applyTcpQos: requested DSCP EF (TOS=0x%1) + TCP_NODELAY; "
                                 "readback TOS=%2 TCP_NODELAY=%3")
                         .arg(kDscpEfTosByte, 2, 16, QLatin1Char('0'))
                         .arg(tosStr).arg(lowDelayStr);
    if (tosOk && nodelayOk) {
        WIFI_LOG(line);
    } else {
        WIFI_WARN(QString("%1 — kernel did not honor request (tosOk=%2 nodelayOk=%3)")
                  .arg(line)
                  .arg(tosOk ? QStringLiteral("yes") : QStringLiteral("no"))
                  .arg(nodelayOk ? QStringLiteral("yes") : QStringLiteral("no")));
    }
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
    //
    // Mark the imminent power_off echo as app-initiated so handlePowerFrame
    // logs at LOG level (we already know about it) rather than WARN. Set
    // BEFORE the send to keep the assignment+send pair locally correct —
    // any future caller or mock that delivers the echo synchronously inside
    // send() still sees the flag set.
    m_powerOffInitiatedByApp = true;
    const bool sent = send(QStringLiteral("{\"command\":\"power\",\"action\":\"off\"}"));
    if (!sent) {
        // Caller (app-exit waitLoop, DE1-sleep handler) hangs forever waiting
        // for sleepCompleted, so emit unconditionally — but warn so the
        // dropped power-off command isn't masked by a misleading
        // "drained successfully" log downstream. Common at app exit after a
        // DE1-sleep keepScaleOn=true+WiFi close (socket already in ClosingState).
        //
        // Clear the latch locally: no echo will arrive (we didn't send the
        // command), so if a firmware-initiated power_off (low battery, button)
        // happens before the next connectToHost() cycle resets it, it must
        // surface, not be silently suppressed. The connectToHost() reset is
        // still the backstop for the case where send() succeeded but the
        // socket dropped before the echo arrived.
        m_powerOffInitiatedByApp = false;
        WIFI_WARN("sleep(): power-off command not delivered (socket not connected)");
    }
    // BT waits for `characteristicWritten` as a "command left the radio" ack.
    // The WS analog is send() returning success above. Emit immediately to
    // match BT's intent.
    emit sleepCompleted();
}

void DecentScaleWifi::requestDebugSnapshot() {
    send(QStringLiteral("debug"));
}

void DecentScaleWifi::setLed(int r, int g, int b) {
    const int rc = std::clamp(r, 0, 255);
    const int gc = std::clamp(g, 0, 255);
    const int bc = std::clamp(b, 0, 255);
    send(QStringLiteral("led %1 %2 %3").arg(rc).arg(gc).arg(bc));
}

void DecentScaleWifi::sendKeepAlive() {
    // Liveness itself is covered by TCP keepalive; this hook exists so the
    // base-class keep-alive timer (30 s) can drive a periodic `status` request,
    // since the WS firmware no longer auto-pushes status frames. Send `status`
    // on every kBatteryPollKeepAliveTicks tick (~4 min) — matches the BT
    // driver's effective battery-poll cadence (see decentscale.cpp's
    // kBatteryPollHeartbeatTicks). The base-class timer is started in
    // setConnected(true) and stopped in setConnected(false), so no extra
    // lifecycle wiring is needed here.
    if (++m_ticksSinceBatteryPoll >= kBatteryPollKeepAliveTicks) {
        m_ticksSinceBatteryPoll = 0;
        send(QStringLiteral("status"));
    }
}

void DecentScaleWifi::onError() {
    const QAbstractSocket::SocketError err = m_socket
        ? m_socket->error() : QAbstractSocket::UnknownSocketError;
    const QString errStr = m_socket ? m_socket->errorString() : QStringLiteral("<no socket>");
    WIFI_WARN(QString("WebSocket error: %1 (code %2)").arg(errStr).arg(static_cast<int>(err)));

    // 503 detection — firmware refuses additional clients past its cap. Treat
    // as an expected refusal so it isn't recorded as a transport error, but
    // don't surface a modal: HDS firmware now allows multiple concurrent
    // clients, so a 503 in practice means the cap is briefly saturated and the
    // standard "Scale Disconnected" / FlowScale fallback notice is sufficient.
    if (errStr.contains(QStringLiteral("503"))) {
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

    // Cached-IP attempt failed under us: ANY error (handshake refused like 403,
    // socket-level HostNotFound/Refused/Network/Timeout, anything else) means
    // this IP isn't a working HDS endpoint. Evict the cached IP so we don't
    // dial it again on the next connect cycle (or on the next proactive
    // switch-back probe), and fall through to a fresh hostname/mDNS lookup.
    //
    // Why catch every error class, not just the original "fast-fail" four: a
    // poisoned cached IP (manually-typed wrong address, DHCP-reassigned to a
    // router/printer/NAS) commonly produces handshake-layer errors instead of
    // socket-layer ones — e.g. the home router answers ws://192.168.1.1/snapshot
    // with HTTP 403, which surfaces here as WebSocketProtocolError /
    // UnknownSocketError, NOT in {HostNotFound, ConnectionRefused, NetworkError,
    // SocketTimeoutError}. Without this, the cached-IP attempt would dead-end:
    // onDisconnected stops the recognition timer, so the only other path that
    // would have triggered hostname fallback (onRecognitionTimeout) never fires.
    //
    // 503 is the one error we DO NOT route here: it's the openscale "client cap
    // saturated" refusal — the scale is real and the cache is good; the link
    // is briefly busy. That case is handled by the 503 early-return at the
    // top of this function (the first thing onError checks).
    const bool cachedIpAttempt = !m_currentTargetIsHostname
                              && !m_triedHostnameFallback
                              && !m_pendingHostnameFallback;
    if (cachedIpAttempt) {
        WIFI_LOG(QString("Cached IP %1 unreachable (%2) — evicting cache and falling back to hostname %3")
                 .arg(m_currentTarget, errStr, m_hostname));
        if (m_ipCacheUpdate) m_ipCacheUpdate(m_hostname, QString());
        m_userInitiatedShutdown = true;
        m_recognitionTimer->stop();

        if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) {
            // Socket is mid-teardown. Use the existing flag mechanism that
            // onRecognitionTimeout uses: abort to force a clean disconnect,
            // and onDisconnected sees m_pendingHostnameFallback and runs
            // attemptHostname() inline once the close completes.
            m_pendingHostnameFallback = true;
            m_socket->abort();
        } else {
            // Socket has already disconnected (onDisconnected raced ahead of
            // this onError and went through its normal path without the
            // pending flag set). Dial the hostname fallback ourselves, queued
            // to the event loop so signal handlers remain re-entrant-safe.
            QMetaObject::invokeMethod(this,
                [this]() { attemptHostname(); },
                Qt::QueuedConnection);
        }
    }
}
