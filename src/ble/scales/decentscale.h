#pragma once

#include "../scaledevice.h"
#include "../transport/scalebletransport.h"
#include <QTimer>

class DecentScale : public ScaleDevice {
    Q_OBJECT

public:
    explicit DecentScale(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~DecentScale() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return m_name; }
    QString type() const override { return ScaleTypeIds::scaleTypeId(ScaleType::DecentScale); }
    QString firmwareVersion() const override { return m_firmwareVersion; }
    bool supportsFirmwareUpdate() const override { return !m_firmwareVersion.isEmpty(); }

public slots:
    void tare() override;
    void sendKeepAlive() override;
    bool supportsTimer() const override { return true; }
    void startTimer() override;
    void stopTimer() override;
    void resetTimer() override;
    void sleep() override;
    void wake() override;
    void disableLcd() override;
    void startFirmwareUpdate() override;
    void setLed(int r, int g, int b);
private slots:
    void onTransportConnected();
    void onTransportDisconnected();
    void onTransportError(const QString& message);
    void onServiceDiscovered(const QBluetoothUuid& uuid);
    void onServicesDiscoveryFinished();
    void onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid);
    void onCharacteristicChanged(const QBluetoothUuid& characteristicUuid, const QByteArray& value);

private:
#ifdef DECENZA_TESTING
    friend class tst_ScaleProtocol;
#endif
    void parseWeightData(const QByteArray& data);
    void sendCommand(const QByteArray& command);
    void sendHeartbeat();
    void enableWeightNotifications(const QString& reason);
    void startHeartbeat();
    void stopHeartbeat();
    void startWatchdog();
    // Restarts the watchdog's countdown when the weight-notify enable reaches
    // the radio, so it times from there rather than from submission.
    void onNotificationsIssued(const QBluetoothUuid& characteristicUuid);
    void stopWatchdog();
    void tickleWatchdog();
    void onWatchdogFired();

    // de1app watchdog constants
    static constexpr int kWatchdogFirstTimeoutMs = 1000;   // Initial: 1s to verify data flowing
    static constexpr int kWatchdogTickleTimeoutMs = 2000;   // Subsequent: 2s after each update
    static constexpr int kWatchdogMaxRetries = 10;          // Re-enable notifications up to 10 times
    static constexpr int kChecksumFailureThreshold = 5;     // Disable checksum on the Nth consecutive failure (Nth packet is accepted)
    // Battery polling: the BT scale only reports battery in the LED-response
    // packet, which it sends in reply to the display-on command. Without
    // periodic re-polling the value goes stale; piggyback on the 1 s
    // heartbeat tick — every Nth tick re-send display-on, the scale's reply
    // refreshes battery. ~4 minutes is a comfortable middle of the 3-5 min
    // range Jeff asked for. The poll auto-pauses when the scale is asleep
    // (sleep() calls stopHeartbeat) and is also gated on m_lcdOn so that
    // disableLcd() (DE1 sleep + keepScaleOn=true) doesn't relight the LCD
    // every ~4 min — see #1279.
    static constexpr int kBatteryPollHeartbeatTicks = 240;

    ScaleBleTransport* m_transport = nullptr;
    QString m_name = "Decent Scale";
    bool m_serviceFound = false;
    bool m_characteristicsReady = false;
    bool m_watchdogUpdatesSeen = false;
    int m_watchdogRetries = 0;
    int m_consecutiveChecksumFailures = 0;
    bool m_checksumDisabled = false;
    // Firmware version captured from the first LED-response packet of each
    // connect (bytes [5-6] of cmd=0x0A, header=0x03). Empty until captured;
    // cleared in onTransportDisconnected so the next connect re-logs it.
    QString m_firmwareVersion;
    // Last battery byte (d[4] of the same LED-response packet) captured per
    // connect. -1 sentinel = not yet captured. Used to log the byte once per
    // connect, then warn-log on any change. Cleared on disconnect.
    int m_lastBatteryByte = -1;
    // Counts 1 s heartbeat ticks; on every kBatteryPollHeartbeatTicks tick
    // we re-send the display-on command so the scale replies with a fresh
    // LED-response packet (which carries the battery byte). Reset in
    // startHeartbeat() so each new connect/wake starts the interval fresh.
    int m_ticksSinceBatteryPoll = 0;
    QTimer* m_heartbeatTimer = nullptr;
    // A wake sequence has requested its weight-notify enable and is waiting for
    // it to reach the radio before the watchdog starts counting.
    bool m_watchdogArmPending = false;
    QTimer* m_watchdogTimer = nullptr;
    // Gates the periodic battery poll — see kBatteryPollHeartbeatTicks above.
    bool m_lcdOn = true;
};
