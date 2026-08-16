#pragma once

#include "../blegattqueue.h"

#include <QObject>
#include <QBluetoothUuid>
#include <QBluetoothDeviceInfo>
#include <QBluetoothAddress>
#include <QByteArray>
#include <QString>
#include <QTimer>

#include <functional>

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

    /**
     * @param queue  The process-wide GATT queue by default. Injected in tests so
     *               ordering can be asserted without sharing state between test
     *               functions.
     */
    explicit ScaleBleTransport(QObject* parent = nullptr, BleGattQueue* queue = nullptr);
    ~ScaleBleTransport() override;

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

    /**
     * True while the transport is holding a connection attempt that has not
     * yet resolved — a controller in Connecting, not yet Connected and not
     * yet failed.
     *
     * Distinct from isConnected(), and the distinction is load-bearing: a
     * connect held in Connecting occupies the platform's connection slot, so
     * every retry against it is rejected as a duplicate, while isConnected()
     * reports false the whole time. A teardown gated on isConnected() alone
     * therefore never fires for exactly the case that needs it.
     *
     * Default false for transports that do not park a controller.
     */
    virtual bool isConnecting() const { return false; }

    /**
     * Connection-priority backoff (dual-HIGH BLE contention, #1093/#1176).
     * When set, the transport must NOT request CONNECTION_PRIORITY_HIGH on
     * (re)connect, leaving the link at the platform-default BALANCED interval.
     * Session-scoped, in-memory only. Default no-op (only QtScaleBleTransport
     * — Android/desktop — implements it; CoreBluetooth is unaffected).
     */
    virtual void setSkipHighPriority(bool skip) { Q_UNUSED(skip); }

    /**
     * Scope the connection-priority + scale-feed-stall machinery to actual
     * scales. A refractometer reuses this transport class but is NOT a scale:
     * it never produces weight samples, and forcing its link to
     * CONNECTION_PRIORITY_HIGH adds a third HIGH connection that contends with
     * the DE1 + scale — the platform GATT scheduler then tears the weakest link
     * (the refractometer) down mid-discovery. Pass false for non-scale links so
     * the connection stays at the platform-default interval with no DE1-fault /
     * feed-stall detection armed. Default no-op (only QtScaleBleTransport —
     * Android/desktop — runs this machinery; the CoreBluetooth transport does
     * not request connection priority).
     */
    virtual void setConnectionPriorityManaged(bool managed) { Q_UNUSED(managed); }

public slots:
    /**
     * Detection inputs wired in main.cpp from the (stable) DE1Device and the
     * WeightProcessor. Default no-op; QtScaleBleTransport correlates them
     * against its own HIGH-request window and triggers the skip-HIGH +
     * self-reconnect backoff. Scale-agnostic — no driver code involved.
     */
    virtual void onDe1LinkFault(const QString& kind) { Q_UNUSED(kind); }
    // SUSPECTED stall (gap > kScaleStaleMs). `gapMs` = silence at detection.
    // No longer drives the backoff — it is the observe/diagnostic breadcrumb;
    // the latch trigger is onScaleFeedStallConfirmed. Default no-op.
    virtual void onScaleFeedStalled(qint64 gapMs) { Q_UNUSED(gapMs); }
    // CONFIRMED stall (epoch-scope-and-stall-confirm): persisted past
    // kScaleStallConfirmMs with no recovery. THIS is what enforce latches on
    // and what observe records as the real "would back off". Default no-op;
    // only QtScaleBleTransport acts.
    virtual void onScaleFeedStallConfirmed(qint64 gapMs) { Q_UNUSED(gapMs); }
    // Recovery counterpart (observe-mode change): a previously-stalled feed
    // resumed on its own. `gapMs` is the measured silent duration. Default
    // no-op; only QtScaleBleTransport logs it (observe mode).
    virtual void onScaleFeedResumed(qint64 gapMs) { Q_UNUSED(gapMs); }
    // Espresso-cycle bracket (#1176): true from EspressoPreheating through
    // shot end, false at idle/between shots. QtScaleBleTransport uses it so a
    // backoff DEFERS the skip-HIGH teardown while a shot is in progress (latch
    // only, apply at the next natural reconnect) instead of bouncing the scale
    // mid-shot; an idle backoff still reconnects immediately. Default no-op.
    virtual void setShotActive(bool active) { Q_UNUSED(active); }

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

protected:
    // -- This transport's half of the shared GATT queue --------------------
    //
    // Scale and refractometer transports had NO concept of an outstanding
    // operation: writeCharacteristic() and friends called straight through to
    // the platform with no in-flight flag, no completion matching and no retry.
    // That asymmetry IS issue #1819 — the DE1 side could tell an operation was
    // outstanding and this side could not, so it had nothing to yield.
    //
    // Everything here is shared by both implementations rather than written
    // twice: the Qt one and the CoreBluetooth one must order operations
    // identically, and two copies of a sequencing rule are two rules.

    /**
     * Queue one operation.
     *
     * @param key        Discard key and completion matcher — the characteristic
     *                   for reads/writes/CCCD, the service for characteristic
     *                   discovery, null for service discovery.
     * @param timeoutMs  The outer bound for an operation the platform never
     *                   answers at all. Discovery legitimately takes seconds
     *                   (6.0 s in the #1819 capture), so it gets its own.
     *
     * No retries, by design: that reproduces exactly what these transports did
     * before they were queued, and inheriting the DE1's budget would let a dead
     * scale hold the shared slot through a ~32 s retry sequence, starving the
     * machine that budget exists to protect.
     */
    void submitGattOperation(const QBluetoothUuid& key,
                             const QString& label,
                             std::function<void()> issue,
                             int timeoutMs = OPERATION_TIMEOUT_MS);

    /** Terminal success for the operation on `key`; no-op if it isn't ours. */
    void completeGattOperation(const QBluetoothUuid& key);

    /**
     * Terminal success for whatever operation we hold, without matching a key.
     *
     * For the two completions that carry no usable identity: service discovery
     * finishing, and a descriptor write ACK (QLowEnergyDescriptor names no
     * characteristic). The guard that matters — never releasing ANOTHER
     * device's slot — still holds. What it cannot catch is a late ACK for an
     * operation of ours that already timed out, releasing our next one early;
     * that needs a timed-out operation to answer afterwards, which is the case
     * the per-operation clock exists to make rare rather than impossible.
     */
    void completeGattOperation();

    /** Terminal failure for whatever operation we hold. */
    void failGattOperation();

    /** Release the slot and drop our queued work. Call on disconnect and teardown. */
    void releaseGattQueue();

    /** True while the slot holds an operation of ours. */
    bool holdsGattSlot() const;

    /** The characteristic/service the held operation targets, or a null UUID. */
    QBluetoothUuid heldGattKey() const;

    static constexpr int OPERATION_TIMEOUT_MS = 5000;
    // Characteristic discovery took 6.0 s in the #1819 capture, so the write
    // budget is not a bound for it — it is a guarantee of failure.
    static constexpr int DISCOVERY_TIMEOUT_MS = 20000;

private:
    BleGattQueue* m_gattQueue = nullptr;
    QTimer m_operationTimeoutTimer;
};
