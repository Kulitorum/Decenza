#include "blegattqueue.h"

#include "blegattlogging.h"

#include <QTimer>

#include <algorithm>

BleGattQueue& BleGattQueue::instance() {
    static BleGattQueue queue;
    return queue;
}

BleGattQueue::BleGattQueue(QObject* parent)
    : QObject(parent)
{
    m_clock.start();
}

void BleGattQueue::submit(Operation op) {
    if (!validate(op)) return;
    op.enqueuedAtMs = nowMs();
    m_queue.enqueue(std::move(op));
    reportDepth();
    scheduleDispatch();
}

void BleGattQueue::submitFront(Operation op) {
    if (!validate(op)) return;
    op.enqueuedAtMs = nowMs();
    m_queue.prepend(std::move(op));
    reportDepth();
    scheduleDispatch();
}

bool BleGattQueue::validate(const Operation& op) {
    // Rejected at SUBMIT, loudly, rather than at dispatch. An operation with no
    // requester can never be completed — noteSucceeded/noteFailed match on the
    // requester — and one with no issue callback would take the slot, issue
    // nothing, and be ended by nothing: the queue stops for every device,
    // permanently, with no error anywhere. That is the worst failure this class
    // has, and it is a programming mistake with no runtime cause, so the right
    // place to catch it is where the caller can be named.
    if (!op.requester) {
        GQ_WARN(QString("An internal BLE operation (%1) was submitted with no owner "
                        "and has been dropped. This is a bug; the operation will "
                        "never run. Nothing else is affected.")
                    .arg(op.label.isEmpty() ? QStringLiteral("unlabelled") : op.label));
        return false;
    }
    if (!op.issue) {
        GQ_WARN(QString("An internal BLE operation (%1) was submitted with nothing to "
                        "do and has been dropped. This is a bug; running it would have "
                        "stalled all Bluetooth traffic until the app restarted.")
                    .arg(op.label.isEmpty() ? QStringLiteral("unlabelled") : op.label));
        return false;
    }
    return true;
}

void BleGattQueue::scheduleDispatch() {
    // Nothing to do while an operation is outstanding: its terminal outcome is
    // what drives the queue forward. Nothing to do either while a dispatch is
    // already posted and has not run.
    if (m_inFlight.has_value() || m_dispatchPosted || m_queue.isEmpty())
        return;

    // Posted, never called inline. A terminal-outcome handler that released the
    // slot and re-entered dispatch on the same stack would let a device recurse
    // into its own next operation beneath its own callback — the re-entrancy
    // class that makes a nested event loop under a QML signal handler fatal
    // (see QML_GOTCHAS.md).
    //
    // A queued invocation, not a zero-interval timer: both hop the event loop,
    // but only one of them says so. There is no duration here to get wrong.
    m_dispatchPosted = true;
    QMetaObject::invokeMethod(this, [this]() { dispatchNext(); }, Qt::QueuedConnection);
}

void BleGattQueue::dispatchNext() {
    m_dispatchPosted = false;
    if (m_inFlight.has_value()) return;
    if (m_queue.isEmpty()) {
        // Emptied between the post and now — a discard, or a teardown. This is
        // the transition to idle just as much as a completion is, and the only
        // one no mutator observes directly.
        emitDrainedIfIdle();
        return;
    }

    m_inFlight = m_queue.dequeue();
    m_retryCount = 0;
    m_retryPending = false;
    m_inFlightSince = nowMs();
    ++m_generation;

    // Logged only for an operation that actually WAITED behind another device.
    // That is the whole readership of this line: it is the per-operation detail
    // under a FOREIGN_WAIT_WARN episode, naming what was delayed and how deep
    // the queue was when it finally ran.
    //
    // Queue DEPTH is deliberately not a trigger, only a payload. Depth is a
    // proxy for delay and a bad one, because the app's own connect sequence is
    // the deepest thing that ever happens here: applyAllSettings enqueues the
    // whole settings blast the moment the DE1 reports ready, and a measured,
    // entirely healthy tablet connect peaks at 35. QUEUE_DEPTH_WARN is 40, so
    // any depth trigger below 35 fires on every launch, and one between 35 and
    // 40 is a five-wide window that a single new setting closes. There is no
    // value of it that is quiet on a good connect and loud on a bad one.
    //
    // Foreign wait has no such problem: it is the delay itself, it is zero on
    // an uncontended queue however deep, and it is already the quantity the WARN
    // is computed from. Half of FOREIGN_WAIT_WARN_MS so a reader sees the
    // near-misses around an episode, not only the operations that crossed it.
    //
    // (Fourth attempt. The first logged unconditionally — 97.8% of a 7-hour
    // device log. The second collapsed repeats but still fired on the routine
    // once-a-minute overlap of the DE1 keepalive and the scale heartbeat,
    // ~120 lines/hour. The third gated on depth >= 2, which was silent at idle
    // but restored 43 lines to every single launch — a healthy 4h17m field
    // session logged 50 of these and not one of them said anything the single
    // WARN episode line did not. Each looked obviously sufficient when written,
    // and only a real device log settled it. Do not reintroduce a depth
    // trigger without a field log showing what it would have caught.)
    if (m_inFlight->foreignWaitMs >= BleGatt::FOREIGN_WAIT_WARN_MS / 2) {
        const QString msg = QString("dispatch %1 (%2 queued)")
                                .arg(m_inFlight->label)
                                .arg(m_queue.size());
        LogCollapse::Collapsed collapsed;
        if (m_dispatchLog.shouldLog(m_inFlight->label, msg, nowMs(), &collapsed))
            GQ_LOG(msg + LogCollapse::suffix(collapsed));
    }

    // Accumulated, not reported here. Reported once when the queue goes idle —
    // see reportForeignWaitEpisode(). One contended connect produced six of
    // these lines in 900 ms on real hardware, which teaches a reader to skip the
    // line that is supposed to mean something is wrong.
    if (m_inFlight->foreignWaitMs >= BleGatt::FOREIGN_WAIT_WARN_MS) {
        ++m_foreignWaitCount;
        if (m_inFlight->foreignWaitMs > m_foreignWaitWorstMs) {
            m_foreignWaitWorstMs = m_inFlight->foreignWaitMs;
            m_foreignWaitWorstLabel = m_inFlight->label;
        }
    }

    // The issue callback runs with the slot already held, so anything it
    // submits re-entrantly queues behind rather than being dispatched under it.
    //
    // Copied out of m_inFlight before it is called. The callback can reach back
    // in and clear the slot on the same stack — noteFailed() from a guard that
    // never reached the platform, forget() from a consumer that tears the link
    // down inside connected() — and either would destroy the std::function
    // whose body is currently running, which is undefined behaviour. A local
    // copy outlives the call.
    const std::function<void()> issue = m_inFlight->issue;
    issue();
}

void BleGattQueue::chargeForeignWait() {
    if (m_queue.isEmpty()) return;

    const qint64 now = nowMs();
    const Requester holder = m_inFlight.has_value() ? m_inFlight->requester : nullptr;
    for (Operation& op : m_queue) {
        if (op.requester == holder) continue;
        // From whichever came LATER — this operation being queued, or the
        // in-flight one starting. Charging the full age of the in-flight
        // operation would credit an operation with waiting through a discovery
        // that was already half over when it arrived.
        const qint64 from = std::max(op.enqueuedAtMs, m_inFlightSince);
        const qint64 waited = now - from;
        if (waited > 0) op.foreignWaitMs += waited;
    }
}

void BleGattQueue::noteSucceeded(Requester requester) {
    // A late or duplicate completion for an operation that is no longer in
    // flight must not release someone else's slot. This is the same
    // misattribution BleTransport::onDescriptorWritten guards against when a
    // reply arrives for a characteristic the sequence has already moved past.
    if (!m_inFlight.has_value() || m_inFlight->requester != requester) return;

    chargeForeignWait();
    m_inFlight.reset();
    m_retryCount = 0;
    m_retryPending = false;
    ++m_generation;

    scheduleDispatch();
    emitDrainedIfIdle();
}

void BleGattQueue::noteFailed(Requester requester) {
    if (!m_inFlight.has_value() || m_inFlight->requester != requester) return;

    // A second failure report arriving INSIDE the retry delay must not arm a
    // second timer. Two DE1 reporters can fire in the same window — the service
    // errorOccurred arm and onServiceStateChanged(InvalidService), which is what
    // a link dropping mid-retry produces — and both captured the same generation,
    // so both passed the guard and both called issue(). That writes the payload
    // twice, and the duplicate's late ACK can release a LATER operation on the
    // same characteristic while it is still on the wire.
    if (m_retryPending) return;

    if (m_retryCount < m_inFlight->policy.maxRetries) {
        ++m_retryCount;
        m_retryPending = true;
        GQ_LOG(QString("retry %1/%2 for %3")
                   .arg(m_retryCount)
                   .arg(m_inFlight->policy.maxRetries)
                   .arg(m_inFlight->label));
        // Keeps the slot across the retry delay. Releasing it and re-queueing
        // would let another device's operation land between a retry and its
        // predecessor, which is precisely the interleaving the retry is trying
        // to recover from.
        //
        // Generation-guarded: forget() can clear the slot while this delay is
        // running, and a bare `if (m_inFlight)` would then re-issue whatever
        // operation had since taken it — a torn-down transport's retry firing
        // as another device's write. The counter changes on every slot
        // transition, so the only thing this can reissue is the operation that
        // asked for it.
        const quint64 generation = m_generation;
        QTimer::singleShot(m_inFlight->policy.retryDelayMs, this, [this, generation]() {
            if (m_generation != generation || !m_inFlight.has_value()) return;
            m_retryPending = false;
            // Copied for the same reason as in dispatchNext().
            const std::function<void()> issue = m_inFlight->issue;
            issue();
        });
        return;
    }

    chargeForeignWait();

    Operation done = *m_inFlight;
    m_inFlight.reset();
    m_retryCount = 0;
    m_retryPending = false;
    ++m_generation;

    if (done.onAbandoned) done.onAbandoned();

    scheduleDispatch();
    emitDrainedIfIdle();
}

qsizetype BleGattQueue::forget(Requester requester) {
    qsizetype dropped = 0;

    QQueue<Operation> kept;
    for (const Operation& op : std::as_const(m_queue)) {
        if (op.requester == requester)
            ++dropped;
        else
            kept.enqueue(op);
    }
    m_queue.swap(kept);

    if (m_inFlight.has_value() && m_inFlight->requester == requester) {
        chargeForeignWait();
        ++dropped;
        m_inFlight.reset();
        m_retryCount = 0;
        m_retryPending = false;
        ++m_generation;
    }

    if (dropped > 0) {
        GQ_LOG(QString("dropped %1 operation(s) for a torn-down transport")
                   .arg(dropped));
    }

    // Whatever else was waiting is now eligible, and the whole point of
    // releasing on teardown is that a dead link does not hold the stack.
    scheduleDispatch();
    emitDrainedIfIdle();
    return dropped;
}

qsizetype BleGattQueue::discard(Requester requester, const QList<QBluetoothUuid>& keys) {
    if (keys.isEmpty() || m_queue.isEmpty()) return 0;

    const qsizetype before = m_queue.size();
    QQueue<Operation> kept;
    for (const Operation& op : std::as_const(m_queue)) {
        if (op.requester == requester && keys.contains(op.key))
            continue;
        kept.enqueue(op);
    }
    m_queue.swap(kept);

    const qsizetype dropped = before - m_queue.size();
    if (dropped > 0) {
        GQ_LOG(QString("discarded %1 queued operation(s) for %2 characteristic(s)")
                   .arg(dropped)
                   .arg(keys.size()));
    }
    // The queue can be empty now with nothing in flight, and no other path will
    // notice: dispatchNext() covers the case where a dispatch was already
    // posted, and this covers the case where none was.
    emitDrainedIfIdle();
    return dropped;
}

qsizetype BleGattQueue::pendingCount(Requester requester) const {
    qsizetype n = 0;
    for (const Operation& op : std::as_const(m_queue))
        if (op.requester == requester) ++n;
    return n;
}

BleGattQueue::Requester BleGattQueue::inFlightRequester() const {
    return m_inFlight.has_value() ? m_inFlight->requester : nullptr;
}

QBluetoothUuid BleGattQueue::inFlightKey() const {
    return m_inFlight.has_value() ? m_inFlight->key : QBluetoothUuid();
}

QString BleGattQueue::inFlightLabel() const {
    return m_inFlight.has_value() ? m_inFlight->label : QString();
}

void BleGattQueue::reportForeignWaitEpisode() {
    if (m_foreignWaitCount == 0) return;

    // WARN and self-contained: these logs are read by users and by their AI
    // assistants, who have no knowledge of this subsystem. This is the one cost
    // the shared queue introduced over the per-device queues it replaced, so a
    // reader has to be able to see it rather than infer it — but once per
    // episode, with the worst case named, rather than once per operation.
    GQ_WARN(QString("%1 Bluetooth operation(s) were delayed because another device "
                    "was using the radio; the worst (%2) waited %3 ms. One operation "
                    "runs at a time across the machine, the scale and the "
                    "refractometer, so a device that is slow to answer delays the "
                    "others. Some delay is normal while devices are connecting; if "
                    "this appears during a shot it may have delayed the stop.")
                .arg(m_foreignWaitCount)
                .arg(m_foreignWaitWorstLabel)
                .arg(m_foreignWaitWorstMs));

    m_foreignWaitCount = 0;
    m_foreignWaitWorstMs = 0;
    m_foreignWaitWorstLabel.clear();
}

void BleGattQueue::emitDrainedIfIdle() {
    if (m_inFlight.has_value() || !m_queue.isEmpty()) return;

    // Idle is the end of a contention episode, so this is where it is summed up.
    // Before the drained() post, not inside it: drained() is suppressed when one
    // is already pending, and the episode must be reported either way.
    reportForeignWaitEpisode();
    // Collapsed to one emission per idle transition. Two paths can observe the
    // same transition — discard() emptying the queue, and the dispatch it had
    // already posted then finding it empty — and a consumer that acts on
    // drained() (BLEManager starts a scale connect) must not be told twice
    // about one event.
    if (m_drainedPosted) return;
    m_drainedPosted = true;

    // Posted for the same reason dispatch is, and one more: forget() is reached
    // from transport DESTRUCTORS, and a consumer of drained() connects a scale —
    // which would run against a half-destroyed object on the destructor's own
    // stack. Emitting queued moves it past the teardown.
    //
    // Re-checked at delivery: anything may have been submitted in between, and a
    // drained() that arrives about a busy queue is exactly the wrong answer for
    // a caller deciding whether the radio is free.
    QMetaObject::invokeMethod(this, [this]() {
        m_drainedPosted = false;
        if (m_inFlight.has_value() || !m_queue.isEmpty()) return;
        emit drained();
    }, Qt::QueuedConnection);
}

void BleGattQueue::reportDepth() {
    if (m_queue.size() >= QUEUE_DEPTH_WARN) {
        if (!m_depthReported) {
            m_depthReported = true;
            // WARN and self-contained: these logs are read by users and by their
            // AI assistants, who have no knowledge of this subsystem.
            GQ_WARN(QString("%1 Bluetooth operations are queued at once across the "
                            "machine, the scale and the refractometer. They run one "
                            "at a time, so the ones at the back will wait. This is "
                            "more than a normal connect queues, which means work is "
                            "being submitted faster than the radio can retire it.")
                        .arg(m_queue.size()));
        }
    } else if (m_queue.size() <= QUEUE_DEPTH_WARN / 2) {
        m_depthReported = false;
    }
}
