#pragma once

#include "../scaledevice.h"
#include <QUrl>
#include <QString>
#include <QTimer>
#include <functional>

class QWebSocket;

/**
 * Half Decent Scale over WiFi (WebSocket).
 *
 * Wire protocol: ws://<host>/snapshot. Snapshot frames are bare JSON
 * objects { "grams": <number>, "ms": <number> } emitted by the firmware.
 * Typed frames (status / button / power / rate) opt-in via "events on".
 *
 * Reports type() == "decent-wifi" so the scale-creation hot-swap path in
 * main.cpp correctly distinguishes a BLE Decent reconnect from a WiFi one
 * (otherwise the type-change guard would always recreate the driver, see
 * #1246 review #6). NOTE: ScaleFactory::resolveScaleType maps "decent" and
 * "decent-wifi" to DIFFERENT enum values (DecentScale vs DecentScaleWifi),
 * so it cannot be used to unify the two transports — downstream code that
 * wants "same physical product" semantics must branch on the string with
 * explicit cases for both (e.g., grinder-calibration baseline in
 * settings_calibration.cpp:423-424). transportType() returns "wifi" for
 * paths that need to distinguish transport explicitly.
 */
class DecentScaleWifi : public ScaleDevice {
    Q_OBJECT

public:
    explicit DecentScaleWifi(QObject* parent = nullptr);
    ~DecentScaleWifi() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;

    // Convenience overload: connect to a known hostname (e.g. "hds.local").
    // Will try the cached IP for this hostname first (if `ipResolver` returns
    // a non-empty string), validate via the recognition window, and fall back
    // to the hostname on failure.
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

    QString m_name = QStringLiteral("Decent Scale (WiFi)");
    QString m_firmwareVersion;   // cached per-connect; cleared on disconnect
    QString m_lastPowerEventReason;
    int m_lastPowerEventCode = -1;
    // Set on intentional shutdown paths (handlePowerFrame, recognition-timeout
    // fallback, disconnectFromScale, 503 server-busy). Lets onDisconnected
    // recognise the close as expected — currently used only for the log line
    // and to drive the m_pendingHostnameFallback path; reconnect itself is
    // owned by main.cpp's scaleReconnectTimer.
    bool m_userInitiatedShutdown = false;

    IpResolver m_ipResolver;     // hostname → cached IP (or empty)
    IpCacheUpdate m_ipCacheUpdate;  // hostname, ip → side-effect
};
