#pragma once

#include "../scaledevice.h"
#include <QUrl>
#include <QString>
#include <QTimer>
#include <QSet>
#include <functional>

class QWebSocket;

/**
 * Half Decent Scale over WiFi (WebSocket).
 *
 * Wire protocol: ws://<host>/snapshot. A frame is a weight snapshot when it
 * carries NO "type" field — a bare JSON object { "grams": <number>,
 * "ms": <number> }. Typed frames (status / button / power / rate / error)
 * always carry "type" and opt in via "events on". NOTE: a "status" frame ALSO
 * carries a "grams" field, so snapshots must be discriminated by the absence of
 * "type", never by the presence of "grams".
 *
 * Reports type() == "decent-wifi" so the scale-creation hot-swap path in
 * main.cpp correctly distinguishes a BLE Decent reconnect from a WiFi one
 * (otherwise the type-change guard would always recreate the driver, see
 * #1246 review #6). NOTE: ScaleFactory::resolveScaleType maps "decent" and
 * "decent-wifi" to DIFFERENT enum values (DecentScale vs DecentScaleWifi),
 * so it cannot be used to unify the two transports — downstream code that
 * wants "same physical product" semantics must branch on the string with
 * explicit cases for both (e.g., grinder-calibration baseline in
 * src/core/settings_calibration.cpp). transportType() returns "wifi" for
 * paths that need to distinguish transport explicitly.
 */
class DecentScaleWifi : public ScaleDevice {
    Q_OBJECT

public:
    explicit DecentScaleWifi(QObject* parent = nullptr);
    ~DecentScaleWifi() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;

    // Convenience overload: connect to a known hostname (e.g. "hds.local").
    // Tries the cached IP for this hostname first (if `ipResolver` returns a
    // non-empty string), validated via the recognition window. With no cached
    // IP (or if it fails validation) it resolves the hostname — on Android via
    // a direct mDNS A-query, since Qt's resolver can't resolve ".local".
    void connectToHost(const QString& hostname);

    QString name() const override { return m_name; }
    QString type() const override { return ScaleTypeIds::scaleTypeId(ScaleType::DecentScaleWifi); }
    QString transportType() const { return QStringLiteral("wifi"); }

    // mDNS-resilience hooks. Production wires these to
    // SettingsNetwork::wifiScaleIp via `settings.network()->wifiScaleIp(...)`
    // (see main.cpp). Tests inject mock callbacks. Both default to no-ops
    // (driver works standalone — no cache, plain hostname connect).
    using IpResolver = std::function<QString(const QString& hostname)>;
    using IpCacheUpdate = std::function<void(const QString& hostname, const QString& ip)>;
    void setIpResolver(IpResolver resolver) { m_ipResolver = std::move(resolver); }
    void setIpCacheUpdate(IpCacheUpdate cb) { m_ipCacheUpdate = std::move(cb); }

signals:
    // Emitted on the first valid HDS frame (snapshot or status) after a
    // connect attempt — confirms the WS endpoint is actually an HDS scale,
    // not just any device that accepted the WS upgrade. Used by the manual
    // "Add WiFi Scale" flow to defer persisting the typed address as the
    // saved primary until we've verified it's a real scale (see #1281).
    void recognizedAsHds();
    // Emitted from onRecognitionTimeout's "give up THIS attempt" branch —
    // the WS handshake completed (so onConnected fired and setConnected(true)
    // was signaled) but no HDS frame arrived within kRecognitionTimeoutMs and
    // there's no further fallback to try. Mutually exclusive with
    // recognizedAsHds for a given attempt (the recognition timer is stopped by
    // onRecognizedAsHds, and the cached-IP fallback branch of
    // onRecognitionTimeout doesn't emit this — only the terminal give-up
    // branch does). main.cpp wires this for manual "Add WiFi Scale" entries
    // because BLEManager's outer 20 s connection timer is stopped at
    // WS-connect time, so without this signal a manual attempt that connects
    // to a non-HDS WS endpoint dead-ends silently (#1281 follow-up).
    void recognitionFailed();

public slots:
    void tare() override;
    void startTimer() override;
    void stopTimer() override;
    void resetTimer() override;
    void sleep() override;
    void wake() override;
    void disableLcd() override;
    void sendKeepAlive() override;
    void disconnectFromScale() override;
    void setLed(int r, int g, int b);
    // Ask the scale to send a one-shot {"type":"debug","status":"ok",...}
    // frame with full health state (SoC temps, stall counters, ADC recovery
    // count). The response lands in handleDebugFrame and gets logged verbatim.
    // Triggered from three places: main.cpp on Qt::ApplicationSuspended (real
    // OS backgrounding), main.qml's goToScreensaver() (the in-app screensaver,
    // which Mac never reaches Suspended for), and the MCP tool
    // devices_request_scale_debug for on-demand triage from the AI/MCP surface.
    void requestDebugSnapshot() override;

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessageReceived(const QString& message);
    void onError();

private slots:
    void onRecognitionTimeout();

private:
    // Send a command text frame over the WS. Returns true on success, false
    // when the socket is not in ConnectedState — Qt's sendTextMessage silently
    // drops in that case (returns 0 bytes, no signal, no log), so the helper
    // checks state and emits a WARN so dropped commands are diagnosable.
    // Most callers ignore the return value; sleep() uses it to decide whether
    // to log a delivery failure for the power-off command.
    bool send(const QString& text);

    // Reaches into QWebSocketPrivate via the private header to set socket
    // options on the underlying QTcpSocket: TCP_NODELAY (LowDelayOption) and
    // DSCP EF (TypeOfServiceOption=0xB8). On Wi-Fi, DSCP→WMM mapping promotes
    // EF/CS5/CS6 to the Voice AC, which has the shortest backoff timers. The
    // intent is to tighten the round-trip distribution for time-critical
    // commands (mainly stop-at-weight). Logs the outcome so we can verify the
    // options actually took on each platform/router combination. Called from
    // onConnected() once the underlying TCP socket exists.
    void applyTcpQos();

    // Tear down the current m_socket and replace it with a fresh QWebSocket
    // before each connect attempt. Reusing a single QWebSocket across multiple
    // open()/abort() cycles surfaces a Qt-internal state-staleness bug where
    // the next attempt's WS handshake completes against the server, then
    // immediately closes with peer-close code 1000 + UnknownSocketError(-1)
    // — likely because Qt's internal close-state/error bookkeeping carries
    // over from the prior abort(). Observed in production: scale auto-
    // reconnect loops would fail this way until the scale-type-change path
    // happened to construct a brand-new DecentScaleWifi (and thus a fresh
    // QWebSocket), at which point the very next dial succeeded immediately.
    void recreateSocket();
    void handleSnapshotFrame(const QJsonObject& obj);
    void handleStatusFrame(const QJsonObject& obj);
    void handleSessionInfoFrame(const QJsonObject& obj);
    void handleDebugFrame(const QJsonObject& obj);
    void handleButtonFrame(const QJsonObject& obj);
    void handlePowerFrame(const QJsonObject& obj);
    void handleRateFrame(const QJsonObject& obj);

    // Open the WebSocket against `target` (either a cached IP or the bare
    // hostname). Starts the recognition timer; on first valid HDS frame the
    // timer is cancelled and (if isHostname) the peer IP is cached.
    void attemptTarget(const QString& target, bool isHostname);
    // Resolve m_hostname and dial it. On Android this runs a direct mDNS
    // A-query (MdnsResolver) on a worker thread because Qt's resolver can't
    // resolve ".local"; on other platforms it dials the hostname and lets the
    // OS resolver (Bonjour / nss-mdns) handle mDNS.
    void attemptHostname();
    // First snapshot or status frame — confirms we're talking to the HDS.
    void onRecognizedAsHds();

    // Button encoding: 0x1000 high bit flags WiFi-encoded buttons so they
    // cannot collide with the BLE driver's 0..0xFF single-byte values.
    static int encodeButton(int buttonNumber, int pressCode);

    static constexpr int kRecognitionTimeoutMs = 5000;
    static constexpr int kWifiButtonFlag = 0x1000;
    // Battery-poll cadence: every Nth base-class keep-alive tick we request a
    // `status` frame from the firmware (which no longer auto-pushes status).
    // Base-class keepAliveTimer ticks at 30 s; 8 ticks → 240 s, matching the
    // BT driver's effective ~4 min battery poll (kBatteryPollHeartbeatTicks =
    // 240 × 1 s heartbeat). Reset on each connect cycle.
    static constexpr int kBatteryPollKeepAliveTicks = 8;

    QWebSocket* m_socket = nullptr;
    QTimer* m_recognitionTimer = nullptr;
    QString m_hostname;             // The canonical hostname (no "wifi:" prefix). Stable across fallback.
    QString m_currentTarget;        // Whatever we're currently dialing — IP or hostname.
    bool m_currentTargetIsHostname = false;
    bool m_recognized = false;      // Set on first valid HDS frame; resets on each attempt.
    bool m_triedHostnameFallback = false;  // Prevents looping if hostname fallback also fails.
    bool m_pendingHostnameFallback = false;  // Set in onRecognitionTimeout; consumed by onDisconnected.
    // Bumped each time we kick off an async mDNS resolve. A resolve result
    // whose generation no longer matches is dropped — a newer connectToHost()
    // or disconnect superseded it while the worker thread was in flight.
    int m_resolveGeneration = 0;

    QString m_name = QStringLiteral("Half Decent Scale (WiFi)");
    // firmware_version, protocol_version, and reset_reason all arrive in the
    // session_info frame on every connect (newer firmware) and previously rode
    // along in the status frame (older firmware still does). Cached per-connect
    // so the same value arriving twice (e.g. status + session_info on a
    // transitional firmware) doesn't spam the log; cleared on disconnect.
    QString m_firmwareVersion;
    int m_loggedProtoVersion = -1;
    QString m_lastResetReason;
    // One sample of each distinct non-snapshot frame "type" is logged per
    // connect (see onTextMessageReceived) so the firmware's actual WS surface
    // is visible — notably whether it ever sends a status frame carrying
    // firmware_version. Cleared on disconnect.
    QSet<QString> m_loggedFrameShapes;
    QString m_lastPowerEventReason;
    int m_lastPowerEventCode = -1;
    // Set on intentional shutdown paths so onDisconnected logs the close as
    // expected and skips noisy follow-up handling. Seven sites set this to
    // true: disconnectFromScale (user close), handlePowerFrame (scale told
    // us it's powering down), attemptHostname (Android mDNS resolution found
    // no responder), onRecognitionTimeout fallback branch (cached IP didn't
    // validate → switching to hostname), onRecognitionTimeout give-up branch
    // (hostname also failed), onError 503 early-return (server-busy), and
    // onError cached-IP-eviction branch (any non-503 error on a cached-IP
    // attempt → evict the cached IP and fall back to hostname; see #1281).
    // Reconnect itself is owned by main.cpp's scaleReconnectTimer — this flag
    // does not gate reconnect.
    bool m_userInitiatedShutdown = false;
    // Whether a GENUINE transport error (errorOccurred not caused by our own
    // abort()/close()) fired during this connection. onDisconnected uses it to
    // flag an abnormal transport drop (RF/WiFi/network loss) vs a clean peer
    // close frame — closeCode() can't be trusted for that (Qt sets it only on a
    // received close frame, and the reused socket keeps a stale value across
    // reconnects). Reset per connect cycle (connectToHost) and attempt
    // (attemptTarget); set in onError only when m_userInitiatedShutdown is false.
    bool m_socketErrorThisConnect = false;
    QString m_lastSocketErrorString;
    // Set in sleep() when we send the firmware power-off JSON. The scale
    // echoes back a `power_off` frame (reason "disabled", code 0) on receipt;
    // this flag lets handlePowerFrame log the echo at LOG level (app-initiated)
    // instead of WARN (firmware-initiated, e.g. low battery / physical button).
    // Consumed-and-cleared in handlePowerFrame; the disconnect path is
    // already classified expected via m_userInitiatedShutdown.
    bool m_powerOffInitiatedByApp = false;
    // Counts base-class keep-alive ticks since the last `status` request; on
    // every kBatteryPollKeepAliveTicks tick we re-send `status` so the scale
    // refreshes battery/charging. Reset in onConnected() and onDisconnected().
    int m_ticksSinceBatteryPoll = 0;

    IpResolver m_ipResolver;     // hostname → cached IP (or empty)
    IpCacheUpdate m_ipCacheUpdate;  // hostname, ip → side-effect
};
