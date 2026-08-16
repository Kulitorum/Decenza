#pragma once

#include "blegattqueue.h"
#include "de1transport.h"

#include <QBluetoothDeviceInfo>
#include <QElapsedTimer>
#include <QLowEnergyController>
#include <QLowEnergyDescriptor>
#include <QLowEnergyService>
#include <QTimer>
#include <functional>

/**
 * BLE transport for DE1 communication.
 *
 * Implements DE1Transport using QLowEnergyController (Bluetooth Low Energy).
 * Handles service discovery, characteristic subscriptions, write retries, and
 * the DE1's half of the process-wide GATT queue.
 *
 * Every GATT operation this class issues — writes, reads, and the CCCD writes
 * that enable notifications — goes through BleGattQueue, which is shared with
 * the scale and refractometer transports. Nothing here dispatches to the
 * platform while any device has an operation outstanding. See blegattqueue.h
 * for why that is process-wide rather than per-device (#1819).
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
    /**
     * @param queue  The GATT queue to submit to. Defaults to the process-wide
     *               instance, which is what production wants. Tests inject their
     *               own so ordering can be asserted without sharing state
     *               between test functions.
     */
    explicit BleTransport(QObject* parent = nullptr, BleGattQueue* queue = nullptr);
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
    // tst_BleCommandQueue pins the queue behaviour this transport is
    // responsible for — FIFO order, urgent-write placement, UUID-scoped
    // discard, clearQueue()'s in-flight accounting, the depth warning, and the
    // retry budget. It was written against the private command queue that
    // preceded BleGattQueue so the move to the shared queue could be checked
    // against a recorded baseline rather than against a reading of the new
    // code.
    friend class tst_BleCommandQueue;
#endif

private slots:
    void onControllerConnected();
    void onControllerDisconnected();
    void onControllerError(QLowEnergyController::Error error);
    void onServiceDiscovered(const QBluetoothUuid& uuid);
    void onServiceDiscoveryFinished();
    void onServiceStateChanged(QLowEnergyService::ServiceState state);
    void onCharacteristicChanged(const QLowEnergyCharacteristic& c, const QByteArray& value);
    // Split from onCharacteristicChanged, which the two signals used to share.
    // A read RESPONSE ends the operation holding the queue slot; an unsolicited
    // notification does not, and releasing the slot on one would free a write
    // still in the air. The conflation was invisible before there was a slot to
    // free.
    void onCharacteristicRead(const QLowEnergyCharacteristic& c, const QByteArray& value);
    void onCharacteristicWritten(const QLowEnergyCharacteristic& c, const QByteArray& value);
    void onDescriptorWritten(const QLowEnergyDescriptor& descriptor, const QByteArray& value);

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

    // -- The DE1's half of the shared GATT queue --------------------------
    //
    // Four submitters, one skeleton. Each submitter decides only what to issue
    // and what giving up means; everything common — the requester tag, the
    // discard key, the retry policy, and arming the operation timeout — is in
    // operationFor().

    /**
     * The common Operation skeleton. The caller fills in onAbandoned and
     * submits.
     *
     * @param timeoutMs  How long this operation may hold the slot with no
     *                   platform answer at all. Armed on dispatch and again on
     *                   every retry; stopped by whichever terminal outcome
     *                   arrives first.
     */
    BleGattQueue::Operation operationFor(const QBluetoothUuid& key,
                                         const QString& verb,
                                         std::function<void()> issue,
                                         int timeoutMs = WRITE_TIMEOUT_MS);
    void submitWrite(const QBluetoothUuid& uuid, const QByteArray& data, bool toFront);
    void submitRead(const QBluetoothUuid& uuid);
    /**
     * Enable notifications for one characteristic.
     *
     * @param required  A stream the machine is unusable without. STATE_INFO and
     *                  SHOT_SAMPLE are the two: with either missing there is no
     *                  phase detection, no chart, no shot detection and no
     *                  stop-at-weight — the state #1819 reported as CONNECTED.
     *                  Failing to enable a required stream fails the connection
     *                  rather than proceeding without it.
     */
    void submitSubscribe(const QBluetoothUuid& uuid, bool required);
    /** Report a required stream as unusable and fail the connection attempt. */
    void failRequiredStream(const QBluetoothUuid& uuid);
    /**
     * A queue entry that issues nothing and completes itself.
     *
     * It is how "every subscription above has been confirmed" is expressed
     * without a flag: FIFO ordering puts it after the subscribes, and a
     * required-stream failure calls forget(), which drops it along with the
     * rest of this transport's queued work. Reaching it IS the confirmation.
     */
    void submitReadyMarker();

    /** True when the slot holds this transport's operation for `uuid`. */
    bool ownsInFlight(const QBluetoothUuid& uuid) const;
    /** Terminal success for the operation on `uuid`; no-op if it isn't ours. */
    void completeOperation(const QBluetoothUuid& uuid);
    /** Emit queueDrained() if this transport has nothing left anywhere. */
    void emitQueueDrainedIfIdle();
    /** The CCCD of `uuid`, or an invalid descriptor if there isn't one. */
    QLowEnergyDescriptor cccdFor(const QBluetoothUuid& uuid) const;

    // Never null after construction: the injected queue, or the process-wide
    // instance. Not owned.
    BleGattQueue* m_gattQueue = nullptr;

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

    // -- Retry policy, carried by every operation this transport submits ---
    //
    // The budget belongs to the LINK, not to the operation type: a read, a
    // characteristic write and a CCCD write all fail for the same reasons on
    // the same radio, so they share one policy rather than three. (Reads did
    // not retry before the shared queue, because nothing tracked them well
    // enough to retry them. They are idempotent GETs and a lost one used to
    // leave the app with no firmware version or no initial state.)
    //
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
    // count(N+1)), and "FAILED after 10 retries" for exhaustions. Every
    // exhaustion path shared one counter and emitted the same "retrying N/10"
    // text, so a cycle could start on one and end on another; they are
    // deliberately counted together.
    //
    // Those are the marker strings in the CORPUS, which is fixed and predates
    // the shared queue. Retries are counted by BleGattQueue now and logged by
    // it as "[Bluetooth][GattQueue] retry N/5 for <verb> <uuid>"; a
    // re-derivation over logs captured since must grep for that instead. The
    // counting rule is unchanged — one counter, every failure path.
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
    static constexpr int WRITE_TIMEOUT_MS = 5000;
    static constexpr int WRITE_RETRY_DELAY_MS = 500;

    // The one clock this transport owns, and the only thing that can end an
    // operation the platform never answers at all.
    //
    // It is deliberately NOT a second bound on an operation the platform DOES
    // answer: every real terminal outcome (characteristicWritten,
    // characteristicRead, descriptorWritten, and the service error signals)
    // stops it. The predecessor of this timer raced Qt's own 3 s
    // RUNNABLE_TIMEOUT on the same CCCD write and turned a DescriptorWriteError
    // that arrived at +45 ms into a 3 s stall, three times in one connect
    // (#1819). One clock, and it is the outer one.
    QTimer m_operationTimeoutTimer;

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
    // Reporting once per episode means the line always prints the threshold —
    // and the threshold is not the number that matters. What separated benign
    // from pathological across the corpus was the PEAK run: nine logs at 1,
    // #1713 at 2, then 7, 8, 11 and 89. A reader (or a field AI) seeing only
    // "3" would take the mildest possible reading of the worst possible link.
    //
    // So the run is restated every RESTATE writes past the threshold, and the
    // episode is closed out with its peak either way it ends — recovery or
    // disconnect. Count-based, not timed: this is a periodic restatement of a
    // counter, so a timer would only add a second clock to reason about.
    static constexpr int WRITE_DEAD_LINK_RESTATE = 10;

    // Counts the abandoned write and reports the link as no longer accepting
    // writes when the count passes the bound.
    void noteWriteAbandoned();
    // Called when a write completes. Closes out a reported episode with the
    // run it reached, at INFO — per LOGGING.md the recurring failure is a fault
    // reported at WARN whose resolution sits at DEBUG, leaving the reader with
    // only the failure half.
    void noteWriteSucceeded();
    // Called on disconnect. Same counters, different story: the link went away
    // rather than recovering, and saying "accepting writes again" there would
    // be false.
    void forgetWriteFailureState();

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
