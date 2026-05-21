#pragma once

#include "../scaledevice.h"
#include <QUrl>
#include <QString>
#include <QTimer>

class QWebSocket;

/**
 * Half Decent Scale over WiFi (WebSocket).
 *
 * Wire protocol: ws://<host>/snapshot. Snapshot frames are bare JSON
 * objects { "grams": <number>, "ms": <number> } emitted by the firmware.
 * Typed frames (status / button / power / rate) opt-in via "events on".
 *
 * Reports type() == "decent" so downstream code that branches on scale
 * type treats this driver and DecentScale (BLE) as the same physical
 * product. transportType() returns "wifi" for paths that need to
 * distinguish.
 */
class DecentScaleWifi : public ScaleDevice {
    Q_OBJECT

public:
    explicit DecentScaleWifi(QObject* parent = nullptr);
    ~DecentScaleWifi() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;

    // Convenience overload: connect to a known hostname (e.g. "hds.local").
    void connectToHost(const QString& hostname);

    QString name() const override { return m_name; }
    QString type() const override { return QStringLiteral("decent"); }
    QString transportType() const { return QStringLiteral("wifi"); }

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

private:
    void send(const QString& text);
    void handleSnapshotFrame(const QJsonObject& obj);
    void handleStatusFrame(const QJsonObject& obj);
    void handleButtonFrame(const QJsonObject& obj);
    void handlePowerFrame(const QJsonObject& obj);
    void handleRateFrame(const QJsonObject& obj);

    // Button encoding: 0x1000 high bit flags WiFi-encoded buttons so they
    // cannot collide with the BLE driver's 0..0xFF single-byte values.
    static int encodeButton(int buttonNumber, int pressCode);

    static constexpr int kReconnectDelayMs = 3000;
    static constexpr int kWifiButtonFlag = 0x1000;

    QWebSocket* m_socket = nullptr;
    QString m_hostname;          // e.g. "hds.local"
    QString m_name = QStringLiteral("Decent Scale (WiFi)");
    QString m_firmwareVersion;   // cached per-connect; cleared on disconnect
    QString m_lastPowerEventReason;
    int m_lastPowerEventCode = -1;
    bool m_userInitiatedShutdown = false;  // Set by power-frame handler; suppresses reconnect
    bool m_reconnectAttempted = false;     // Single-attempt reconnect (no exponential backoff)
};
