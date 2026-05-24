#pragma once

#include "../scaledevice.h"
#include "../transport/scalebletransport.h"
#include <QTimer>

class VariaAkuScale : public ScaleDevice {
    Q_OBJECT

public:
    explicit VariaAkuScale(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~VariaAkuScale() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return m_name; }
    QString type() const override { return ScaleTypeIds::scaleTypeId(ScaleType::VariaAku); }

public slots:
    void tare() override;
    void sendKeepAlive() override;

private slots:
    void onTransportConnected();
    void onTransportDisconnected();
    void onTransportError(const QString& message);
    void onServiceDiscovered(const QBluetoothUuid& uuid);
    void onServicesDiscoveryFinished();
    void onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid);
    void onCharacteristicChanged(const QBluetoothUuid& characteristicUuid, const QByteArray& value);
    void onWatchdogTimeout();
    void onTickleTimeout();

private:
    void sendCommand(const QByteArray& cmd);
    void enableNotifications();
    void startWatchdog();
    void tickleWatchdog();
    void stopWatchdog();

    ScaleBleTransport* m_transport = nullptr;
    QString m_name = "Varia Aku";
    bool m_serviceFound = false;
    bool m_characteristicsReady = false;

    // Watchdog to re-enable notifications if they stop arriving
    QTimer* m_watchdogTimer = nullptr;
    QTimer* m_tickleTimer = nullptr;
    int m_watchdogRetries = 0;
    bool m_updatesReceived = false;
    static constexpr int WATCHDOG_TIMEOUT_MS = 1000;      // Retry interval
    static constexpr int TICKLE_TIMEOUT_MS = 2000;        // No-update timeout
    static constexpr int MAX_WATCHDOG_RETRIES = 10;
};
