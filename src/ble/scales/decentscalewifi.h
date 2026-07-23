#pragma once

#include "../scaledevice.h"
#include <QUrl>
#include <QString>
#include <QTimer>
#include <QSet>
#include <QAbstractSocket>
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
    //
    // `preferredIp`: a just-completed mDNS resolution the caller already has in
    // hand (a scan selection / the "Add WiFi Scale" dialog's "Use" button).
    // When set it's dialed FIRST — it's the freshest ground truth for where the
    // scale is right now — ahead of the persisted cache. Deliberately NOT
    // written to that cache: only a verified connect (onRecognizedAsHds) or an
    // eviction ever touches it, so an unverified/stale preferredIp can never
    // clobber a good cached value. On recognition failure the normal fallback
    // re-resolves. Empty for manual entries and cache-driven reconnects.
    void connectToHost(const QString& hostname, const QString& preferredIp = QString());

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

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessageReceived(const QString& message);
    void onError();

private slots:
    void onRecognitionTimeout();

private:
#ifdef DECENZA_TESTING
    // Grants the test the classifier below. Exercising it as a pure function
    // covers every SocketError value on every platform; the behavioural tests
    // can only reach the two or three an OS will actually produce on demand.
    friend class tst_DecentScaleWifi;
#endif
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
    void handleButtonFrame(const QJsonObject& obj);
    void handlePowerFrame(const QJsonObject& obj);
    void handleRateFrame(const QJsonObject& obj);

    // Open the WebSocket against `target` (either a cached IP or the bare
    // hostname). Starts the recognition timer; on first valid HDS frame the
    // timer is cancelled and (if isHostname) the peer IP is cached.
    void attemptTarget(const QString& target, bool isHostname);
    // Resolve m_hostname and dial it. On Android this runs a direct mDNS
    // A-query (MdnsResolver) on a worker thread because Qt's resolver can't
    // resolve ".local" at all there. On other platforms, a ".local" name is
    // resolved explicitly via QHostInfo::lookupHost (the same call
    // WifiScaleDiscovery uses) rather than letting QWebSocket::open() resolve
    // it implicitly — that implicit path was observed to stall ~5s and fail
    // even where an explicit QHostInfo lookup for the same name succeeds
    // quickly. A non-".local" name dials directly with no resolution step.
    void attemptHostname();
    // Last resort when hostname resolution fails: dial the cached IP if we hold
    // one. Returns true if an attempt was started. Keeps a deaf-mDNS device
    // dialling something every cycle instead of nothing — see the call sites in
    // attemptHostname and the note on m_retryShouldReresolve.
    bool dialCachedIpAfterResolveFailure();
    // First snapshot or status frame — confirms we're talking to the HDS.
    void onRecognizedAsHds();

    // Button encoding: 0x1000 high bit flags WiFi-encoded buttons so they
    // cannot collide with the BLE driver's 0..0xFF single-byte values.
    static int encodeButton(int buttonNumber, int pressCode);

    // Classify a connect-time socket failure by what it proves about the
    // address we dialed. The discriminator is "did ANY peer answer", NOT "did
    // the attempt fail" — those are different questions and conflating them is
    // what made a briefly-unreachable scale look like a wrong cached IP.
    //
    //   nothing answered  -> transient. Says nothing about who owns the IP, so
    //                        the cached IP is kept and the retry is left to the
    //                        app-level reconnect loop.
    //   something answered -> NOT transient. Evidence the address was
    //                        reassigned; the caller evicts and falls back.
    //
    // The .cpp carries the per-value reasoning and the Qt source references —
    // read it before adding or moving a case. Two entries are counter-intuitive
    // and are explained there: ConnectionRefusedError is NOT transient, and
    // HostNotFoundError is NOT transient.
    //
    // Deliberately ignores the error *string*: only the enum is stable across
    // Qt versions and locales. onError logs errorString() next to the enum so a
    // support log can still separate the errno values Qt collapses onto one
    // enumerator.
    static bool isTransientTransportError(QAbstractSocket::SocketError err);

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
    // Set when an attempt ends with a transient transport error (nothing
    // answered), cleared in onRecognizedAsHds once we're talking to a real
    // scale. While set, the next connectToHost() bypasses the CACHED-IP
    // shortcut and goes through attemptHostname(), which re-resolves the name.
    //
    // A supplied preferredIp always wins over this flag and consumes it — it
    // was resolved AFTER the attempt that set the flag, so it is strictly
    // fresher than anything a re-resolve could produce. See the priority note
    // at the top of connectToHost(); this flag never causes preferredIp to be
    // skipped.
    //
    // WHY THIS EXISTS — it closes a hole that the transient classification
    // itself opens. When DHCP moves the scale, the address it left behind
    // usually goes dark rather than being immediately reassigned. A dark
    // address answers nothing, so it classifies as transient, so the cache is
    // retained — and without this flag the driver would re-dial that dead
    // address on every cycle forever, with no other path to correction (the
    // WiFi->BLE fallback scan is useless for a WiFi-only scale, and the
    // switch-back probe only tests the same cached IP). Before this change any
    // error evicted, so the case self-corrected. Re-resolving is what restores
    // that.
    //
    // Note the justification is ADDRESS FRESHNESS, not any claim about
    // clearing operating-system state for an unreachable peer. For a ".local"
    // name the mDNS responder IS the scale, so during an unreachability window
    // the resolve most likely fails too and falls back to the cached IP —
    // recovery there comes from the backoff delay, not from re-resolving.
    // The cached IP is deliberately NOT evicted: it's still our best guess at
    // the scale's identity, it just isn't what we dial on the recovery attempt.
    //
    // An event-based flag, not a timer: it is set by a failure event and
    // cleared by a recognition event, per the project's no-timers-as-guards
    // rule. Retry TIMING remains entirely owned by main.cpp's
    // scaleReconnectTimer.
    bool m_retryShouldReresolve = false;
    // Bumped each time we kick off an async mDNS resolve. A resolve result
    // whose generation no longer matches is dropped — a newer connectToHost()
    // or disconnect superseded it while the worker thread was in flight.
    int m_resolveGeneration = 0;

    QString m_name = QStringLiteral("Half Decent Scale (WiFi)");
    // firmware_version and protocol_version arrive in the status frame.
    // Cached per-connect so the same value arriving repeatedly doesn't spam
    // the log; cleared on disconnect.
    QString m_firmwareVersion;
    int m_loggedProtoVersion = -1;
    // One sample of each distinct non-snapshot frame "type" is logged per
    // connect (see onTextMessageReceived) so the firmware's actual WS surface
    // is visible — notably whether it ever sends a status frame carrying
    // firmware_version. Cleared on disconnect.
    QSet<QString> m_loggedFrameShapes;
    QString m_lastPowerEventReason;
    int m_lastPowerEventCode = -1;
    // Set on intentional shutdown paths so onDisconnected logs the close as
    // expected and skips noisy follow-up handling. Set at:
    //   - disconnectFromScale (user close)
    //   - handlePowerFrame (scale told us it's powering down)
    //   - attemptHostname, BOTH resolution-failure branches (the Android mDNS
    //     one and the QHostInfo one that runs everywhere else) — but ONLY when
    //     dialCachedIpAfterResolveFailure() found no cached IP to fall back on.
    //     When it dials one, that early return runs first and the flag is not
    //     set, because the cycle is still in progress.
    //   - onRecognitionTimeout fallback branch (cached IP didn't validate →
    //     switching to hostname)
    //   - onRecognitionTimeout give-up branch (hostname also failed)
    //   - onError 503 early-return (server-busy)
    //   - onError cached-IP-eviction branch (a peer-answered non-503 error on a
    //     cached-IP attempt → evict the cached IP and fall back to hostname;
    //     see #1281)
    // Deliberately not carrying a count: it has been wrong twice, and the list
    // is the part with any value. `grep -n "m_userInitiatedShutdown = true"` is
    // the authority.
    //
    // NOT set by onError's transient-transport branch: nothing answered there,
    // which is a genuine abnormal drop, so onDisconnected must keep logging it
    // as unexpected rather than as a close we asked for.
    //
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
