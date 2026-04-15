#include <QtTest>
#include <QRegularExpression>
#include <QSignalSpy>

#include "ble/de1device.h"
#include "ble/protocol/de1characteristics.h"
#include "profile/profile.h"
#include "profile/profileframe.h"
#include "mocks/MockTransport.h"

// Verifies DE1Device::uploadProfile()'s frame-ACK verification (modeled on
// de1app's confirm_de1_send_shot_frames_worked). Regression test for the
// "shot ended early, DE1 was running a stale/mismatched profile" bug where
// the per-write counter said "success" but the frames hadn't actually landed
// in the right order on the DE1. After this change, the profileUploaded()
// signal carries a real verdict: true iff the FRAME_WRITE ACKs' leading
// FrameToWrite bytes matched the sequence we queued, in order.

class tst_ProfileUpload : public QObject {
    Q_OBJECT

private:
    // Build a minimal frame-based profile with two steps — enough to exercise
    // regular frames + tail frame without pulling in extension frames.
    static Profile makeSimpleProfile() {
        Profile p;
        p.setTitle(QStringLiteral("test-profile"));
        p.setMode(Profile::Mode::FrameBased);

        QList<ProfileFrame> steps;

        ProfileFrame fill;
        fill.name = QStringLiteral("fill");
        fill.pump = QStringLiteral("pressure");
        fill.pressure = 4.0;
        fill.flow = 6.0;
        fill.temperature = 93.0;
        fill.seconds = 8.0;
        fill.volume = 60;
        steps.append(fill);

        ProfileFrame pour;
        pour.name = QStringLiteral("pour");
        pour.pump = QStringLiteral("flow");
        pour.pressure = 9.0;
        pour.flow = 2.0;
        pour.temperature = 93.0;
        pour.seconds = 30.0;
        pour.volume = 100;
        steps.append(pour);

        p.setSteps(steps);
        return p;
    }

private slots:

    // ===== Happy path: all ACKs arrive, in order =====

    void uploadSucceedsWhenAcksMatchQueueOrder() {
        MockTransport transport;
        DE1Device device;
        device.setTransport(&transport);

        QSignalSpy spy(&device, &DE1Device::profileUploaded);

        Profile p = makeSimpleProfile();
        device.uploadProfile(p);

        // Sanity: 1 header + N frames (2 steps → 2 regular + 1 tail = 3 frames)
        QCOMPARE(transport.writes.size(), 4);
        QCOMPARE(transport.writes.first().first, DE1::Characteristic::HEADER_WRITE);
        QCOMPARE(transport.writes.at(1).first, DE1::Characteristic::FRAME_WRITE);
        QCOMPARE(transport.writes.at(2).first, DE1::Characteristic::FRAME_WRITE);
        QCOMPARE(transport.writes.at(3).first, DE1::Characteristic::FRAME_WRITE);

        // Replay ACKs in queue order and confirm the upload resolves as success.
        transport.ackAllWritesInOrder();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);
    }

    // ===== Failure path: a frame ACK goes missing =====

    void uploadFailsWhenFrameAckIsDropped() {
        MockTransport transport;
        DE1Device device;
        device.setTransport(&transport);

        QSignalSpy spy(&device, &DE1Device::profileUploaded);

        // The failure path emits a qWarning describing the mismatch —
        // that's the behaviour we want to verify is still happening, so mark
        // it as expected rather than letting it show up as noise.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("profile upload FAILED — frame sequence mismatch"));

        device.uploadProfile(makeSimpleProfile());

        // Ack the header, then emit the first frame's ACK three times in a
        // row (never ACKing the later frames at all). The count satisfies
        // the tracker (three FRAME_WRITE ACKs == three queued frames), but
        // the observed sequence is [frame0, frame0, frame0] instead of the
        // expected [frame0, frame1, tail], so the sequence check fires.
        // In a well-behaved stack duplicate ACKs shouldn't happen, but it's
        // the cheapest way to trip the sequence check without waiting out
        // the 10-second timeout.
        emit transport.writeComplete(DE1::Characteristic::HEADER_WRITE,
                                     transport.writes.at(0).second);
        emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                     transport.writes.at(1).second);
        emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                     transport.writes.at(1).second);
        emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                     transport.writes.at(1).second);

        QCOMPARE(spy.count(), 1);
        auto args = spy.takeFirst();
        QCOMPARE(args.at(0).toBool(), false);
        // The reason argument (index 1) carries the same text the qWarning
        // logs. Listeners (e.g. ProfileManager's retry loop) pattern-match on
        // it to decide whether to retry — lock it down.
        QVERIFY(args.at(1).toString().startsWith("frame sequence mismatch"));
    }

    // ===== Failure path: frames ACKed out of order =====

    void uploadFailsWhenFramesAckedOutOfOrder() {
        MockTransport transport;
        DE1Device device;
        device.setTransport(&transport);

        QSignalSpy spy(&device, &DE1Device::profileUploaded);

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("profile upload FAILED — frame sequence mismatch"));

        device.uploadProfile(makeSimpleProfile());

        // Header first, then frames in reversed order. Count matches, but the
        // FrameToWrite bytes will arrive as [tail, frame1, frame0] instead of
        // [frame0, frame1, tail] — the sequence check must catch this.
        emit transport.writeComplete(DE1::Characteristic::HEADER_WRITE,
                                     transport.writes.at(0).second);
        emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                     transport.writes.at(3).second);
        emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                     transport.writes.at(2).second);
        emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                     transport.writes.at(1).second);

        QCOMPARE(spy.count(), 1);
        auto args = spy.takeFirst();
        QCOMPARE(args.at(0).toBool(), false);
        QVERIFY(args.at(1).toString().startsWith("frame sequence mismatch"));
    }

    // ===== Unrelated writes don't satisfy the tracker =====

    void unrelatedWritesDoNotCountTowardUploadCompletion() {
        MockTransport transport;
        DE1Device device;
        device.setTransport(&transport);

        QSignalSpy spy(&device, &DE1Device::profileUploaded);

        device.uploadProfile(makeSimpleProfile());

        // An MMR or ShotSettings write completing mid-upload must not bump
        // the counter into a false "success" verdict.
        emit transport.writeComplete(DE1::Characteristic::HEADER_WRITE,
                                     transport.writes.at(0).second);
        emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                     transport.writes.at(1).second);
        // Bogus unrelated write — should be ignored.
        emit transport.writeComplete(DE1::Characteristic::SHOT_SETTINGS,
                                     QByteArray(9, 0));

        // No profileUploaded signal yet — upload still in flight.
        QCOMPARE(spy.count(), 0);

        // Now finish with the remaining legit ACKs.
        emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                     transport.writes.at(2).second);
        emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                     transport.writes.at(3).second);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);
    }

    // ===== Espresso-start variant: requires REQUESTED_STATE(Espresso) ACK =====

    void uploadAndStartEspressoRequiresEspressoAck() {
        MockTransport transport;
        DE1Device device;
        device.setTransport(&transport);

        QSignalSpy spy(&device, &DE1Device::profileUploaded);

        device.uploadProfileAndStartEspresso(makeSimpleProfile());

        // 1 header + 3 frames + 1 requested-state = 5 writes
        QCOMPARE(transport.writes.size(), 5);
        QCOMPARE(transport.writes.last().first, DE1::Characteristic::REQUESTED_STATE);

        // Ack everything except the espresso-start write. Upload must NOT
        // resolve yet.
        emit transport.writeComplete(DE1::Characteristic::HEADER_WRITE,
                                     transport.writes.at(0).second);
        for (qsizetype i = 1; i <= 3; ++i) {
            emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                         transport.writes.at(i).second);
        }
        QCOMPARE(spy.count(), 0);

        // Now ack the espresso-start write — upload resolves as success.
        emit transport.writeComplete(DE1::Characteristic::REQUESTED_STATE,
                                     transport.writes.at(4).second);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);
    }

    // ===== Regression: leftover FRAME_WRITE acks from a prior batch ignored =====
    //
    // Reproduces the first-connect profile-upload failure documented in the
    // logs: sendInitialSettings() writes a basic profile (1 HEADER + 2 FRAMEs)
    // to the DE1 before the user profile is uploaded. Those writes sit in the
    // transport's queue when startProfileUploadTracking() attaches to
    // writeComplete. As they drain, their FRAME_WRITE acks leak into the new
    // tracker, so the first three "seen" frame bytes are [0x00, 0x01, ...]
    // from the basic frames instead of the user profile's real frames, and
    // the sequence check fails. The retry fires 1s later, by which time the
    // queue is drained, and succeeds.
    //
    // Fix: treat HEADER_WRITE completion as a barrier — clear any frame bytes
    // accumulated before our header lands, and ignore FRAME_WRITE acks that
    // arrive before the header. This test asserts both behaviours.

    void leakedFrameAcksBeforeOurHeaderAreIgnored() {
        MockTransport transport;
        DE1Device device;
        device.setTransport(&transport);

        QSignalSpy spy(&device, &DE1Device::profileUploaded);

        device.uploadProfile(makeSimpleProfile());

        // Simulate the two basic-profile FRAME_WRITE acks from a prior
        // (non-tracked) batch landing AFTER our tracker attaches but BEFORE
        // our own HEADER_WRITE is acked. Our gate should drop them.
        emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                     QByteArray(1, char(0x00)));  // basic frame 0
        emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                     QByteArray(1, char(0x01)));  // basic tail frame

        // No verdict yet — leaked frames are ignored, header still pending.
        QCOMPARE(spy.count(), 0);

        // Now our actual acks flow through in order.
        emit transport.writeComplete(DE1::Characteristic::HEADER_WRITE,
                                     transport.writes.at(0).second);
        emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                     transport.writes.at(1).second);
        emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                     transport.writes.at(2).second);
        emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                     transport.writes.at(3).second);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);
    }

    // Variant of the above: the prior batch's HEADER_WRITE ack ALSO arrives
    // after tracker attach (plausible when the tracker attaches between the
    // prior header's queueing and its completion). That header would flip
    // m_uploadHeaderAcked = true and the subsequent leaked frame acks would
    // pass the gate. The fix additionally clears accumulated frame bytes on
    // every HEADER_WRITE — so when our own header lands, the leaked frames
    // are wiped out before our real frames accumulate.

    void leakedHeaderAndFrameAcksAreWipedByOurHeader() {
        MockTransport transport;
        DE1Device device;
        device.setTransport(&transport);

        QSignalSpy spy(&device, &DE1Device::profileUploaded);

        device.uploadProfile(makeSimpleProfile());

        // Simulate the full prior batch (HEADER + 2 FRAMEs) landing after
        // tracker attach. The leaked frames would be appended, but our own
        // HEADER_WRITE ack must wipe them.
        emit transport.writeComplete(DE1::Characteristic::HEADER_WRITE,
                                     QByteArray(5, 0));           // prior header
        emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                     QByteArray(1, char(0x00)));  // prior frame 0
        emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                     QByteArray(1, char(0x01)));  // prior tail frame

        QCOMPARE(spy.count(), 0);

        // Our own header lands — wipes the leaked frames.
        emit transport.writeComplete(DE1::Characteristic::HEADER_WRITE,
                                     transport.writes.at(0).second);
        emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                     transport.writes.at(1).second);
        emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                     transport.writes.at(2).second);
        emit transport.writeComplete(DE1::Characteristic::FRAME_WRITE,
                                     transport.writes.at(3).second);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);
    }

    // ===== Disconnect mid-upload surfaces a failure =====

    void disconnectMidUploadReportsFailure() {
        MockTransport transport;
        DE1Device device;
        device.setTransport(&transport);

        QSignalSpy spy(&device, &DE1Device::profileUploaded);

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("profile upload FAILED — BLE disconnect during upload"));

        device.uploadProfile(makeSimpleProfile());
        // Ack header, then tear down — the listener must see a failure
        // verdict rather than hanging indefinitely.
        emit transport.writeComplete(DE1::Characteristic::HEADER_WRITE,
                                     transport.writes.at(0).second);
        device.disconnect();

        QCOMPARE(spy.count(), 1);
        auto args = spy.takeFirst();
        QCOMPARE(args.at(0).toBool(), false);
        QCOMPARE(args.at(1).toString(), QStringLiteral("BLE disconnect during upload"));
    }

    // ===== Unexpected transport drop surfaces the same non-retryable reason =====

    void unexpectedTransportDropReportsBleDisconnect() {
        // Unlike disconnectMidUploadReportsFailure() (which drives the
        // app-initiated DE1Device::disconnect() path), this test simulates
        // the transport layer firing disconnected() on its own — e.g. the
        // DE1 powered off, or BLE link-loss. DE1Device::onTransportDisconnected
        // must finish the in-flight upload with the "BLE disconnect during
        // upload" reason so it's classified non-retryable. Without this,
        // the 10-second upload-timeout timer would eventually fire with
        // "timeout waiting for write ACKs" (retryable) and poison any
        // downstream retry counter across the reconnect.
        MockTransport transport;
        DE1Device device;
        device.setTransport(&transport);

        QSignalSpy spy(&device, &DE1Device::profileUploaded);
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("profile upload FAILED — BLE disconnect during upload"));

        device.uploadProfile(makeSimpleProfile());
        // Ack header to get the tracker past its first write, then simulate
        // the transport going away. Note this bypasses DE1Device::disconnect
        // and only fires DE1Transport::disconnected, which is the path
        // that onTransportDisconnected handles.
        emit transport.writeComplete(DE1::Characteristic::HEADER_WRITE,
                                     transport.writes.at(0).second);
        transport.setConnectedSim(false);

        QCOMPARE(spy.count(), 1);
        auto args = spy.takeFirst();
        QCOMPARE(args.at(0).toBool(), false);
        QCOMPARE(args.at(1).toString(), QStringLiteral("BLE disconnect during upload"));
    }
};

QTEST_GUILESS_MAIN(tst_ProfileUpload)
#include "tst_profileupload.moc"
