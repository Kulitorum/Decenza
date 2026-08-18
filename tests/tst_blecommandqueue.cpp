#include <QtTest>
#include <QSignalSpy>

#include "ble/blegattqueue.h"
#include "ble/bletransport.h"
#include "ble/protocol/de1characteristics.h"

// What BleTransport puts on the shared GATT queue (#1819).
//
// This file was written against the private command queue that preceded
// BleGattQueue, so the move to the shared queue could be checked against a
// RECORDED baseline rather than against a reading of the new code. Every
// assertion below passed before the move. Three did not survive it unchanged,
// and each one says so at its own site with the evidence — that is the file
// working, not the file being talked into a new baseline.
//
// Headless by construction: with no QLowEnergyService, a dispatched write
// reaches writeCharacteristic()'s `!m_service` guard, which reports the failure
// to the queue. The queue then holds the slot across the operation's retry
// delay, which is what the in-flight cases below use to get an operation into
// the slot without a radio.
//
// Every test builds its own BleGattQueue and injects it. The production default
// is the process-wide instance, and sharing that between test functions would
// leak one test's queue contents into the next.
class tst_BleCommandQueue : public QObject {
    Q_OBJECT

private:
    // Three distinct real characteristics, so discard scoping is asserted
    // against UUIDs the DE1 actually uses rather than invented ones.
    static QBluetoothUuid frameWrite()   { return DE1::Characteristic::FRAME_WRITE; }
    static QBluetoothUuid headerWrite()  { return DE1::Characteristic::HEADER_WRITE; }
    static QBluetoothUuid writeToMmr()   { return DE1::Characteristic::WRITE_TO_MMR; }

    static QByteArray payload(char tag) { return QByteArray(4, tag); }

    // Run the event loop long enough for the queue's posted dispatch. The
    // dispatch is a zero-interval single-shot; 20 ms is far inside the 500 ms
    // retry delay that then holds the slot.
    static void dispatch() { QTest::qWait(20); }

private slots:
    void init() { QTest::failOnWarning(); }

    // --- FIFO and dispatch ------------------------------------------------

    void writesQueueInSubmissionOrder() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(headerWrite(), payload('h'));
        t.write(frameWrite(), payload('1'));
        t.write(frameWrite(), payload('2'));

        QCOMPARE(queue.pendingCount(), qsizetype(3));
        QCOMPARE(queue.m_queue.at(0).key, headerWrite());
        QCOMPARE(queue.m_queue.at(1).key, frameWrite());
        QCOMPARE(queue.m_queue.at(2).key, frameWrite());
    }

    // Not dispatched inline. Dispatching under the submitting call would let a
    // completion handler recurse into its own next operation, and would defeat
    // the serialization for the first operation of every burst.
    void aSubmittedOperationIsPostedRatherThanDispatchedInline() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(frameWrite(), payload('1'));

        QVERIFY(!queue.isBusy());
        QCOMPARE(queue.pendingCount(), qsizetype(1));
    }

    // Submitted AFTER the first is already in flight, which is what makes this
    // reach the guard: a submit that arrives while the dispatch timer is still
    // armed is held back by the timer alone, so the in-flight check is never
    // consulted and the slot would pass this test even with the check gone.
    // Verified by removing it and watching this go red.
    void submittingWhileAnOperationIsInFlightLeavesItQueued() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(frameWrite(), payload('1'));
        dispatch();
        QVERIFY(queue.isBusy());
        const QBluetoothUuid held = queue.inFlightKey();

        t.write(writeToMmr(), payload('2'));
        dispatch();  // give an unguarded dispatch every chance to fire

        QCOMPARE(queue.pendingCount(), qsizetype(1));
        QCOMPARE(queue.inFlightKey(), held);
    }

    void dispatchTakesExactlyOneOperation() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(headerWrite(), payload('h'));
        t.write(frameWrite(), payload('1'));

        dispatch();

        QCOMPARE(queue.inFlightKey(), headerWrite());
        QCOMPARE(queue.pendingCount(), qsizetype(1));
        QCOMPARE(queue.m_queue.at(0).key, frameWrite());
    }

    // A retry holds the slot rather than releasing and re-queueing. Releasing
    // would let another device's operation land between a retry and its
    // predecessor, which is the interleaving the retry is trying to recover
    // from.
    void aRetryKeepsTheSlotAcrossItsDelay() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(frameWrite(), payload('1'));
        t.write(frameWrite(), payload('2'));

        dispatch();

        QVERIFY(queue.isBusy());
        QCOMPARE(queue.pendingCount(), qsizetype(1));
    }

    // --- writeUrgent -----------------------------------------------------

    // CHANGED BY THE MOVE, deliberately. Urgent used to call writeCharacteristic
    // directly when nothing was in flight, and prepend only when something was.
    //
    // A bypass cannot survive a shared queue: "nothing is issued while another
    // device has an operation outstanding" is the entire guarantee, and an
    // operation that skips the queue when THIS device looks idle knows nothing
    // about the others. So urgency is position, always.
    //
    // The cost is one event-loop turn for the app-suspend charger write, and it
    // is not a real cost: QLowEnergyService::writeCharacteristic is itself
    // asynchronous, so the synchronous call only ever reached Qt's own
    // per-controller queue, which needs the same event loop to drain.
    void urgentWriteGoesToTheFrontRatherThanBypassing() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(frameWrite(), payload('1'));

        t.writeUrgent(writeToMmr(), payload('u'));

        QCOMPARE(queue.pendingCount(), qsizetype(2));
        QCOMPARE(queue.m_queue.at(0).key, writeToMmr());
        QCOMPARE(queue.m_queue.at(1).key, frameWrite());
    }

    void urgentWriteGoesAheadOfEverythingAlreadyWaiting() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(frameWrite(), payload('1'));
        t.write(frameWrite(), payload('2'));

        t.writeUrgent(writeToMmr(), payload('u'));

        QCOMPARE(queue.pendingCount(), qsizetype(3));
        QCOMPARE(queue.m_queue.at(0).key, writeToMmr());
        QCOMPARE(queue.m_queue.at(1).key, frameWrite());
    }

    // It still waits for the in-flight operation. Jumping the queue is not
    // jumping the slot.
    void urgentWriteStillWaitsForTheOperationInFlight() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(frameWrite(), payload('1'));
        dispatch();
        const QBluetoothUuid held = queue.inFlightKey();

        t.writeUrgent(writeToMmr(), payload('u'));

        QCOMPARE(queue.inFlightKey(), held);
        QCOMPARE(queue.m_queue.at(0).key, writeToMmr());
    }

    // Urgent does not clear. Callers that need to clear do so explicitly, so an
    // app-suspend charger write cannot drop pending extraction frames.
    void urgentWriteDoesNotClearPendingWork() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(frameWrite(), payload('1'));
        t.writeUrgent(writeToMmr(), payload('u'));

        QCOMPARE(queue.pendingCount(), qsizetype(2));
    }

    // --- discardQueued ---------------------------------------------------

    void discardDropsOnlyTheNamedCharacteristics() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(headerWrite(), payload('h'));
        t.write(frameWrite(), payload('1'));
        t.write(writeToMmr(), payload('m'));
        t.write(frameWrite(), payload('2'));

        const qsizetype dropped = t.discardQueued({frameWrite()});

        QCOMPARE(dropped, qsizetype(2));
        QCOMPARE(queue.pendingCount(), qsizetype(2));
        QCOMPARE(queue.m_queue.at(0).key, headerWrite());
        QCOMPARE(queue.m_queue.at(1).key, writeToMmr());
    }

    // Several at once, which is how a profile upload withdraws its own work:
    // header and frames together, everything else untouched.
    void discardHandlesSeveralCharacteristicsAtOnce() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(headerWrite(), payload('h'));
        t.write(frameWrite(), payload('1'));
        t.write(writeToMmr(), payload('m'));
        t.write(frameWrite(), payload('2'));

        const qsizetype dropped = t.discardQueued({headerWrite(), frameWrite()});

        QCOMPARE(dropped, qsizetype(3));
        QCOMPARE(queue.pendingCount(), qsizetype(1));
        QCOMPARE(queue.m_queue.at(0).key, writeToMmr());
    }

    void discardPreservesTheOrderOfWhatItKeeps() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(headerWrite(), payload('h'));
        t.write(frameWrite(), payload('1'));
        t.write(writeToMmr(), payload('m'));

        t.discardQueued({frameWrite()});

        QCOMPARE(queue.m_queue.at(0).key, headerWrite());
        QCOMPARE(queue.m_queue.at(1).key, writeToMmr());
    }

    void discardOfAnAbsentCharacteristicDropsNothing() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(headerWrite(), payload('h'));

        QCOMPARE(t.discardQueued({frameWrite()}), qsizetype(0));
        QCOMPARE(queue.pendingCount(), qsizetype(1));
    }

    void discardWithNoUuidsDropsNothing() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(headerWrite(), payload('h'));

        QCOMPARE(t.discardQueued({}), qsizetype(0));
        QCOMPARE(queue.pendingCount(), qsizetype(1));
    }

    // Deliberate asymmetry with clearQueue() below: discard withdraws work the
    // caller queued itself, and an operation already dispatched has either
    // landed or is being retried. Counting it would over-report and, worse,
    // imply the in-flight operation was cancelled when it was not.
    void discardDoesNotTouchOrCountTheInFlightOperation() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(frameWrite(), payload('1'));
        t.write(frameWrite(), payload('2'));
        dispatch();
        QVERIFY(queue.isBusy());

        QCOMPARE(t.discardQueued({frameWrite()}), qsizetype(1));
        QVERIFY(queue.isBusy());
        QCOMPARE(queue.inFlightKey(), frameWrite());
    }

    // Scoped to this transport. Another device's work for the same
    // characteristic is not this caller's to withdraw — and on a shared queue
    // that is no longer hypothetical.
    void discardLeavesAnotherTransportsWorkAlone() {
        BleGattQueue queue;
        BleTransport mine(nullptr, &queue);
        BleTransport other(nullptr, &queue);
        mine.write(frameWrite(), payload('1'));
        other.write(frameWrite(), payload('2'));

        QCOMPARE(mine.discardQueued({frameWrite()}), qsizetype(1));
        QCOMPARE(queue.pendingCount(&other), qsizetype(1));
    }

    // The point of the whole change (#1819): one device's operation is not
    // issued while another's is outstanding. Asserted with two real transports
    // rather than fakes, because the guarantee has to hold for the code that
    // actually submits — and the queue is requester-agnostic, so this is the
    // same path the scale and refractometer transports take.
    void oneDeviceIsNotDispatchedWhileAnotherHoldsTheSlot() {
        BleGattQueue queue;
        BleTransport first(nullptr, &queue);
        BleTransport second(nullptr, &queue);
        first.write(frameWrite(), payload('1'));
        dispatch();
        QCOMPARE(queue.inFlightRequester(), static_cast<const void*>(&first));

        // Submitted while the first is already in flight and the dispatch timer
        // is idle — the only arrangement that actually consults the in-flight
        // check rather than being held back by the timer.
        second.write(writeToMmr(), payload('2'));
        dispatch();

        QCOMPARE(queue.inFlightRequester(), static_cast<const void*>(&first));
        QCOMPARE(queue.pendingCount(&second), qsizetype(1));

        // And the second device is served once the first releases — the queue
        // orders, it does not starve.
        first.clearQueue();
        dispatch();

        QCOMPARE(queue.inFlightRequester(), static_cast<const void*>(&second));
    }

    // --- clearQueue ------------------------------------------------------

    // clearQueue DOES count the in-flight operation. Its callers are about to
    // change machine state and need the MMR dedup cache invalidated if an MMR
    // write was mid-air; under-reporting there leaves m_lastMMRValues claiming
    // the DE1 holds a value it never received.
    void clearCountsTheInFlightOperationAsWellAsTheQueued() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(frameWrite(), payload('1'));
        t.write(frameWrite(), payload('2'));
        t.write(frameWrite(), payload('3'));
        dispatch();
        QVERIFY(queue.isBusy());

        QCOMPARE(t.clearQueue(), qsizetype(3));
        QCOMPARE(queue.pendingCount(), qsizetype(0));
        QVERIFY(!queue.isBusy());
    }

    void clearWithNothingInFlightCountsOnlyTheQueued() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(frameWrite(), payload('1'));
        t.write(frameWrite(), payload('2'));

        QCOMPARE(t.clearQueue(), qsizetype(2));
    }

    void clearStopsTheOperationClock() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(frameWrite(), payload('1'));
        dispatch();
        QVERIFY(t.m_operationTimeoutTimer.isActive());

        t.clearQueue();

        QVERIFY(!t.m_operationTimeoutTimer.isActive());
    }

    void clearOfAnEmptyQueueReportsNothingCleared() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        QCOMPARE(t.clearQueue(), qsizetype(0));
    }

    // Scoped to this transport, like discard. A DE1 clearing its queue for a
    // sleep must not throw away a scale's tare.
    void clearLeavesAnotherTransportsWorkAlone() {
        BleGattQueue queue;
        BleTransport mine(nullptr, &queue);
        BleTransport other(nullptr, &queue);
        mine.write(frameWrite(), payload('1'));
        other.write(frameWrite(), payload('2'));

        QCOMPARE(mine.clearQueue(), qsizetype(1));
        QCOMPARE(queue.pendingCount(&other), qsizetype(1));
    }

    // --- retry and timeout constants -------------------------------------

    // Pinned as VALUES, not as "whatever the header says". The retry budget
    // carries a 60-line derivation and is coupled to the DE1-fault-cluster
    // weighting in QtScaleBleTransport::onDe1LinkFault. A change here is a
    // change to that derivation, and should fail until the derivation is redone.
    void de1RetryPolicyIsUnchanged() {
        QCOMPARE(BleTransport::MAX_WRITE_RETRIES, 5);
        QCOMPARE(BleTransport::WRITE_TIMEOUT_MS, 5000);
        QCOMPARE(BleTransport::WRITE_RETRY_DELAY_MS, 500);
    }

    // And that the budget is what the operations actually carry. Declaring the
    // constants and attaching something else is exactly the drift a shared
    // queue makes possible, since the policy now travels as data.
    void submittedOperationsCarryThatBudget() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(frameWrite(), payload('1'));

        QCOMPARE(queue.m_queue.at(0).policy.maxRetries, BleTransport::MAX_WRITE_RETRIES);
        QCOMPARE(queue.m_queue.at(0).policy.retryDelayMs, BleTransport::WRITE_RETRY_DELAY_MS);
    }

    // The operation clock is armed by the dispatch, at the write budget. It is
    // the outer bound for an operation the platform never answers at all — see
    // its declaration for why there is exactly one clock and not two.
    void dispatchArmsTheOperationClockAtTheWriteBudget() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        t.write(frameWrite(), payload('1'));
        QVERIFY(!t.m_operationTimeoutTimer.isActive());

        dispatch();

        QVERIFY(t.m_operationTimeoutTimer.isActive());
        QCOMPARE(t.m_operationTimeoutTimer.interval(), BleTransport::WRITE_TIMEOUT_MS);
        QVERIFY(t.m_operationTimeoutTimer.isSingleShot());
    }

    // --- queue depth reporting -------------------------------------------

    // Edge-triggered: one warning per episode, not one per submit past the
    // threshold. de1app warns at the same depth and also sheds nothing — a
    // depth report is a diagnosis, not a policy.
    //
    // Asserted through the WARNING, not through the latch member: one message
    // for 25 submits past the threshold. failOnWarning() in init() is what makes
    // this able to fail — a second, unignored warning fails the slot, so "once"
    // is genuinely pinned rather than merely described.
    void depthWarningFiresOnceWhenTheQueueBacksUp() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);
        // 40, not 20: a normal DE1 connect peaks the shared queue at 35 (measured
        // on an SM-X210 with a scale connecting alongside), so the old value
        // fired on every healthy start. See the constant's comment.
        QCOMPARE(BleGattQueue::QUEUE_DEPTH_WARN, qsizetype(40));

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("Bluetooth operations are queued at once")));
        for (int i = 0; i < BleGattQueue::QUEUE_DEPTH_WARN + 5; ++i)
            t.write(frameWrite(), payload('x'));
    }

    // Re-arms only once well clear of the threshold, so a queue hovering at the
    // boundary does not log on every other submit.
    //
    // Hysteresis, asserted end to end: back up, drain to just under the
    // threshold, back up again, and show the second episode reports. A latch
    // that never re-armed would emit one warning and fail the second
    // ignoreMessage; one that re-armed on the first dip below the threshold
    // would emit an extra warning at the drain step and fail on that.
    void depthWarningReportsEachEpisodeButNotEachSubmit() {
        BleGattQueue queue;
        BleTransport t(nullptr, &queue);

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("Bluetooth operations are queued at once")));
        for (int i = 0; i < BleGattQueue::QUEUE_DEPTH_WARN + 2; ++i)
            t.write(frameWrite(), payload('x'));

        // Drain to below half the threshold, so the next submit re-arms without
        // reporting. Any warning emitted here is unignored and fails the slot.
        queue.m_queue.resize(BleGattQueue::QUEUE_DEPTH_WARN / 2 - 1);
        t.write(frameWrite(), payload('x'));

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("Bluetooth operations are queued at once")));
        while (queue.pendingCount() < BleGattQueue::QUEUE_DEPTH_WARN)
            t.write(frameWrite(), payload('x'));
    }

    // --- abandonment signals ---------------------------------------------

    // The consecutive-abandonment threshold recognises a link that has stopped
    // accepting writes while still reporting itself connected. Reporting only —
    // nothing is torn down on this signal — and the shared queue must not
    // change either half of that.
    //
    // Only the CONSTANTS are asserted here. The detector's behaviour — report
    // once per episode, restate as the run grows, close out differently on a
    // recovery than on a disconnect — is asserted in tst_bletransporterror,
    // against the exact log text and run counts. Three slots here re-ran a
    // strict subset of that and were deleted rather than kept as insurance.
    void writeDeadLinkReportingThresholdsAreUnchanged() {
        QCOMPARE(BleTransport::WRITE_DEAD_LINK_THRESHOLD, 3);
        QCOMPARE(BleTransport::WRITE_DEAD_LINK_RESTATE, 10);
    }

};

QTEST_MAIN(tst_BleCommandQueue)
#include "tst_blecommandqueue.moc"
