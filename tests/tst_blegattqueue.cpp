#include <QtTest>
#include <QSignalSpy>

#include "ble/blegattqueue.h"
#include "ble/protocol/de1characteristics.h"
#include "messagecapture.h"

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
    // post and any retry delay a test asked for, without being a race.
    static void pump(int ms = 30) { QTest::qWait(ms); }

    // Advance the QUEUE's clock without advancing the wall clock.
    //
    // These slots assert thresholds measured in hundreds of milliseconds, and
    // sleeping through them made the suite slow and timing-dependent — a timer
    // in a test, which is the same anti-pattern the production code is not
    // allowed. pump() still exists and is still needed: a posted dispatch is a
    // real event that needs a real event-loop turn. Only the DURATIONS are
    // simulated.
    static void advanceQueueClock(BleGattQueue& q, qint64 ms) { q.m_testClockSkewMs += ms; }

    // The needle every log-volume slot below counts, DERIVED from the production
    // formatter rather than retyped. A hardcoded copy would keep passing the
    // three zero-assertions after someone reworded the line, matching nothing.
    static QString dispatchNeedle() {
        return BleGatt::dispatchCollapseKey(QStringLiteral("x"), 0)
            .left(QStringLiteral("dispatch ").size());
    }

private slots:
    void init() { QTest::failOnWarning(); }

    // --- one at a time ---------------------------------------------------

    // The headline property. Without it, #1819: a scale's discovery lands on
    // top of the DE1's descriptor writes and all three are rejected.
    // The second operation is submitted AFTER the first has been dispatched, and
    // that ordering is the whole test. Submitting both up front would pass on
    // FIFO order alone, with the in-flight guard deleted — which is how the
    // first version of this slot survived its negative control.
    void onlyOneOperationIsIssuedUntilItCompletes() {
        BleGattQueue q;
        Recorder rec;

        q.submit(op(de1(), QStringLiteral("de1-write"), &rec));
        pump();
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

        // A reissue arrives on a singleShot(retryDelayMs), so these wait on the
        // reissue itself. A fixed pump() asserts that a timer fired inside a
        // wall-clock budget, which is a race the parallel sanitizer build loses
        // often enough to be seen.
        q.noteFailed(de1());
        QTRY_COMPARE(rec.issued.size(), 2);
        QVERIFY(q.isBusy());          // slot held across the retry
        QVERIFY(rec.abandoned.isEmpty());

        q.noteFailed(de1());
        QTRY_COMPARE(rec.issued.size(), 3);
        QVERIFY(rec.abandoned.isEmpty());

        // Budget spent: the count must STAY at 3, so this one waits out the
        // retry delay rather than polling — QTRY would pass on the first look.
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

    // Two failure reports inside one retry delay must arm ONE retry. Both
    // captured the same generation, so both passed the guard and both called
    // issue(): the payload written twice, and the duplicate's late ACK able to
    // release a LATER operation on the same characteristic while it is still on
    // the wire. Reachable on the DE1 side, where the service errorOccurred arm
    // and onServiceStateChanged(InvalidService) both fire when a link drops
    // mid-retry.
    void asecondFailureInsideTheRetryDelayDoesNotArmASecondRetry() {
        BleGattQueue q;
        Recorder rec;
        BleGattQueue::Policy retrying;
        retrying.maxRetries = 3;
        retrying.retryDelayMs = 40;

        q.submit(op(de1(), QStringLiteral("flaky"), &rec, retrying));
        pump();
        QCOMPARE(rec.issued.size(), 1);

        q.noteFailed(de1());
        q.noteFailed(de1());   // same window, same generation

        // QTRY_COMPARE for the arrival, then a settle for the absence — the two
        // halves need opposite waits and a single fixed pump() cannot serve both.
        // The retry is a real QTimer::singleShot (blegattqueue.cpp:267), NOT
        // something advanceQueueClock() can drive: m_testClockSkewMs feeds nowMs()
        // for the dispatch-wait log and nothing else. So this slot must wait real
        // time, and a fixed pump(80) against a 40 ms delay left 40 ms of slack that
        // a full parallel ctest run consumed — observed failing here at issued=1,
        // i.e. the retry had not fired yet, which is the opposite of the 3 this
        // slot exists to catch.
        QTRY_COMPARE(rec.issued.size(), 2);
        pump(80);
        QCOMPARE(rec.issued.size(), 2);   // not 3
    }

    // --- dispatch log volume ----------------------------------------------
    //
    // These four slots assert VOLUME, not correctness — a regression here costs
    // a ring buffer full of noise, not a missed stop-at-weight. They are cheap
    // because they share a file, and they exist because this gate has been got
    // wrong four times.
    //
    // Three of them assert ZERO lines, which is only an assertion at all
    // because anOperationDelayedByAnotherDeviceIsLogged() proves the same
    // needle CAN reach a non-zero count in this binary. That slot is
    // load-bearing for the other three: delete or rename it and they quietly
    // become vacuous. MessageCapture::count() spans every tier for the same
    // reason — a line promoted to qInfo must turn these red, not satisfy them.

    // At idle the Decent Scale's 1 Hz heartbeat made this line periodic forever
    // and it dominated the log; the measurement is on m_dispatchLog in
    // blegattqueue.h, which holds custody of that figure. With the gate in place
    // an idle beat never reaches the collapser at all.
    void anIdleDispatchIsNotLoggedAtAll() {
        BleGattQueue q;
        Recorder rec;
        MessageCapture log;

        for (int i = 0; i < 5; ++i) {
            q.submit(op(scale(), QStringLiteral("scale write"), &rec));
            pump();
            q.noteSucceeded(scale());
            pump();
        }

        QCOMPARE(rec.issued.size(), 5);   // all five ran...
        // ...and none reached the log. Counted rather than inferred from the
        // collapser — see MessageCapture's header for why anything derived from
        // it is blind here.
        QCOMPARE(log.count(dispatchNeedle()), qsizetype(0));
    }

    // Queue DEPTH alone must not produce a line, at the depth the old gate
    // tripped on.
    //
    // This is the regression that shipped in the third attempt: gating on
    // depth >= 2 was silent at idle and therefore looked correct, but the app's
    // own connect sequence is the deepest thing that ever happens on this queue
    // (see the QUEUE_DEPTH_WARN comment in blegattqueue.h for the measured
    // peak), so a healthy launch wrote a line per operation every single time —
    // the count and its evidence are on the gate in blegattqueue.cpp.
    //
    // All three operations belong to the SAME requester, so chargeForeignWait()
    // skips every one of them and foreignWaitMs is identically zero. This slot
    // can therefore only fail to a DEPTH trigger — which is exactly what it is
    // guarding, and what makes it the negative control for the shipped defect.
    void aDeepQueueOfOneDevicesOwnWorkIsNotLogged() {
        BleGattQueue q;
        Recorder rec;
        MessageCapture log;

        q.submit(op(de1(), QStringLiteral("de1-a"), &rec));
        q.submit(op(de1(), QStringLiteral("de1-b"), &rec));
        q.submit(op(de1(), QStringLiteral("de1-c"), &rec));
        pump();
        QCOMPARE(rec.issued, QStringList{QStringLiteral("de1-a")});
        QCOMPARE(log.count(dispatchNeedle()), qsizetype(0));

        // Drain, so the assertion covers depths 1 and 0 as well as 2 rather
        // than only the one the old threshold happened to sit on.
        q.noteSucceeded(de1());
        pump();
        q.noteSucceeded(de1());
        pump();
        QCOMPARE(rec.issued.size(), 3);
        QCOMPARE(log.count(dispatchNeedle()), qsizetype(0));
    }

    // ...but an operation that actually WAITED behind another device is logged,
    // and the depth behind it rides along as PAYLOAD.
    //
    // The two rows are the pair that makes the distinction assertable: depth 2
    // with a wait logs and reports "2 queued", while depth 2 WITHOUT a wait
    // (aDeepQueueOfOneDevicesOwnWorkIsNotLogged) logs nothing at all. Same
    // depth, opposite outcome, so the trigger is demonstrably the wait.
    //
    // Held below FOREIGN_WAIT_WARN_MS deliberately: the near-miss must reach the
    // log as DEBUG context WITHOUT raising the WARN, so this also pins that the
    // two thresholds are independent. init()'s failOnWarning is what makes that
    // half able to fail. Written as a proportion of the constant so it tracks
    // BOTH bounds — above FOREIGN_WAIT_DETAIL_MS to reach the gate, below
    // FOREIGN_WAIT_WARN_MS to stay under the warning.
    void anOperationDelayedByAnotherDeviceIsLogged_data() {
        QTest::addColumn<int>("queuedBehind");
        QTest::addColumn<QString>("expectedDepth");
        QTest::newRow("nothing behind it") << 0 << "(0 queued)";
        QTest::newRow("two behind it")     << 2 << "(2 queued)";
    }

    void anOperationDelayedByAnotherDeviceIsLogged() {
        QFETCH(int, queuedBehind);
        QFETCH(QString, expectedDepth);

        BleGattQueue q;
        Recorder rec;

        q.submit(op(scale(), QStringLiteral("slow-scale"), &rec));
        pump();

        q.submit(op(de1(), QStringLiteral("de1 stop"), &rec));
        for (int i = 0; i < queuedBehind; ++i)
            q.submit(op(de1(), QStringLiteral("de1-filler-%1").arg(i), &rec));
        advanceQueueClock(q, BleGatt::FOREIGN_WAIT_WARN_MS * 3 / 4);

        // Installed AFTER the setup submits, so it counts only the dispatch
        // under test.
        MessageCapture log;
        q.noteSucceeded(scale());
        pump();

        MessageCapture::Entry line;
        QVERIFY(log.single(dispatchNeedle(), &line));
        QVERIFY(line.text.contains(QStringLiteral("dispatch de1 stop")));
        QVERIFY(line.text.contains(expectedDepth));
        // The value the gate fired on has to be IN the line it fired: without it
        // a reader cannot tell a 260 ms wait from a 480 ms one.
        QVERIFY(line.text.contains(QStringLiteral("waited")));
    }

    // TWO devices overlapping briefly is silent too — the slot above covers one
    // device's own backlog, this is the routine interleave of two healthy
    // periodic ones: the DE1's once-a-minute keepalive landing on the scale's
    // 1 Hz heartbeat, clearing ~54 ms apart. It repeats forever, so logging it
    // made this line the loudest source in the app at ~120/hour even after
    // collapsing.
    //
    // NOT redundant with aDeepQueueOfOneDevicesOwnWorkIsNotLogged: two
    // requesters means chargeForeignWait() DOES charge the second operation, so
    // this is the only slot that would catch a gate loosened towards
    // foreignWaitMs > 0. The 54 ms is simulated rather than left to whatever
    // pump() really takes, so the number in this comment is the number the test
    // encodes, and lowering the gate towards the routine interleave turns it
    // red.
    void aBriefOverlapOfTwoDevicesIsNotWorthALine() {
        BleGattQueue q;
        Recorder rec;
        MessageCapture log;

        q.submit(op(de1(), QStringLiteral("de1-keepalive"), &rec));
        q.submit(op(scale(), QStringLiteral("scale write"), &rec));
        pump();
        advanceQueueClock(q, 54);
        q.noteSucceeded(de1());
        pump();

        QCOMPARE(rec.issued, (QStringList{QStringLiteral("de1-keepalive"),
                                          QStringLiteral("scale write")}));
        QCOMPARE(log.count(dispatchNeedle()), qsizetype(0));
    }

    // --- foreign wait reporting -------------------------------------------
    //
    // The one cost the shared queue introduced over the per-device queues it
    // replaced: an operation can now be delayed by a DIFFERENT device. These pin
    // that it is measured and reported, because whether it happens in the field
    // is what decides if further work (suppressing optional scale writes during
    // a shot) is justified. Without the log there is nothing to decide on.

    void waitingBehindAnotherDeviceIsReported() {
        BleGattQueue q;
        Recorder rec;

        q.submit(op(scale(), QStringLiteral("slow-scale"), &rec));
        pump();
        QCOMPARE(q.inFlightRequester(), scale());

        // The DE1's work arrives while the scale holds the radio.
        q.submit(op(de1(), QStringLiteral("de1 stop"), &rec));
        advanceQueueClock(q, BleGatt::FOREIGN_WAIT_WARN_MS + 80);

        // Reported once when the queue goes IDLE, not at the moment the delayed
        // operation dispatches — so the DE1 operation has to finish too.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("worst \\(de1 stop\\) waited [0-9]+ ms")));
        q.noteSucceeded(scale());
        pump();
        q.noteSucceeded(de1());
        pump();

        QVERIFY(!q.isBusy());
    }

    // One line per EPISODE, not per delayed operation. A contended connect
    // delayed six operations in 900 ms on real hardware and produced six
    // identical warnings, which is how a signal meant to mean "something is
    // wrong" becomes scrollback. failOnWarning() plus a single ignoreMessage is
    // what makes "once" able to fail: a second unignored warning fails the slot.
    void severalDelayedOperationsAreReportedAsOneEpisode() {
        BleGattQueue q;
        Recorder rec;

        q.submit(op(scale(), QStringLiteral("slow-scale"), &rec));
        pump();
        q.submit(op(de1(), QStringLiteral("de1-a"), &rec));
        q.submit(op(de1(), QStringLiteral("de1-b"), &rec));
        q.submit(op(de1(), QStringLiteral("de1-c"), &rec));
        advanceQueueClock(q, BleGatt::FOREIGN_WAIT_WARN_MS + 80);

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("3 Bluetooth operation\\(s\\) were delayed")));
        q.noteSucceeded(scale());
        pump();
        q.noteSucceeded(de1());
        pump();
        q.noteSucceeded(de1());
        pump();
        q.noteSucceeded(de1());
        pump();

        QVERIFY(!q.isBusy());
    }

    // A SECOND episode reports only its own operations. Without the counter
    // being cleared, a connect episode's tally is carried into the next one and
    // every later report is inflated — the connect burst would make a single
    // mid-shot delay read as seven. Added because deleting the reset did NOT
    // make any other slot here go red: every one of them has exactly one
    // episode, so none of them could see the carry-over.
    void aSecondEpisodeCountsOnlyItsOwnOperations() {
        BleGattQueue q;
        Recorder rec;

        // Episode one: two operations delayed.
        q.submit(op(scale(), QStringLiteral("scale-1"), &rec));
        pump();
        q.submit(op(de1(), QStringLiteral("de1-a"), &rec));
        q.submit(op(de1(), QStringLiteral("de1-b"), &rec));
        advanceQueueClock(q, BleGatt::FOREIGN_WAIT_WARN_MS + 80);

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("2 Bluetooth operation\\(s\\) were delayed")));
        q.noteSucceeded(scale());
        pump();
        q.noteSucceeded(de1());
        pump();
        q.noteSucceeded(de1());
        pump();
        QVERIFY(!q.isBusy());

        // Episode two: ONE operation delayed. Reported as 1, not 3.
        q.submit(op(scale(), QStringLiteral("scale-2"), &rec));
        pump();
        q.submit(op(de1(), QStringLiteral("de1-c"), &rec));
        advanceQueueClock(q, BleGatt::FOREIGN_WAIT_WARN_MS + 80);

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("1 Bluetooth operation\\(s\\) were delayed")));
        q.noteSucceeded(scale());
        pump();
        q.noteSucceeded(de1());
        pump();
        QVERIFY(!q.isBusy());
    }

    // Charged from when this operation ARRIVED, not from when the in-flight one
    // started. Without that, an operation queued moments before a long
    // discovery ends is reported as having waited the whole discovery — the
    // #1819 capture had a 6 s one, so the figure could be two orders of
    // magnitude high. These numbers are the evidence for whether a
    // stop-at-weight was really delayed, and a wrong number is worse than none.
    void aLateArrivalIsNotChargedForWaitingItDidNotDo() {
        BleGattQueue q;
        Recorder rec;

        q.submit(op(scale(), QStringLiteral("long-scale"), &rec));
        pump();

        // The scale has already held the radio for a good while...
        advanceQueueClock(q, BleGatt::FOREIGN_WAIT_WARN_MS + 200);
        // ...and only now does the DE1's work arrive, waiting a short time.
        q.submit(op(de1(), QStringLiteral("de1 stop"), &rec));
        advanceQueueClock(q, 40);

        // No warning: this operation waited ~40 ms, not the ~700 ms the scale
        // operation had been running. failOnWarning() in init() is the assertion.
        q.noteSucceeded(scale());
        pump();

        QCOMPARE(q.inFlightRequester(), de1());
    }

    // Waiting behind your OWN queued work is expected and must stay silent — a
    // profile upload is ~20 writes and the last one waits for the other 19.
    // init()'s failOnWarning is what makes this assertion real.
    void waitingBehindOwnWorkIsNotReported() {
        BleGattQueue q;
        Recorder rec;

        q.submit(op(de1(), QStringLiteral("first"), &rec));
        pump();
        q.submit(op(de1(), QStringLiteral("second"), &rec));
        advanceQueueClock(q, BleGatt::FOREIGN_WAIT_WARN_MS + 80);

        q.noteSucceeded(de1());
        pump();

        QCOMPARE(rec.issued.size(), 2);
    }

    // A brief foreign wait is the normal case — a healthy scale heartbeat — and
    // must not warn either, or the signal is worthless.
    void aBriefWaitBehindAnotherDeviceIsNotReported() {
        BleGattQueue q;
        Recorder rec;

        q.submit(op(scale(), QStringLiteral("quick-scale"), &rec));
        pump();
        q.submit(op(de1(), QStringLiteral("de1 stop"), &rec));

        q.noteSucceeded(scale());
        pump();

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

    // --- unrunnable operations --------------------------------------------

    // An operation with no issue callback would take the slot, issue nothing,
    // and be ended by nothing — every device's BLE traffic stopped for the rest
    // of the session, with no error. Rejected at submit, where the caller can
    // still be named in the log.
    void anOperationWithNothingToDoIsRejectedRatherThanDispatched() {
        BleGattQueue q;
        BleGattQueue::Operation broken;
        broken.requester = de1();
        broken.label = QStringLiteral("no-issue");

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("submitted with nothing to do")));
        q.submit(std::move(broken));
        pump();

        QCOMPARE(q.pendingCount(), qsizetype(0));
        QVERIFY(!q.isBusy());
    }

    // No requester means no key to complete against: noteSucceeded/noteFailed
    // both match on it, so this one could take the slot and never give it back
    // either.
    void anOperationWithNoOwnerIsRejected() {
        BleGattQueue q;
        Recorder rec;
        BleGattQueue::Operation orphan = op(nullptr, QStringLiteral("orphan"), &rec);

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("submitted with no owner")));
        q.submit(std::move(orphan));
        pump();

        QCOMPARE(q.pendingCount(), qsizetype(0));
        QVERIFY(rec.issued.isEmpty());
    }

    // --- drained(), the release event for work that is NOT a queued operation --
    //
    // A BLE connect is not queued (queueing it would let a scale reconnect
    // mid-shot stall a DE1 write, and stop-at-weight sits on that path), so it
    // competes with queued GATT traffic for the radio instead of waiting its
    // turn. BLEManager defers a scale connect until this fires. That makes it
    // the whole release mechanism — there is no interval and no cap behind it —
    // so it has to be exactly right about "clear".

    void drainedFiresWhenTheLastOperationCompletes() {
        BleGattQueue q;
        Recorder rec;
        QSignalSpy drained(&q, &BleGattQueue::drained);

        q.submit(op(de1(), QStringLiteral("only"), &rec));
        pump();
        QCOMPARE(drained.count(), 0);   // in flight, not clear

        q.noteSucceeded(de1());
        pump();

        QCOMPARE(drained.count(), 1);
        QVERIFY(!q.isBusy());
        QCOMPARE(q.pendingCount(), qsizetype(0));
    }

    // Not on every release — only on the one that empties the queue. A gate
    // released while work remains would put a connect back onto a busy radio,
    // which is the thing it is there to avoid.
    void drainedDoesNotFireWhileWorkRemains() {
        BleGattQueue q;
        Recorder rec;
        QSignalSpy drained(&q, &BleGattQueue::drained);

        q.submit(op(de1(), QStringLiteral("first"), &rec));
        q.submit(op(de1(), QStringLiteral("second"), &rec));
        pump();

        q.noteSucceeded(de1());
        pump();
        QCOMPARE(drained.count(), 0);   // 'second' took the slot

        q.noteSucceeded(de1());
        pump();
        QCOMPARE(drained.count(), 1);
    }

    // An abandoned operation clears the radio just as a successful one does.
    // Missing this would leave a scale connect deferred forever behind work
    // that failed — the failure mode a release-by-event design has to not have.
    void drainedFiresWhenTheLastOperationIsAbandoned() {
        BleGattQueue q;
        Recorder rec;
        QSignalSpy drained(&q, &BleGattQueue::drained);

        q.submit(op(de1(), QStringLiteral("doomed"), &rec));
        pump();
        q.noteFailed(de1());            // no retry budget -> abandoned
        pump();

        QCOMPARE(rec.abandoned, QStringList{QStringLiteral("doomed")});
        QCOMPARE(drained.count(), 1);
    }

    // And when a transport tears down. A dead link must not hold a scale
    // connect any more than it holds the slot.
    void drainedFiresWhenTeardownEmptiesTheQueue() {
        BleGattQueue q;
        Recorder rec;
        QSignalSpy drained(&q, &BleGattQueue::drained);

        q.submit(op(de1(), QStringLiteral("held"), &rec));
        q.submit(op(de1(), QStringLiteral("queued"), &rec));
        pump();

        q.forget(de1());
        pump();

        QCOMPARE(drained.count(), 1);
        QVERIFY(!q.isBusy());
    }

    // discard() is the one mutator that can empty the queue without any
    // dispatch being posted and without any slot transition, so it is the one
    // path that would silently never report idle. BLEManager's deferred scale
    // connect waits on drained(); missing it here strands the connect until
    // some unrelated traffic happens to end.
    void drainedFiresWhenDiscardEmptiesTheQueue() {
        BleGattQueue q;
        Recorder rec;
        QSignalSpy drained(&q, &BleGattQueue::drained);

        const QBluetoothUuid frame = DE1::Characteristic::FRAME_WRITE;

        q.submit(op(de1(), QStringLiteral("dead-frame"), &rec, {}, frame));
        QCOMPARE(q.discard(de1(), {frame}), qsizetype(1));
        pump();

        QCOMPARE(drained.count(), 1);
        QVERIFY(!q.isBusy());
        QVERIFY(rec.issued.isEmpty());
    }

    // Teardown that leaves ANOTHER device's work behind is not "clear".
    void teardownDoesNotReportDrainedWhileAnotherDeviceIsWaiting() {
        BleGattQueue q;
        Recorder rec;
        QSignalSpy drained(&q, &BleGattQueue::drained);

        q.submit(op(de1(), QStringLiteral("de1-held"), &rec));
        q.submit(op(scale(), QStringLiteral("scale-queued"), &rec));
        pump();

        q.forget(de1());
        pump();

        QCOMPARE(drained.count(), 0);   // the scale's operation took the slot
        QCOMPARE(q.inFlightRequester(), scale());
    }

    // The label is what the deferral log line names as "what is holding the
    // radio". A submitted log with an empty name there answers nothing.
    void theInFlightLabelIsAvailableForLogging() {
        BleGattQueue q;
        Recorder rec;
        QVERIFY(q.inFlightLabel().isEmpty());

        q.submit(op(de1(), QStringLiteral("write a00e"), &rec));
        pump();

        QCOMPARE(q.inFlightLabel(), QStringLiteral("write a00e"));
    }

    // --- depth reporting --------------------------------------------------

    // Edge-triggered: one warning per episode. failOnWarning() in init() is
    // what makes "once" able to fail — a second unignored warning fails the
    // slot.
    void depthWarningFiresOncePerEpisode() {
        BleGattQueue q;
        Recorder rec;

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("Bluetooth operations are queued at once")));
        for (int i = 0; i < BleGattQueue::QUEUE_DEPTH_WARN + 5; ++i)
            q.submit(op(de1(), QStringLiteral("x"), &rec));

        // Drain everything so the queue does not report again at destruction.
        q.forget(de1());
    }
};

QTEST_MAIN(tst_BleGattQueue)
#include "tst_blegattqueue.moc"
