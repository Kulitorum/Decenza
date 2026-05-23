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
    QString type() const override { return QStringLiteral("decent-wifi"); }
    QString transportType() const { return QStringLiteral("wifi"); }

    // mDNS-resilience hooks. Production wires these to
    // SettingsNetwork::wifiScaleIp via `settings.network()->wifiScaleIp(...)`
    // (see main.cpp). Tests inject mock callbacks. Both default to no-ops
    // (driver works standalone — no cache, plain hostname connect).
    using IpResolver = std::function<QString(const QString& hostname)>;
    using IpCacheUpdate = std::function<void(const QString& hostname, const QString& ip)>;
    void setIpResolver(IpResolver resolver) { m_ipResolver = std::move(resolver); }
    void setIpCacheUpdate(IpCacheUpdate cb) { m_ipCacheUpdate = std::move(cb); }

    // Optional UI-string translator — when set, user-visible error strings
    // (those emitted via errorOccurred) are translated via stable i18n keys
    // with the English text as fallback. Scale debug-log lines (WIFI_LOG /
    // WIFI_WARN) stay in English — they're diagnostic, not user-facing.
    // Decoupled from TranslationManager as a std::function so tests can link
    // this driver without pulling in the full Settings/TranslationManager
    // stack — matches the IpResolver / IpCacheUpdate pattern above.
    using UiTranslator = std::function<QString(const QString& key, const QString& fallback)>;
    void setUiTranslator(UiTranslator translator) { m_uiTranslator = std::move(translator); }

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
    void send(const QString& text);
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
    QString m_firmwareVersion;   // cached per-connect; cleared on disconnect
    int m_loggedProtoVersion = -1;  // protocol version last logged this connect; reset on disconnect
    // One sample of each distinct non-snapshot frame "type" is logged per
    // connect (see onTextMessageReceived) so the firmware's actual WS surface
    // is visible — notably whether it ever sends a status frame carrying
    // firmware_version. Cleared on disconnect.
    QSet<QString> m_loggedFrameShapes;
    QString m_lastPowerEventReason;
    int m_lastPowerEventCode = -1;
    // Set on intentional shutdown paths so onDisconnected logs the close as
    // expected and skips noisy follow-up handling. Six sites set this to
    // true: disconnectFromScale (user close), handlePowerFrame (scale told
    // us it's powering down), attemptHostname (Android mDNS resolution found
    // no responder), onRecognitionTimeout fallback branch (cached IP didn't
    // validate → switching to hostname), onRecognitionTimeout give-up branch
    // (hostname also failed), and onError (503 server-busy and the mapped
    // socket-error paths). Reconnect itself is owned by main.cpp's
    // scaleReconnectTimer — this flag does not gate reconnect.
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

    IpResolver m_ipResolver;     // hostname → cached IP (or empty)
    IpCacheUpdate m_ipCacheUpdate;  // hostname, ip → side-effect

    UiTranslator m_uiTranslator;  // empty by default → falls back to English
    // Translate `key` with `fallback`. Returns fallback if no UI translator
    // is set. Use ONLY for user-visible strings (errorOccurred payloads).
    QString translateUiString(const QString& key, const QString& fallback) const;
};
