#include <QtTest>
#include <QSignalSpy>

#include <algorithm>
#include <QTemporaryDir>
#include <QFile>
#include <QRegularExpression>

#include "controllers/firmwareupdater.h"
#include "core/firmwareassetcache.h"
#include "core/firmwareheader.h"
#include "ble/de1device.h"
#include "ble/protocol/de1characteristics.h"
#include "ble/protocol/firmwarepackets.h"
#include "mocks/MockTransport.h"

// FirmwareUpdater state-machine tests (§4).
//
// Drives a real DE1Device (MockTransport) + real FirmwareAssetCache
// (QTemporaryDir root pre-filled with a synthetic 320-byte .dat) through
// the three-phase flow. Timers are injected at 0 ms intervals so the
// test runs in < 100 ms instead of the real 45-second flash.
//
// This file starts with the happy path only (§4a); error-path tests
// (disconnect, timeout, verify failure, precondition, same-version) land
// in §4b, and retry/dismiss/verify-retroactive-success semantics in §4c.

namespace {

void packU32LE(QByteArray& buf, int off, uint32_t v) {
    buf[off + 0] = char(v       & 0xFF);
    buf[off + 1] = char((v >> 8) & 0xFF);
    buf[off + 2] = char((v >> 16)& 0xFF);
    buf[off + 3] = char((v >> 24)& 0xFF);
}

// Build a synthetic valid firmware file: 64-byte header with the real
// BoardMarker, the requested Version, a ByteCount of 256, and a 256-byte
// zero payload. No encryption or checksum fields are populated — client
// side accepts this per the spec (BoardMarker + size check only).
QByteArray makeFirmwareBlob(uint32_t version, qsizetype payloadSize = 256) {
    QByteArray blob(DE1::Firmware::HEADER_SIZE + payloadSize, char(0));
    packU32LE(blob, 0,  0x11223344);                            // CheckSum
    packU32LE(blob, 4,  DE1::Firmware::BOARD_MARKER);
    packU32LE(blob, 8,  version);
    packU32LE(blob, 12, static_cast<uint32_t>(payloadSize));    // ByteCount
    packU32LE(blob, 16, static_cast<uint32_t>(payloadSize / 2));// CpuBytes
    // offset 20 Unused stays zero
    packU32LE(blob, 24, 0x55667788);                            // DCSum
    // The checksum words and the IV must be non-zero: validateFile() rejects
    // an all-zero one, because no published image leaves them empty and that
    // is what distinguishes a real header from a spliced or synthetic one.
    for (int i = 0; i < 32; ++i) {
        blob[28 + i] = static_cast<char>(i + 1);                // IV
    }
    packU32LE(blob, 60, 0x99AABBCC);                            // HeaderCheckSum
    return blob;
}

void writeCachedBlob(const FirmwareUpdater*, DE1::Firmware::FirmwareAssetCache* cache,
                     const QByteArray& blob) {
    QFile f(cache->cachePath());
    QVERIFY2(f.open(QIODevice::WriteOnly), "can't write synthetic firmware cache file");
    f.write(blob);
    f.close();
}

// Drive the test through the upload phase by waiting for all chunks to be
// queued into MockTransport, then simulating BLE ACKs for each. Returns
// after the state has advanced to Verifying (or whatever follows, depending
// on timings in the specific test).
// A firmware chunk on the wire: 20 bytes on WRITE_TO_MMR whose length byte
// is 16. Same discriminator FirmwareUpdater::onFirmwareWriteAcked uses to
// tell chunks from the MMR writes that share the characteristic.
bool isFirmwareChunkWrite(const QPair<QBluetoothUuid, QByteArray>& w) {
    return w.first == DE1::Characteristic::WRITE_TO_MMR &&
           w.second.size() == 20 &&
           static_cast<uint8_t>(w.second[0]) == 16;
}

qsizetype firmwareChunkWriteCount(const MockTransport& transport) {
    return std::count_if(transport.writes.begin(), transport.writes.end(),
                         isFirmwareChunkWrite);
}

void simulateFullUpload(MockTransport& transport, const QByteArray& blob,
                        int ackWaitTimeoutMs = 5000) {
    const qsizetype expectedChunks = (blob.size() + 15) / 16;
    // Count the CHUNK writes, not the total. This used to wait for
    // `writes.size() >= 1 + expectedChunks`, assuming exactly one non-chunk
    // write (the erase FWMapRequest) precedes them — which stopped being true
    // the moment beginErasePhase() started sending the DE1 to sleep first.
    // The total was then reached one chunk early, ackAllWritesInOrder() acked
    // one chunk short of the total, and the updater sat in Uploading forever
    // waiting for an ACK that had already been counted against a different
    // write. Counting the thing we actually mean survives any further
    // pre-erase traffic.
    QTRY_VERIFY_WITH_TIMEOUT(
        firmwareChunkWriteCount(transport) >= expectedChunks, ackWaitTimeoutMs);
    transport.ackAllWritesInOrder();
}

}  // namespace

class tst_FirmwareUpdater : public QObject {
    Q_OBJECT

private:
    struct Fixture {
        QTemporaryDir                    tmp;
        MockTransport                    transport;
        DE1Device                        device;
        DE1::Firmware::FirmwareAssetCache cache;
        FirmwareUpdater                  updater;

        Fixture() : updater(&device, &cache) {
            device.setTransport(&transport);
            cache.setCacheRoot(tmp.path());

            // Fast timings so the test runs in milliseconds.
            updater.setPostEraseWaitMs(0);
            updater.setChunkPumpIntervalMs(0);
            updater.setPostUploadSettleMs(0);
            updater.setEraseTimeoutMs(5000);
            updater.setVerifyTimeoutMs(5000);

            // Pretend machine is idle and installed firmware is older.
            updater.setPreconditionProvider([]{ return true; });
            updater.setInstalledVersionProvider([]{ return 1200u; });
        }
    };

private slots:
    void init() { QTest::failOnWarning(); }

    // ===== §4b: error paths =====

    void eraseTimeout_failsRetryable() {
        Fixture f;
        // failWith() logs a qCWarning; that's the behaviour under test here.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(R"(\[firmware\] FAIL phase=\s*Erasing.*reason=\s*Erase did not complete)"));
        // Erase-timeout path requires that the post-erase wait does NOT fire
        // first — otherwise state transitions Erasing → Uploading and the
        // timeout's early-return guard (state != Erasing) means no Failed.
        // Set wait >> timeout so the timeout is the first thing to fire.
        f.updater.setEraseTimeoutMs(50);
        f.updater.setPostEraseWaitMs(5000);
        writeCachedBlob(&f.updater, &f.cache, makeFirmwareBlob(1352));
        f.updater.startUpdate();
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Erasing);
        // Deliberately never send the erase-done notification.
        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(), FirmwareUpdater::State::Failed, 2000);
        QVERIFY(f.updater.retryAvailable());
        QVERIFY(f.updater.errorMessage().contains("Erase"));
    }

    void erasePhase_sleepsTheMachineBeforeEngagingTheGuard() {
        Fixture f;
        writeCachedBlob(&f.updater, &f.cache, makeFirmwareBlob(1352));
        f.updater.startUpdate();
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Uploading);

        // The sleep must reach the wire, and it must precede the erase
        // request. DE1Device::goToSleep() drops the write once
        // m_firmwareFlashInProgress is set, so moving the call below
        // setFirmwareFlashInProgress(true) turns it into a silent no-op that
        // nothing else would catch: the flash still succeeds, it just runs
        // with the heaters and refill logic live for its whole ~18 minutes.
        qsizetype sleepAt = -1;
        qsizetype eraseAt = -1;
        for (qsizetype i = 0; i < f.transport.writes.size(); ++i) {
            const auto& w = f.transport.writes.at(i);
            if (sleepAt < 0 && w.first == DE1::Characteristic::REQUESTED_STATE &&
                w.second == QByteArray(1, static_cast<char>(DE1::State::Sleep))) {
                sleepAt = i;
            }
            if (eraseAt < 0 && w.first == DE1::Characteristic::FW_MAP_REQUEST) {
                eraseAt = i;
            }
        }
        QVERIFY2(sleepAt >= 0, "no sleep written to REQUESTED_STATE before the flash");
        QVERIFY2(eraseAt >= 0, "no erase FWMapRequest written to A009");
        QVERIFY2(sleepAt < eraseAt, "sleep must precede the erase request");
    }

    void eraseCompleteNotification_startsUploadBeforeFallbackTimer() {
        Fixture f;
        // The post-erase wait is a fallback, not the trigger. With it set far
        // beyond the test's patience, the only thing that can reach Uploading
        // is the erase-complete notification. A captured DE1+/PCB 1.3 flash
        // reported it at +1.31 s against a 10 s wait, so this is the normal
        // path and the timer is the exception.
        f.updater.setPostEraseWaitMs(60000);
        f.updater.setEraseTimeoutMs(60000);
        writeCachedBlob(&f.updater, &f.cache, makeFirmwareBlob(1352));
        f.updater.startUpdate();
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Erasing);

        // The erase request's own write ACK is what marks this erase cycle as
        // started; a notification arriving before it cannot be its answer.
        emit f.transport.writeComplete(DE1::Characteristic::FW_MAP_REQUEST,
                                       QByteArray::fromHex("00000101000000"));

        // firstError here is the 0,0,0 we sent on the erase request, echoed
        // back — the transition must not depend on its value.
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000001000000"));
        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(),
                                  FirmwareUpdater::State::Uploading, 2000);
    }

    void eraseCompleteBeforeRequestAck_isIgnored() {
        Fixture f;
        // A terminal VERIFY notification is byte-identical to an
        // erase-complete one. The retry path makes that reachable: a verify
        // that timed out at 60 s leaves the DE1 still scanning, the user taps
        // Retry, and the late response lands in the new Erasing window. Acting
        // on it would stream the whole upload into a bank still being erased.
        // Without the ACK gate this test transitions to Uploading.
        f.updater.setPostEraseWaitMs(60000);
        f.updater.setEraseTimeoutMs(60000);
        writeCachedBlob(&f.updater, &f.cache, makeFirmwareBlob(1352));
        f.updater.startUpdate();
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Erasing);

        // No writeComplete for the erase request — so this notification cannot
        // be its answer.
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000001FFFFFD"));
        QTest::qWait(100);
        QCOMPARE(f.updater.state(), FirmwareUpdater::State::Erasing);

        // Once the request is ACKed, the next notification is acted on.
        emit f.transport.writeComplete(DE1::Characteristic::FW_MAP_REQUEST,
                                       QByteArray::fromHex("00000101000000"));
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000001000000"));
        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(),
                                  FirmwareUpdater::State::Uploading, 2000);
    }

    void eraseInProgressNotification_doesNotStartUpload() {
        Fixture f;
        // fwToErase=1 is "still erasing" — v1333+ skips it, but older
        // firmware sends it first and it must not be mistaken for completion.
        f.updater.setPostEraseWaitMs(60000);
        f.updater.setEraseTimeoutMs(60000);
        writeCachedBlob(&f.updater, &f.cache, makeFirmwareBlob(1352));
        f.updater.startUpdate();
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Erasing);

        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000101000000"));
        QTest::qWait(100);
        QCOMPARE(f.updater.state(), FirmwareUpdater::State::Erasing);
    }

    void disconnectDuringUpload_failsRetryable() {
        Fixture f;
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(R"(\[firmware\] FAIL phase=\s*Uploading.*reason=\s*DE1 disconnected)"));
        writeCachedBlob(&f.updater, &f.cache, makeFirmwareBlob(1352, 4096));  // more chunks
        f.updater.setChunkPumpIntervalMs(5);            // slow enough to catch mid-upload
        f.updater.startUpdate();
        // Fixture's postEraseWaitMs=0 races past Erasing synchronously —
        // observe Uploading directly. The erase notifications that used to
        // live here were informational (didn't trigger the transition) so
        // dropping them doesn't change behavior.
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Uploading);
        // Yank the BLE transport mid-upload.
        f.transport.setConnectedSim(false);
        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(), FirmwareUpdater::State::Failed, 2000);
        QVERIFY(f.updater.retryAvailable());
        QVERIFY(f.updater.errorMessage().contains("disconnected", Qt::CaseInsensitive));
    }

    void verifyFailure_reportsErrorOffsetRetryable() {
        Fixture f;
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(R"(\[firmware\] FAIL phase=\s*Verifying.*reason=\s*Verification failed at block)"));
        const QByteArray blob = makeFirmwareBlob(1352);
        writeCachedBlob(&f.updater, &f.cache, blob);
        f.updater.startUpdate();
        // postEraseWaitMs=0 races past Erasing — observe Uploading directly.
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Uploading);
        simulateFullUpload(f.transport, blob);
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Verifying);
        // Non-success firstError: {0x12, 0x34, 0x56} (arbitrary non-success)
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000001123456"));
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Failed);
        QVERIFY(f.updater.retryAvailable());
        QVERIFY(f.updater.errorMessage().contains("Verification"));
    }

    void verifyIgnoresNonTerminalNotification_thenAcceptsVerdict() {
        Fixture f;
        const QByteArray blob = makeFirmwareBlob(1352);
        writeCachedBlob(&f.updater, &f.cache, blob);
        f.updater.startUpdate();
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Uploading);
        simulateFullUpload(f.transport, blob);
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Verifying);

        // WindowIncrement 0x0120 — the bootloader talking mid-scan. Its
        // FirstError is a cursor, not a corrupt-block address. Treating it as
        // a verdict reports "Verification failed at block 0.72.0" on a flash
        // that is still verifying, and Retry cannot help because the next
        // attempt reports the same cursor.
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("01200001004800"));
        QTest::qWait(50);
        QCOMPARE(f.updater.state(), FirmwareUpdater::State::Verifying);

        // The real verdict still lands.
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000001FFFFFD"));
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::AwaitingReboot);
    }

    void preconditionRefuses_duringShot() {
        Fixture f;
        // The precondition-fail path goes through setState(Failed) in
        // startUpdate(), not failWith(), so there's no [firmware] FAIL log.
        // No ignoreMessage needed — verified by noting no warnings in the
        // original run.
        // Override: machine is pulling a shot — Update must refuse.
        f.updater.setPreconditionProvider([]{ return false; });
        writeCachedBlob(&f.updater, &f.cache, makeFirmwareBlob(1352));
        f.updater.startUpdate();

        QCOMPARE(f.updater.state(), FirmwareUpdater::State::Failed);
        QVERIFY(f.updater.retryAvailable());
        QVERIFY(f.updater.errorMessage().contains("Finish", Qt::CaseInsensitive));
        // No BLE writes — refused before any firmware transaction.
        bool sawFirmwareWrite = false;
        for (const auto& w : f.transport.writes) {
            if (w.first == DE1::Characteristic::FW_MAP_REQUEST ||
                (w.first == DE1::Characteristic::WRITE_TO_MMR &&
                 w.second.size() == 20 && uint8_t(w.second[0]) == 0x10)) {
                sawFirmwareWrite = true; break;
            }
        }
        QVERIFY(!sawFirmwareWrite);
    }

    void sameVersion_stillFlashes() {
        Fixture f;
        // Installed version already equals what the file contains. This used
        // to short-circuit to Succeeded without writing anything, which made
        // the case that most needs a flash — a bank that verified but did not
        // take, so the DE1 reports the new build while running the old one —
        // unreachable. de1app has no version gate at all (every check in
        // start_firmware_update is commented out); we match it and warn in the
        // UI instead. The flash must actually start.
        f.updater.setInstalledVersionProvider([]{ return 1352u; });
        writeCachedBlob(&f.updater, &f.cache, makeFirmwareBlob(1352));
        f.updater.startUpdate();

        QTRY_VERIFY(f.updater.state() != FirmwareUpdater::State::Downloading);
        QVERIFY(f.updater.state() != FirmwareUpdater::State::Succeeded);

        bool sawEraseReq = false;
        for (const auto& w : f.transport.writes) {
            if (w.first == DE1::Characteristic::FW_MAP_REQUEST) {
                sawEraseReq = true; break;
            }
        }
        QVERIFY(sawEraseReq);
    }

    // ===== §4b+: firmware-flash MMR-write guard =====

    void firmwareGuard_engagedDuringFlash_andClearedOnSuccess() {
        Fixture f;
        auto installed = std::make_shared<uint32_t>(1200);
        f.updater.setInstalledVersionProvider([installed]{ return *installed; });
        const QByteArray blob = makeFirmwareBlob(/*version*/ 1352);
        writeCachedBlob(&f.updater, &f.cache, blob);

        QVERIFY(!f.device.firmwareFlashInProgress());
        f.updater.startUpdate();

        // The guard is engaged inside beginErasePhase, before setState(Erasing).
        // With the fixture's postEraseWaitMs=0 the state machine transitions
        // Erasing → Uploading synchronously, so we check the guard once state
        // settles in Uploading (the first state that persists long enough for
        // QTRY_COMPARE to observe).
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Uploading);
        QVERIFY(f.device.firmwareFlashInProgress());

        // Drive the flash to completion.
        simulateFullUpload(f.transport, blob);
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Verifying);
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000001FFFFFD"));

        // Verify-success no longer jumps straight to Succeeded; it enters
        // AwaitingReboot until the DE1 actually comes back on the new
        // firmware. Guard stays engaged through the wait.
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::AwaitingReboot);
        QVERIFY(f.device.firmwareFlashInProgress());

        // Simulate auto-reboot: DE1 disconnects, reconnects with new version.
        f.transport.setConnectedSim(false);
        *installed = 1352;
        f.transport.setConnectedSim(true);

        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(),
                                  FirmwareUpdater::State::Succeeded, 2000);

        // Guard must be cleared once we reach Succeeded — otherwise every
        // subsequent MMR write would be silently dropped.
        QVERIFY(!f.device.firmwareFlashInProgress());
    }

    void firmwareGuard_clearedOnFailure() {
        Fixture f;
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(R"(\[firmware\] FAIL phase=\s*Uploading.*reason=\s*DE1 disconnected)"));
        // More chunks + slow pump so the upload is still in flight when we
        // yank the transport. A disconnect mid-upload routes through
        // failWith(), which is the path we want to cover here.
        writeCachedBlob(&f.updater, &f.cache, makeFirmwareBlob(1352, 4096));
        f.updater.setChunkPumpIntervalMs(5);
        f.updater.startUpdate();
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Uploading);
        QVERIFY(f.device.firmwareFlashInProgress());

        // Yank BLE → failWith fires → guard must be cleared.
        f.transport.setConnectedSim(false);
        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(),
                                  FirmwareUpdater::State::Failed, 2000);
        QVERIFY(!f.device.firmwareFlashInProgress());
    }

    void firmwareGuard_dropsMMRWrites() {
        Fixture f;
        // The guard emits a qWarning on every drop — verifying all three
        // drop paths also means we expect all three warnings.
        const QRegularExpression dropRe(
            R"(\[MMR\] write(?: urgent| verified)? DROPPED \(firmware flash in progress\))");
        QTest::ignoreMessage(QtWarningMsg, dropRe);
        QTest::ignoreMessage(QtWarningMsg, dropRe);
        QTest::ignoreMessage(QtWarningMsg, dropRe);

        // Baseline: a normal writeMMR goes through to the transport.
        f.device.writeMMR(0x80000C, 42, QStringLiteral("baseline"));
        const qsizetype baselineWrites = f.transport.writes.size();
        QVERIFY(baselineWrites > 0);

        // Engage the guard directly (avoid the full flash flow here — this
        // test targets only the DE1Device write gate).
        f.device.setFirmwareFlashInProgress(true);

        // All three MMR write paths must now drop their packets. We call
        // each with a distinct address so dedup can't mask the drop via an
        // unchanged-value short-circuit.
        f.device.writeMMR(0x80000D, 42, QStringLiteral("blocked"));
        f.device.writeMMRUrgent(0x80000E, 42, QStringLiteral("blocked"));
        f.device.writeMMRVerified(0x80000F, 42, QStringLiteral("blocked"));

        QCOMPARE(f.transport.writes.size(), baselineWrites);

        // Once the guard clears, writes go through again.
        f.device.setFirmwareFlashInProgress(false);
        f.device.writeMMR(0x800010, 42, QStringLiteral("post-flash"));
        QVERIFY(f.transport.writes.size() > baselineWrites);
    }

    void startUpdate_isNoOpOnSimulator() {
        Fixture f;
        f.device.setSimulationMode(true);
        writeCachedBlob(&f.updater, &f.cache, makeFirmwareBlob(1352));

        f.updater.startUpdate();

        // Sim-mode gate in startUpdate() means we never leave Idle and no
        // BLE traffic is issued.
        QCOMPARE(f.updater.state(), FirmwareUpdater::State::Idle);
        QVERIFY(!f.device.firmwareFlashInProgress());
        for (const auto& w : f.transport.writes) {
            QVERIFY2(w.first != DE1::Characteristic::FW_MAP_REQUEST,
                     "simulator must not have issued any FWMapRequest");
        }
    }

    // ===== §4c: retry, dismiss, verify-disconnect retroactive success =====

    void retryAfterFailure_restartsFromErase() {
        Fixture f;
        // First attempt intentionally fails with an erase timeout.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(R"(\[firmware\] FAIL phase=\s*Erasing.*reason=\s*Erase did not complete)"));
        writeCachedBlob(&f.updater, &f.cache, makeFirmwareBlob(1352));
        // Short erase timeout so the first attempt fails fast. Bump the
        // post-erase wait past the timeout so the timeout actually fires
        // before the synchronous transition to Uploading would mask it.
        f.updater.setEraseTimeoutMs(50);
        f.updater.setPostEraseWaitMs(5000);
        f.updater.startUpdate();
        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(),
                                  FirmwareUpdater::State::Failed, 2000);
        QVERIFY(f.updater.retryAvailable());

        // Restore a sensible erase timeout so retry has room to succeed.
        // Leave postEraseWaitMs large so retry observably re-enters Erasing.
        f.updater.setEraseTimeoutMs(5000);
        f.transport.clearWrites();

        f.updater.retry();
        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(),
                                  FirmwareUpdater::State::Erasing, 2000);

        // A fresh erase packet (the same 7-byte "00000101000000") must
        // appear on the wire — retry restarts from Phase 1, not from wherever
        // the previous attempt left off.
        bool sawFreshErase = false;
        for (const auto& w : f.transport.writes) {
            if (w.first == DE1::Characteristic::FW_MAP_REQUEST &&
                w.second == QByteArray::fromHex("00000101000000")) {
                sawFreshErase = true; break;
            }
        }
        QVERIFY2(sawFreshErase, "retry did not re-issue the erase request");
    }

    void dismissAvailability_suppressesBannerForSameVersion() {
        Fixture f;
        // Drive the availability signal directly — no startUpdate, no
        // Erasing state to get in the way.
        DE1::Firmware::FirmwareAssetCache::CheckResult r;
        r.kind = DE1::Firmware::FirmwareAssetCache::CheckResult::Newer;
        r.remoteVersion = 1352;

        emit f.cache.checkFinished(r);
        QVERIFY(f.updater.updateAvailable());

        f.updater.dismissAvailability();
        QVERIFY(!f.updater.updateAvailable());

        // Same remote version again → stays dismissed.
        emit f.cache.checkFinished(r);
        QVERIFY(!f.updater.updateAvailable());
    }

    void olderRemoteSetsDowngradeAvailability() {
        // Mirrors de1app's "Firmware downgrade available" UX: when the
        // cached firmware is strictly older than what's on the DE1 (e.g.
        // user flipped channel nightly → stable), updateAvailable still
        // becomes true and isDowngrade is set.
        Fixture f;
        f.updater.setInstalledVersionProvider([]{ return uint32_t(1352); });
        DE1::Firmware::FirmwareAssetCache::CheckResult r;
        r.kind          = DE1::Firmware::FirmwareAssetCache::CheckResult::Older;
        r.remoteVersion = 1333;
        emit f.cache.checkFinished(r);

        QVERIFY(f.updater.updateAvailable());
        QVERIFY(f.updater.isDowngrade());
        QCOMPARE(f.updater.availableVersion(), 1333);
    }

    void dismissedBannerReappearsOnNewerVersion() {
        Fixture f;
        DE1::Firmware::FirmwareAssetCache::CheckResult r;
        r.kind = DE1::Firmware::FirmwareAssetCache::CheckResult::Newer;

        r.remoteVersion = 1352;
        emit f.cache.checkFinished(r);
        f.updater.dismissAvailability();
        QVERIFY(!f.updater.updateAvailable());

        // A strictly newer version clears the dismissal → banner returns.
        r.remoteVersion = 1353;
        emit f.cache.checkFinished(r);
        QVERIFY(f.updater.updateAvailable());
    }

    void verifyDisconnectRetroactive_succeedsOnVersionMatch() {
        Fixture f;
        auto installed = std::make_shared<uint32_t>(1200);
        f.updater.setInstalledVersionProvider([installed]{ return *installed; });
        const QByteArray blob = makeFirmwareBlob(1352);
        writeCachedBlob(&f.updater, &f.cache, blob);

        f.updater.startUpdate();
        // postEraseWaitMs=0 races past Erasing synchronously — observe
        // Uploading directly.
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Uploading);
        simulateFullUpload(f.transport, blob);
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Verifying);

        // Disconnect mid-verify — ambiguous: could be a real failure, could
        // be the DE1's post-flash reboot.
        f.transport.setConnectedSim(false);
        // State should NOT flip to Failed immediately; it waits in the
        // grace window.
        QCOMPARE(f.updater.state(), FirmwareUpdater::State::Verifying);

        // Post-reboot: DE1 comes back reporting the new firmware version.
        *installed = 1352;
        f.transport.setConnectedSim(true);

        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(),
                                  FirmwareUpdater::State::Succeeded, 2000);
    }

    void verifyDisconnectGraceTimeout_failsRetryable() {
        Fixture f;
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(R"(\[firmware\] FAIL phase=\s*Verifying.*reason=\s*DE1 did not reconnect)"));
        f.updater.setVerifyDisconnectGraceMs(50);   // short grace for test
        const QByteArray blob = makeFirmwareBlob(1352);
        writeCachedBlob(&f.updater, &f.cache, blob);
        f.updater.startUpdate();
        // postEraseWaitMs=0 races past Erasing — observe Uploading directly.
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Uploading);
        simulateFullUpload(f.transport, blob);
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Verifying);

        f.transport.setConnectedSim(false);
        // Never reconnects; grace timer fires → Failed (retryable).
        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(),
                                  FirmwareUpdater::State::Failed, 2000);
        QVERIFY(f.updater.retryAvailable());
    }

    // ===== §4a: happy path =====

    void happyPath_endToEnd() {
        Fixture f;
        auto installed = std::make_shared<uint32_t>(1200);
        f.updater.setInstalledVersionProvider([installed]{ return *installed; });
        const QByteArray blob = makeFirmwareBlob(/*version*/ 1352);
        writeCachedBlob(&f.updater, &f.cache, blob);

        QSignalSpy stateSpy(&f.updater, &FirmwareUpdater::stateChanged);

        // Kick off the update.
        f.updater.startUpdate();

        // Downloading should finish synchronously (cache short-circuits when
        // the file already exists and validates), and with postEraseWaitMs=0
        // the state machine transitions Downloading → Ready → Erasing →
        // Uploading synchronously inside the startUpdate() call chain. So we
        // observe Uploading directly — the intermediate Erasing packet is
        // verified below by inspecting the wire log, not by catching state.
        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(), FirmwareUpdater::State::Uploading, 2000);

        // Verify the erase packet landed on A009.
        bool sawEraseReq = false;
        for (const auto& w : f.transport.writes) {
            if (w.first == DE1::Characteristic::FW_MAP_REQUEST &&
                w.second == QByteArray::fromHex("00000101000000")) {
                sawEraseReq = true; break;
            }
        }
        QVERIFY2(sawEraseReq, "erase FWMapRequest was not written to A009");

        // Verify the firmware notifications characteristic was subscribed.
        QVERIFY(f.transport.subscribes.contains(DE1::Characteristic::FW_MAP_REQUEST));

        // Wait for all chunks to land in MockTransport (the pump queues them
        // synchronously with a 0 ms interval). Count chunk writes rather than
        // total writes — the erase FWMapRequest is not the only non-chunk
        // write any more, since beginErasePhase() sends the DE1 to sleep
        // first. See firmwareChunkWriteCount().
        const qsizetype expectedChunksQueued = (blob.size() + 15) / 16;
        QTRY_VERIFY_WITH_TIMEOUT(
            firmwareChunkWriteCount(f.transport) >= expectedChunksQueued, 5000);

        // Now simulate BLE ACKing each write. The updater counts
        // WRITE_TO_MMR ACKs with length==16; after the last chunk's ACK
        // it schedules beginVerifyPhase() after postUploadSettleMs (0 in
        // test mode).
        f.transport.ackAllWritesInOrder();

        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(), FirmwareUpdater::State::Verifying, 5000);

        // Confirm the exact number of 16-byte chunks went out on A006.
        qsizetype chunkCount = 0;
        bool       sawVerifyReq = false;
        for (const auto& w : f.transport.writes) {
            if (w.first == DE1::Characteristic::WRITE_TO_MMR &&
                w.second.size() == 20 && uint8_t(w.second[0]) == 0x10) {
                chunkCount++;
            }
            if (w.first == DE1::Characteristic::FW_MAP_REQUEST &&
                w.second == QByteArray::fromHex("00000001FFFFFF")) {
                sawVerifyReq = true;
            }
        }
        // de1app streams the ENTIRE file starting at offset 0 (header + payload):
        // the DE1 itself parses the 64-byte header during flash. A 320-byte
        // synthetic blob therefore produces 20 chunks, not 16.
        const qsizetype expectedChunks = (blob.size() + 15) / 16;
        QCOMPARE(chunkCount, expectedChunks);
        QVERIFY2(sawVerifyReq, "verify FWMapRequest was not written to A009");

        // Simulate Phase 3 success notification. State should transition to
        // AwaitingReboot first; Succeeded only lands once the DE1 actually
        // reconnects reporting the new firmware version.
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000001FFFFFD"));

        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(),
                                  FirmwareUpdater::State::AwaitingReboot, 2000);

        // Simulate auto-reboot: disconnect → reconnect with new version.
        f.transport.setConnectedSim(false);
        *installed = 1352;
        f.transport.setConnectedSim(true);

        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(), FirmwareUpdater::State::Succeeded, 2000);
        QCOMPARE(f.updater.progress(), 1.0);
        QVERIFY(!f.updater.updateAvailable());

        // stateSpy fired at least once per transition; sanity check the count.
        QVERIFY2(stateSpy.count() >= 4,
                 qPrintable(QStringLiteral("only %1 state transitions").arg(stateSpy.count())));
    }
};

QTEST_GUILESS_MAIN(tst_FirmwareUpdater)
#include "tst_firmwareupdater.moc"
