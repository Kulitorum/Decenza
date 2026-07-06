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
     * Used for time-critical operations like stop-at-weight (SAW) and
     * ensureChargerOn (app suspend). Default implementation delegates to
     * write(). BLE overrides this to bypass its 50ms command queue for
     * lower latency. Does not clear the queue — callers that need to
     * clear pending commands do so explicitly before calling this.
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
     * Clear any pending command queue and return the number of commands
     * that were dropped. Called before urgent operations (SAW stop, sleep)
     * to prevent stale commands from interfering. Transports without
     * queuing return 0. The count lets DE1Device skip invalidating its
     * per-register MMR dedup cache when nothing was actually dropped (the
     * cache is only at risk when a queued write never reached the wire).
     */
    virtual qsizetype clearQueue() { return 0; }

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
     * Emitted when a transport error occurs. Surfaced to the user as a modal
     * (via BLEManager), so only for errors that need user awareness or action.
     * Write-retry exhaustion is deliberately NOT routed here — it marks a dead
     * link that the reconnect ladder handles, and is reported via de1LinkFault
     * instead (#1423).
     * @param message Human-readable error description.
     */
    void errorOccurred(const QString& message);

    /**
     * Emitted when the command queue is empty and no write is pending.
     * Used at app exit to know when sleep commands have been sent.
     */
    void queueDrained();

    /**
     * Emitted on a DE1-link transport fault that signifies real contention:
     * a write that failed after all retries, or a connection-class controller
     * error. `kind` is a short stable tag ("write-failed", "controller-error").
     * A single transient write retry is deliberately NOT emitted (it is normal
     * on healthy hardware and produced false positives). Scale-agnostic — the
     * connection-priority detector correlates these against the scale's
     * HIGH-priority request timing; this layer has no scale knowledge.
     */
    void de1LinkFault(const QString& kind);

    /**
     * Emitted while the transport is in BLE service + characteristic discovery —
     * a high-burst window where the local BLE controller is saturated and any
     * write to a co-resident peer (e.g. a scale on the same adapter) will fail
     * with CharacteristicWriteError on weaker radios (Samsung Tab A8, #1176).
     * Consumers (BLEManager → DecentScale heartbeat) pause non-essential scale
     * writes for the duration. Serial transports never emit this — there is no
     * radio contention to coordinate around.
     */
    void serviceDiscoveryActiveChanged(bool active);

    /**
     * Emitted for debug/diagnostic logging.
     * @param message Log text to be captured by ShotDebugLogger.
     */
    void logMessage(const QString& message);
};
