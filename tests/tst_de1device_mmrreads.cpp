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
// or a writeMMRVerified read-back whose response notification is dropped must be
// retried, and on exhaustion must leave the associated value at its safe default
// rather than pending forever. Also covers the profile-upload settle window that
// keeps startEspresso() from racing the DE1 firmware's post-upload flash write.
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
        QTest::ignoreMessage(QtDebugMsg,
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
            QRegularExpression("\\[MMR\\] read timeout, retrying"));
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
            QRegularExpression("\\[MMR\\] read timeout, retrying"));
        expireAndSweep(f.device);

        // Response finally arrives on the retry.
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression("GHC status: active"));
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
            QRegularExpression("\\[MMR\\] read timeout, retrying"));
        expireAndSweep(f.device);
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("\\[MMR\\] read timeout, retrying"));
        expireAndSweep(f.device);
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("\\[MMR\\] read FAILED after retries"));
        // GHC exhaustion additionally logs the capability-unconfirmed advisory.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("GHC status unconfirmed after retries"));
        expireAndSweep(f.device);

        QVERIFY(!f.device.m_pendingMMRReads.contains(DE1::MMR::GHC_INFO));
        // Never got a response → stays at the permissive default, not stuck
        // in some unknown state.
        QCOMPARE(f.device.isHeadless(), true);
    }

    // ===== writeMMRVerified read-back: dropped response is not abandoned =====

    void verifyReadbackExhaustionClearsVerify() {
        TestFixture f;
        // Register a pending verify and its read-back exactly as
        // writeMMRVerified()/scheduleMMRVerifyRead() would, without the 50ms
        // singleShot in between.
        const uint32_t addr = DE1::MMR::FAN_THRESHOLD;
        f.device.m_pendingMMRVerifies.insert(
            addr, DE1Device::PendingMMRVerify{55, 3, QStringLiteral("test")});
        f.device.scheduleMMRVerifyRead(addr);
        QVERIFY(f.device.m_pendingMMRReads.contains(addr));

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("\\[MMR\\] read timeout, retrying"));
        expireAndSweep(f.device);
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("\\[MMR\\] read timeout, retrying"));
        expireAndSweep(f.device);
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("\\[MMR\\] read FAILED after retries"));
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("\\[MMR\\] verify abandoned"));
        expireAndSweep(f.device);

        // Both the read tracking and the verify entry are cleared — the verify
        // no longer sits pending forever with no signal.
        QVERIFY(!f.device.m_pendingMMRReads.contains(addr));
        QVERIFY(!f.device.m_pendingMMRVerifies.contains(addr));
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
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression("GHC status: active"));
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
                    QRegularExpression(round < 2 ? "\\[MMR\\] read timeout, retrying"
                                                 : "\\[MMR\\] read FAILED after retries"));
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

private:
    static constexpr int PROFILE_UPLOAD_SETTLE_WAIT_MS = 700;  // > 500ms window
};

QTEST_MAIN(tst_DE1DeviceMMRReads)
#include "tst_de1device_mmrreads.moc"
