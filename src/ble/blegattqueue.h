#pragma once

#include <QBluetoothUuid>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QTimer>

#include <functional>
#include <optional>

/**
 * The one GATT operation queue, shared by every BLE peripheral in the process.
 *
 * ---- Why this exists ----------------------------------------------------
 *
 * Qt serializes GATT work PER CONTROLLER: `readWriteQueue` and `pendingJob` are
 * instance fields of QtBluetoothLE, one instance per QLowEnergyController
 * (qtconnectivity/src/android/bluetooth/.../QtBluetoothLE.java:949-950), and the
 * darwin backend has no cross-peripheral queue either. Nothing in the framework
 * orders one peripheral's operations against another's, on any platform.
 *
 * That is issue #1819. A scale's connect and characteristic discovery ran across
 * the DE1's notification-enable descriptor writes; all three were rejected with
 * DescriptorWriteError, and the app went on to report the machine CONNECTED with
 * STATE_INFO, SHOT_SAMPLE and WATER_LEVELS never enabled — no chart, no shot
 * detection, no stop-at-weight, and a shot that ran until stopped by hand.
 *
 * de1app has one queue for the DE1 and the scale together (`::de1(cmdstack)`,
 * single `::de1(wrote)` flag) and decaid gets the same property from
 * universal_ble's per-device serialized queue. Both make the state unreachable
 * rather than handling the error better. This is the same move.
 *
 * ---- What is deliberately NOT here --------------------------------------
 *
 * **No timer that decides an operation has finished.** Dispatch is driven by the
 * operation's terminal outcome — noteSucceeded() or noteFailed() — and by
 * teardown. Where a platform can deliver neither (Android's
 * handleOnDescriptorWrite discards a reply arriving after Qt's own 3 s
 * RUNNABLE_TIMEOUT and notifies nothing), the release comes from the requesting
 * transport's EXISTING terminal machinery, which already bounds that operation
 * and verifies real state before acting. A second bound here would recreate the
 * exact condition this change removes: BleTransport's SUBSCRIBE_TIMEOUT_MS was
 * 3000 against Qt's own 3000, and it turned a DescriptorWriteError that arrived
 * at +45 ms into a 3 s stall, three times in one connect.
 *
 * The one timer below is the PACING timer, and it is not a guard — it is the
 * "minimum interval between operations" that already existed for the DE1. It
 * decides when the next operation may start, never whether the previous one
 * finished.
 *
 * ---- Policy travels with the operation ----------------------------------
 *
 * Retry budget, retry delay and pacing are per Operation, not per queue. The
 * DE1's tuned values (MAX_WRITE_RETRIES=5 and the corpus derivation behind it,
 * WRITE_RETRY_DELAY_MS, the 50 ms pacing) stay attached to DE1 operations, and
 * everything else defaults to zero retries — which is exactly what the scale and
 * refractometer transports did before they had a queue at all.
 *
 * Defaulting scales to the DE1's budget would be actively harmful, not merely
 * generous: a dead scale link would hold the shared slot through the DE1's
 * ~32 s worst-case retry sequence, starving the machine the budget exists to
 * protect.
 */
namespace BleGatt {
/**
 * The outer bound for service and characteristic DISCOVERY, as opposed to a
 * read or a write.
 *
 * Shared by every transport rather than declared per class: it answers one
 * question, and the answer is a property of the radio, not of who is asking.
 * Characteristic discovery took 6.0 s in the #1819 capture (23.13 s to 29.14 s),
 * so a write-sized budget would not be a bound on it — it would be a guarantee
 * of failure.
 *
 * Like every clock in this subsystem it decides only "no answer at all is also
 * an answer". It is not a second opinion about an operation the platform does
 * answer; those end on their own terminal signals.
 */
inline constexpr int DISCOVERY_TIMEOUT_MS = 20000;
}  // namespace BleGatt

class BleGattQueue : public QObject {
    Q_OBJECT

public:
    /**
     * Opaque per-transport identity: the transport's own address.
     *
     * Stable for its lifetime, unique among live transports, and never
     * dereferenced — it is only ever compared. A transport must call
     * forget(this) before it dies so nothing outlives it in the queue; the
     * address could otherwise be reused by a later allocation.
     */
    using Requester = const void*;

    struct Policy {
        // 0 means "do not retry", which is the default and is what every
        // non-DE1 caller wants: it reproduces the fire-and-forget behaviour the
        // scale and refractometer transports had before they were queued.
        int maxRetries = 0;
        int retryDelayMs = 0;
        // Minimum interval before the NEXT operation may be dispatched. Carried
        // by the operation that just ran, so one device's pacing does not become
        // every device's.
        int paceMsAfter = 0;
    };

    struct Operation {
        Requester requester = nullptr;
        // Discard key. Lets a requester withdraw just its own work for one
        // characteristic (dead profile frames), the way de1app matches on its
        // per-entry comment string. Null when the operation is not discardable.
        QBluetoothUuid key;
        // Short human-readable label for the log — "write a00e", "connect",
        // "discover". Not parsed.
        QString label;
        // Issues the operation to the platform. Called on dispatch, and again on
        // each retry.
        std::function<void()> issue;
        // Called once when the operation is given up on: retries exhausted, or
        // a failure with no retry budget. Not called on teardown discard — a
        // requester tearing down is not told about work it is itself dropping.
        std::function<void()> onAbandoned;
        Policy policy;
    };

    /**
     * The process-wide instance. Every transport uses this one.
     *
     * Tests construct their own instead, so ordering can be asserted without a
     * radio and without sharing state between test functions.
     */
    static BleGattQueue& instance();

    explicit BleGattQueue(QObject* parent = nullptr);

    /** Enqueue at the back. */
    void submit(Operation op);

    /**
     * Enqueue at the front, ahead of everything already waiting.
     *
     * This is the DE1's urgent path (the app-suspend charger write): it must
     * reach the machine before the process is frozen rather than wait out
     * whatever is queued. It still waits for the in-flight operation — the whole
     * point of the queue is that nothing is issued under an outstanding
     * operation — so urgency is expressed as position, never as a bypass.
     */
    void submitFront(Operation op);

    /** The in-flight operation completed successfully. */
    void noteSucceeded(Requester requester);

    /**
     * The in-flight operation failed. Retried if its policy allows, otherwise
     * abandoned (onAbandoned) and the slot released.
     */
    void noteFailed(Requester requester);

    /**
     * Drop every queued operation for this requester and release the slot if it
     * holds it. Called when a transport disconnects or is destroyed.
     *
     * Returns the number dropped, counting the in-flight operation when it
     * belonged to this requester — the caller is tearing down and needs to know
     * what never reached the device.
     */
    qsizetype forget(Requester requester);

    /**
     * Drop this requester's queued operations whose key is in `keys`. Does NOT
     * touch the in-flight operation, and does not count it.
     *
     * The asymmetry with forget() is deliberate and predates this class: a
     * caller withdrawing work it queued itself is not cancelling a write already
     * dispatched, which has either landed or is being retried. Saying otherwise
     * would report a cancellation that did not happen.
     */
    qsizetype discard(Requester requester, const QList<QBluetoothUuid>& keys);

    /** True while an operation is outstanding, for any requester. */
    bool isBusy() const { return m_inFlight.has_value(); }

    /** Queued (not in-flight) operations, all requesters. */
    qsizetype pendingCount() const { return m_queue.size(); }

    /** Queued operations belonging to one requester. */
    qsizetype pendingCount(Requester requester) const;

    /** The requester holding the slot, or nullptr. */
    Requester inFlightRequester() const;

    /** Label of the in-flight operation, or empty. For logging only. */
    QString inFlightLabel() const;

    /**
     * The discard key of the in-flight operation, or a null UUID.
     *
     * Lets a requester tell "this reply ends the operation I am holding the
     * slot for" from "this is a late reply for one already abandoned". Without
     * it, a stray ACK releases whatever holds the slot now — the misattribution
     * BleTransport's old CCCD ACK matcher existed to prevent.
     */
    QBluetoothUuid inFlightKey() const;

signals:
    /**
     * Nothing is in flight and nothing is queued, for any device.
     *
     * The release event for work that must not compete with GATT traffic for
     * the radio — a BLE connect, which is not itself a queued operation. This is
     * what lets a caller wait for "the radio is clear" without polling for it.
     */
    void drained();

#ifdef DECENZA_TESTING
    friend class tst_BleGattQueue;
    // tst_BleCommandQueue asserts what BleTransport puts IN the queue — order,
    // urgent placement, discard scoping, the retry policy each operation
    // carries. That is queue contents, so it needs the same access.
    friend class tst_BleCommandQueue;
    // tst_BleTransportError drives BleTransport's retry-exhaustion path for
    // real. Reaching it needs the in-flight operation put one failure from
    // exhaustion, which is 2.5 s of retry delay to walk in real time.
    friend class tst_BleTransportError;
#endif

private:
    void dispatchNext();
    void scheduleDispatch();
    void reportDepth();
    // Emits drained() when the slot has just been released and nothing remains.
    // Called from every release path; never from dispatch.
    void emitDrainedIfIdle();

    QQueue<Operation> m_queue;
    std::optional<Operation> m_inFlight;
    int m_retryCount = 0;
    // Bumped on EVERY slot transition (dispatch, success, abandon, teardown).
    // A delayed retry captures it and reissues only if it still matches, so a
    // retry whose operation was dropped mid-delay cannot fire against whatever
    // took the slot next.
    quint64 m_generation = 0;

    // Single-shot, and armed for exactly two reasons: the pacing interval the
    // previous operation asked for, and the retry delay of an operation about to
    // be reissued. It never decides that something finished.
    QTimer m_dispatchTimer;

    // Edge-triggered depth reporting. de1app warns at the same depth and also
    // sheds nothing — a depth report is a diagnosis, not a policy. Re-arms only
    // once well clear, so a queue hovering at the boundary does not log on every
    // other submit.
    bool m_depthReported = false;
    static constexpr qsizetype QUEUE_DEPTH_WARN = 20;
};
