#pragma once

#include <QObject>
#include <QBluetoothUuid>
#include <QByteArray>
#include <QString>

/**
 * Abstract transport interface for DE1 communication.
 *
 * This abstraction allows DE1Device to communicate over different transports:
 * - BleTransport: Uses QLowEnergyController (BLE over Bluetooth)
 * - SerialTransport: Uses QSerialPort (USB-C wired connection)
 *
 * Both transports use QBluetoothUuid to identify characteristics. BLE uses
 * them natively; SerialTransport maps them to single-letter codes internally.
 *
 * Data payloads are QByteArray in both cases - the binary protocol is identical
 * regardless of physical transport.
 */
class DE1Transport : public QObject {
    Q_OBJECT

public:
    explicit DE1Transport(QObject* parent = nullptr) : QObject(parent) {}
    ~DE1Transport() override = default;

    /**
     * Write data to a characteristic.
     * Implementations may queue writes (BLE) or send immediately (serial).
     */
    virtual void write(const QBluetoothUuid& uuid, const QByteArray& data) = 0;

    /**
     * Write data bypassing any command queue.
     * Used for time-critical operations like stop-at-weight (SAW).
     * Default implementation delegates to write(). BLE overrides this
     * to bypass its 50ms command queue for lower latency.
     */
    virtual void writeUrgent(const QBluetoothUuid& uuid, const QByteArray& data) {
        write(uuid, data);
    }

    /**
     * Read data from a characteristic.
     * Result arrives via dataReceived() signal.
     */
    virtual void read(const QBluetoothUuid& uuid) = 0;

    /**
     * Subscribe to notifications for a single characteristic.
     * Subsequent value changes arrive via dataReceived() signal.
     */
    virtual void subscribe(const QBluetoothUuid& uuid) = 0;

    /**
     * Subscribe to all DE1 notification characteristics.
     * Convenience method that subscribes to STATE_INFO, SHOT_SAMPLE,
     * WATER_LEVELS, READ_FROM_MMR, and TEMPERATURES.
     */
    virtual void subscribeAll() = 0;

    /**
     * Disconnect from the device and release resources.
     * Emits disconnected() when complete.
     */
    virtual void disconnect() = 0;

    /**
     * Clear any pending command queue.
     * Called when extraction starts to prevent stale commands from interfering.
     * No-op for transports without queuing (e.g., serial).
     */
    virtual void clearQueue() {}

    /**
     * Check if the transport is currently connected.
     */
    virtual bool isConnected() const = 0;

    /**
     * Human-readable transport name for logging and UI display.
     * E.g., "BLE" or "USB Serial".
     */
    virtual QString transportName() const = 0;

signals:
    /**
     * Emitted when the transport connection is established and ready for I/O.
     */
    void connected();

    /**
     * Emitted when the transport connection is lost or closed.
     */
    void disconnected();

    /**
     * Emitted when data is received from a characteristic (notification or read response).
     * @param uuid The characteristic that produced the data.
     * @param data The raw binary payload.
     */
    void dataReceived(const QBluetoothUuid& uuid, const QByteArray& data);

    /**
     * Emitted when a write operation completes successfully.
     * @param uuid The characteristic that was written.
     * @param data The data that was written.
     */
    void writeComplete(const QBluetoothUuid& uuid, const QByteArray& data);

    /**
     * Emitted when a transport error occurs.
     * @param message Human-readable error description.
     */
    void errorOccurred(const QString& message);

    /**
     * Emitted for debug/diagnostic logging.
     * @param message Log text to be captured by ShotDebugLogger.
     */
    void logMessage(const QString& message);
};
