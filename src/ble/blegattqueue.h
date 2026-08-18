#pragma once

#include <QBluetoothUuid>
#include <QObject>
#include "../core/logcollapse.h"

#include <QElapsedTimer>
#include <QQueue>
#include <QString>

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
 * de1app has one queue for the DE1 and the scale together — `::de1(cmdstack)`
 * with a single `::de1(wrote)` in-flight flag (`machine.tcl:132-133`,
 * `de1_comms.tcl:43,56-59`) — so the state is unreachable there rather than
 * handled better. That is the precedent this follows.
 *
 * decaid is NOT a second precedent, and an earlier version of this comment said
 * it was. It sets `UniversalBle.queueType = QueueType.perDevice`
 * (`universal_ble_discovery_service.dart:403`) and its own comment says why:
 * "each BLE peripheral gets its own command queue, so DE1 GATT operations never
 * block scale heartbeat writes and vice versa". That is a deliberate choice to
 * allow the concurrency #1819 is about — a considered counter-example, recorded
 * here rather than quietly dropped, because a reader weighing this design should
 * know a peer app went the other way on purpose.
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
 * **The queue owns no QTimer at all.** Dispatch is posted with a queued
 * invocation rather than a zero-interval timer, which is the same thing said
 * honestly: it is an event-loop hop, not a duration. The only delay left in the
 * class is an operation's own retry backoff, which is a property of that
 * operation and travels with it.
 *
 * ---- Policy travels with the operation ----------------------------------
 *
 * Retry budget and retry delay are per Operation, not per queue. The DE1's
 * tuned values (MAX_WRITE_RETRIES=5 and the corpus derivation behind it, and
 * WRITE_RETRY_DELAY_MS) stay attached to DE1 operations, and everything else
 * defaults to zero retries — which is exactly what the scale and refractometer
 * transports did before they had a queue at all.
 *
 * The DE1's old 50 ms inter-write pacing is NOT carried across. It was armed
 * only on an enqueue that found the link idle, and the completion path
 * dispatched the next command with no delay at all, so it paced the first write
 * after a pause and never consecutive ones. Reproducing it as a real per-write
 * interval would have added ~1 s to a 20-write profile upload for the first
 * time. What kept the link from being flooded is the property below, not that
 * constant.
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

// Report an operation that waited this long behind OTHER devices' work.
//
// 500 ms because a healthy scale or refractometer operation completes in far
// less — the common in-shot case is a 1 Hz Decent Scale heartbeat — so half a
// second of foreign wait already means something is not answering, not that the
// radio is merely busy. High enough that a normal burst is silent; low enough
// that it fires well before the 3 s read/write clock would end the operation
// holding things up.
inline constexpr int FOREIGN_WAIT_WARN_MS = 500;

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
        // Delay before a failed operation is reissued. A backoff, not a guard:
        // it decides when to try again, never whether something finished. Zero
        // with zero retries, which is the default.
        int retryDelayMs = 0;
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
        // Milliseconds this operation spent queued while ANOTHER device held the
        // slot. Accumulated by the queue, never set by callers.
        //
        // Only the foreign part, deliberately. Waiting behind your own queued
        // work is expected and uninteresting — a profile upload is ~20 writes
        // and the last one waits for the other 19. Waiting behind a DIFFERENT
        // device is the one cost this shared queue introduced that separate
        // per-controller queues did not have, and it is the number that decides
        // whether a stop-at-weight was ever actually held up in the field.
        qint64 foreignWaitMs = 0;
        // When this operation was enqueued, on the queue's own clock. Set by
        // submit()/submitFront(), never by callers. Without it the charge below
        // is the AGE of the operation in flight rather than the part of it this
        // operation actually waited through — an operation queued 50 ms before a
        // 6 s discovery ends would be reported as having waited 6 s, and these
        // numbers exist to decide whether a stop-at-weight was really held up.
        qint64 enqueuedAtMs = 0;
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
    // Rejects an unrunnable operation at submit. See the definition.
    static bool validate(const Operation& op);
    void scheduleDispatch();
    void reportDepth();
    // Sums up one contention episode when the queue goes idle.
    void reportForeignWaitEpisode();
    // Charges the time the just-released operation held the slot to every queued
    // operation belonging to a DIFFERENT requester.
    void chargeForeignWait();
    // Emits drained() when nothing is in flight and nothing remains queued.
    // Called from every release path, and from the two places that can empty
    // the queue without one: discard(), and a dispatch that finds the queue
    // already emptied. Re-checks at delivery, so a spurious call is harmless.
    void emitDrainedIfIdle();

    QQueue<Operation> m_queue;
    std::optional<Operation> m_inFlight;
    int m_retryCount = 0;
    // Monotonic source for the foreign-wait measurement. Measurement only: it
    // decides nothing and gates nothing, which is what separates it from the
    // timers this design does not use.
    QElapsedTimer m_clock;

    // Every read of the clock goes through here so a test can advance it instead
    // of sleeping. Same reasoning as LogCollapse::shouldLog taking `nowMs` as a
    // parameter: a class whose behaviour depends on elapsed time is only
    // testable without wall-clock waits if the time is injectable.
    //
    // Asserting a 500 ms threshold by waiting 580 ms is a timer in the test —
    // slow (seconds per suite) and timing-dependent, which is exactly the shape
    // that goes flaky on a loaded runner. The skew is compiled out entirely
    // outside test builds.
#ifdef DECENZA_TESTING
    qint64 m_testClockSkewMs = 0;
    qint64 nowMs() const { return m_clock.elapsed() + m_testClockSkewMs; }
#else
    qint64 nowMs() const { return m_clock.elapsed(); }
#endif
    qint64 m_inFlightSince = 0;

    // A retry timer is armed and has not fired. Suppresses a second
    // failure report for the same attempt; see noteFailed().
    bool m_retryPending = false;
    // Bumped on EVERY slot transition (dispatch, success, abandon, teardown).
    // A delayed retry captures it and reissues only if it still matches, so a
    // retry whose operation was dropped mid-delay cannot fire against whatever
    // took the slot next.
    quint64 m_generation = 0;

    // True between posting a dispatch and running it. The guard a timer's
    // isActive() used to provide, without the timer.
    bool m_dispatchPosted = false;
    // Collapses a repeated dispatch line inside one contention episode.
    //
    // Its original job is gone. The dispatch line used to be emitted for every
    // operation, which at idle made it periodic forever on the Decent Scale's
    // 1 Hz heartbeat — measured on a tablet at 25,992 of 26,565 lines in a
    // 7-hour session, 97.8%, which had evicted every other subsystem's history
    // from the ring buffer. That is now the GATE's doing, not the collapser's:
    // an idle beat never reaches this object at all. The figure is kept because
    // it is why the gate exists, not because it describes what happens here.
    //
    // What is left is narrow but real: one label repeatedly crossing the
    // foreign-wait threshold during a sustained episode — a stalled DE1
    // delaying successive 1 Hz scale heartbeats produces identical text at the
    // same depth. Keyed by label so one device's repeats cannot swallow
    // another's, and changed text always emits at once.
    //
    // EPISODIC, which changes the contract. LogCollapse's own header warns that
    // an episodic source must flush at its run end or a suppressed tally is not
    // merely late but MISATTRIBUTED — stapled onto the next episode's first
    // line hours later, annotated with a span that dates it to now. While the
    // dispatch line was periodic there was no run end and no flush was needed;
    // there is one now, and reportForeignWaitEpisode() is it.
    LogCollapse m_dispatchLog{60 * 1000};

    // One line per CONTENTION EPISODE rather than per delayed operation. A
    // connect with a scale present produced six of these in 900 ms, all saying
    // the same thing, which is how a signal meant to mean "something is wrong"
    // becomes scrollback. Accumulated here and summarised when the queue next
    // goes idle, which is the natural end of an episode.
    qsizetype m_foreignWaitCount = 0;
    qint64 m_foreignWaitWorstMs = 0;
    QString m_foreignWaitWorstLabel;

    // One drained() per idle transition; see emitDrainedIfIdle().
    bool m_drainedPosted = false;

    // Edge-triggered depth reporting. de1app warns at the same depth and also
    // sheds nothing — a depth report is a diagnosis, not a policy. Re-arms only
    // once well clear, so a queue hovering at the boundary does not log on every
    // other submit.
    bool m_depthReported = false;
    // Above a NORMAL connect burst, not above nothing. Measured on a Samsung
    // SM-X210 with a DE1 and a Decent Scale connecting together: the DE1's
    // startup sequence — five subscribes, the ready marker, four reads, then the
    // initial settings writes — peaks the shared queue at 35. At the old
    // threshold of 20 this fired on every single connect, and said "the radio is
    // not keeping up" about a radio that drained all 35 in ~1.5 s. A warning
    // that is guaranteed on a healthy start is one a reader learns to skip.
    //
    // 40 leaves headroom over that measured peak, so what reaches this now is
    // work piling up faster than it drains rather than one device starting up.
    static constexpr qsizetype QUEUE_DEPTH_WARN = 40;
};
