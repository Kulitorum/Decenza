#include <QtTest>

#include "ble/blegattqueue.h"
#include "ble/protocol/de1characteristics.h"

// The shared cross-device GATT queue (#1819).
//
// Every test builds its OWN BleGattQueue rather than touching
// BleGattQueue::instance() — a process-wide singleton shared between test
// functions would make ordering assertions depend on what ran before them,
// which is the one thing a queue test cannot afford.
//
// Dispatch is posted, never inline, so every assertion about what got issued
// has to pump the event loop first. That is not a testing inconvenience: it is
// the property being asserted. Dispatching inline from a completion handler
// would let a device recurse into its own next operation beneath its own
// callback.
class tst_BleGattQueue : public QObject {
    Q_OBJECT

private:
    // Two distinct requester identities. Never dereferenced — the queue only
    // ever compares them — so any two stable addresses will do.
    // Distinct VALUES, not just distinct declarations: two identical const
    // ints are eligible to share an address under constant merging, which
    // would silently collapse the two requesters into one and make every
    // ordering assertion below vacuous.
    static BleGattQueue::Requester de1()   { static const int tag = 1; return &tag; }
    static BleGattQueue::Requester scale() { static const int tag = 2; return &tag; }

    // Records the order operations were issued in, so ordering is asserted
    // against what actually reached the platform rather than against queue
    // contents.
    struct Recorder {
        QStringList issued;
        QStringList abandoned;
    };

    static BleGattQueue::Operation op(BleGattQueue::Requester who,
                                      const QString& label,
                                      Recorder* rec,
                                      BleGattQueue::Policy policy = {},
                                      QBluetoothUuid key = {}) {
        BleGattQueue::Operation o;
        o.requester = who;
        o.label = label;
        o.key = key;
        o.policy = policy;
        o.issue = [rec, label]() { rec->issued << label; };
        o.onAbandoned = [rec, label]() { rec->abandoned << label; };
        return o;
    }

    // Let posted dispatches run. Generous enough to cover the zero-interval
    // post plus any pacing the test asked for, without being a race.
    static void pump(int ms = 30) { QTest::qWait(ms); }

private slots:
    void init() { QTest::failOnWarning(); }

    // --- one at a time ---------------------------------------------------

    // The headline property. Without it, #1819: a scale's discovery lands on
    // top of the DE1's descriptor writes and all three are rejected.
    void onlyOneOperationIsIssuedUntilItCompletes() {
        BleGattQueue q;
        Recorder rec;

        q.submit(op(de1(), QStringLiteral("de1-write"), &rec));
        q.submit(op(scale(), QStringLiteral("scale-discover"), &rec));
        pump();

        QCOMPARE(rec.issued, QStringList{QStringLiteral("de1-write")});
        QVERIFY(q.isBusy());
        QCOMPARE(q.inFlightRequester(), de1());

        q.noteSucceeded(de1());
        pump();

        QCOMPARE(rec.issued, (QStringList{QStringLiteral("de1-write"),
                                          QStringLiteral("scale-discover")}));
    }

    void dispatchIsPostedRatherThanIssuedInsideSubmit() {
        BleGattQueue q;
        Recorder rec;

        q.submit(op(de1(), QStringLiteral("a"), &rec));
        QVERIFY(rec.issued.isEmpty());
        QVERIFY(!q.isBusy());

        pump();
        QCOMPARE(rec.issued.size(), 1);
    }

    // Each requester's own submission order survives interleaving. A profile
    // upload is a stateful sequence — header declares N frames, then indexed
    // frames — and reordering it corrupts the DE1's receive state machine.
    void eachRequestersOwnOrderIsPreservedWhileInterleaving() {
        BleGattQueue q;
        Recorder rec;

        q.submit(op(de1(), QStringLiteral("de1-1"), &rec));
        q.submit(op(scale(), QStringLiteral("scale-1"), &rec));
        q.submit(op(de1(), QStringLiteral("de1-2"), &rec));
        q.submit(op(scale(), QStringLiteral("scale-2"), &rec));

        for (int i = 0; i < 4; ++i) {
            pump();
            if (q.isBusy()) q.noteSucceeded(q.inFlightRequester());
        }
        pump();

        QCOMPARE(rec.issued, (QStringList{QStringLiteral("de1-1"),
                                          QStringLiteral("scale-1"),
                                          QStringLiteral("de1-2"),
                                          QStringLiteral("scale-2")}));
    }

    // --- front insertion -------------------------------------------------

    // Urgent is expressed as queue POSITION, never as a bypass: it goes ahead
    // of everything waiting but still behind the operation in flight.
    void frontSubmissionJumpsTheQueueButNotTheInFlightOperation() {
        BleGattQueue q;
        Recorder rec;

        q.submit(op(de1(), QStringLiteral("first"), &rec));
        q.submit(op(de1(), QStringLiteral("queued"), &rec));
        pump();
        QCOMPARE(rec.issued, QStringList{QStringLiteral("first")});

        q.submitFront(op(de1(), QStringLiteral("urgent"), &rec));
        pump();

        // Still only "first" — the urgent operation did not preempt it.
        QCOMPARE(rec.issued, QStringList{QStringLiteral("first")});

        q.noteSucceeded(de1());
        pump();
        QCOMPARE(rec.issued.last(), QStringLiteral("urgent"));
    }

    // --- failure and retry -----------------------------------------------

    // Zero retries is the default, and it is what the scale and refractometer
    // transports had before they were queued at all.
    void anOperationWithNoRetryBudgetIsAbandonedOnFirstFailure() {
        BleGattQueue q;
        Recorder rec;

        q.submit(op(scale(), QStringLiteral("scale-write"), &rec));
        q.submit(op(de1(), QStringLiteral("de1-write"), &rec));
        pump();

        q.noteFailed(scale());
        pump();

        QCOMPARE(rec.abandoned, QStringList{QStringLiteral("scale-write")});
        QCOMPARE(rec.issued, (QStringList{QStringLiteral("scale-write"),
                                          QStringLiteral("de1-write")}));
    }

    // The slot is released AT the failure. This is the whole point of handling
    // the error instead of waiting a bound out: in #1819 the
    // DescriptorWriteError arrived at +45 ms and the code sat on a 3 s timer.
    void failureReleasesTheSlotImmediatelyRatherThanAfterAnyDelay() {
        BleGattQueue q;
        Recorder rec;

        q.submit(op(scale(), QStringLiteral("doomed"), &rec));
        pump();
        QVERIFY(q.isBusy());

        q.noteFailed(scale());
        QVERIFY(!q.isBusy());
    }

    void anOperationWithARetryBudgetIsReissuedUpToThatBudget() {
        BleGattQueue q;
        Recorder rec;
        BleGattQueue::Policy retrying;
        retrying.maxRetries = 2;
        retrying.retryDelayMs = 1;

        q.submit(op(de1(), QStringLiteral("flaky"), &rec, retrying));
        pump();
        QCOMPARE(rec.issued.size(), 1);

        q.noteFailed(de1());
        pump();
        QCOMPARE(rec.issued.size(), 2);
        QVERIFY(q.isBusy());          // slot held across the retry
        QVERIFY(rec.abandoned.isEmpty());

        q.noteFailed(de1());
        pump();
        QCOMPARE(rec.issued.size(), 3);
        QVERIFY(rec.abandoned.isEmpty());

        // Budget spent.
        q.noteFailed(de1());
        pump();
        QCOMPARE(rec.issued.size(), 3);
        QCOMPARE(rec.abandoned, QStringList{QStringLiteral("flaky")});
        QVERIFY(!q.isBusy());
    }

    // A dead link must not hold the shared slot for another device's budget.
    // This is why policy travels with the operation: had the scale inherited
    // the DE1's five retries, its failure would occupy the stack for tens of
    // seconds while the machine waited.
    void aFailingRequesterDoesNotHoldTheSlotForAnotherRequestersBudget() {
        BleGattQueue q;
        Recorder rec;
        BleGattQueue::Policy de1Policy;
        de1Policy.maxRetries = 5;
        de1Policy.retryDelayMs = 1;

        q.submit(op(scale(), QStringLiteral("dead-scale"), &rec));   // no retries
        q.submit(op(de1(), QStringLiteral("de1-work"), &rec, de1Policy));
        pump();

        q.noteFailed(scale());
        pump();

        QVERIFY(rec.issued.contains(QStringLiteral("de1-work")));
        QCOMPARE(q.inFlightRequester(), de1());
    }

    // --- late and misattributed completions -------------------------------

    void aCompletionFromARequesterThatDoesNotHoldTheSlotIsIgnored() {
        BleGattQueue q;
        Recorder rec;

        q.submit(op(de1(), QStringLiteral("de1-write"), &rec));
        pump();
        QCOMPARE(q.inFlightRequester(), de1());

        // A stale reply from the scale must not release the DE1's slot.
        q.noteSucceeded(scale());
        q.noteFailed(scale());

        QVERIFY(q.isBusy());
        QCOMPARE(q.inFlightRequester(), de1());
        QVERIFY(rec.abandoned.isEmpty());
    }

    void aCompletionWithNothingInFlightIsIgnored() {
        BleGattQueue q;
        q.noteSucceeded(de1());
        q.noteFailed(de1());
        QVERIFY(!q.isBusy());
    }

    // --- teardown ---------------------------------------------------------

    void forgetDropsQueuedWorkAndReleasesTheSlot() {
        BleGattQueue q;
        Recorder rec;

        q.submit(op(scale(), QStringLiteral("scale-1"), &rec));
        q.submit(op(scale(), QStringLiteral("scale-2"), &rec));
        q.submit(op(de1(), QStringLiteral("de1-1"), &rec));
        pump();
        QCOMPARE(q.inFlightRequester(), scale());

        // In flight + one queued = 2.
        QCOMPARE(q.forget(scale()), qsizetype(2));
        QVERIFY(!q.isBusy());

        pump();
        QCOMPARE(q.inFlightRequester(), de1());
    }

    // A requester tearing down is not told about work it is itself dropping —
    // onAbandoned is for an operation the queue gave up on, not for one the
    // caller withdrew.
    void forgetDoesNotReportDroppedWorkAsAbandoned() {
        BleGattQueue q;
        Recorder rec;

        q.submit(op(scale(), QStringLiteral("scale-1"), &rec));
        pump();
        q.forget(scale());
        pump();

        QVERIFY(rec.abandoned.isEmpty());
    }

    // The delayed re-issue must not fire against whatever took the slot next.
    // Without the generation guard this reissues the DE1's operation as the
    // torn-down scale's retry.
    void aRetryWhoseOperationWasTornDownMidDelayDoesNotFireAgainstTheNextOne() {
        BleGattQueue q;
        Recorder rec;
        BleGattQueue::Policy slowRetry;
        slowRetry.maxRetries = 3;
        slowRetry.retryDelayMs = 40;

        q.submit(op(scale(), QStringLiteral("scale-flaky"), &rec, slowRetry));
        q.submit(op(de1(), QStringLiteral("de1-work"), &rec));
        pump();
        QCOMPARE(rec.issued, QStringList{QStringLiteral("scale-flaky")});

        q.noteFailed(scale());     // arms a 40 ms re-issue
        q.forget(scale());         // ...and the transport dies before it fires
        pump(120);

        // The DE1's operation ran exactly once, and no second "scale-flaky".
        QCOMPARE(rec.issued, (QStringList{QStringLiteral("scale-flaky"),
                                          QStringLiteral("de1-work")}));
    }

    void forgetOfARequesterWithNoWorkDropsNothing() {
        BleGattQueue q;
        Recorder rec;
        q.submit(op(de1(), QStringLiteral("de1-1"), &rec));
        QCOMPARE(q.forget(scale()), qsizetype(0));
        QCOMPARE(q.pendingCount(de1()), qsizetype(1));
    }

    // --- discard ----------------------------------------------------------

    void discardDropsOnlyTheNamedKeysForThatRequester() {
        BleGattQueue q;
        Recorder rec;
        const QBluetoothUuid frame = DE1::Characteristic::FRAME_WRITE;
        const QBluetoothUuid header = DE1::Characteristic::HEADER_WRITE;

        q.submit(op(de1(), QStringLiteral("header"), &rec, {}, header));
        q.submit(op(de1(), QStringLiteral("frame-1"), &rec, {}, frame));
        q.submit(op(de1(), QStringLiteral("frame-2"), &rec, {}, frame));
        q.submit(op(scale(), QStringLiteral("scale-frame"), &rec, {}, frame));

        QCOMPARE(q.discard(de1(), {frame}), qsizetype(2));
        QCOMPARE(q.pendingCount(de1()), qsizetype(1));
        // The scale's operation shares the key but not the requester.
        QCOMPARE(q.pendingCount(scale()), qsizetype(1));
    }

    // Deliberate asymmetry with forget(): a caller withdrawing work it queued
    // itself is not cancelling a write already dispatched.
    void discardDoesNotTouchOrCountTheInFlightOperation() {
        BleGattQueue q;
        Recorder rec;
        const QBluetoothUuid frame = DE1::Characteristic::FRAME_WRITE;

        q.submit(op(de1(), QStringLiteral("frame-1"), &rec, {}, frame));
        q.submit(op(de1(), QStringLiteral("frame-2"), &rec, {}, frame));
        pump();
        QVERIFY(q.isBusy());

        QCOMPARE(q.discard(de1(), {frame}), qsizetype(1));
        QVERIFY(q.isBusy());
    }

    void discardWithNoKeysDropsNothing() {
        BleGattQueue q;
        Recorder rec;
        q.submit(op(de1(), QStringLiteral("a"), &rec));
        QCOMPARE(q.discard(de1(), {}), qsizetype(0));
        QCOMPARE(q.pendingCount(), qsizetype(1));
    }

    // --- pacing -----------------------------------------------------------

    // Pacing is carried by the operation that just ran, so one device's
    // interval does not become every device's.
    void pacingDelaysTheNextDispatchWithoutHoldingTheSlot() {
        BleGattQueue q;
        Recorder rec;
        BleGattQueue::Policy paced;
        paced.paceMsAfter = 60;

        q.submit(op(de1(), QStringLiteral("paced"), &rec, paced));
        q.submit(op(de1(), QStringLiteral("next"), &rec));
        pump();
        QCOMPARE(rec.issued, QStringList{QStringLiteral("paced")});

        q.noteSucceeded(de1());
        QVERIFY(!q.isBusy());          // released at once...
        pump(20);
        QCOMPARE(rec.issued.size(), 1); // ...but the next one waits out the pace

        pump(80);
        QCOMPARE(rec.issued.size(), 2);
    }

    void anUnpacedOperationDispatchesTheNextOneStraightAway() {
        BleGattQueue q;
        Recorder rec;

        q.submit(op(de1(), QStringLiteral("a"), &rec));
        q.submit(op(de1(), QStringLiteral("b"), &rec));
        pump();
        q.noteSucceeded(de1());
        pump();

        QCOMPARE(rec.issued.size(), 2);
    }

    // --- depth reporting --------------------------------------------------

    // Edge-triggered: one warning per episode. failOnWarning() in init() is
    // what makes "once" able to fail — a second unignored warning fails the
    // slot.
    void depthWarningFiresOncePerEpisode() {
        BleGattQueue q;
        Recorder rec;

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("queue is 20 deep")));
        for (int i = 0; i < 25; ++i)
            q.submit(op(de1(), QStringLiteral("x"), &rec));

        // Drain everything so the queue does not report again at destruction.
        q.forget(de1());
    }
};

QTEST_MAIN(tst_BleGattQueue)
#include "tst_blegattqueue.moc"
