#include "scalebletransport.h"

#include "../bluetoothlogging.h"

// [Bluetooth], like the queue itself: an ordering decision that delayed one
// device because another held the slot is not a fault of either, and filing it
// under [Scale] would send a reader looking in the wrong file.
#define SQ_LOG(msg) BT_LOG_TAGGED("GattQueue", msg)

ScaleBleTransport::ScaleBleTransport(QObject* parent, BleGattQueue* queue)
    : QObject(parent)
    , m_gattQueue(queue ? queue : &BleGattQueue::instance())
{
    m_operationTimeoutTimer.setSingleShot(true);
    connect(&m_operationTimeoutTimer, &QTimer::timeout, this, [this]() {
        if (!holdsGattSlot()) return;
        SQ_LOG(QString("no answer for %1 within %2 ms — releasing the slot")
                   .arg(heldGattKey().toString().mid(1, 8))
                   .arg(m_operationTimeoutTimer.interval()));
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
        SQ_LOG(QString("%1 failed and was not retried").arg(label));
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
    m_gattQueue->noteSucceeded(this);
}

void ScaleBleTransport::failGattOperation() {
    if (!holdsGattSlot()) return;
    m_operationTimeoutTimer.stop();
    m_gattQueue->noteFailed(this);
}

void ScaleBleTransport::releaseGattQueue() {
    m_operationTimeoutTimer.stop();
    m_gattQueue->forget(this);
}

bool ScaleBleTransport::holdsGattSlot() const {
    return m_gattQueue->inFlightRequester() == this;
}

QBluetoothUuid ScaleBleTransport::heldGattKey() const {
    return holdsGattSlot() ? m_gattQueue->inFlightKey() : QBluetoothUuid();
}
