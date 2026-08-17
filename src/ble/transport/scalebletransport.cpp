#include "scalebletransport.h"

#include "../blegattlogging.h"

ScaleBleTransport::ScaleBleTransport(QObject* parent, BleGattQueue* queue)
    : QObject(parent)
    , m_gattQueue(queue ? queue : &BleGattQueue::instance())
{
    m_operationTimeoutTimer.setSingleShot(true);
    connect(&m_operationTimeoutTimer, &QTimer::timeout, this, [this]() {
        if (!holdsGattSlot()) return;
        // WARN, and self-contained: these logs are read by users and by their AI
        // assistants. An operation the platform never answered did not just fail
        // for this device — it held the shared radio for the whole interval, so
        // every other device's traffic was blocked behind it. That is the half a
        // reader would otherwise have to infer.
        GQ_WARN(QString("A %1 operation on this scale/refractometer got no answer "
                        "within %2 ms. The BLE radio was held for that whole time, "
                        "delaying every other device including the machine. "
                        "Reconnecting the device clears it.")
                    .arg(heldGattKey().toString().mid(1, 8))
                    .arg(m_operationTimeoutTimer.interval()));
        onGattSlotReleased();
        m_gattQueue->noteFailed(this);
    });
}

ScaleBleTransport::~ScaleBleTransport() {
    // Nothing may outlive this object in the queue: the requester tag is its
    // address, and a later allocation could reuse it.
    releaseGattQueue();
}

void ScaleBleTransport::submitGattOperation(const QBluetoothUuid& key,
                                            const QString& label,
                                            std::function<void()> issue,
                                            int timeoutMs) {
    BleGattQueue::Operation op;
    op.requester = this;
    op.key = key;
    op.label = label;
    // Policy left at its default: no retries. See the header.
    op.issue = [this, timeoutMs, issue = std::move(issue)]() {
        m_operationTimeoutTimer.start(timeoutMs);
        issue();
    };
    op.onAbandoned = [this, label]() {
        m_operationTimeoutTimer.stop();
        onGattSlotReleased();
        // INFO, not DEBUG: the connections view a user reads filters to INFO, and
        // this is a terminal outcome. Per LOGGING.md the recurring failure is a
        // fault whose resolution — or here, whose conclusion — sits a tier below
        // the noise around it, leaving the reader with half the story.
        GQ_INFO(QString("%1 failed and was not retried").arg(label));
    };
    m_gattQueue->submit(std::move(op));
}

void ScaleBleTransport::completeGattOperation(const QBluetoothUuid& key) {
    if (heldGattKey() != key) return;
    completeGattOperation();
}

void ScaleBleTransport::completeGattOperation() {
    if (!holdsGattSlot()) return;
    m_operationTimeoutTimer.stop();
    onGattSlotReleased();
    m_gattQueue->noteSucceeded(this);
}

void ScaleBleTransport::failGattOperation() {
    if (!holdsGattSlot()) return;
    m_operationTimeoutTimer.stop();
    onGattSlotReleased();
    m_gattQueue->noteFailed(this);
}

void ScaleBleTransport::releaseGattQueue() {
    m_operationTimeoutTimer.stop();
    onGattSlotReleased();
    m_gattQueue->forget(this);
}

bool ScaleBleTransport::holdsGattSlot() const {
    return m_gattQueue->inFlightRequester() == this;
}

QBluetoothUuid ScaleBleTransport::heldGattKey() const {
    return holdsGattSlot() ? m_gattQueue->inFlightKey() : QBluetoothUuid();
}
