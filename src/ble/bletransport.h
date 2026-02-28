#pragma once

#include "de1transport.h"

#include <QBluetoothDeviceInfo>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QTimer>
#include <QQueue>
#include <functional>

/**
 * BLE transport for DE1 communication.
 *
 * Implements DE1Transport using QLowEnergyController (Bluetooth Low Energy).
 * Manages the BLE command queue with 50ms inter-write spacing, write retry
 * logic, service discovery, and characteristic subscriptions.
 *
 * Lifecycle:
 *   1. Construct BleTransport
 *   2. Call connectToDevice(deviceInfo)
 *   3. BleTransport handles service discovery, subscribes to notifications,
 *      reads initial values, and requests Idle state
 *   4. Emits connected() when ready for I/O
 *   5. Call disconnect() or delete to tear down
 */
class BleTransport : public DE1Transport {
    Q_OBJECT

public:
    explicit BleTransport(QObject* parent = nullptr);
    ~BleTransport() override;

    // -- DE1Transport interface --
    void write(const QBluetoothUuid& uuid, const QByteArray& data) override;
    void writeUrgent(const QBluetoothUuid& uuid, const QByteArray& data) override;
    void read(const QBluetoothUuid& uuid) override;
    void subscribe(const QBluetoothUuid& uuid) override;
    void subscribeAll() override;
    void disconnect() override;
    void clearQueue() override;
    bool isConnected() const override;
    QString transportName() const override { return QStringLiteral("BLE"); }

    // -- BLE-specific API (not part of DE1Transport) --

    /**
     * Initiate a BLE connection to the given device.
     * This is BLE-specific and not part of the DE1Transport interface.
     * Emits connected() when service discovery completes and notifications
     * are subscribed.
     */
    void connectToDevice(const QBluetoothDeviceInfo& device);

private slots:
    void onControllerConnected();
    void onControllerDisconnected();
    void onControllerError(QLowEnergyController::Error error);
    void onServiceDiscovered(const QBluetoothUuid& uuid);
    void onServiceDiscoveryFinished();
    void onServiceStateChanged(QLowEnergyService::ServiceState state);
    void onCharacteristicChanged(const QLowEnergyCharacteristic& c, const QByteArray& value);
    void onCharacteristicWritten(const QLowEnergyCharacteristic& c, const QByteArray& value);
    void processCommandQueue();

private:
    void setupService();
    void writeCharacteristic(const QBluetoothUuid& uuid, const QByteArray& data);
    void queueCommand(std::function<void()> command);

    QLowEnergyController* m_controller = nullptr;
    QLowEnergyService* m_service = nullptr;
    QMap<QBluetoothUuid, QLowEnergyCharacteristic> m_characteristics;

    // Command queue (50ms spacing between BLE writes)
    QQueue<std::function<void()>> m_commandQueue;
    QTimer m_commandTimer;
    bool m_writePending = false;

    // Write retry logic (like de1app)
    std::function<void()> m_lastCommand;
    int m_writeRetryCount = 0;
    static constexpr int MAX_WRITE_RETRIES = 3;
    QTimer m_writeTimeoutTimer;
    static constexpr int WRITE_TIMEOUT_MS = 5000;
    QString m_lastWriteUuid;
    QByteArray m_lastWriteData;

    // Service discovery retry logic
    QBluetoothDeviceInfo m_pendingDevice;
    QTimer m_retryTimer;
    int m_retryCount = 0;
    static constexpr int MAX_RETRIES = 3;
    static constexpr int RETRY_DELAY_MS = 2000;
};
