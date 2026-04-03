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
    QString type() const override { return "decent"; }

public slots:
    void tare() override;
    void sendKeepAlive() override;
    void startTimer() override;
    void stopTimer() override;
    void resetTimer() override;
    void sleep() override;
    void wake() override;
    void disableLcd() override;
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
    void stopWatchdog();
    void tickleWatchdog();
    void onWatchdogFired();

    // de1app watchdog constants
    static constexpr int kWatchdogFirstTimeoutMs = 1000;   // Initial: 1s to verify data flowing
    static constexpr int kWatchdogTickleTimeoutMs = 2000;   // Subsequent: 2s after each update
    static constexpr int kWatchdogMaxRetries = 10;          // Re-enable notifications up to 10 times
    static constexpr int kChecksumFailureThreshold = 5;     // Disable checksum after N consecutive failures

    ScaleBleTransport* m_transport = nullptr;
    QString m_name = "Decent Scale";
    bool m_serviceFound = false;
    bool m_characteristicsReady = false;
    bool m_watchdogUpdatesSeen = false;
    int m_watchdogRetries = 0;
    int m_consecutiveChecksumFailures = 0;
    bool m_checksumDisabled = false;
    QTimer* m_heartbeatTimer = nullptr;
    QTimer* m_watchdogTimer = nullptr;
};
