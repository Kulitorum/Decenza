#include <QtTest>
#include <QSignalSpy>

#include "ble/bletransport.h"
#include "ble/protocol/de1characteristics.h"

// Baseline for the shared-GATT-queue move (#1819).
//
// BleTransport's command queue is the most carefully tuned code in the BLE
// layer — 50 ms pacing, a retry budget derived from a 26-log corpus, an urgent
// path that must not re-enter writeCharacteristic, and a UUID-scoped discard
// that withdraws dead profile frames without touching unrelated work. All of it
// is about to move onto a queue shared with the scale and refractometer
// transports.
//
// A move like that is only verifiable against a RECORDED baseline. Reading the
// new implementation and agreeing it looks equivalent is exactly how a tuned
// constant changes meaning without anything failing — and this file's whole
// purpose is that the numbers below were true before the move, so a difference
// after it is a regression rather than a new baseline to be talked into.
//
// Every assertion here is written against the CURRENT implementation and passes
// on it. None of them may be edited to accommodate the move.
//
// Headless by construction: with no QLowEnergyService, writeCharacteristic()
// returns early at its `!m_service` guard, so a dispatched command is a no-op
// and never sets m_writePending. That makes queue MECHANICS observable without a
// radio, and it is why the in-flight cases below set m_writePending explicitly
// rather than pretending a write is in the air.
class tst_BleCommandQueue : public QObject {
    Q_OBJECT

private:
    // Three distinct real characteristics, so discard scoping is asserted
    // against UUIDs the DE1 actually uses rather than invented ones.
    static QBluetoothUuid frameWrite()   { return DE1::Characteristic::FRAME_WRITE; }
    static QBluetoothUuid headerWrite()  { return DE1::Characteristic::HEADER_WRITE; }
    static QBluetoothUuid writeToMmr()   { return DE1::Characteristic::WRITE_TO_MMR; }

    static QByteArray payload(char tag) { return QByteArray(4, tag); }

private slots:
    void init() { QTest::failOnWarning(); }

    // --- FIFO and pacing -------------------------------------------------

    void writesQueueInSubmissionOrder() {
        BleTransport t;
        t.write(headerWrite(), payload('h'));
        t.write(frameWrite(), payload('1'));
        t.write(frameWrite(), payload('2'));

        QCOMPARE(t.m_commandQueue.size(), qsizetype(3));
        QCOMPARE(t.m_commandQueue.at(0).uuid, headerWrite());
        QCOMPARE(t.m_commandQueue.at(1).uuid, frameWrite());
        QCOMPARE(t.m_commandQueue.at(2).uuid, frameWrite());
    }

    // The pacing interval is the contract, not an implementation detail: it is
    // what keeps the DE1's link from being flooded, and the shared queue must
    // carry it per-operation rather than adopting one interval for every
    // device.
    void queuedWorkIsPacedAtFiftyMilliseconds() {
        BleTransport t;
        QCOMPARE(t.m_commandTimer.interval(), 50);
        QVERIFY(t.m_commandTimer.isSingleShot());
    }

    // A first enqueue arms the timer; it is not dispatched inline. Dispatching
    // inline would remove the pacing entirely for the first command of every
    // burst, which is the common case during a profile upload.
    void firstEnqueueArmsTheTimerRatherThanDispatchingInline() {
        BleTransport t;
        QVERIFY(!t.m_commandTimer.isActive());
        t.write(frameWrite(), payload('1'));
        QVERIFY(t.m_commandTimer.isActive());
        QCOMPARE(t.m_commandQueue.size(), qsizetype(1));
    }

    // With a write in flight the timer is NOT armed — the completion path
    // re-drives the queue. An extra arm here would let a second write be
    // dispatched under the first.
    void enqueueWhileAWriteIsInFlightDoesNotArmTheTimer() {
        BleTransport t;
        t.m_writePending = true;
        t.write(frameWrite(), payload('1'));
        QVERIFY(!t.m_commandTimer.isActive());
        QCOMPARE(t.m_commandQueue.size(), qsizetype(1));
    }

    void processDequeuesExactlyOneCommand() {
        BleTransport t;
        t.write(headerWrite(), payload('h'));
        t.write(frameWrite(), payload('1'));

        t.processCommandQueue();

        QCOMPARE(t.m_commandQueue.size(), qsizetype(1));
        QCOMPARE(t.m_commandQueue.at(0).uuid, frameWrite());
    }

    void processIsANoOpWhileAWriteIsInFlight() {
        BleTransport t;
        t.write(frameWrite(), payload('1'));
        t.m_writePending = true;

        t.processCommandQueue();

        QCOMPARE(t.m_commandQueue.size(), qsizetype(1));
    }

    // --- writeUrgent -----------------------------------------------------

    // Urgent bypasses the queue when nothing is in flight. This is the app-
    // suspend charger write: it must reach the machine before the process is
    // frozen, not 50 ms later behind whatever else is queued.
    void urgentWriteBypassesTheQueueWhenIdle() {
        BleTransport t;
        t.write(frameWrite(), payload('1'));

        t.writeUrgent(writeToMmr(), payload('u'));

        // Went straight to writeCharacteristic (a no-op headlessly) rather than
        // joining the queue, which still holds only the earlier frame write.
        QCOMPARE(t.m_commandQueue.size(), qsizetype(1));
        QCOMPARE(t.m_commandQueue.at(0).uuid, frameWrite());
    }

    // With a write in flight it PREPENDS instead. writeCharacteristic is not
    // re-entrant — calling it under an in-flight write corrupts
    // m_writePending/m_lastWriteUuid/m_writeTimeoutTimer — so the urgency is
    // expressed as queue position, not as an immediate call.
    void urgentWritePrependsWhileAWriteIsInFlight() {
        BleTransport t;
        t.write(frameWrite(), payload('1'));
        t.write(frameWrite(), payload('2'));
        t.m_writePending = true;

        t.writeUrgent(writeToMmr(), payload('u'));

        QCOMPARE(t.m_commandQueue.size(), qsizetype(3));
        QCOMPARE(t.m_commandQueue.at(0).uuid, writeToMmr());
        QCOMPARE(t.m_commandQueue.at(1).uuid, frameWrite());
    }

    // Urgent does not clear. Callers that need to clear do so explicitly, so an
    // app-suspend charger write cannot drop pending extraction frames.
    void urgentWriteDoesNotClearPendingWork() {
        BleTransport t;
        t.write(frameWrite(), payload('1'));
        t.m_writePending = true;
        t.writeUrgent(writeToMmr(), payload('u'));
        t.m_writePending = false;

        QVERIFY(t.m_commandQueue.size() >= qsizetype(2));
    }

    // --- discardQueued ---------------------------------------------------

    void discardDropsOnlyTheNamedCharacteristics() {
        BleTransport t;
        t.write(headerWrite(), payload('h'));
        t.write(frameWrite(), payload('1'));
        t.write(writeToMmr(), payload('m'));
        t.write(frameWrite(), payload('2'));

        const qsizetype dropped = t.discardQueued({frameWrite()});

        QCOMPARE(dropped, qsizetype(2));
        QCOMPARE(t.m_commandQueue.size(), qsizetype(2));
        QCOMPARE(t.m_commandQueue.at(0).uuid, headerWrite());
        QCOMPARE(t.m_commandQueue.at(1).uuid, writeToMmr());
    }

    void discardPreservesTheOrderOfWhatItKeeps() {
        BleTransport t;
        t.write(headerWrite(), payload('h'));
        t.write(frameWrite(), payload('1'));
        t.write(writeToMmr(), payload('m'));

        t.discardQueued({frameWrite()});

        QCOMPARE(t.m_commandQueue.at(0).uuid, headerWrite());
        QCOMPARE(t.m_commandQueue.at(1).uuid, writeToMmr());
    }

    void discardOfAnAbsentCharacteristicDropsNothing() {
        BleTransport t;
        t.write(headerWrite(), payload('h'));

        QCOMPARE(t.discardQueued({frameWrite()}), qsizetype(0));
        QCOMPARE(t.m_commandQueue.size(), qsizetype(1));
    }

    void discardWithNoUuidsDropsNothing() {
        BleTransport t;
        t.write(headerWrite(), payload('h'));

        QCOMPARE(t.discardQueued({}), qsizetype(0));
        QCOMPARE(t.m_commandQueue.size(), qsizetype(1));
    }

    // Deliberate asymmetry with clearQueue() below: discard withdraws work the
    // caller queued itself, and a write already dispatched has either landed or
    // is being retried. Counting it would over-report and, worse, imply the
    // in-flight write was cancelled when it was not.
    void discardDoesNotTouchOrCountTheInFlightWrite() {
        BleTransport t;
        t.write(frameWrite(), payload('1'));
        t.m_writePending = true;

        QCOMPARE(t.discardQueued({frameWrite()}), qsizetype(1));
        QVERIFY(t.m_writePending);
    }

    // --- clearQueue ------------------------------------------------------

    // clearQueue DOES count the in-flight write. Its callers are about to
    // change machine state and need the MMR dedup cache invalidated if an MMR
    // write was mid-air; under-reporting there leaves m_lastMMRValues claiming
    // the DE1 holds a value it never received.
    void clearCountsTheInFlightWriteAsWellAsTheQueued() {
        BleTransport t;
        t.write(frameWrite(), payload('1'));
        t.write(frameWrite(), payload('2'));
        t.m_writePending = true;

        QCOMPARE(t.clearQueue(), qsizetype(3));
        QCOMPARE(t.m_commandQueue.size(), qsizetype(0));
        QVERIFY(!t.m_writePending);
    }

    void clearWithNothingInFlightCountsOnlyTheQueued() {
        BleTransport t;
        t.write(frameWrite(), payload('1'));
        t.write(frameWrite(), payload('2'));

        QCOMPARE(t.clearQueue(), qsizetype(2));
    }

    void clearResetsRetryState() {
        BleTransport t;
        t.write(frameWrite(), payload('1'));
        t.m_writePending = true;
        t.m_writeRetryCount = 3;
        t.m_lastWriteUuid = QStringLiteral("0000a010");
        t.m_lastWriteData = payload('1');

        t.clearQueue();

        QCOMPARE(t.m_writeRetryCount, 0);
        QVERIFY(t.m_lastWriteUuid.isEmpty());
        QVERIFY(t.m_lastWriteData.isEmpty());
        QVERIFY(!t.m_writeTimeoutTimer.isActive());
    }

    void clearOfAnEmptyQueueReportsNothingCleared() {
        BleTransport t;
        QCOMPARE(t.clearQueue(), qsizetype(0));
    }

    // --- retry and timeout constants -------------------------------------

    // Pinned as VALUES, not as "whatever the header says". The retry budget
    // carries a 60-line derivation and is coupled to the DE1-fault-cluster
    // weighting in QtScaleBleTransport::onDe1LinkFault; the shared queue must
    // carry these per-operation rather than imposing one policy on every
    // device. A change here is a change to that derivation, and should fail
    // until the derivation is redone.
    void de1RetryPolicyIsUnchanged() {
        QCOMPARE(BleTransport::MAX_WRITE_RETRIES, 5);
        QCOMPARE(BleTransport::WRITE_TIMEOUT_MS, 5000);
        QCOMPARE(BleTransport::WRITE_RETRY_DELAY_MS, 500);
    }

    void writeTimeoutTimerIsConfiguredFromThatBudget() {
        BleTransport t;
        QCOMPARE(t.m_writeTimeoutTimer.interval(), BleTransport::WRITE_TIMEOUT_MS);
        QVERIFY(t.m_writeTimeoutTimer.isSingleShot());
    }

    // --- queue depth reporting -------------------------------------------

    // Edge-triggered: one warning per episode, not one per enqueue past the
    // threshold. de1app warns at the same depth and also sheds nothing — a
    // depth report is a diagnosis, not a policy.
    // Asserted through the WARNING, not through the latch member: one message
    // for 25 enqueues past the threshold. failOnWarning() in init() is what
    // makes this able to fail — a second, unignored warning fails the slot, so
    // "once" is genuinely pinned rather than merely described.
    void depthWarningFiresOnceWhenTheQueueBacksUp() {
        BleTransport t;
        QCOMPARE(BleTransport::QUEUE_DEPTH_WARN, qsizetype(20));

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("BLE write queue is 20 deep")));
        for (int i = 0; i < 25; ++i)
            t.write(frameWrite(), payload('x'));
    }

    // Re-arms only once well clear of the threshold, so a queue hovering at the
    // boundary does not log on every other enqueue.
    // Hysteresis, asserted end to end: back up, drain to just under the
    // threshold, back up again, and show the second episode reports. A latch
    // that never re-armed would emit one warning and fail the second
    // ignoreMessage; one that re-armed on the first dip below the threshold
    // would emit an extra warning at the drain step and fail on that.
    void depthWarningReportsEachEpisodeButNotEachEnqueue() {
        BleTransport t;

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("BLE write queue is 20 deep")));
        for (int i = 0; i < 22; ++i)
            t.write(frameWrite(), payload('x'));

        // Drain to 9 — below half, so the next enqueue re-arms without
        // reporting. Any warning emitted here is unignored and fails the slot.
        t.m_commandQueue.resize(9);
        t.write(frameWrite(), payload('x'));

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("BLE write queue is 20 deep")));
        while (t.m_commandQueue.size() < 20)
            t.write(frameWrite(), payload('x'));
    }

    // --- abandonment signals ---------------------------------------------

    // The consecutive-abandonment threshold recognises a link that has stopped
    // accepting writes while still reporting itself connected. Reporting only —
    // nothing is torn down on this signal — and the shared queue must not
    // change either half of that.
    void writeDeadLinkReportingThresholdsAreUnchanged() {
        QCOMPARE(BleTransport::WRITE_DEAD_LINK_THRESHOLD, 3);
        QCOMPARE(BleTransport::WRITE_DEAD_LINK_RESTATE, 10);
    }

    void abandonedWritesReportTheLinkAsNoLongerAcceptingWrites() {
        BleTransport t;
        QSignalSpy faults(&t, &BleTransport::de1LinkFault);

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("stopped accepting writes")));
        for (int i = 0; i < BleTransport::WRITE_DEAD_LINK_THRESHOLD; ++i)
            t.noteWriteAbandoned();

        QVERIFY(t.m_writeDeadLinkReported);
        QCOMPARE(t.m_consecutiveWriteFailures, BleTransport::WRITE_DEAD_LINK_THRESHOLD);
        // noteWriteAbandoned counts and reports; the fault itself is emitted by
        // the two retry-exhaustion sites, not here.
        QCOMPARE(faults.count(), 0);

        // The episode is still open, and ~BleTransport calls disconnect() ->
        // forgetWriteFailureState(), which closes it out with a second warning.
        // Expected, and worth pinning rather than sidestepping: a link torn down
        // mid-episode must still say how bad it got, and the destructor is the
        // only place that happens when nothing recovers first.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("Link dropped while it had stopped")));
    }

    // A recovery closes the episode out at INFO with the run it reached. Per
    // LOGGING.md the recurring failure is a fault reported at WARN whose
    // resolution sits at DEBUG, leaving a reader with only the failure half.
    void aSuccessfulWriteClosesTheEpisodeAndResetsTheRun() {
        BleTransport t;
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("stopped accepting writes")));
        for (int i = 0; i < BleTransport::WRITE_DEAD_LINK_THRESHOLD; ++i)
            t.noteWriteAbandoned();

        t.noteWriteSucceeded();

        QCOMPARE(t.m_consecutiveWriteFailures, 0);
        QVERIFY(!t.m_writeDeadLinkReported);
    }

    // A disconnect is a different story from a recovery: the link went away
    // rather than started working, and "accepting writes again" would be false.
    void aDisconnectClosesTheEpisodeWithoutClaimingRecovery() {
        BleTransport t;
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("stopped accepting writes")));
        for (int i = 0; i < BleTransport::WRITE_DEAD_LINK_THRESHOLD; ++i)
            t.noteWriteAbandoned();

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("Link dropped while it had stopped")));
        t.forgetWriteFailureState();

        QCOMPARE(t.m_consecutiveWriteFailures, 0);
        QVERIFY(!t.m_writeDeadLinkReported);
    }

};

QTEST_MAIN(tst_BleCommandQueue)
#include "tst_blecommandqueue.moc"
