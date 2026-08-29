#include <QtTest>
#include <QRegularExpression>
#include <QSignalSpy>

#include "ble/de1device.h"
#include "ble/protocol/de1characteristics.h"
#include "profile/profile.h"
#include "profile/profileframe.h"
#include "mocks/MockTransport.h"

// Covers the one-shot MMR read reliability work in the harden-de1-ble-reliability
// change: a post-connect informational read (GHC info, machine identity, etc.)
// whose response notification is dropped must be retried, and on exhaustion must
// leave the associated value at its safe default rather than pending forever.
// Also covers the profile-upload settle window that keeps startEspresso() from
// racing the DE1 firmware's post-upload flash write.
//
// The read timeout/retry sweep (checkMMRReadTimeouts) is driven directly with
// deadlines forced into the past instead of waiting out the real 4s timeout, so
// these stay fast unit tests.

class tst_DE1DeviceMMRReads : public QObject {
    Q_OBJECT

private:
    struct TestFixture {
        MockTransport transport;
        DE1Device device;
        TestFixture() { device.setTransport(&transport); }
    };

    // An MMR read response for `address`: len byte, 3 address bytes (big
    // endian), then the value byte(s). parseMMRResponse reads d[4] for the
    // single-byte GHC/refill status and d[4..7] (LE) for the 4-byte reads.
    static QByteArray mmrResponse(uint32_t address, uint8_t value0) {
        QByteArray r(20, 0);
        r[1] = static_cast<char>((address >> 16) & 0xFF);
        r[2] = static_cast<char>((address >> 8) & 0xFF);
        r[3] = static_cast<char>(address & 0xFF);
        r[4] = static_cast<char>(value0);
        return r;
    }

    // A GHC_INFO read response: len byte, 3 address bytes (big endian), status byte.
    static QByteArray ghcResponse(uint8_t status) {
        return mmrResponse(DE1::MMR::GHC_INFO, status);
    }

    // Force every pending read's deadline into the past and run one sweep,
    // simulating the real 4s timeout elapsing without a response.
    static void expireAndSweep(DE1Device& device) {
        for (auto it = device.m_pendingMMRReads.begin();
             it != device.m_pendingMMRReads.end(); ++it) {
            it.value().deadlineMs = 0;
        }
        device.checkMMRReadTimeouts();
    }

    static qsizetype countReadRequests(const MockTransport& t) {
        qsizetype n = 0;
        for (const auto& w : t.writes)
            if (w.first == DE1::Characteristic::READ_FROM_MMR) ++n;
        return n;
    }

private slots:
    void init() { QTest::failOnWarning(); }

    // ===== One-shot read: response clears the pending entry =====

    void ghcResponseClearsPendingAndUpdatesHeadless() {
        TestFixture f;
        f.device.issueMMRReadWithRetry(DE1::MMR::GHC_INFO, QStringLiteral("GHC info"));
        QCOMPARE(countReadRequests(f.transport), qsizetype(1));
        QVERIFY(f.device.m_pendingMMRReads.contains(DE1::MMR::GHC_INFO));

        // Status 3 = active GHC → app CANNOT start → isHeadless false.
        QTest::ignoreMessage(QtInfoMsg,
            QRegularExpression("GHC status: active"));
        emit f.transport.dataReceived(DE1::Characteristic::READ_FROM_MMR, ghcResponse(3));

        QCOMPARE(f.device.isHeadless(), false);
        QVERIFY(!f.device.m_pendingMMRReads.contains(DE1::MMR::GHC_INFO));
    }

    // ===== One-shot read: dropped response is retried =====

    void droppedGhcResponseIsRetried() {
        TestFixture f;
        f.device.issueMMRReadWithRetry(DE1::MMR::GHC_INFO, QStringLiteral("GHC info"));
        QCOMPARE(countReadRequests(f.transport), qsizetype(1));

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("\\[DE1\\]\\[MMR\\] read timeout, retrying"));
        expireAndSweep(f.device);

        // The read request was re-sent, and the entry is still pending with one
        // fewer attempt remaining.
        QCOMPARE(countReadRequests(f.transport), qsizetype(2));
        QVERIFY(f.device.m_pendingMMRReads.contains(DE1::MMR::GHC_INFO));
    }

    void droppedGhcRecoversWhenRetrySucceeds() {
        TestFixture f;
        f.device.issueMMRReadWithRetry(DE1::MMR::GHC_INFO, QStringLiteral("GHC info"));

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("\\[DE1\\]\\[MMR\\] read timeout, retrying"));
        expireAndSweep(f.device);

        // Response finally arrives on the retry.
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("GHC status: active"));
        emit f.transport.dataReceived(DE1::Characteristic::READ_FROM_MMR, ghcResponse(7));

        QCOMPARE(f.device.isHeadless(), false);
        QVERIFY(!f.device.m_pendingMMRReads.contains(DE1::MMR::GHC_INFO));
    }

    // ===== One-shot read: exhaustion leaves the safe default =====

    void exhaustedGhcReadLeavesHeadlessDefault() {
        TestFixture f;
        QCOMPARE(f.device.isHeadless(), true);  // permissive default
        f.device.issueMMRReadWithRetry(DE1::MMR::GHC_INFO, QStringLiteral("GHC info"));

        // MMR_READ_MAX_RETRIES retries, then one more sweep to expire.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("\\[DE1\\]\\[MMR\\] read timeout, retrying"));
        expireAndSweep(f.device);
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("\\[DE1\\]\\[MMR\\] read timeout, retrying"));
        expireAndSweep(f.device);
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("\\[DE1\\]\\[MMR\\] read FAILED after retries"));
        // GHC exhaustion additionally logs the capability-unconfirmed advisory.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("GHC status unconfirmed after retries"));
        expireAndSweep(f.device);

        QVERIFY(!f.device.m_pendingMMRReads.contains(DE1::MMR::GHC_INFO));
        // Never got a response → stays at the permissive default, not stuck
        // in some unknown state.
        QCOMPARE(f.device.isHeadless(), true);
    }


    // ===== Multiple concurrent pending reads (the real sendInitialSettings path) =====

    void oneResponseLeavesOtherReadsPendingAndTimerRunning() {
        // The production path issues six reads at once; a single-entry test
        // never exercises the selective timer-stop or the collect-then-mutate
        // iterator safety. Two concurrent reads pin both.
        TestFixture f;
        f.device.issueMMRReadWithRetry(DE1::MMR::GHC_INFO, QStringLiteral("GHC info"));
        f.device.issueMMRReadWithRetry(DE1::MMR::REFILL_KIT, QStringLiteral("refill kit status"));
        QVERIFY(f.device.m_mmrReadRetryTimer.isActive());
        QCOMPARE(f.device.m_pendingMMRReads.size(), 2);

        // One response arrives: only that entry clears, the other stays pending,
        // and the sweep timer keeps running (not stopped while work remains).
        emit f.transport.dataReceived(DE1::Characteristic::READ_FROM_MMR,
                                      mmrResponse(DE1::MMR::REFILL_KIT, 1));
        QVERIFY(!f.device.m_pendingMMRReads.contains(DE1::MMR::REFILL_KIT));
        QVERIFY(f.device.m_pendingMMRReads.contains(DE1::MMR::GHC_INFO));
        QVERIFY(f.device.m_mmrReadRetryTimer.isActive());

        // The last response drains the table and stops the timer.
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("GHC status: active"));
        emit f.transport.dataReceived(DE1::Characteristic::READ_FROM_MMR, ghcResponse(3));
        QVERIFY(f.device.m_pendingMMRReads.isEmpty());
        QVERIFY(!f.device.m_mmrReadRetryTimer.isActive());
    }

    void concurrentReadsExpiringTogetherAreIteratorSafe() {
        // Two reads expiring in the same sweep exercises the collect-then-mutate
        // guard against hash-iterator invalidation (a crash/UB regression if the
        // guard were removed). Runs under ASan/UBSan in the debug build.
        TestFixture f;
        f.device.issueMMRReadWithRetry(DE1::MMR::CPU_BOARD_MODEL, QStringLiteral("CPU board model"));
        f.device.issueMMRReadWithRetry(DE1::MMR::MACHINE_MODEL, QStringLiteral("machine model"));

        // Exhaust both together: 2 retries then expire, each sweep emits one
        // warning per still-pending read.
        for (int round = 0; round < 3; ++round) {
            for (auto it = f.device.m_pendingMMRReads.begin();
                 it != f.device.m_pendingMMRReads.end(); ++it) {
                QTest::ignoreMessage(QtWarningMsg,
                    QRegularExpression(round < 2 ? "\\[DE1\\]\\[MMR\\] read timeout, retrying"
                                                 : "\\[DE1\\]\\[MMR\\] read FAILED after retries"));
            }
            expireAndSweep(f.device);
        }

        QVERIFY(f.device.m_pendingMMRReads.isEmpty());
        QVERIFY(!f.device.m_mmrReadRetryTimer.isActive());
    }

    // ===== Disconnect clears pending reads and settle state =====

    void disconnectClearsPendingReadsAndStopsTimer() {
        TestFixture f;
        f.device.issueMMRReadWithRetry(DE1::MMR::GHC_INFO, QStringLiteral("GHC info"));
        QVERIFY(!f.device.m_pendingMMRReads.isEmpty());
        QVERIFY(f.device.m_mmrReadRetryTimer.isActive());

        f.transport.setConnectedSim(false);

        QVERIFY(f.device.m_pendingMMRReads.isEmpty());
        QVERIFY(!f.device.m_mmrReadRetryTimer.isActive());
    }

    // ===== Profile-upload settle window before starting espresso =====

    static Profile makeSimpleProfile() {
        Profile p;
        p.setTitle(QStringLiteral("settle-test"));
        p.setMode(Profile::Mode::FrameBased);
        QList<ProfileFrame> steps;
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

    static qsizetype countEspressoStateWrites(const MockTransport& t) {
        qsizetype n = 0;
        for (const auto& w : t.writes) {
            if (w.first == DE1::Characteristic::REQUESTED_STATE
                && w.second.size() == 1
                && static_cast<uint8_t>(w.second.at(0))
                       == static_cast<uint8_t>(DE1::State::Espresso)) {
                ++n;
            }
        }
        return n;
    }

    void startEspressoFiresImmediatelyWithNoRecentUpload() {
        TestFixture f;
        // No profile upload has completed — no settle window applies.
        f.device.startEspresso();
        QCOMPARE(countEspressoStateWrites(f.transport), qsizetype(1));
    }

    void startEspressoDefersAfterProfileUpload() {
        TestFixture f;
        // A completed, verified upload stamps the settle window.
        f.device.uploadProfile(makeSimpleProfile());
        f.transport.ackAllWritesInOrder();
        f.transport.clearWrites();

        // startEspresso right behind the upload must NOT issue the Espresso
        // state change yet — it races the firmware's flash write otherwise.
        f.device.startEspresso();
        QCOMPARE(countEspressoStateWrites(f.transport), qsizetype(0));

        // After the settle window elapses, the deferred start fires.
        QTRY_COMPARE_WITH_TIMEOUT(countEspressoStateWrites(f.transport), qsizetype(1), 2000);
    }

    void secondStartWithinSettleWindowDoesNotBypass() {
        TestFixture f;
        f.device.uploadProfile(makeSimpleProfile());
        f.transport.ackAllWritesInOrder();
        f.transport.clearWrites();

        // First start defers.
        f.device.startEspresso();
        QCOMPARE(countEspressoStateWrites(f.transport), qsizetype(0));
        // A second start inside the window must NOT slip an immediate Espresso
        // state change past the settle (it would race the firmware flash write).
        f.device.startEspresso();
        QCOMPARE(countEspressoStateWrites(f.transport), qsizetype(0));

        // Exactly one deferred start fires after the window — not two.
        QTRY_COMPARE_WITH_TIMEOUT(countEspressoStateWrites(f.transport), qsizetype(1), 2000);
        QTest::qWait(200);
        QCOMPARE(countEspressoStateWrites(f.transport), qsizetype(1));
    }

    void disconnectCancelsDeferredStart() {
        // Safety: a deferred start must NOT fire on a link that reconnects
        // inside the settle window — that would be an unrequested shot. The
        // settle timer is stopped on disconnect, cancelling the pending start.
        TestFixture f;
        f.device.uploadProfile(makeSimpleProfile());
        f.transport.ackAllWritesInOrder();

        f.device.startEspresso();  // defers
        QVERIFY(f.device.m_espressoStartDeferred);
        QVERIFY(f.device.m_espressoSettleTimer.isActive());

        // Link drops within the window.
        f.transport.setConnectedSim(false);
        QVERIFY(!f.device.m_espressoStartDeferred);
        QVERIFY(!f.device.m_espressoSettleTimer.isActive());

        // Reconnect, then wait past the original window. No Espresso state
        // change should have been issued by the cancelled deferral.
        f.transport.clearWrites();
        f.transport.setConnectedSim(true);
        QTest::qWait(PROFILE_UPLOAD_SETTLE_WAIT_MS);
        QCOMPARE(countEspressoStateWrites(f.transport), qsizetype(0));
    }

    void disconnectClearsSettleWindow() {
        // After a disconnect, a fresh startEspresso must fire immediately — the
        // dead connection's upload timestamp must not gate the new link.
        TestFixture f;
        f.device.uploadProfile(makeSimpleProfile());
        f.transport.ackAllWritesInOrder();
        f.transport.setConnectedSim(false);
        f.transport.setConnectedSim(true);
        f.transport.clearWrites();

        f.device.startEspresso();
        QCOMPARE(countEspressoStateWrites(f.transport), qsizetype(1));
    }

    // ===== Sensor calibration (A012) =====
    //
    // The correction lives in the machine. What these assert is that Decenza
    // sends the right record, and — the part that matters — that it treats only
    // a reply with WriteKey == 0 as carrying a real value, so an echo of our own
    // write can never make a refused write look like it succeeded.

    void calibrationWriteSendsOneRecordWithTheFirmwareKey() {
        TestFixture f;
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("write pressure"));
        // Result deliberately dropped: this asserts the BYTES that go out, and
        // the fixture's transport always accepts.
        (void)f.device.sendCalibration(DE1::Calibration::Target::Pressure,
                                       DE1::Calibration::Command::Write, 9.0, 8.25);

        QCOMPARE(countCalibrationWrites(f.transport), qsizetype(1));
        const QByteArray sent = lastCalibrationWrite(f.transport);
        const auto record = DE1::Calibration::parseRecord(sent);
        QVERIFY(record.has_value());
        QCOMPARE(record->writeKey, DE1::Calibration::WRITE_KEY);
        QCOMPARE(record->command, DE1::Calibration::Command::Write);
        QCOMPARE(record->target, DE1::Calibration::Target::Pressure);
        QVERIFY(qAbs(record->reported - 9.0) < 1e-4);
        QVERIFY(qAbs(record->measured - 8.25) < 1e-4);
    }

    void calibrationReadsUseTheReadKeyAndCarryNoValues() {
        TestFixture f;
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("read.*temperature"));
        (void)f.device.readCalibration(int(DE1::Calibration::Target::Temperature));

        const auto record = DE1::Calibration::parseRecord(lastCalibrationWrite(f.transport));
        QVERIFY(record.has_value());
        QCOMPARE(record->writeKey, DE1::Calibration::READ_KEY);
        QCOMPARE(record->command, DE1::Calibration::Command::ReadCurrent);
        QCOMPARE(record->target, DE1::Calibration::Target::Temperature);
        QCOMPARE(record->reported, 0.0);
        QCOMPARE(record->measured, 0.0);
    }

    // Nothing may send ResetFactory. It has no working reference — de1app's
    // helper names 2 while its reset buttons passed 3 for their whole eleven-week
    // life (added 2018-02-27 a2092efc, disabled 2018-05-15 69e4277c, both with
    // empty commit messages) — and no firmware source here settles it. This test
    // is what stops it being added back casually: writing an unverified command
    // to a machine needs a reference or a lot of hardware testing, not a guess.
    void nothingEverSendsTheUnverifiedResetCommand() {
        TestFixture f;
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("write pressure"));
        // Result deliberately dropped — see above.
        (void)f.device.sendCalibration(DE1::Calibration::Target::Pressure,
                                       DE1::Calibration::Command::Write, 9.0, 8.2);
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("read.*pressure"));
        (void)f.device.readCalibration(int(DE1::Calibration::Target::Pressure));

        for (const auto& w : f.transport.writes) {
            if (w.first != DE1::Characteristic::CALIBRATION) continue;
            const auto record = DE1::Calibration::parseRecord(w.second);
            QVERIFY(record.has_value());
            QVERIFY(record->command != DE1::Calibration::Command::ResetFactory);
        }
    }

    void calibrationTargetOutOfRangeIsRefusedNotSent_data() {
        QTest::addColumn<int>("target");
        QTest::newRow("negative") << -1;
        QTest::newRow("past end") << 3;
    }

    void calibrationTargetOutOfRangeIsRefusedNotSent() {
        QFETCH(int, target);
        TestFixture f;

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("read refused, target out of range"));
        (void)f.device.readCalibration(target);

        QCOMPARE(countCalibrationWrites(f.transport), qsizetype(0));
        // And the accessors must not walk off the arrays.
        QCOMPARE(f.device.hasStoredCalibration(target), false);
        QCOMPARE(f.device.storedCalibration(target), 0.0);
    }

    void echoedCalibrationReplyIsDiscarded() {
        TestFixture f;
        QSignalSpy changed(&f.device, &DE1Device::calibrationChanged);

        // An echo of our own write: same record, non-zero WriteKey. Nothing may
        // land, or a write the machine refused would read back as accepted.
        emit f.transport.dataReceived(
            DE1::Characteristic::CALIBRATION,
            calibrationReply(DE1::Calibration::WRITE_KEY,
                             DE1::Calibration::Command::Write,
                             DE1::Calibration::Target::Pressure, 8.25));

        QCOMPARE(changed.count(), 0);
        QCOMPARE(f.device.hasStoredCalibration(int(DE1::Calibration::Target::Pressure)), false);
    }

    // Measured on hardware: CalCommand 3 ("read factory") returns the CURRENT
    // value, not a distinct factory one — before any write the machine reported
    // +0.89 for both, after a write +0.91 for both. So there is ONE slot, and a
    // reply to either read command lands in it.
    void everyValueCarryingReplyIsTheCurrentOffset() {
        TestFixture f;
        const int pressure = int(DE1::Calibration::Target::Pressure);
        QSignalSpy changed(&f.device, &DE1Device::calibrationChanged);

        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("stored pressure calibration"));
        emit f.transport.dataReceived(
            DE1::Characteristic::CALIBRATION,
            calibrationReply(DE1::Calibration::REPLY_VALUE_KEY,
                             DE1::Calibration::Command::ReadCurrent,
                             DE1::Calibration::Target::Pressure, -0.8));

        QVERIFY(f.device.hasStoredCalibration(pressure));
        QVERIFY(qAbs(f.device.storedCalibration(pressure) + 0.8) < 1e-4);
        QCOMPARE(changed.count(), 1);

        // A CalCommand 3 reply updates the SAME slot rather than a second one.
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("stored pressure calibration"));
        emit f.transport.dataReceived(
            DE1::Characteristic::CALIBRATION,
            calibrationReply(DE1::Calibration::REPLY_VALUE_KEY,
                             DE1::Calibration::Command::ReadFactory,
                             DE1::Calibration::Target::Pressure, 0.15));

        QVERIFY(qAbs(f.device.storedCalibration(pressure) - 0.15) < 1e-4);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(f.device.calibrationVersion(), 2);
    }

    void oneTargetsValueDoesNotLeakIntoAnother() {
        TestFixture f;
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("stored temperature calibration"));
        emit f.transport.dataReceived(
            DE1::Characteristic::CALIBRATION,
            calibrationReply(DE1::Calibration::REPLY_VALUE_KEY,
                             DE1::Calibration::Command::ReadCurrent,
                             DE1::Calibration::Target::Temperature, 1.5));

        QVERIFY(f.device.hasStoredCalibration(int(DE1::Calibration::Target::Temperature)));
        QCOMPARE(f.device.hasStoredCalibration(int(DE1::Calibration::Target::Pressure)), false);
    }

    void malformedCalibrationReplyChangesNothing() {
        TestFixture f;
        QSignalSpy changed(&f.device, &DE1Device::calibrationChanged);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("unparseable calibration reply"));
        emit f.transport.dataReceived(DE1::Characteristic::CALIBRATION, QByteArray(6, 0));

        QCOMPARE(changed.count(), 0);
        QCOMPARE(f.device.hasStoredCalibration(int(DE1::Calibration::Target::Pressure)), false);
    }

    void aRefusedWriteIsReportedRatherThanSwallowed() {
        // Without a transport nothing can be sent. The caller has to be able to
        // tell — a wizard that shows "applied" for a write that never left the
        // app sends the user off to re-run against a machine that never changed.
        DE1Device orphan;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("cannot write pressure — no transport"));
        QVERIFY(!orphan.writeCalibration(int(DE1::Calibration::Target::Pressure), 9.0, 8.2));

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("cannot read pressure — no transport"));
        QVERIFY(!orphan.readCalibration(int(DE1::Calibration::Target::Pressure)));
    }

    void calibrationCacheIsClearedWhenTheMachineGoesAway() {
        // These values are facts about ONE machine. Carrying them across a
        // reconnect means a second DE1 shows the first's offsets — and the
        // wizard's Apply gate is exactly "has this machine answered", so a stale
        // true opens a write against a baseline this machine never reported.
        TestFixture f;
        const int pressure = int(DE1::Calibration::Target::Pressure);

        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("stored pressure calibration"));
        emit f.transport.dataReceived(
            DE1::Characteristic::CALIBRATION,
            calibrationReply(DE1::Calibration::REPLY_VALUE_KEY,
                             DE1::Calibration::Command::ReadCurrent,
                             DE1::Calibration::Target::Pressure, 0.4));
        QVERIFY(f.device.hasStoredCalibration(pressure));

        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("calibration cache cleared"));
        f.transport.setConnectedSim(false);

        QCOMPARE(f.device.hasStoredCalibration(pressure), false);
    }

#ifdef DECENZA_SIMULATOR
    void simulatedMachineAnswersItsOwnCalibrationRequests() {
        // The only way this feature is exercised without a DE1 on the bench, and
        // every way it can break is silent — the wizard just sits on "not read
        // yet" and the next person concludes the FEATURE is broken.
        TestFixture f;
        f.device.m_simulationMode = true;
        const int pressure = int(DE1::Calibration::Target::Pressure);

        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("simulated machine stored"));
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("stored pressure calibration"));
        QVERIFY(f.device.writeCalibration(pressure, 9.0, 8.2));

        // Nothing may reach the transport on the simulated path.
        QCOMPARE(countCalibrationWrites(f.transport), qsizetype(0));
        QVERIFY(f.device.hasStoredCalibration(pressure));
        // A TENTH of the -0.8 delta, matching the machine.
        QVERIFY(qAbs(f.device.storedCalibration(pressure) + 0.08) < 1e-4);

        // A second write accumulates on top. This pins the DIRECTION and the
        // fraction — one write alone cannot distinguish a sign flip.
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("simulated machine stored"));
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("stored pressure calibration"));
        QVERIFY(f.device.writeCalibration(pressure, 9.0, 8.6));
        QVERIFY(qAbs(f.device.storedCalibration(pressure) + 0.12) < 1e-4);

        // A read answers with that same stored value and moves nothing.
        // CAL_DETAIL is qDebug — mechanics, not an outcome.
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression("confirmed unchanged"));
        QVERIFY(f.device.readCalibration(pressure));
        QVERIFY(qAbs(f.device.storedCalibration(pressure) + 0.12) < 1e-4);
    }
#endif

    // ===== Nominal heater voltage =====

    void heaterVoltageBuckets_data() {
        QTest::addColumn<int>("raw");
        QTest::addColumn<int>("expected");

        QTest::newRow("unknown zero")     << 0    << 0;
        QTest::newRow("measured 110")     << 110  << 120;
        QTest::newRow("measured 120")     << 120  << 120;
        QTest::newRow("measured 220")     << 220  << 230;
        QTest::newRow("measured 230")     << 230  << 230;
        // Above 1000 means the machine was TOLD rather than measured.
        QTest::newRow("told 120")         << 1120 << 120;
        QTest::newRow("told 230")         << 1230 << 230;
        QTest::newRow("low edge 90")      << 90   << 120;
        QTest::newRow("high edge 150")    << 150  << 120;
        QTest::newRow("low edge 180")     << 180  << 230;
        QTest::newRow("high edge 260")    << 260  << 230;
        // Between the bands and outside them: unknown, never a guess.
        QTest::newRow("gap 165")          << 165  << 0;
        QTest::newRow("below band 42")    << 42   << 0;
        QTest::newRow("above band 400")   << 400  << 0;
    }

    void heaterVoltageBuckets() {
        QFETCH(int, raw);
        QFETCH(int, expected);
        // A NONZERO value that lands in neither band is warned about, because it
        // renders identically to "the machine reported nothing" and the two are
        // otherwise indistinguishable on screen.
        if (expected == 0 && raw != 0)
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("falls in neither band"));
        QCOMPARE(DE1Device::bucketHeaterVoltage(raw), expected);
    }

    void heaterVoltageWriteAcceptsOnlyTheTwoLegalValues_data() {
        QTest::addColumn<int>("volts");
        QTest::addColumn<bool>("accepted");

        QTest::newRow("120")        << 120  << true;
        QTest::newRow("230")        << 230  << true;
        // Refused rather than clamped: the two legal values are far apart, so a
        // caller asking for something between them is confused, not approximating.
        QTest::newRow("110")        << 110  << false;
        QTest::newRow("240")        << 240  << false;
        QTest::newRow("zero")       << 0    << false;
        QTest::newRow("told form")  << 1120 << false;
    }

    void heaterVoltageWriteAcceptsOnlyTheTwoLegalValues() {
        QFETCH(int, volts);
        QFETCH(bool, accepted);

        TestFixture f;
        if (accepted) {
            QTest::ignoreMessage(QtInfoMsg, QRegularExpression("heater voltage ="));
        } else {
            QTest::ignoreMessage(QtWarningMsg,
                QRegularExpression("heater voltage refused, expected 120 or 230"));
        }
        f.device.setHeaterVoltage(volts);

        qsizetype mmrWrites = 0;
        for (const auto& w : f.transport.writes)
            if (w.first == DE1::Characteristic::WRITE_TO_MMR) ++mmrWrites;
        QCOMPARE(mmrWrites, accepted ? qsizetype(1) : qsizetype(0));
    }

private:
    static constexpr int PROFILE_UPLOAD_SETTLE_WAIT_MS = 700;  // > 500ms window

    static qsizetype countCalibrationWrites(const MockTransport& t) {
        qsizetype n = 0;
        for (const auto& w : t.writes)
            if (w.first == DE1::Characteristic::CALIBRATION) ++n;
        return n;
    }

    static QByteArray lastCalibrationWrite(const MockTransport& t) {
        for (auto it = t.writes.crbegin(); it != t.writes.crend(); ++it)
            if (it->first == DE1::Characteristic::CALIBRATION) return it->second;
        return {};
    }

    // A reply as the machine sends one. `key` decides whether it carries a real
    // value (0) or is an echo of a request (anything else).
    static QByteArray calibrationReply(uint32_t key,
                                       DE1::Calibration::Command command,
                                       DE1::Calibration::Target target,
                                       double measured) {
        DE1::Calibration::Record r;
        r.writeKey = key;
        r.command  = command;
        r.target   = target;
        r.measured = measured;
        return DE1::Calibration::packRecord(r);
    }
};

QTEST_MAIN(tst_DE1DeviceMMRReads)
#include "tst_de1device_mmrreads.moc"
