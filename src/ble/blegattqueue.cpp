#include "blegattqueue.h"

#include "bluetoothlogging.h"

#include <QTimer>

// [Bluetooth], not [DE1] or [Scale], for the reason bluetoothlogging.h already
// gives about the adapter: this sits BENEATH every device. An ordering decision
// that delayed the DE1 because a refractometer held the slot is not a DE1 fault
// and not a refractometer fault, and filing it under either sends a reader
// looking in the wrong file.
#define GQ_LOG(msg)  BT_LOG_TAGGED("GattQueue", msg)
#define GQ_WARN(msg) BT_WARN_TAGGED("GattQueue", msg)

BleGattQueue& BleGattQueue::instance() {
    static BleGattQueue queue;
    return queue;
}

BleGattQueue::BleGattQueue(QObject* parent)
    : QObject(parent)
{
    m_dispatchTimer.setSingleShot(true);
    connect(&m_dispatchTimer, &QTimer::timeout, this, &BleGattQueue::dispatchNext);
}

void BleGattQueue::submit(Operation op) {
    m_queue.enqueue(std::move(op));
    reportDepth();
    scheduleDispatch();
}

void BleGattQueue::submitFront(Operation op) {
    m_queue.prepend(std::move(op));
    reportDepth();
    scheduleDispatch();
}

void BleGattQueue::scheduleDispatch() {
    // Nothing to do while an operation is outstanding: its terminal outcome is
    // what drives the queue forward. Nothing to do either while the timer is
    // already armed — it is holding either a pacing interval or a retry delay,
    // and re-arming it here would shorten one of them.
    if (m_inFlight.has_value() || m_dispatchTimer.isActive() || m_queue.isEmpty())
        return;

    // Posted, never called inline. A terminal-outcome handler that released the
    // slot and re-entered dispatch on the same stack would let a device recurse
    // into its own next operation beneath its own callback — the re-entrancy
    // class that makes a nested event loop under a QML signal handler fatal
    // (see QML_GOTCHAS.md). A zero-interval single-shot is the post.
    m_dispatchTimer.start(0);
}

void BleGattQueue::dispatchNext() {
    if (m_inFlight.has_value() || m_queue.isEmpty()) return;

    m_inFlight = m_queue.dequeue();
    m_retryCount = 0;
    ++m_generation;

    GQ_LOG(QString("dispatch %1 (%2 queued)")
               .arg(m_inFlight->label)
               .arg(m_queue.size()));

    // The issue callback runs with the slot already held, so anything it
    // submits re-entrantly queues behind rather than being dispatched under it.
    m_inFlight->issue();
}

void BleGattQueue::noteSucceeded(Requester requester) {
    // A late or duplicate completion for an operation that is no longer in
    // flight must not release someone else's slot. This is the same
    // misattribution BleTransport::onDescriptorWritten guards against when a
    // reply arrives for a characteristic the sequence has already moved past.
    if (!m_inFlight.has_value() || m_inFlight->requester != requester) return;

    const int paceMs = m_inFlight->policy.paceMsAfter;
    m_inFlight.reset();
    m_retryCount = 0;
    ++m_generation;

    if (paceMs > 0 && !m_queue.isEmpty()) {
        m_dispatchTimer.start(paceMs);
        return;
    }
    scheduleDispatch();
}

void BleGattQueue::noteFailed(Requester requester) {
    if (!m_inFlight.has_value() || m_inFlight->requester != requester) return;

    if (m_retryCount < m_inFlight->policy.maxRetries) {
        ++m_retryCount;
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
            if (m_generation == generation && m_inFlight.has_value())
                m_inFlight->issue();
        });
        return;
    }

    Operation done = *m_inFlight;
    m_inFlight.reset();
    m_retryCount = 0;
    ++m_generation;
    m_dispatchTimer.stop();

    if (done.onAbandoned) done.onAbandoned();

    scheduleDispatch();
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
        ++dropped;
        m_inFlight.reset();
        m_retryCount = 0;
        ++m_generation;
        m_dispatchTimer.stop();
    }

    if (dropped > 0) {
        GQ_LOG(QString("dropped %1 operation(s) for a torn-down transport")
                   .arg(dropped));
    }

    // Whatever else was waiting is now eligible, and the whole point of
    // releasing on teardown is that a dead link does not hold the stack.
    scheduleDispatch();
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

void BleGattQueue::reportDepth() {
    if (m_queue.size() >= QUEUE_DEPTH_WARN) {
        if (!m_depthReported) {
            m_depthReported = true;
            // WARN and self-contained: these logs are read by users and by their
            // AI assistants, who have no knowledge of this subsystem.
            GQ_WARN(QString("BLE operation queue is %1 deep across all devices — "
                            "the radio is not keeping up with the commands being "
                            "issued")
                        .arg(m_queue.size()));
        }
    } else if (m_queue.size() <= QUEUE_DEPTH_WARN / 2) {
        m_depthReported = false;
    }
}
