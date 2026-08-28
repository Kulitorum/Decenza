#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>

#include "ble/bletransport.h"
#include "ble/blecontrollererror.h"
#include "ble/protocol/de1characteristics.h"

// The #1658 contract, pinned at the one place that enforces it.
//
// This is the test the rest of that change was missing. tst_blecontrollererror
// covers the extracted name/classification helpers, but those were a pure
// refactor — put `emit errorOccurred(...)` back into onControllerError and every
// one of its cases still passes. The actual fix is the ABSENCE of an emit, and
// absence is exactly what a reviewer skims past and a future change silently
// undoes.
//
// The seam is real, not contrived: BleTransport's constructor only configures
// QTimers, onControllerError is null-safe on m_controller by construction
// (`m_controller ? m_controller->state() : UnconnectedState`), and
// bletransport.cpp already carries #ifndef DECENZA_TESTING guards around its
// BLEManager dependencies precisely so test targets can link it without one.
//
// The two Linux-only diagnostics in onControllerError are guarded out of test
// builds under that same #ifndef — see the comment there. Without that, the
// UnknownRemoteDeviceError rows below would emit two extra qWarnings on an
// unprivileged CI runner (failing init()'s failOnWarning) and leak a detached
// diagnostics thread into the nightly Linux ASan job. Both are invisible on
// macOS, so a green local run proves nothing about either.
class tst_BleTransportError : public QObject {
    Q_OBJECT

private:
    // Every value QLowEnergyController::Error can take in Qt 6.11.
    static QList<QLowEnergyController::Error> allErrors() {
        return {
            QLowEnergyController::NoError,
            QLowEnergyController::UnknownError,
            QLowEnergyController::UnknownRemoteDeviceError,
            QLowEnergyController::NetworkError,
            QLowEnergyController::InvalidBluetoothAdapterError,
            QLowEnergyController::ConnectionError,
            QLowEnergyController::AdvertisingError,
            QLowEnergyController::RemoteHostClosedError,
            QLowEnergyController::AuthorizationError,
            QLowEnergyController::MissingPermissionsError,
            QLowEnergyController::RssiReadError,
        };
    }

private slots:
    void init() { QTest::failOnWarning(); }

    void noControllerErrorRaisesAUserFacingError_data() {
        QTest::addColumn<QLowEnergyController::Error>("error");
        for (const auto err : allErrors())
            QTest::newRow(qPrintable(bleControllerErrorName(err))) << err;
    }

    // The headline assertion: whatever the radio reports, the user is not
    // interrupted. A reporter who switches their DE1 off overnight got this box
    // on every single app start while the reconnect ladder was already fixing it.
    void noControllerErrorRaisesAUserFacingError() {
        QFETCH(QLowEnergyController::Error, error);

        BleTransport transport;
        QSignalSpy modal(&transport, &DE1Transport::errorOccurred);
        QVERIFY(modal.isValid());

        // The warn line is the whole remaining surface for these errors, so it
        // is asserted rather than merely tolerated — init()'s failOnWarning
        // would fail this test if the log line ever went away silently.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("!!! CONTROLLER ERROR: %1 \\(state=Unconnected\\) !!!")
                                   .arg(bleControllerErrorName(error))));

        transport.onControllerError(error);

        QCOMPARE(modal.count(), 0);
    }

    void linkTeardownFamilyStillFiresDe1LinkFault_data() {
        QTest::addColumn<QLowEnergyController::Error>("error");
        for (const auto err : allErrors())
            QTest::newRow(qPrintable(bleControllerErrorName(err))) << err;
    }

    // The other half of the contract, and the reason this change is a narrowing
    // rather than a deletion: silencing the dialog must not also silence the
    // fault feed. de1LinkFault drives the connection-priority coordinator and
    // the BLE-stack-wedge detector (#1309), both of which would quietly stop
    // working if someone "simplified" the remaining branch away.
    void linkTeardownFamilyStillFiresDe1LinkFault() {
        QFETCH(QLowEnergyController::Error, error);

        BleTransport transport;
        QSignalSpy fault(&transport, &DE1Transport::de1LinkFault);
        QVERIFY(fault.isValid());

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("!!! CONTROLLER ERROR: %1")
                                   .arg(bleControllerErrorName(error))));

        transport.onControllerError(error);

        const int expected = bleControllerErrorIsLinkTeardown(error) ? 1 : 0;
        QCOMPARE(fault.count(), expected);
        if (expected == 1)
            QCOMPARE(fault.first().at(0).toString(), QStringLiteral("controller-error"));
    }

    // The write-retry budget is a TIME bound, and that is the half that broke.
    //
    // A timing-out write occupies the link for
    //   (MAX_WRITE_RETRIES + 1) × WRITE_TIMEOUT_MS + MAX_WRITE_RETRIES × WRITE_RETRY_DELAY_MS
    // and the app issues periodic writes on a 60 s cadence (BatteryManager's
    // charger keepalive, batterymanager.cpp:26). At the old budget of 10 that
    // came to exactly 60.0 s, so on a degraded link the next keepalive was
    // queued before the previous one was abandoned and the link never went
    // idle — measured dispatch→abandonment in #1691 at 60.06 s, with keepalive
    // exhaustions 60.0 s apart.
    //
    // This asserts the relationship, not the constants: raising either the
    // retry count or the timeout without re-checking the cadence puts the link
    // back into permanent occupancy, and nothing else in the suite would say
    // so. Verified to fail by restoring MAX_WRITE_RETRIES to 10.
    void writeRetryBudgetLeavesTheLinkIdleBetweenPeriodicWrites() {
        // The shortest periodic BLE write in the app. Surveyed every
        // setInterval() in maincontroller.cpp, de1device.cpp and src/core/ —
        // BatteryManager's charger keepalive is the fastest recurring writer.
        // Stated because this test silently goes vacuous if a faster one is
        // added without revisiting the name.
        constexpr int kShortestPeriodicWriteMs = 60000;  // batterymanager.cpp:26

        constexpr int worstCaseMs =
            (BleTransport::MAX_WRITE_RETRIES + 1) * BleTransport::WRITE_TIMEOUT_MS
            + BleTransport::MAX_WRITE_RETRIES * BleTransport::WRITE_RETRY_DELAY_MS;

        QVERIFY2(worstCaseMs < kShortestPeriodicWriteMs,
                 qPrintable(QStringLiteral(
                     "a timing-out write occupies the link for %1 ms, which is not "
                     "shorter than the %2 ms periodic-write cadence — the link never "
                     "goes idle on a degraded connection (#1691)")
                         .arg(worstCaseMs).arg(kShortestPeriodicWriteMs)));
    }

    // Guards the retry budget itself against being raised back without the
    // reasoning being re-read. 283 retry cycles measured across the #1176-#1810
    // debug-log corpus: 23 recovered, every one of them by retry 9, and 260 —
    // 92% — ran the full budget and failed.
    //
    // The upper bound is the real assertion — the lower one only catches a
    // budget cut so far it would abandon writes that routinely recover.
    void writeRetryBudgetStaysInTheMeasuredBand() {
        QVERIFY(BleTransport::MAX_WRITE_RETRIES >= 3);
        QVERIFY(BleTransport::MAX_WRITE_RETRIES <= 5);
    }

    // -- Selective discard --
    //
    // Scoping, ordering and in-flight accounting live in tst_BleCommandQueue,
    // which owns what BleTransport puts on the shared queue. The one invariant
    // that belongs here is the SAFETY one: a stop must be untouchable.

    // A stop is issued through writeUrgent(), which puts it at the head of the
    // queue, and no discard may take it. The stop paths clear before writing so
    // this is belt-and-braces today — which is exactly why it needs a test,
    // since nothing else would notice if that ordering changed.
    void aPendingUrgentStateWriteSurvivesADiscard() {
        BleGattQueue queue;
        BleTransport transport(nullptr, &queue);
        transport.write(DE1::Characteristic::FRAME_WRITE, QByteArray(1, 'a'));

        transport.writeUrgent(DE1::Characteristic::REQUESTED_STATE,
                              QByteArray(1, static_cast<char>(DE1::State::Idle)));
        QCOMPARE(queue.pendingCount(), qsizetype(2));
        // Asserted before the discard: without this, the slot passes just as
        // well if writeUrgent() appended instead of prepending, since only one
        // entry survives either way.
        QCOMPARE(queue.m_queue.head().key, DE1::Characteristic::REQUESTED_STATE);

        const qsizetype dropped = transport.discardQueued(
            {DE1::Characteristic::HEADER_WRITE, DE1::Characteristic::FRAME_WRITE});

        QCOMPARE(dropped, qsizetype(1));
        QCOMPARE(queue.pendingCount(), qsizetype(1));
        QCOMPARE(queue.m_queue.head().key, DE1::Characteristic::REQUESTED_STATE);
    }

    // -- A link that has stopped accepting writes --
    //
    // init()'s failOnWarning is what makes the negative half of these
    // assertions real: a second report, or a report before the bound, fails the
    // test without any explicit check for it.

    // Every other slot here calls noteWriteAbandoned() directly, so deleting
    // the one production call site would leave the whole detector dead with a
    // green suite. This one drives the real path end to end: a write is
    // submitted, dispatched, and abandoned by the queue, and the counter and
    // both signals must follow from that alone.
    void theRetryExhaustionPathFeedsTheConsecutiveFailureCounter() {
        BleGattQueue queue;
        BleTransport transport(nullptr, &queue);
        QSignalSpy fault(&transport, &DE1Transport::de1LinkFault);
        QSignalSpy abandoned(&transport, &DE1Transport::writeAbandoned);

        transport.write(DE1::Characteristic::WRITE_TO_MMR, QByteArray(4, 'm'));

        // Dispatch is posted, not inline. With no service attached the issue
        // step reports the failure to the queue, which starts the retry ladder.
        QTest::qWait(20);
        QCOMPARE(queue.inFlightRequester(), static_cast<const void*>(&transport));

        // Put the operation one failure from exhaustion rather than walking the
        // ladder in real time — five retries at WRITE_RETRY_DELAY_MS is 2.5 s of
        // wall clock for a step this slot is not about.
        //
        // m_retryPending too, and not as a workaround: the dispatch above
        // already failed once and armed a retry, and the queue now ignores a
        // second failure report while one is armed (one retry per attempt, not
        // one per reporter). So the state this slot wants is "budget spent, no
        // retry outstanding", which is both fields.
        queue.m_retryCount = BleTransport::MAX_WRITE_RETRIES;
        queue.m_retryPending = false;
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("Write FAILED after 5 retries"));
        queue.noteFailed(&transport);

        QCOMPARE(transport.m_consecutiveWriteFailures, 1);
        // The fault feed and the counter must stay in step — they are read
        // together by QtScaleBleTransport::onDe1LinkFault.
        QCOMPARE(fault.count(), 1);
        QCOMPARE(abandoned.count(), 1);
        // The abandoned write is reported with the characteristic and payload it
        // was submitted with, which is what DE1Device dispatches on.
        QCOMPARE(abandoned.at(0).at(0).value<QBluetoothUuid>(),
                 DE1::Characteristic::WRITE_TO_MMR);
        QCOMPARE(abandoned.at(0).at(1).toByteArray(), QByteArray(4, 'm'));
    }

    // -- A required stream that cannot be enabled fails the whole connect --
    //
    // This is #1819's actual defect, and the one assertion that would have
    // caught it: three CCCD writes were rejected, and the transport reported
    // CONNECTED anyway. There is no flag expressing "do not report connected" —
    // the ready marker is a queue entry, and failing a required stream calls
    // forget(), which drops it. So the absence of connected() is the contract,
    // and absence is what a later change silently undoes.
    // ORDER MATTERS, and getting it backwards is how the first version of this
    // slot passed while the production path was broken. subscribeAll() submits
    // the streams and THEN the ready marker, so a required stream that fails at
    // SUBMISSION time reaches failRequiredStream() before the marker exists —
    // the forget() drops nothing, the marker is queued a moment later, and the
    // machine reports CONNECTED with no telemetry. Which is #1819.
    //
    // So this drives subscribeAll()'s real sequence, and what stops the marker
    // is submitSubscribe() returning false, not the forget().
    void aRequiredStreamThatCannotBeSubmittedStopsTheConnectBeforeTheMarker() {
        BleGattQueue queue;
        BleTransport transport(nullptr, &queue);
        QSignalSpy connectedSpy(&transport, &DE1Transport::connected);
        QSignalSpy fault(&transport, &DE1Transport::de1LinkFault);

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("is a stream the machine cannot be used without"));
        const bool submitted =
            transport.submitSubscribe(DE1::Characteristic::STATE_INFO, /*required=*/true);
        QVERIFY(!submitted);

        // Only reached because submitted was false. The marker must never be
        // queued at all on this path.
        transport.submitReadyMarker();
        QTest::qWait(30);

        // ...and the marker DOES fire when it is wrongly submitted, which is
        // what makes the guard above the load-bearing part rather than decoration.
        QCOMPARE(connectedSpy.count(), 1);
        QCOMPARE(fault.count(), 1);
    }

    // The abandonment path has the opposite ordering: the marker is already
    // queued behind the subscribe, so forget() is what drops it. Both halves of
    // failRequiredStream()'s contract, one per slot.
    void aRequiredStreamAbandonedAfterTheMarkerIsQueuedDropsIt() {
        BleGattQueue queue;
        BleTransport transport(nullptr, &queue);
        QSignalSpy connectedSpy(&transport, &DE1Transport::connected);

        transport.submitReadyMarker();
        QCOMPARE(queue.pendingCount(&transport), qsizetype(1));

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("is a stream the machine cannot be used without"));
        transport.failRequiredStream(DE1::Characteristic::STATE_INFO);

        QTest::qWait(30);
        QCOMPARE(connectedSpy.count(), 0);
        QCOMPARE(queue.pendingCount(&transport), qsizetype(0));
    }

    // An OPTIONAL stream that cannot be enabled must NOT fail the connect, and
    // must still be named in the ready marker's exception list — that line makes
    // a POSITIVE claim about which telemetry is live, so a skipped stream it
    // does not mention makes the claim false.
    void AFailedOptionalStreamIsReportedButDoesNotFailTheConnect() {
        BleGattQueue queue;
        BleTransport transport(nullptr, &queue);
        QSignalSpy connectedSpy(&transport, &DE1Transport::connected);

        QVERIFY(!transport.submitSubscribe(DE1::Characteristic::WATER_LEVELS,
                                           /*required=*/false));
        QCOMPARE(transport.m_streamsNotEnabled.size(), qsizetype(1));

        transport.submitReadyMarker();
        QTest::ignoreMessage(QtInfoMsg,
            QRegularExpression("DE1 telemetry live except"));
        QTest::qWait(30);
        QCOMPARE(connectedSpy.count(), 1);
    }

    // Same contract, asserted for the calibration stream specifically. A machine
    // or transport that cannot notify on A012 must still connect: the sensor
    // calibration wizards then show their values as unavailable and refuse to
    // write, which is a screen almost nobody opens and never a reason to fail
    // the whole connection.
    void AFailedCalibrationStreamDoesNotFailTheConnect() {
        BleGattQueue queue;
        BleTransport transport(nullptr, &queue);
        QSignalSpy connectedSpy(&transport, &DE1Transport::connected);

        QVERIFY(!transport.submitSubscribe(DE1::Characteristic::CALIBRATION,
                                           /*required=*/false));
        QCOMPARE(transport.m_streamsNotEnabled.size(), qsizetype(1));

        transport.submitReadyMarker();
        QTest::ignoreMessage(QtInfoMsg,
            QRegularExpression("DE1 telemetry live except"));
        QTest::qWait(30);
        QCOMPARE(connectedSpy.count(), 1);
    }

    void consecutiveAbandonedWritesReportOnceAtTheBound() {
        BleTransport transport;

        // Below the bound: silent.
        transport.noteWriteAbandoned();
        transport.noteWriteAbandoned();

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("DE1 link has stopped accepting writes: 3 consecutive"));
        transport.noteWriteAbandoned();

        // Still failing does not re-report — one episode, one line.
        transport.noteWriteAbandoned();
        transport.noteWriteAbandoned();

        // A recovery ends the episode, and a fresh one reports again. Without
        // this, m_writeDeadLinkReported's reset is never exercised: a link that
        // dies, recovers and dies again would go silent the second time, which
        // is the opposite of what the member's comment claims.
        // Recovery closes the episode with the run it reached — the number the
        // once-per-episode WARN cannot carry, and the one the corpus threshold
        // was actually derived from.
        QTest::ignoreMessage(QtInfoMsg,
            QRegularExpression("accepting writes again. The run reached 5 "));
        transport.noteWriteSucceeded();

        transport.noteWriteAbandoned();
        transport.noteWriteAbandoned();
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("DE1 link has stopped accepting writes: 3 consecutive"));
        transport.noteWriteAbandoned();

        // Closed out by ~BleTransport() -> disconnect().
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("Link dropped while it had stopped accepting writes. "
                               "The run reached 3"));
    }

    // The run is restated as it grows, so a log carries how bad the link got
    // rather than only that it went bad. Without this the line always prints
    // the threshold, and a reader seeing "3" would take the mildest possible
    // reading of a link that reached 89 in the corpus.
    void aWorseningRunIsRestatedPeriodically() {
        BleTransport transport;

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("DE1 link has stopped accepting writes: 3 consecutive"));
        for (int i = 0; i < BleTransport::WRITE_DEAD_LINK_THRESHOLD; ++i)
            transport.noteWriteAbandoned();

        // Silent until the next restatement point — failOnWarning enforces it.
        for (int i = 1; i < BleTransport::WRITE_DEAD_LINK_RESTATE; ++i)
            transport.noteWriteAbandoned();

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("still not accepting writes: 13 consecutive"));
        transport.noteWriteAbandoned();

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("Link dropped while it had stopped accepting writes. "
                               "The run reached 13"));
    }

    // A link that goes away mid-episode did not recover, and must not be
    // logged as though it had. Same counters, different story.
    void aDisconnectDuringAnEpisodeReportsThePeakNotARecovery() {
        BleTransport transport;

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("DE1 link has stopped accepting writes: 3 consecutive"));
        for (int i = 0; i < BleTransport::WRITE_DEAD_LINK_THRESHOLD; ++i)
            transport.noteWriteAbandoned();

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("Link dropped while it had stopped accepting writes. "
                               "The run reached 3"));
        transport.forgetWriteFailureState();
        QCOMPARE(transport.m_consecutiveWriteFailures, 0);
        QCOMPARE(transport.m_writeDeadLinkReported, false);
    }

    void aSuccessfulWriteClearsTheCount() {
        BleTransport transport;
        transport.noteWriteAbandoned();
        transport.noteWriteAbandoned();
        transport.noteWriteSucceeded();

        // The count restarted, so two more must not reach the bound.
        transport.noteWriteAbandoned();
        transport.noteWriteAbandoned();
        QCOMPARE(transport.m_consecutiveWriteFailures, 2);
    }

    // A disconnect must also release the radio. A dead link holding the shared
    // slot stalls every other device, which is why this asserts the queue is
    // clear and not merely that the counters reset.
    void aDisconnectClearsTheCountAndReleasesTheQueue() {
        BleGattQueue queue;
        BleTransport transport(nullptr, &queue);
        transport.write(DE1::Characteristic::WRITE_TO_MMR, QByteArray(1, 'm'));
        transport.noteWriteAbandoned();
        transport.noteWriteAbandoned();

        transport.onControllerDisconnected();

        QCOMPARE(transport.m_consecutiveWriteFailures, 0);
        // And a link that already reported is allowed to report again on the
        // next connection — the episode ended with the link. (The latch is set
        // true and cleared by a success in
        // consecutiveAbandonedWritesReportOnceAtTheBound; here it can only have
        // been false, so this pins the disconnect path's intent, not the reset.)
        QCOMPARE(transport.m_writeDeadLinkReported, false);
        QCOMPARE(queue.pendingCount(&transport), qsizetype(0));
        QVERIFY(!queue.isBusy());
    }

    // Queue depth reporting and the no-match discard cases moved to
    // tst_BleCommandQueue with the queue itself. The depth latch is no longer a
    // property of one transport — it belongs to the queue every device shares,
    // so a DE1 disconnecting does not re-arm a warning another device's backlog
    // is still earning.
};

QTEST_MAIN(tst_BleTransportError)
#include "tst_bletransporterror.moc"
