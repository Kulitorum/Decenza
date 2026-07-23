#pragma once

#include "de1transport.h"

#include <QBluetoothDeviceInfo>
#include <QElapsedTimer>
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
 *   4. Emits connected() when ready for I/O (isConnected() returns false until
 *      characteristic discovery completes, even if the BLE link is up)
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
    qsizetype clearQueue() override;
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
    void onDescriptorWritten(const QLowEnergyDescriptor& descriptor, const QByteArray& value);
    void processCommandQueue();

private:
    void log(const QString& message);
    void warn(const QString& message);
    // Update m_serviceDiscoveryActive and emit serviceDiscoveryActiveChanged()
    // only on transitions. Coalesces the four reset call sites (chars-ready,
    // disconnect(), onControllerDisconnected(), onControllerError()) so the
    // signal cleanly brackets one discovery window per attempt.
    void setServiceDiscoveryActive(bool active);
    bool setupController(const QBluetoothDeviceInfo& device);
    void setupService();
    void writeCharacteristic(const QBluetoothUuid& uuid, const QByteArray& data);
    void queueCommand(std::function<void()> command);

    // Post-connect notification subscription (CCCD enable), sequenced and
    // confirmed one at a time — see subscribeAll(). Fires connected() only
    // once every characteristic in m_pendingSubscribeQueue has been confirmed
    // or individually timed out, closing the race where a one-shot MMR read's
    // response notification could be sent before the client had actually
    // finished enabling notifications for it.
    void subscribeNext();
    QList<QBluetoothUuid> m_pendingSubscribeQueue;
    QBluetoothUuid m_currentSubscribeUuid;
    QTimer m_subscribeTimeoutTimer;
    static constexpr int SUBSCRIBE_TIMEOUT_MS = 3000;

    QLowEnergyController* m_controller = nullptr;
    QLowEnergyService* m_service = nullptr;
    QMap<QBluetoothUuid, QLowEnergyCharacteristic> m_characteristics;
    bool m_characteristicsReady = false;
    // True while discoverDetails() is in flight (service+characteristic
    // discovery window). Used to gate serviceDiscoveryActiveChanged() emissions
    // so consumers don't see false→false on the disconnect path.
    bool m_serviceDiscoveryActive = false;
    // True once disconnected() has been emitted for the current connection
    // attempt (either via Qt's native signal on a Connected→Disconnected
    // transition, or synthesized by us when a connection attempt fails and
    // goes Connecting→Unconnected without ever reaching Connected). Reset
    // to false at every point where a fresh BLE-level connect is about to
    // start: the outer connectToDevice(), the internal service-discovery
    // retry timer, and the tail of disconnect() (defensive — the next
    // connectToDevice() would reset it anyway). Each of those reset points
    // corresponds to a subsequent m_controller->connectToDevice() call.
    bool m_disconnectedEmittedForAttempt = false;

    // Command queue (50ms spacing between BLE writes)
    QQueue<std::function<void()>> m_commandQueue;
    QTimer m_commandTimer;
    bool m_writePending = false;

    // Write retry logic (like de1app)
    std::function<void()> m_lastCommand;
    int m_writeRetryCount = 0;
    static constexpr int MAX_WRITE_RETRIES = 10;
    QTimer m_writeTimeoutTimer;
    static constexpr int WRITE_TIMEOUT_MS = 5000;
    static constexpr int WRITE_RETRY_DELAY_MS = 500;
    QString m_lastWriteUuid;
    QByteArray m_lastWriteData;

    // Service discovery retry logic
    QBluetoothDeviceInfo m_pendingDevice;
    QTimer m_retryTimer;
    int m_retryCount = 0;
    static constexpr int MAX_RETRIES = 3;
    static constexpr int RETRY_DELAY_MS = 2000;

    // Connect watchdog: on Android the GATT stack can leave a connect attempt
    // wedged in Connecting forever — no Connected, no error — so neither Qt's
    // stateChanged→Unconnected synthesis nor the error path ever fires, and the
    // reconnect loop stalls until the app is restarted (issue #1303). This timer
    // is (re)started whenever the controller enters Connecting and stopped on any
    // resolution; if it fires while still Connecting it aborts the hung attempt
    // and synthesizes disconnected() so the retry path can recreate the
    // controller. The deadline exceeds the slowest legitimate connect observed
    // (~26s) and Android's own ~30s supervision timeout, so it only fires on a
    // genuine wedge where nothing else did.
    QTimer m_connectWatchdogTimer;
    static constexpr int CONNECT_WATCHDOG_MS = 35000;

    // Zombie-link detection: a link that stays GATT-connected and keeps ACKing
    // writes but has silently stopped delivering push notifications (reaprime
    // PR #246/#431 describe the same failure on the same DE1 protocol). Every
    // inbound notification restarts m_notificationLiveness; connectToDevice()
    // treats an already-"connected" link whose last notification is older than
    // NOTIFICATION_STALE_MS as a zombie and tears it down instead of the
    // usual "already connected" early return. Checked only at a reconnect
    // attempt (never a background poll), so a false positive costs one extra
    // reconnect, not a spurious disconnect of a healthy in-use link.
    //
    // The threshold is deliberately conservative and PROVISIONAL: the DE1's
    // real minimum push cadence across machine phases still needs on-device
    // measurement of RAW (pre-throttle) notification arrivals — the app's
    // WaterLevel/StateInfo logging is post-dedup and understates the true
    // rate. See tasks 5.2 / 8.5 in the harden-de1-ble-reliability change.
    QElapsedTimer m_notificationLiveness;
    static constexpr int NOTIFICATION_STALE_MS = 30000;
};
