#pragma once

#include <QObject>
#include <QBluetoothUuid>
#include <QBluetoothDeviceInfo>
#include <QBluetoothAddress>
#include <QByteArray>
#include <QString>

/**
 * Abstract BLE transport interface for scales.
 *
 * This abstraction allows different BLE implementations:
 * - QtScaleBleTransport: Uses Qt's QLowEnergyController (Android, desktop)
 * - CoreBluetoothScaleBleTransport: Uses native CoreBluetooth (iOS, macOS)
 *
 * Scale classes use this interface for all BLE operations.
 * Protocol parsing remains in each scale class.
 */
class ScaleBleTransport : public QObject {
    Q_OBJECT

public:
    /**
     * BLE write types - must match Android BluetoothGattCharacteristic constants
     */
    enum class WriteType {
        WithResponse = 2,    // WRITE_TYPE_DEFAULT - waits for acknowledgment
        WithoutResponse = 1  // WRITE_TYPE_NO_RESPONSE - fire and forget
    };

    explicit ScaleBleTransport(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~ScaleBleTransport() = default;

    /**
     * Connect to a BLE device by address (for Android/desktop).
     * Emits connected() on success, error() on failure.
     */
    virtual void connectToDevice(const QString& address, const QString& name) = 0;

    /**
     * Connect to a BLE device using full device info (required for iOS).
     * Default implementation extracts address - override for iOS support.
     */
    virtual void connectToDevice(const QBluetoothDeviceInfo& device) {
        connectToDevice(device.address().toString(), device.name());
    }

    /**
     * Disconnect from the current device.
     * Emits disconnected() when complete.
     */
    virtual void disconnectFromDevice() = 0;

    /**
     * Start service discovery.
     * Emits serviceDiscovered() for each service found.
     * Emits servicesDiscoveryFinished() when complete.
     */
    virtual void discoverServices() = 0;

    /**
     * Discover characteristics for a specific service.
     * Emits characteristicDiscovered() for each characteristic found.
     */
    virtual void discoverCharacteristics(const QBluetoothUuid& serviceUuid) = 0;

    /**
     * Enable notifications for a characteristic.
     * This is the critical operation that differs between Qt and native:
     * - Qt: writeDescriptor(CCCD, 0x0100) - fails on some scales
     * - Native Android: setCharacteristicNotification + CCCD write (more robust)
     */
    virtual void enableNotifications(const QBluetoothUuid& serviceUuid,
                                     const QBluetoothUuid& characteristicUuid) = 0;

    /**
     * Write data to a characteristic.
     * @param writeType Controls acknowledgment behavior:
     *   - WithResponse (default): Wait for acknowledgment (WRITE_TYPE_DEFAULT)
     *   - WithoutResponse: Fire and forget (WRITE_TYPE_NO_RESPONSE)
     * Note: IPS (older Acaia/Lunar) requires WithoutResponse,
     *       Pyxis (newer Lunar 2021) requires WithResponse.
     */
    virtual void writeCharacteristic(const QBluetoothUuid& serviceUuid,
                                     const QBluetoothUuid& characteristicUuid,
                                     const QByteArray& data,
                                     WriteType writeType = WriteType::WithResponse) = 0;

    /**
     * Read data from a characteristic.
     * Result comes via characteristicRead() signal.
     */
    virtual void readCharacteristic(const QBluetoothUuid& serviceUuid,
                                    const QBluetoothUuid& characteristicUuid) = 0;

    /**
     * Check if currently connected.
     */
    virtual bool isConnected() const = 0;

signals:
    /**
     * Emitted when BLE connection is established.
     */
    void connected();

    /**
     * Emitted when BLE connection is lost or closed.
     */
    void disconnected();

    /**
     * Emitted for each service discovered during discoverServices().
     */
    void serviceDiscovered(const QBluetoothUuid& serviceUuid);

    /**
     * Emitted when service discovery is complete.
     */
    void servicesDiscoveryFinished();

    /**
     * Emitted for each characteristic discovered during discoverCharacteristics().
     * Properties is a bitmask of QLowEnergyCharacteristic::PropertyType.
     */
    void characteristicDiscovered(const QBluetoothUuid& serviceUuid,
                                  const QBluetoothUuid& characteristicUuid,
                                  int properties);

    /**
     * Emitted when characteristic discovery is complete for a service.
     */
    void characteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid);

    /**
     * Emitted when a characteristic value changes (notifications).
     * This is the primary way scales receive weight data.
     */
    void characteristicChanged(const QBluetoothUuid& characteristicUuid,
                               const QByteArray& value);

    /**
     * Emitted when a characteristic read completes.
     */
    void characteristicRead(const QBluetoothUuid& characteristicUuid,
                            const QByteArray& value);

    /**
     * Emitted when a write operation completes successfully.
     */
    void characteristicWritten(const QBluetoothUuid& characteristicUuid);

    /**
     * Emitted when notifications are successfully enabled.
     */
    void notificationsEnabled(const QBluetoothUuid& characteristicUuid);

    /**
     * Emitted on any BLE error.
     */
    void error(const QString& message);

    /**
     * Emitted for debug logging (shown in UI and written to log file).
     */
    void logMessage(const QString& message);
};
