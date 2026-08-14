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
    qsizetype discardQueued(const QList<QBluetoothUuid>& uuids) override;
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

#ifdef DECENZA_TESTING
    // tst_BleTransportError drives onControllerError directly to assert the
    // #1658 contract: a controller error raises no user-facing errorOccurred,
    // and the link-teardown family still fires de1LinkFault. That contract is
    // otherwise enforced only by the ABSENCE of an emit, which no test can see.
    friend class tst_BleTransportError;
#endif

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
    // The three audience tiers — see src/ble/de1logging.h. DEBUG for protocol
    // detail, INFO for the connect/disconnect narrative a user reads in the
    // connections view, WARN for problems.
    void log(const QString& message);
    void info(const QString& message);
    void warn(const QString& message);
    // Update m_serviceDiscoveryActive and emit serviceDiscoveryActiveChanged()
    // only on transitions. Coalesces the four reset call sites (chars-ready,
    // disconnect(), onControllerDisconnected(), onControllerError()) so the
    // signal cleanly brackets one discovery window per attempt.
    void setServiceDiscoveryActive(bool active);
    bool setupController(const QBluetoothDeviceInfo& device);
    void setupService();
    void writeCharacteristic(const QBluetoothUuid& uuid, const QByteArray& data);
    void queueCommand(const QBluetoothUuid& uuid, std::function<void()> command);

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

    // Command queue (50ms spacing between BLE writes).
    //
    // Each entry carries the characteristic it targets so a caller can discard
    // just its own pending work — see discardQueued(). Without it the queue is
    // opaque and the only available correction is clearQueue(), which throws
    // away unrelated writes that nothing has superseded. de1app makes the same
    // distinction by matching on a per-entry comment string
    // (remove_matching_ble_queue_entries, de1_comms.tcl:1423, called at 14
    // sites); the UUID is the equivalent handle here and is already in scope at
    // both queueCommand() call sites.
    struct QueuedCommand {
        QBluetoothUuid uuid;
        std::function<void()> run;
    };
    QQueue<QueuedCommand> m_commandQueue;
    QTimer m_commandTimer;
    bool m_writePending = false;

    // Write retry logic (like de1app)
    std::function<void()> m_lastCommand;
    int m_writeRetryCount = 0;

    // Was 10. Measured across 283 retry cycles in the 26-log user-submitted
    // corpus (#1176 … #1810). 12 of those logs carry a "retrying 1/10" line; 14
    // carry at least one exhaustion, and the two extra are head-trimmed captures
    // whose opening retry lines are not present — which is also where the four
    // unattributed exhaustions below come from. Both counts are real; they count
    // different things.
    // Cycles are counted by their "retrying 1/10" line and their outcome by the
    // highest retry they reached:
    //
    //     retries needed   1   2   3   4   5   6   7   8   9   ran out
    //     cycles          10   2   3   1   1   2   1   2   1       260
    //
    // So 23 cycles recovered and 260 — 92% of all retry activity — ran the full
    // budget and failed. (264 exhaustion lines were logged; the extra four are
    // head-trimmed sessions whose "retrying 1/10" is not in the capture. No
    // cycle is observed to recover at retry 10.)
    //
    // The budget is also a TIME bound, and that is what actually broke: at 10
    // retries a timing-out write occupies the link for
    //   11 × WRITE_TIMEOUT_MS + 10 × WRITE_RETRY_DELAY_MS = 60.0 s
    // which is not shorter than the 60 s MMR keepalive period — so on a degraded
    // link the next keepalive is queued before the previous one is abandoned and
    // the link never goes idle (measured dispatch→abandonment in #1691: 60.06 s,
    // with keepalive exhaustions exactly 60.0 s apart). At 5 the worst case is
    //   6 × 5000 + 5 × 500 = 32.5 s
    // leaving the link idle before the next periodic write.
    //
    // 5 rather than 3 (both are in range) retains 17 of the 23 observed
    // recoveries against 15 at 3 — two cycles out of 283, for 11 s more
    // worst-case latency that still clears the keepalive period. That is a
    // narrow margin and the honest reading is that either value is defensible;
    // 5 is the more conservative of the two. Flat, NOT scaled by recent failure
    // history: at 260/283 cycles the failing-link case already dominates, so a
    // graduated budget would buy under a second and cost a second concept.
    //
    // How to re-derive, since the point of the paragraph below is that these
    // numbers must be reproducible: over the extracted corpus, count
    // "retrying 1/10" lines for cycles started, per-N "retrying N/10" line
    // counts for the distribution (recoveries at exactly N = count(N) -
    // count(N+1)), and "FAILED after 10 retries" for exhaustions. Both
    // exhaustion paths (write timeout, CharacteristicWriteError) share
    // m_writeRetryCount and emit the same "retrying N/10" text, so a cycle may
    // start on one and end on the other; they are deliberately counted together.
    //
    // These figures replace an earlier set (434 cycles, 380 exhaustions, "43 of
    // 54 recoveries at a budget of 5") that were stated here as measurement and
    // did not survive re-derivation from the corpus. The QUALITATIVE conclusion
    // was unchanged — if anything the true share running the full budget is
    // higher — but the counts were not reproducible, so they are recorded here
    // as having been wrong rather than quietly swapped.
    //
    // Changing this REQUIRES re-deriving the DE1-fault-cluster weighting in
    // QtScaleBleTransport::onDe1LinkFault, which treats a cascade as two faults
    // and used to justify that by "ten retries is ~5 s of sustained write
    // starvation". That was re-derived when this budget moved — see the
    // COUPLED TO MAX_WRITE_RETRIES block there for the current durations. Do
    // not restate them from here.
    //
    // (An earlier version of this comment added "and shorter cascades also fire
    // more often". That is wrong, and the block it points at says so: the fault
    // threshold is >=2 per 60 s and a cascade already counts 2, so an episode
    // fires on its first cascade at any budget. What changed is that it is
    // detected SOONER, not more often.)
    static constexpr int MAX_WRITE_RETRIES = 5;
    QTimer m_writeTimeoutTimer;
    static constexpr int WRITE_TIMEOUT_MS = 5000;
    static constexpr int WRITE_RETRY_DELAY_MS = 500;
    QString m_lastWriteUuid;
    // The same characteristic in full. m_lastWriteUuid above is an
    // eight-character abbreviation built for log lines; writeAbandoned()
    // carries a real QBluetoothUuid so the device layer can dispatch on it.
    QBluetoothUuid m_lastWriteUuidFull;
    QByteArray m_lastWriteData;

    // Consecutive writes abandoned after exhausting their retries, reset by any
    // successful write and by a disconnect. Recognises a link that has stopped
    // accepting writes while still reporting itself connected — the one state
    // where every other indicator looks healthy: the controller says
    // ConnectedState and notifications can keep arriving, so neither
    // m_notificationLiveness below nor the wedge detector (which gates on
    // !m_de1Connected in evaluateBleWedge, blemanager.cpp:380) can see it.
    //
    // Separation in the corpus is wide: nine logs peak at 1 abandoned write,
    // #1713 at 2, and the pathological ones sit at 7, 8, 11 and 89. The bound
    // is set just above the benign side of that gap. The corpus figure is a
    // PROXY that overestimates — those logs carry no per-write success marker,
    // so runs were computed resetting only at disconnects and session
    // boundaries — which is a further argument for the low end.
    //
    // Reporting only. Nothing is torn down on this signal and no user-facing
    // error is raised; the point is to make the condition visible in a
    // submitted log before deciding what to do about it. (decaid does tear
    // down on its equivalent detector: _maxConsecutiveOpTimeouts = 3 at
    // universal_ble_transport.dart:42, compared at :426, tearing down via
    // _declareLinkDead at :491. That number does not transfer: it counts
    // operations carrying no per-write retries, whereas one unit here is an
    // already-exhausted write.)
    int m_consecutiveWriteFailures = 0;
    bool m_writeDeadLinkReported = false;
    static constexpr int WRITE_DEAD_LINK_THRESHOLD = 3;

    // Edge-triggered so a backed-up queue reports once rather than on every
    // enqueue. de1app warns at 20 (de1_comms.tcl:49) and has no cap either;
    // this is the same signal, and like de1app's it sheds nothing — a depth
    // report is a diagnosis, not a policy.
    bool m_queueDepthReported = false;
    static constexpr qsizetype QUEUE_DEPTH_WARN = 20;

    // Called from both retry-exhaustion sites. Counts the abandoned write and
    // reports the link as no longer accepting writes when the count passes the
    // bound.
    void noteWriteAbandoned();
    // Called wherever a write completes and on disconnect.
    void noteWriteSucceeded();

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
    // writes but has silently stopped delivering push notifications (Decaid
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
